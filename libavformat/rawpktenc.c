/*
 * RAW AV Packet muxer
 * Copyright (c) 2011 Epiphan Systems Inc
 *
 * This file is part of FFmpeg with Epiphan mods.
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

#include "libavformat/avformat.h"
#include "internal.h"
#include "rawpkt.h"

static void raw_write_tag(AVIOContext *pb, int64_t tag, int param, uint8_t *data, uint32_t len)
{
    avio_wl64(pb, tag);
    avio_wb32(pb, param); // reserved for version, flags etc
    avio_wb32(pb, len);
    avio_write(pb, data, len);
}

static int raw_write_codec(AVIOContext *pb0, AVCodecParameters *codec)
{
    uint8_t *buf = NULL;
    AVIOContext *pb =  NULL;
    /* the global header codec flag can be turned off to allow local header generation.
     * Turn it on just for the format header.
     */
    int len;
    //if( codec->extradata_size != 0 )
        //flags |= CODEC_FLAG_GLOBAL_HEADER;

    avio_open_dyn_buf(&pb);

    // generic info
    avio_wb32(pb, codec->codec_id);
    avio_wb32(pb, codec->codec_type);
    avio_wb64(pb, codec->bit_rate);
    avio_wb32(pb, codec->format);
    avio_wb32(pb, codec->codec_tag);
    avio_wb32(pb, codec->bits_per_coded_sample);
    avio_wb32(pb, codec->bits_per_raw_sample);
    avio_wb32(pb, codec->profile);
    avio_wb32(pb, codec->level);

    if(codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        avio_wb32(pb, codec->sample_rate);
        avio_wb32(pb, codec->channels);
        avio_wb32(pb, codec->frame_size);
        avio_wb32(pb, codec->channel_layout);
        avio_wb32(pb, codec->block_align);
        avio_wb32(pb, codec->initial_padding);
        avio_wb32(pb, codec->trailing_padding);
        avio_wb32(pb, codec->seek_preroll);
    }
    
    if(codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        avio_wb32(pb, codec->width);
        avio_wb32(pb, codec->height);
        avio_wb32(pb, codec->sample_aspect_ratio.num);
        avio_wb32(pb, codec->sample_aspect_ratio.den);
        avio_wb32(pb, codec->field_order);
        avio_wb32(pb, codec->color_range);
        avio_wb32(pb, codec->color_primaries);
        avio_wb32(pb, codec->color_trc);
        avio_wb32(pb, codec->color_space);
        avio_wb32(pb, codec->chroma_location);
        avio_wb32(pb, codec->video_delay);
    }
    // Write codec extra data
    avio_wb32(pb, codec->extradata_size);
    if( codec->extradata_size != 0 )
        avio_write(pb, codec->extradata, codec->extradata_size);
    RAWTAG_SEEK(pb);

    len = avio_close_dyn_buf(pb,&buf);
    if(len >= 0 ) {
        raw_write_tag(pb0,RAWHEADER_CODECCTX,1,buf,len);
        av_free(buf);
        len = 0;
    }
    return len;
}

static void raw_write_string(AVIOContext *pb, const char *str)
{
    int len = strlen(str);
    avio_wb32(pb,len);
    avio_write(pb,str,len);
}

static int raw_write_metadata(AVIOContext *pb0, AVDictionary *metadata)
{
    uint8_t *buf = NULL;
    int len;
    AVIOContext *pb =  NULL;
    AVDictionaryEntry *t = NULL;

    avio_open_dyn_buf(&pb);
    while(!!(t = av_dict_get(metadata,"",t,AV_DICT_IGNORE_SUFFIX))) {
        raw_write_string(pb,t->key);
        raw_write_string(pb,t->value);
    }
    RAWTAG_SEEK(pb);
    len = avio_close_dyn_buf(pb,&buf);
    if(len >= 0 ) {
        raw_write_tag(pb0,RAWHEADER_METADATA,av_dict_count(metadata),buf,len);
        av_free(buf);
        len = 0;
    }
    return len;
}

static int raw_write_trackinfo(AVIOContext *pb0,AVStream *st)
{
    uint8_t *buf = NULL;
    int len,err;
    AVIOContext *pb =  NULL;

    avio_open_dyn_buf(&pb);

    avio_wl32(pb, st->avg_frame_rate.num);
    avio_wl32(pb, st->avg_frame_rate.den);
    avio_wl32(pb, st->r_frame_rate.num);
    avio_wl32(pb, st->r_frame_rate.den);

    err = raw_write_codec(pb, st->codecpar);
    if(err == 0) {
        /* track metadata here */
        if(av_dict_count(st->metadata)) {
            err = raw_write_metadata(pb, st->metadata);
        }
    }

    len = avio_close_dyn_buf(pb,&buf);
    
    if(err == 0 && len >= 0 ) {
        raw_write_tag(pb0,RAWHEADER_TRACK,0,buf,len);
        av_free(buf);
    }
    return err;
}

static int raw_write_streaminfo(AVIOContext *pb0,AVFormatContext *s)
{
    int len;
    uint8_t *buf = NULL;
    AVIOContext *pb =  NULL;

    avio_open_dyn_buf(&pb);

    // generic info
    avio_wb64(pb, s->start_time_realtime);
    RAWTAG_SEEK(pb);

    len = avio_close_dyn_buf(pb,&buf);
    if(len >= 0 ) {
        raw_write_tag(pb0,RAWHEADER_STREAM,0,buf,len);
        av_free(buf);
        len = 0;
    }
    return len;
}

// Write fixed size header
//  Fixed size is used to simplify header parser on ffserver side
static int raw_write_header(AVFormatContext* s)
{
    int i, len, err = 0;
    AVIOContext *pb = NULL;
    uint8_t *buf = NULL;
    
    avio_open_dyn_buf(&pb);

    /* global stream info */
    err = raw_write_streaminfo(pb,s);
    if(err < 0 )
        goto fail;

    /* global metadata */
    if(av_dict_count(s->metadata)) {
        err = raw_write_metadata(pb, s->metadata);
        if(err < 0 )
            goto fail;
    }

    // Streams
    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        avpriv_set_pts_info(st,64,1,1000000);
        err = raw_write_trackinfo(pb,st);
        if(err < 0 )
            goto fail;
    }
fail:
    len = avio_close_dyn_buf(pb,&buf);
    if(err == 0 && len >= 0 ) {
        raw_write_tag(s->pb, RAWHEADER_MAGIC, RAWPACKET_VERSION, buf, len);
        av_free(buf);
        avio_flush(s->pb);
    }
    return err;
}

// RAW PACKET HEADER
// +00 - signature (RAW PACK)
// +08 - total size (header size + data size + padding)
// +12 - pts
// +20 - dts
// +28 - stream index
// +32 - flags
// +36 - duration
// +44 - pos 
// +52 - data size
// +56 - packet data

static int raw_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    int len;
    uint8_t *buf = NULL;
    AVIOContext *pb =  NULL;

    // Write header
    avio_open_dyn_buf(&pb);

    // packet info
    avio_wb32(pb, pkt->size);
    avio_wb64(pb, pkt->pts);
    avio_wb64(pb, pkt->dts);
    avio_wb32(pb, pkt->stream_index);
    avio_wb32(pb, pkt->flags);
    avio_wb64(pb, pkt->duration);
    avio_wb64(pb, pkt->pos);
    RAWTAG_SEEK(pb);

    len = avio_close_dyn_buf(pb,&buf);
    if(len >= 0 ) {
        raw_write_tag(s->pb,RAWPACKET_MAGIC,0,buf,len);
        av_free(buf);
        // Write data
        avio_write(s->pb, pkt->data, pkt->size);
        avio_flush(s->pb);
        len = 0;
    }

    return len;
}

AVOutputFormat ff_rawpacket_muxer = {
    .name              = "rawpacket",
    .long_name         = "raw avpacket format",
    .extensions        = "avpkt",
    .priv_data_size    = 0,
    .audio_codec       = AV_CODEC_ID_PCM_S16LE, // Not realy used
    .video_codec       = AV_CODEC_ID_RAWVIDEO, // Not realy used
    .write_header      = raw_write_header,
    .write_packet      = raw_write_packet,
    .write_trailer     = NULL,
    .flags             = 0,
};

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
