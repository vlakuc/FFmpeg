/*
 * RTP/mpegts muxer
 * Copyright (c) 2011 Martin Storsjo
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

#include "libavutil/mathematics.h"
#include "avformat.h"
#include "avio_internal.h"
#include "libavutil/opt.h"


// MPEG-TS packet size
#define MPEGTS_PACKET_SIZE 188

// Size of working buffer (up to 5 MPEGTS packets)
#define WORK_BUFFER_SIZE 5*MPEGTS_PACKET_SIZE

// Initial size for output buffer
//  Output buffer holds MPEG-TS packet for whole encoded frame
#define INIT_OUTPUT_BUFFER_SIZE 4*1024

typedef struct
{
    AVFormatContext *mpegts_ctx;
    AVFormatContext *rtp_ctx;
    
    // Working buffer
    uint8_t         buffer[WORK_BUFFER_SIZE];
    
    // Output buffer
    uint8_t*        output;             // Output buffer
    int             output_max_size;    // Maximum size of output buffer
    int             output_size;        // Current size of date in output buffer

} MpegTSRtpContext;


// MPEGTS BytIOContext write function
static int mpegts_rtp_write_buff_packet(void* opaque, uint8_t* buf, int buf_size);



// Free resources for MPEG-TS/RTP context
static void mpegts_rtp_free(MpegTSRtpContext* ctx)
{
    avformat_free_context(ctx->rtp_ctx);
    ctx->rtp_ctx = NULL;
    if (ctx->mpegts_ctx)
    {
        av_freep(&ctx->mpegts_ctx->pb);
        avformat_free_context(ctx->mpegts_ctx);
        ctx->mpegts_ctx = NULL;
    }
    av_freep(&ctx->output);
    ctx->output_max_size = 0;
}

// Write data to output buffer
static int mpegts_rtp_output_write(MpegTSRtpContext* ctx, uint8_t* buf, int buf_size)
{
    // Grow output buffer if needed
    if ((ctx->output_size + buf_size) > ctx->output_max_size)
    {
        // To reduce realloc operations grown by 32x required size
        ctx->output_max_size += 32*(ctx->output_size + buf_size - ctx->output_max_size);
        ctx->output = av_realloc(ctx->output, ctx->output_max_size);
        if ( !ctx->output )
            return -1;
    }
    
    // Append new data to output buffer
    memcpy(&ctx->output[ctx->output_size], buf, buf_size);
    ctx->output_size += buf_size;
    
    return 0;
}


// Reset output buffer
static void mpegts_rtp_output_reset(MpegTSRtpContext* ctx)
{
    ctx->output_size = 0;
}

///////////////////////////////////////////////////////
// MPEGTS ByteIOContext
static int mpegts_rtp_write_buff_packet(void* opaque, uint8_t* buf, int buf_size)
{
    return mpegts_rtp_output_write((MpegTSRtpContext*)(opaque), buf, buf_size);
}


///////////////////////////////////////////////////////
// MPEGTS/RTP muxer
static int mpegts_rtp_write_header(AVFormatContext *s)
{
    int i;
    MpegTSRtpContext* ctx = s->priv_data;
    AVStream *st = NULL;
    
    int res = avformat_alloc_output_context2(&ctx->mpegts_ctx, NULL, "mpegts", NULL);
    if (res < 0)
        return res;
    ctx->mpegts_ctx->max_delay = s->max_delay;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream* st = avformat_new_stream(ctx->mpegts_ctx, NULL);
        if (!st) 
            return -1;
        st->time_base           = s->streams[i]->time_base;
        st->sample_aspect_ratio = s->streams[i]->sample_aspect_ratio;
        avcodec_parameters_copy(st->codecpar, s->streams[i]->codecpar);
    }
    ctx->output_max_size = INIT_OUTPUT_BUFFER_SIZE;
    ctx->output = av_mallocz(ctx->output_max_size);
    if (!ctx->output)
        return AVERROR(ENOMEM);
    ctx->mpegts_ctx->pb = avio_alloc_context(ctx->buffer, sizeof(ctx->buffer), 1, ctx, NULL, mpegts_rtp_write_buff_packet, NULL);
    if( !ctx->mpegts_ctx->pb )
        return AVERROR(ENOMEM);

    if ((res = avformat_write_header(ctx->mpegts_ctx, NULL)) < 0)
        return res;
    for (i = 0; i < s->nb_streams; i++) {
        s->streams[i]->time_base = ctx->mpegts_ctx->streams[i]->time_base;
        s->streams[i]->pts_wrap_bits = ctx->mpegts_ctx->streams[i]->pts_wrap_bits;
    }

    res = avformat_alloc_output_context2(&ctx->rtp_ctx, NULL, "rtp", NULL);
    if (res < 0)
        return res;

    st = avformat_new_stream(ctx->rtp_ctx, NULL);
    st->time_base.num   = 1;
    st->time_base.den   = 90000;
    st->codecpar->codec_id = AV_CODEC_ID_MPEG2TS;
    ctx->rtp_ctx->pb = s->pb;

    res = avformat_write_header(ctx->rtp_ctx, NULL);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to write RTP header. Error:%d\n", res);
        return res;
    }
  
    return 0;
}

static int mpegts_rtp_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int res;
    MpegTSRtpContext*   ctx = s->priv_data;
    AVPacket            new_pkt = *pkt;         // Copy packet attributes

    // Assign underlying transport to RTP muxer (may changed from call to call)
    ctx->rtp_ctx->pb = s->pb;

    // Write packet to MPEGTS context
    res = ctx->mpegts_ctx->oformat->write_packet(ctx->mpegts_ctx, pkt);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to write MPEGTS packet. Error:%d\n", res);
        return res;
    }

    // Write data from output buffer to RTP
    new_pkt.data = ctx->output;
    new_pkt.size = ctx->output_size;
    new_pkt.stream_index = 0;
    res = ctx->rtp_ctx->oformat->write_packet(ctx->rtp_ctx, &new_pkt);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to write RTP packets. Error:%d\n", res);
        return res;
    }
    // Reset output buffer
    mpegts_rtp_output_reset(ctx);
    return 0;
}


static int mpegts_rtp_write_trailer(AVFormatContext *s)
{
    int res, r = 0;
    MpegTSRtpContext* ctx = s->priv_data;

    // Assign underlying transport to RTP muxer (may changed from call to call)
    ctx->rtp_ctx->pb = s->pb;
    
    // Write MPEGTS trailer
    res = av_write_trailer(ctx->mpegts_ctx);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to write MPEGTS trailer. Error:%d\n", res);
        r = res;
    }

    // Write remaining data
    if (ctx->output_size > 0)
    {
        AVPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.data = ctx->output;
        pkt.size = ctx->output_size;
        pkt.dts = AV_NOPTS_VALUE;
        pkt.pts = AV_NOPTS_VALUE;
        
        res = ctx->rtp_ctx->oformat->write_packet(ctx->rtp_ctx, &pkt);
        if (res < 0) {
            av_log(s, AV_LOG_ERROR, "Failed to write RTP packets. Error:%d\n", res);
            r = res;
        }
    }

    // Write RTP trailer
    res = av_write_trailer(ctx->rtp_ctx);
    if (res < 0) {
        av_log(s, AV_LOG_ERROR, "Failed to write RTP trailer. Error:%d\n", res);
        r = res;
    }
    
    return r;
}

static void mpegts_rtp_deinit(AVFormatContext *s)
{
    MpegTSRtpContext* ctx = s->priv_data;
    mpegts_rtp_free(ctx);
}


static const AVOption options[] = {
    { NULL, },
};


AVOutputFormat ff_rtp_mpegts_epiphan_muxer = {
    .name              = "rtp_mpegts_epiphan",
    .long_name         = NULL_IF_CONFIG_SMALL("RTP/mpegts output format"),
    .priv_data_size    = sizeof(MpegTSRtpContext),
    .audio_codec       = AV_CODEC_ID_AAC,
    .video_codec       = AV_CODEC_ID_MPEG4,
    .write_header      = mpegts_rtp_write_header,
    .write_packet      = mpegts_rtp_write_packet,
    .write_trailer     = mpegts_rtp_write_trailer,
    .deinit            = mpegts_rtp_deinit
};
