/*
 * Epiphan Shared Memory output
 * Copyright (c) 2017 Epiphan Systems Inc.
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

/**
 * @file
 * Epiphan Shared Memory output device
 */

#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <libshm.h>


#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libavformat/internal.h"
#include "avdevice.h"

#define SHMEM_TSM_RELATIVE 0    // Relative arrival first packet with source timestamp 0
#define SHMEM_TSM_IGNORE   1    // Ignore source timestamp and use wall clock instead
#define SHMEM_TSM_ABSOLUTE 2    // Do not modify source timestamps

typedef struct ShmemOutContext {
    AVClass* class;

    int             max_consumers;      // Maximum number of consumers
    int             video_buffer_size;  // In frames
    int             audio_buffer_size;  // In seconds
    int             ignore_nospace;     // Ignore full buffer
    int             timestamp_mode;     // Timestamp handling mode

    shm_writer_t*   writer;
    int             video_index;
    int             audio_index;
    int64_t         realtime_ts_offset; // Timestamp for stream pts 0 (SHMEM_TSM_RELATIVE) in AV_TIME_BASE
} ShmemOutContext;


static int64_t shmem_adjust_timestamp(ShmemOutContext* s, const AVStream* st, int64_t ts)
{
    switch(s->timestamp_mode) {
    case SHMEM_TSM_RELATIVE:
        if (s->realtime_ts_offset == AV_NOPTS_VALUE) {
            // Initial value
            s->realtime_ts_offset = av_gettime() - av_rescale_q(ts, st->time_base, AV_TIME_BASE_Q);
        }
        return s->realtime_ts_offset + av_rescale_q(ts, st->time_base, AV_TIME_BASE_Q); 

    case SHMEM_TSM_IGNORE:
        return av_gettime();

    case SHMEM_TSM_ABSOLUTE:
        return ts;

    default:
        return ts;
    }
}

static int shmem_init(AVFormatContext *ctx)
{
    // TODO: configure libshm logging here
    return 0;
}

static int shmem_write_header(AVFormatContext *ctx)
{
    ShmemOutContext *s = ctx->priv_data;
    int video_index = -1;
    int audio_index = -1;
    int i = 0;
    int audio_buffer_size = 0;
    int sample_rate = 0;
    int video_buffer_size = 0;
    int width = 0;
    int height = 0;
    int rc;

    // Validate input parameters
    //  Support only one video stream and only one audio stream
    for (i = 0; i < ctx->nb_streams; i++) {
        AVStream *stream = ctx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;
        switch (codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (audio_index >= 0) {
                av_log(ctx, AV_LOG_ERROR, "Only one audio stream is supported.\n");
                return AVERROR(EINVAL);
            }
            if (!((codecpar->codec_id == AV_CODEC_ID_PCM_S16LE) &&
                  (codecpar->channels == SHM_AUDIO_CHANNELS))) {
                av_log(ctx, AV_LOG_ERROR, "Only PCM 16bit mono is supported.\n");
                return AVERROR(EINVAL);
            }
            audio_index = i;
            break;

        case AVMEDIA_TYPE_VIDEO:
            if (video_index >= 0) {
                av_log(ctx, AV_LOG_ERROR, "Only one video stream is supported.\n");
                return AVERROR(EINVAL);
            }
            if (!((codecpar->codec_id == AV_CODEC_ID_RAWVIDEO) &&
                  (codecpar->format == AV_PIX_FMT_YUV420P))) {
                av_log(ctx, AV_LOG_ERROR, "Only rawvideo YUV420P is supported.\n");
                return AVERROR(EINVAL);
            }
            video_index = i;
            break;

        default:
            av_log(ctx, AV_LOG_ERROR, "Unsupported stream type.\n");
            return AVERROR(EINVAL);
        }
    }
    if (audio_index == -1 && video_index == -1) {
        av_log(ctx, AV_LOG_ERROR, "At least one audio or video stream must be present.\n");
        return AVERROR(EINVAL);
    }    

    if (audio_index >= 0) {
        AVStream* stream = ctx->streams[audio_index];
        AVCodecParameters *codecpar = stream->codecpar;
        sample_rate = codecpar->sample_rate;
        audio_buffer_size = s->audio_buffer_size;

        if (s->timestamp_mode == SHMEM_TSM_ABSOLUTE)
            avpriv_set_pts_info(stream, 64, 1, 1000000);
    }
    if (video_index >= 0) {
        AVStream* stream = ctx->streams[video_index];
        AVCodecParameters *codecpar = stream->codecpar;
        width = codecpar->width;
        height = codecpar->height;
        video_buffer_size = s->video_buffer_size;

        if (s->timestamp_mode == SHMEM_TSM_ABSOLUTE)
            avpriv_set_pts_info(stream, 64, 1, 1000000);
    }

    s->writer = shm_writer_create(ctx->filename, video_buffer_size, s->max_consumers, audio_buffer_size);
    if (!s->writer)
        return AVERROR(ENXIO);

    rc = shm_writer_open(s->writer, width, height, 0, sample_rate);
    if (rc < 0) {
        shm_writer_destroy(s->writer);
        s->writer = NULL;
        av_log(ctx, AV_LOG_ERROR, "Could not open shared memory %s\n", ctx->filename);
        return AVERROR(ENXIO);
    }

    s->audio_index = audio_index;
    s->video_index = video_index;
    s->realtime_ts_offset = AV_NOPTS_VALUE;

    return 0;
}

static int shmem_write_video_frame(AVFormatContext *ctx, const AVFrame* frame, int64_t pts)
{
    ShmemOutContext *s = ctx->priv_data;
    const AVStream* st = ctx->streams[s->video_index];
    const AVCodecParameters* codecpar = st->codecpar;
    shm_video_frame_t video_frame;

    if (shm_writer_open_video_buffer(s->writer, &video_frame) != 0) {
        if (s->ignore_nospace)
            return 0;
        av_log(ctx, AV_LOG_ERROR, "Could not allocate video buffer\n");
        return AVERROR_EXTERNAL;
    }

    av_image_copy(video_frame.data, video_frame.linesize,
                  (const uint8_t**)frame->data, frame->linesize,
                  codecpar->format, codecpar->width, codecpar->height);
    video_frame.pts = shmem_adjust_timestamp(s, st, pts);

    if (shm_writer_close_video_buffer(s->writer, &video_frame) != 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not close video buffer\n");
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int shmem_write_audio_frame(AVFormatContext *ctx, const AVFrame* frame, int64_t pts)
{
    ShmemOutContext *s = ctx->priv_data;
    const uint16_t* data = (const uint16_t*)(frame->data[0]);
    pts = shmem_adjust_timestamp(s, ctx->streams[s->audio_index], pts);

    shm_writer_write_audio_buffer(s->writer, data, frame->nb_samples, pts);
    return 0;
}

static int shmem_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    ShmemOutContext *s = ctx->priv_data;
    
    if (pkt == NULL)
        return 0;       // Just skip null packets

    if (pkt->stream_index == s->video_index)
        return shmem_write_video_frame(ctx, (const AVFrame*)(pkt->data), pkt->pts);
    if (pkt->stream_index == s->audio_index) {
        AVFrame frame;

        frame.data[0] = pkt->data;
        frame.nb_samples = pkt->size / ctx->streams[s->audio_index]->codecpar->block_align;
        frame.pts = pkt->pts;
        return shmem_write_audio_frame(ctx, &frame, pkt->pts);
    }
    
    return AVERROR(EINVAL);
}

static int shmem_write_frame(AVFormatContext *ctx, int stream_index, AVFrame **frame,
                          unsigned flags)
{
    ShmemOutContext *s = ctx->priv_data;   
    if ((flags & AV_WRITE_UNCODED_FRAME_QUERY))
        return 0;

    if (!frame)
        return 0;   // Skip empty frames

    if (stream_index == s->video_index)
        return shmem_write_video_frame(ctx, *frame, (*frame)->pts);
    if (stream_index == s->audio_index) {
        return shmem_write_audio_frame(ctx, *frame, (*frame)->pts);
    }
    
    return AVERROR(EINVAL);
}

static int shmem_write_trailer(AVFormatContext *ctx)
{
    ShmemOutContext *s = ctx->priv_data;
    if (s->writer) {
        shm_writer_close(s->writer);
        shm_writer_destroy(s->writer);
        s->writer = NULL;
    }
    return 0;
}

#define OFFSET(x) offsetof(ShmemOutContext, x)
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "max_consumers",  "set maximum number of consumers",  OFFSET(max_consumers),      AV_OPT_TYPE_INT,    {.i64 = 16 }, 1, 128,   E },
    { "video_size",     "set video buffer size (frames)",   OFFSET(video_buffer_size),  AV_OPT_TYPE_INT,    {.i64 = 30 }, 1, 512,   E },
    { "audio_size",     "set audio buffer size (seconds)",  OFFSET(audio_buffer_size),  AV_OPT_TYPE_INT,    {.i64 = 5  }, 1, 60,    E },
    { "timestamps",     "timestamp handling mode",          OFFSET(timestamp_mode),     AV_OPT_TYPE_INT,    {.i64 = SHMEM_TSM_RELATIVE }, 0, 3, E, "timestamps"},
    { "relative",       NULL,                               0,                          AV_OPT_TYPE_CONST,  {.i64 = SHMEM_TSM_RELATIVE},  0, 0, E, "timestamps"},
    { "ignore",         NULL,                               0,                          AV_OPT_TYPE_CONST,  {.i64 = SHMEM_TSM_IGNORE},    0, 0, E, "timestamps"},
    { "absolute",       NULL,                               0,                          AV_OPT_TYPE_CONST,  {.i64 = SHMEM_TSM_ABSOLUTE},  0, 0, E, "timestamps"},
    { "ignore_nospace", "Ignore full shared memory",        OFFSET(ignore_nospace),     AV_OPT_TYPE_BOOL,   {.i64 = 0  }, 0, 1,     E },
    { NULL }
};

static const AVClass shmem_muxer_class = {
    .class_name     = "Epiphan shared memory output device",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
    .category       = AV_CLASS_CATEGORY_DEVICE_OUTPUT,
};

AVOutputFormat ff_shmem_muxer = {
    .name           = "shmem",
    .long_name      = NULL_IF_CONFIG_SMALL("Epiphan shared memory a/v output"),
    .priv_data_size = sizeof(ShmemOutContext),
    .audio_codec    = AV_CODEC_ID_PCM_S16LE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .subtitle_codec = AV_CODEC_ID_NONE,
    .init           = shmem_init,
    .write_header   = shmem_write_header,
    .write_packet   = shmem_write_packet,
    .write_uncoded_frame = shmem_write_frame, 
    .write_trailer  = shmem_write_trailer,
    .flags          = AVFMT_NOFILE | AVFMT_RAWPICTURE | AVFMT_VARIABLE_FPS,
    .priv_class     = &shmem_muxer_class,
};
