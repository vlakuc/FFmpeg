/*
 * H.264 Bitstream Level Constant Video Frame Rate Filter
 *
 * Copyright (c) 2017 Epiphan Systems Inc. All rights reserved.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "h264_constrate_filter.h"
#include "h264.h"
#include "h264_parse.h"
#include "h2645_parse.h" 
#include "golomb.h"

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/timestamp.h"

#include "h264_constrate_filter_data.h"


/**
 * Naive H264 Packet to NAL splitter
 *  This filter requires an access to the very first bytes in splice header
 *  and assume that emulation_prevention_tree_bytes are not required for that first RBSP
 * Data points to NAL data
 */
typedef struct H264NAL {
    int         type;
    uint8_t*    data;
    int         size;
} H264NAL;

// Since avcodec cannot use libavformat, the following functions are copied from libavformat/avc.c
static const uint8_t *avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
//      if ((x - 0x01000100) & (~x) & 0x80008000) // little endian
//      if ((x - 0x00010001) & (~x) & 0x00800080) // big endian
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

static const uint8_t *avc_find_startcode(const uint8_t *p, const uint8_t *end){
    const uint8_t *out= avc_find_startcode_internal(p, end);
    if(p<out && out<end && !out[-1]) out--;
    return out;
}



/**
 * Split one H264 packet in ANNEX B format into NAL units
 *  Return number of parser units
 */
static int split_h264_packet_annexb(H264NAL* nals, int nb_nals, uint8_t* data, int size, void* log_ctx)
{
    int i = 0;
    const uint8_t* end = data + size;
    const uint8_t* nal_start;
    const uint8_t* nal_end;
    nal_start = avc_find_startcode(data, end);
    while(i < nb_nals) {
        H264NAL* nal = &nals[i];
        uint8_t nal_header;
        int nal_size = 0;

        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;
        
        nal_end = avc_find_startcode(nal_start, end);
        nal_size = nal_end - nal_start;
        if (nal_size == 0) {            
            av_log(log_ctx, AV_LOG_WARNING, "Invalid NAL size %d\n", nal_size);
            return AVERROR_INVALIDDATA;
        }

        nal_header = *nal_start;
        if (!(nal_header & 0x80)) {
            nal->data = (uint8_t*)(nal_start + 1);
            nal->size = nal_size - 1;
            nal->type = nal_header & 0x1F;
            i++;
        }
        nal_start = nal_end;
    }
    return i;
}

/**
 * Split one H264 packet in AVCC format into NAL units
 *  Return number of parser units
 */
static int split_h264_packet_avcc(H264NAL* nals, int nb_nals, uint8_t* data, int size, int nal_length_size, void* log_ctx)
{
    int i = 0;
    while(size > nal_length_size && i < nb_nals) {
        H264NAL* nal = &nals[i];
        uint8_t nal_header;
        int nal_size = 0;
        int j;

        for(j = 0; j < nal_length_size; j++, data++, size--)
            nal_size = ((unsigned)nal_size << 8) | *data;

        if (nal_size == 0 || nal_size > size) {
            av_log(log_ctx, AV_LOG_WARNING, "Invalid NAL size %d\n", nal_size);
            return AVERROR_INVALIDDATA;
        }

        nal->data = data;
        nal->size = size;

        data += nal_size;
        size -= nal_size;

        // Parse NAL header
        nal_header = *nal->data;
        if (nal_header & 0x80) 
            continue;           // Invalid forbidden_zero_bit. Skip NAL
        
        nal->data += 1;
        nal->size -= 1;
        nal->type = nal_header & 0x1F;
        i++;
    }
    return i;
}

/**
 * Split one H264 packet into NAL units
 *  Return number of parser units
 */

static int split_h264_packet(H264NAL* nals, int nb_nals, uint8_t* data, int size, int nal_length_size, void* log_ctx) 
{
    if (nal_length_size > 0)
        return split_h264_packet_avcc(nals, nb_nals, data, size, nal_length_size, log_ctx);
    return split_h264_packet_annexb(nals, nb_nals, data, size, log_ctx);
}

/**
 * Replace n bits starting from offset 
 *  Simply and stupid bit-by-bit implementation, but it is enough to replace only few bits
 */
static void replace_bits(uint8_t* data, int offset, int n, int value)
{
    data += offset / 8;
    offset = (8 - offset % 8) - 1;

    while(--n >= 0) {
        *data = (*data & ~(1 << offset)) | (((value >> n) & 1) << offset);
        
        if (--offset < 0) {
            offset = 7;
            data++;
        }
    }
}

#define DUMMY_SLICE_QP 11

/**
 * Create byte-aligned slice header (CABAC and CAVLC) and slice data (CAVLC only)
 */
static int create_skipframe_generated_data(const H264ConstRateContext* filter, int frame_num, uint8_t *buf, int buf_size)
{
    PutBitContext pb;

    int i;
    const SPS* sps = filter->sps;
    const PPS* pps = filter->pps;

    // Build slice header according to 7.3.2.11
    init_put_bits(&pb, buf, buf_size);
    
    // write prefix
    // Slice header NAL:
    //   forbidden_zero_bit : 0 
    //   nal_ref_idc : 0 
    //   nal_unit_type : 1 ( Coded slice of a non-IDR picture ) 
    //  According to 7.4.1 nal_ref_idc should be 0, but x264 and QSV use values 2 and 1 correspondingly
    put_bits(&pb, 8, 0x01);  // Slice header NAL
    // Slice header:
    set_ue_golomb(&pb, 0);//first_mb_in_slice = 0
    set_ue_golomb(&pb, 5);//slice_type = 5
    set_ue_golomb(&pb, 0);//pic_parameter_set_id = 0

    // write frame_num
    put_bits(&pb,  sps->log2_max_frame_num, (frame_num % (1 << sps->log2_max_frame_num)));

    if (pps->redundant_pic_cnt_present) {
        set_ue_golomb(&pb, 0);  // redundant_pic_cnt = 0
    }

    // if( slice_type = = P | | slice_type = = SP | | slice_type = = B ) 
    //  write num_ref_idx_active_override_flag = 0
    put_bits(&pb,  1, 0); 

    // ref_pic_list_modification
    // write ref_pic_list_modification_flag_l0 = 0
    put_bits(&pb,  1, 0);

    // pred_weight_table (if(( weighted_pred_flag && ( slice_type = = P | | slice_type = = SP )) || ( weighted_bipred_idc = = 1 && slice_type == B ))
    if (pps->weighted_pred) {
        // write luma_log2_weight_denom = 0 ue = 1
        set_ue_golomb(&pb, 0);
        if (sps->chroma_format_idc) {
            // write chroma_log2_weight_denom = 0
            set_ue_golomb(&pb, 0);
        }

        // for( i = 0; i <= num_ref_idx_l0_active_minus1; i++ ) {
        for( i = 0; i <= (pps->ref_count[0] - 1); i++ ) {
            put_bits(&pb,  1, 0);       // luma_weight_l0_flag = 0
            // luma_weight_l0[ i ] if( luma_weight_l0_flag )
            // luma_offset_l0      if( luma_weight_l0_flag )
            if (sps->chroma_format_idc) { 
                put_bits(&pb,  1, 0);   // chroma_weight_l0_flag = 0
                // chroma_weight_l0[ i ][ j ] if chroma_weight_l0_flag
                // chroma_offset_l0[ i ][ j ] if chroma_weight_l0_flag
            }
        }
    }
    
    // dec_ref_pic_marking() if( nal_ref_idc != 0 )

    // write cabac_init_idc = 0 if (entropy_coding_mode_flag && slice_type != I && slice_type != SI)
    if (pps->cabac)
        set_ue_golomb(&pb, 0);

     // write slice_delta_qp
    set_se_golomb(&pb, DUMMY_SLICE_QP - pps->init_qp);

    if (pps->deblocking_filter_parameters_present) {
        set_ue_golomb(&pb, 0);  // disable_deblocking_filter_idc
        set_se_golomb(&pb, 0);  // slice_alpha_c0_offset_div2  if disable_deblocking_filter_idc != 1
        set_se_golomb(&pb, 0);  // slice_beta_offset_div2      if disable_deblocking_filter_idc != 1
            
    }

    // slice_group_change_cycle if( num_slice_groups_minus1 > 0 && slice_group_map_type >= 3 && slice_group_map_type <= 5)

    // Slice data (CAVLC only)
    //  CAVLC slice data is not byte-aligned
    if (!pps->cabac)
        set_ue_golomb(&pb, sps->mb_width*sps->mb_height);   // mb_skip_run

    avpriv_align_put_bits(&pb);
    flush_put_bits(&pb);
    return put_bits_count(&pb) / 8;
}

static int create_skipframe(const H264ConstRateContext* filter, int64_t dts, int64_t duration, int frame_num, int stream_index, AVPacket* pkt)
{
    static const int HEADER_MAX_SIZE = 64;
    int header_size = filter->is_avc ? filter->nal_length_size : 4;
    int max_size = header_size + HEADER_MAX_SIZE + filter->slice_data_size;
    int size, ret;
    uint8_t* frame_data = av_malloc(max_size);
    
    if (!frame_data)
        return AVERROR(ENOMEM);

    size = create_skipframe_generated_data(filter, frame_num, frame_data + header_size, HEADER_MAX_SIZE);
    if (filter->slice_data)
        memcpy(frame_data + filter->nal_length_size + size, filter->slice_data, filter->slice_data_size);
    if (filter->is_avc) {
        if (filter->nal_length_size == 4)
            AV_WB32(frame_data, size + filter->slice_data_size);
        else if (filter->nal_length_size == 2)
            AV_WB16(frame_data, size + filter->slice_data_size); 
    } else {
        AV_WB32(frame_data, 0x00000001);    // Start code
    }

    ret = av_packet_from_data(pkt, frame_data, header_size + size + filter->slice_data_size);
    if (ret < 0) {
        av_free(frame_data);
        return ret;
    }        

    pkt->dts = dts;
    pkt->pts = dts;     // FIXME: Maybe incorrect
    pkt->stream_index = stream_index;
    pkt->duration = duration;

    return 0;
}

// Assume that there is no more than 128 NALs per one packet
#define MAX_NALS_PER_PACKET 128

static const SPS* get_slice_header_sps(const H264ParamSets* ps, unsigned int pps_id, void* log_ctx)
{
    const PPS* pps;

    if (pps_id >= MAX_PPS_COUNT) {
        av_log(log_ctx, AV_LOG_ERROR, "pps_id %u out of range\n", pps_id);
        return NULL;
    }
    if (!ps->pps_list[pps_id]) {
        av_log(log_ctx, AV_LOG_ERROR, "non-existing PPS %u referenced\n", pps_id);
        return NULL;
    }
    pps = (const PPS*)ps->pps_list[pps_id]->data;

    if (!ps->sps_list[pps->sps_id]) {
        av_log(log_ctx, AV_LOG_ERROR, "non-existing SPS %u referenced\n", pps->sps_id);
        return NULL;
    }
    return (const SPS*)ps->sps_list[pps->sps_id]->data;
}

/**
 * Partially parse H.264 slice header and return frame_num.
 *  Optional offset and size arguments are used to return position and size of frame number
*/
static int parse_slice_header_frame_number(const H264NAL* nal, const H264ParamSets* ps, void* log_ctx, int* offset, int* size)
{
    unsigned int pps_id;
    const SPS* sps = NULL;
    GetBitContext gb;
    init_get_bits8(&gb, nal->data, nal->size);

    get_ue_golomb_long(&gb);             // first_mb_addr
    get_ue_golomb_31(&gb);               // slice_type
    pps_id = get_ue_golomb(&gb);         // TODO: for skip frames pps_id == 0 is used.

    sps = get_slice_header_sps(ps, pps_id, log_ctx);
    if (!sps)
        return AVERROR_INVALIDDATA;

    if (offset)
        *offset = (gb.buffer - nal->data) * 8 + gb.index;
    if (size)
        *size = sps->log2_max_frame_num;

    return get_bits(&gb, sps->log2_max_frame_num);
}

/**
 * Retrieve frame number from I/P slices
 */
static int parse_slice_frame_number(const H264ConstRateContext* filter, const uint8_t* data, int size)
{
    int i;
    H264NAL nals[MAX_NALS_PER_PACKET];
    int nb_nals = split_h264_packet(&nals[0], sizeof(nals)/sizeof(nals[0]), (uint8_t*)data, size, filter->nal_length_size, filter->log_ctx);
    if (nb_nals < 0)
        return nb_nals;

    // Assume that one packet contains slices with the exactly same number (no more then one frame per packet)
    //  In this case it is enough to check frame number for the first slice
    for(i = 0; i < nb_nals; i++) {
        H264NAL* nal = &nals[i];
        if (nal->type == H264_NAL_IDR_SLICE)
            return 0;   // For IDR assume frame_num = 0
        if (nal->type == H264_NAL_SLICE)
            return parse_slice_header_frame_number(nal, &filter->paramsets, filter->log_ctx, NULL, NULL);
    }

    return 0;
}

static int patch_slice_header_frame_number(H264NAL* nal, const H264ParamSets* ps, int adj, void* log_ctx)
{
    int offset;
    int size;
    int frame_num;
    // Find position and size of frame_num field in the slice header
    int ret = parse_slice_header_frame_number(nal, ps, log_ctx, &offset, &size);
    if (ret < 0)
        return ret;

    frame_num = (ret + adj) % (1 << size);
    av_log(log_ctx, AV_LOG_DEBUG, "slice header frame_num has been changed: %d => %d (+%d)\n", ret, frame_num, adj);
    replace_bits(nal->data, offset, size, frame_num);

    return frame_num;
}

/**
 * Patch H.264 slice header frame_num (current value + adj) for non-IDR frames only.
 *  Returns adjusted frame number or AVERROR_EOF if the end of the GOP is detected
*/
static int patch_slice_frame_number(const H264ConstRateContext* filter, uint8_t* data, int size, int adj)
{
    int ret = AVERROR_EOF;
    int i;
    H264NAL nals[MAX_NALS_PER_PACKET];
    int nb_nals = split_h264_packet(&nals[0], sizeof(nals)/sizeof(nals[0]), (uint8_t*)data, size, filter->nal_length_size, filter->log_ctx);
    if (nb_nals < 0)
        return nb_nals;
    
    for(i = 0; i < nb_nals; i++) {
        H264NAL* nal = &nals[i];
        if (nal->type == H264_NAL_IDR_SLICE)
            return AVERROR_EOF;     // IDR: End of GOP detected
        if (nal->type == H264_NAL_SLICE) {
            ret = patch_slice_header_frame_number(nal, &filter->paramsets, adj, filter->log_ctx);
            if (ret < 0)
                return ret; 
        }
    }    
    
    return ret;     // adjusted frame_num for the very last slice in the packet
}

static int validate_bitstream_parameters(const SPS* sps, const PPS* pps, void* log_ctx)
{
    if (!sps || !pps) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: SPS and/or PPS are invalid\n");
        return AVERROR_INVALIDDATA;
    }

    if (!sps->frame_mbs_only_flag) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: interlaced frames are not supported\n");
        return AVERROR_INVALIDDATA;
    }

    if (sps->poc_type != 2) { 
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: Unsupported picture order count type\n");
        return AVERROR_INVALIDDATA;
    }
    
    if (pps->slice_group_count > 1 && pps->mb_slice_group_map_type >= 3 && pps->mb_slice_group_map_type <= 5) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: Unsupported slice group count\n");
        return AVERROR_INVALIDDATA;
    }
    
    if (sps->ref_frame_count > 1) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: Too many references frames\n");
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

 /** 
 * Allocate and initialize new H264 Constant Rate Filter
 */
int av_h264_constrate_create(H264ConstRateContext** filter, const AVCodecParameters* par, AVRational time_base, AVRational frame_rate, void* log_ctx)
{
    H264ConstRateContext* flt;
    int ret;

    av_assert1(filter != NULL);
    av_assert1(par != NULL);

    // Validate codec parameters
    //  Only H.264 codec
    if (!(par->codec_type == AVMEDIA_TYPE_VIDEO && par->codec_id == AV_CODEC_ID_H264)) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: only H.264 bitstream is supported\n");
        return AVERROR_INVALIDDATA;
    }
    //  Extra data is mandatory
    if (!(par->extradata && par->extradata_size)) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: missing extra data\n");
        return AVERROR_INVALIDDATA;
    }

    *filter = NULL;
    flt = av_mallocz(sizeof(H264ConstRateContext));
    if (!flt)
        return AVERROR(ENOMEM);
    flt->log_ctx = log_ctx;

    ret = ff_h264_decode_extradata(par->extradata, par->extradata_size, &flt->paramsets, &flt->is_avc, &flt->nal_length_size, 0, NULL);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: Failed to parse extra data\n");
        return av_h264_constrate_free(&flt), ret;
    }

    if (flt->is_avc && !(flt->nal_length_size == 2 || flt->nal_length_size == 4)) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: Unsupported NAL size\n");
        return av_h264_constrate_free(&flt), AVERROR_INVALIDDATA;
    } 

    // Always use first PPS for the skip frames
    if (!flt->paramsets.pps_list[0]) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: missing PPS\n");
     } else {
        flt->pps = (const PPS*)flt->paramsets.pps_list[0]->data;

        if (!flt->paramsets.sps_list[flt->pps->sps_id]) {
            av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: non-existing SPS %u referenced\n", flt->pps->sps_id);
        } else {
            flt->sps = (const SPS*)flt->paramsets.sps_list[flt->pps->sps_id]->data;
        }
    }

    ret = validate_bitstream_parameters(flt->sps, flt->pps, log_ctx);
    if (ret < 0)
        return av_h264_constrate_free(&flt), ret;

    // For CABAC only fixed set of resolutions is supported
    if (flt->pps->cabac) {
        const H264SkipSliceData* skip_slice = ff_find_h264_skipslice_data(par->width, par->height);
        if (!skip_slice) {
            av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: resolution %dx%d is not supported\n", par->width, par->height);
            return av_h264_constrate_free(&flt), AVERROR_INVALIDDATA;
        }
        flt->slice_data = skip_slice->data;
        flt->slice_data_size = skip_slice->size;
    }

    if (frame_rate.den == 0 || frame_rate.num == 0) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: invalid frame rate (zero)\n");
        return av_h264_constrate_free(&flt), AVERROR(EINVAL);
    }

    // Ideal frame duration
    flt->duration = av_rescale_q(1, av_inv_q(frame_rate), time_base);
    if (flt->duration == 0) {
        av_log(log_ctx, AV_LOG_WARNING, "Constant Video Rate: invalid frame rate (%d/%d) for timebase (%d/%d)\n", frame_rate.num, frame_rate.den, time_base.num, time_base.den);
        return av_h264_constrate_free(&flt), AVERROR(EINVAL);   
    }

    // Everything is good
    flt->time_base = time_base;
    av_init_packet(&flt->pkt);

    flt->last_dts = AV_NOPTS_VALUE;
    flt->last_frame_num = 0;
    flt->frame_num_adj = 0;

    *filter = flt;
    return 0;
}

/*
 * Free H264 Constant Rate Filter and its resources
 */
void av_h264_constrate_free(H264ConstRateContext** filter)
{
    if (!filter || !*filter)
        return;

    ff_h264_ps_uninit(&(*filter)->paramsets);
    av_packet_unref(&(*filter)->pkt);

    av_freep(filter);
}


int av_h264_constrate_send_packet(H264ConstRateContext* filter, AVPacket* pkt)
{
    av_assert1(filter != NULL);
    av_assert1(pkt != NULL);

    if (filter->pkt.data)
        return AVERROR(EINVAL);     // Should be drained before send a new packet

    if (filter->last_dts != AV_NOPTS_VALUE) {
        int64_t expected_dts = filter->last_dts + filter->duration;
        int nframes = (pkt->dts - expected_dts + filter->duration/2) / filter->duration;

        if (nframes > 1000) {
            av_log(filter->log_ctx, 
                   AV_LOG_WARNING, 
                   "Frames gap is too big (%d frames, %s second(s)), skip frames will not be inserted\n", 
                   nframes,
                   av_ts2timestr((pkt->dts - expected_dts), &filter->time_base));
        } else if (nframes > 0) {
            av_log(filter->log_ctx, 
                   AV_LOG_DEBUG, 
                   "Frames gap in %s second(s) is detected [%s - %s]. %d skip frame(s) will be inserted starting with frame number %d\n", 
                   av_ts2timestr((pkt->dts - expected_dts), &filter->time_base),
                   av_ts2str(expected_dts), av_ts2str(pkt->dts), 
                   nframes,
                   (filter->last_frame_num + 1) % (1 << filter->sps->log2_max_frame_num));

            filter->num_skip_frames = nframes;
            filter->skip_frame_num  = filter->last_frame_num + 1;
            filter->skip_frame_dts  = expected_dts;
            filter->frame_num_adj  += nframes;
        }
    }

    av_packet_ref(&filter->pkt, pkt);
    filter->last_dts = filter->pkt.dts;

    return 0;
}

int av_h264_constrate_receive_packet(H264ConstRateContext* filter, AVPacket* pkt)
{
    av_assert1(filter != NULL);
    av_assert1(pkt != NULL);

    if (!filter->pkt.data)
        return AVERROR(EAGAIN);     // Drained

    if (filter->num_skip_frames > 0) {
        // Need to generate another skip frame
        int ret = create_skipframe(filter, filter->skip_frame_dts, filter->duration, filter->skip_frame_num, filter->pkt.stream_index, pkt);
        if (ret < 0)
            return ret;

        filter->skip_frame_dts  += filter->duration;
        filter->skip_frame_num  += 1;
        filter->num_skip_frames -= 1;
        return 0;
    }

    if (filter->frame_num_adj > 0) {
        // Frame number adjustment is required until the end of the GOP
        if (filter->pkt.flags & AV_PKT_FLAG_KEY) {
            // Fast end of the GOP detection
            filter->frame_num_adj = 0;
            filter->last_frame_num = 0;
        } else {
            // Make copy of the frame before possible packet data modification
            int ret;
            AVPacket pkt1;
            av_init_packet(&pkt1);
            if ((ret = av_copy_packet(&pkt1, &filter->pkt)) < 0)
                return ret;
            av_packet_unref(&filter->pkt);

            // Patch frame number
            ret = patch_slice_frame_number(filter, pkt1.data, pkt1.size, filter->frame_num_adj);
            
            if (ret == AVERROR_EOF) {
                // End of GOP detected. Stop frame number adjustment
                filter->frame_num_adj = 0;
                filter->last_frame_num = 0;
            } else if (ret >= 0) {
                filter->last_frame_num = ret;
            } else {
                av_packet_unref(&pkt1);
                return ret; 
            }

            av_packet_move_ref(pkt, &pkt1);
            return 0;
        }
    }

    // Nothing to insert. Return original frame
    //  But before that remember its frame number for future use
    if (filter->pkt.flags & AV_PKT_FLAG_KEY) {
        filter->last_frame_num = 0;
    } else {
        int frame_num = parse_slice_frame_number(filter, filter->pkt.data, filter->pkt.size);
        if (frame_num < 0)
            return frame_num;
        filter->last_frame_num = frame_num;
    }

    av_packet_move_ref(pkt, &filter->pkt);
    return 0;
}

