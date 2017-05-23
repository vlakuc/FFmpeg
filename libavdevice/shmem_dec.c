/*
 * Epiphan Shared Memory input
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
 * Epiphan Shared Memory input device
 */

#include "config.h"

#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <dirent.h>
#include <libshm/libshm.h>

#include "libavutil/pixdesc.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libavutil/buffer_internal.h"

#include "libavformat/internal.h"

#include "avdevice.h"

#define SHMEM_NO_INPUT 0
#define SHMEM_A_INPUT  1
#define SHMEM_V_INPUT  2
#define SHMEM_AV_INPUT 3

typedef struct ShmemInContext_ {
    AVClass *class;
    // Command line parameters:
    AVRational framerate;             // desirable reading framerate
    int        force_framerate;       // flag to make shmem producer write with provided framerate
    int        input;                 // input source: video|audio|all
    int        realtime;              // read the last frame from shared memory
    char*      threshold;             // take frames, that satisfy threshold
    int        threshold_min;         //    minimum threshold value
    int        threshold_max;         //    maximum threshold value

    // Technical fields:
    int64_t    video_prev_pts;        // previous read video pts
    int64_t    video_prev_read;       // previous successful shmem_lock/unlock call time
    int64_t    video_delay;           // delay to read frame delayed by this value
    int64_t    frames;                // total umbers of read frames
    int        has_video;             // does source shmem contains video

    //     fields to provide realtime video reading:
    int64_t    video_pts;             // current video pts
    int64_t    next_frame_read_time;  // next desired pts value
    int64_t    last_read_frame_ts;    // previuos frame pts
    int64_t    current_shm_time;      // time of newest shmem frame

    int64_t    audio_pts;             // current audio pts
    int        has_audio;             // does source shmem contains audio

    int           close_reader;       // flag to show whether shm_reader should be closed
    shm_reader_t* reader;             // pointer to shm_reader structure
} ShmemInContext;

typedef struct opaque_shm_frame_ {
    shm_video_frame_t video_frame;
    shm_reader_t* s;
} opaque_shm_frame_t;

static void destruct_packet_with_locked_frame(void *opaque, uint8_t *data)
{
    opaque_shm_frame_t *o = (opaque_shm_frame_t *)opaque;

    if ( o != NULL ) {
        shm_reader_unlock_video(o->s, &o->video_frame);
        free(o);
    }

    av_free(data);
}

static int open_video_stream(AVFormatContext *ctx, const char *shm_name)
{
    AVStream *stream = NULL;
    ShmemInContext *s = ctx->priv_data;
    int16_t width, height;
    int bytes_per_pixel, frame_linesize, frame_size;

    if (!shm_reader_is_ready(s->reader))
        return AVERROR(EAGAIN);

    if (s->force_framerate) {
        shm_reader_set_fps(s->reader, s->framerate.num, s->framerate.den);
    }

    shm_reader_video_frame_size(s->reader, &width, &height);
    bytes_per_pixel = 12; /* YUV420p */
    frame_linesize  = width * bytes_per_pixel;
    frame_size      = frame_linesize * height;

    if (!frame_size) {
        av_log(ctx, AV_LOG_INFO, "No video in shared memory '%s'\n", shm_name);
        return AVERROR(EIO);
    }

    s->video_prev_pts = AV_NOPTS_VALUE;
    s->has_video = 1;

    if (!(stream = avformat_new_stream(ctx, NULL)))
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(stream, 64, 1, 1000000); /* 64 bits pts in microseconds */

    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    stream->codecpar->width      = width;
    stream->codecpar->height     = height;
    stream->codecpar->format     = AV_PIX_FMT_YUV420P;
    stream->codecpar->bit_rate   = width * height * bytes_per_pixel * av_q2d(s->framerate) * 8;
    stream->avg_frame_rate       = s->framerate;
    stream->index                = s->has_audio;
    stream->id                   = s->has_audio;

    av_log(ctx, AV_LOG_INFO,
           "VIDEO w:%d h:%d pixfmt:%s fps:%d/%d bit_rate:%"PRId64"\n",
           width, height,
           av_get_pix_fmt_name(AV_PIX_FMT_YUV420P),
           s->framerate.num, s->framerate.den,
           (int64_t)stream->codecpar->bit_rate);

    return 0;
}

static int open_audio_stream(AVFormatContext *ctx, const char *shm_name)
{
    AVStream *stream = NULL;
    ShmemInContext *s = ctx->priv_data;
    int sample_rate = 0;

    if (!shm_reader_is_ready(s->reader))
        return AVERROR(EAGAIN);

    sample_rate = shm_reader_audio_sampling_rate(s->reader);

    if (!sample_rate) {
        av_log(ctx, AV_LOG_INFO, "No audio in shared memory '%s'\n", shm_name);
        return AVERROR(EIO);
    }

    s->audio_pts = AV_NOPTS_VALUE;
    s->has_audio = 1;

    if (!(stream = avformat_new_stream(ctx, NULL)))
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(stream, 64, 1, 1000000); /* 64 bits pts in microseconds */

    stream->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    stream->codecpar->format      = AV_SAMPLE_FMT_S16;
    stream->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
    stream->codecpar->sample_rate = sample_rate;
    stream->codecpar->channels    = SHM_AUDIO_CHANNELS;
    stream->codecpar->frame_size  = SHM_AUDIO_CHANNELS * sizeof(uint16_t) * 1024;
    stream->index                 = s->has_video;
    stream->id                    = s->has_video;

    av_log(ctx, AV_LOG_INFO,
           "AUDIO codec:AV_CODEC_ID_PCM_S16LE sample_rate:%d channels:%d\n", stream->codecpar->sample_rate, stream->codecpar->channels);

    return 0;
}

static int parse_threshold(AVFormatContext *ctx)
{
    ShmemInContext *s = ctx->priv_data;

    if (!s->threshold)
        return 0;

    if (!sscanf(s->threshold, "%d,%d", &s->threshold_min, &s->threshold_max)) {
        errno = EINVAL;
        return errno;
    }

    if (s->threshold_min > s->threshold_max) {
        errno = EINVAL;
        return errno;
    }

    return 0;
}

static int shmem_read_header(AVFormatContext *ctx)
{
    ShmemInContext *s = ctx->priv_data;
    int rc = 0;
    char *shm_name = NULL;

    shm_name = av_strdup(ctx->filename);
    if (!shm_name) {
        rc = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "No shared memory name provided\n");
        goto out;
    }

    s->reader = shm_reader_open(shm_name);
    if (!s->reader) {
        rc = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "Couldn't open shared memory '%s'\n", shm_name);
        goto out;
    }

    if (!shm_reader_is_ready(s->reader)) {
        rc = AVERROR(EBUSY);
        av_log(ctx, AV_LOG_ERROR, "Shared memory '%s' is not ready\n", shm_name);
        goto out;
    }

    if (parse_threshold(ctx)) {
        rc = AVERROR(errno);
        goto out;
    }

    s->has_video       = 0;
    s->has_audio       = 0;
    s->video_prev_pts  = AV_NOPTS_VALUE;
    s->video_prev_read = 0;
    s->video_delay     = (s->realtime)?0:(5.0e6 / av_q2d(s->framerate));
    s->frames          = 0;

    switch (s->input) {
    case SHMEM_V_INPUT:
        rc = open_video_stream(ctx, shm_name);
        break;
    case SHMEM_A_INPUT:
        rc = open_audio_stream(ctx, shm_name);
        break;
    default:
        rc = open_video_stream(ctx, shm_name);
        rc &= open_audio_stream(ctx, shm_name);
    }

out:
    av_free(shm_name);
    return rc;
}

static int64_t precise_pts(AVFormatContext *ctx)
{
    ShmemInContext *s = ctx->priv_data;
    int i = 0;
    int min_diff_i = 0;
    int jitter = shm_reader_get_jitter(s->reader);
    int duration = 1.0e6/av_q2d(s->framerate);
    int64_t desired_pts = s->video_prev_pts;
    int64_t now = shm_reader_get_video_pts(s->reader) - s->video_delay;
    int64_t timestamps[7] = {now - duration - jitter, now - duration, now - duration + jitter,
                             now,
                             now + duration - jitter, now + duration, now + duration + jitter};
    int64_t differencies[7] = {0};

    for (i = 0; i < 7 ; i++) {
        shm_video_frame_t frame = {0};
        frame.pts = timestamps[i];
        shm_reader_lock_video(s->reader, &frame);
        timestamps[i] = frame.pts;
        differencies[i] = abs(timestamps[i] - desired_pts);
        shm_reader_unlock_video(s->reader, &frame);
    }

    for (i = 1; i < 7; i++) {
        if (differencies[min_diff_i] > differencies[i]) {
            min_diff_i = i;
        }
    }

    return timestamps[min_diff_i];
}

static int read_video_packet_realtime(AVFormatContext *ctx, AVPacket *pkt)
{
    ShmemInContext *s = ctx->priv_data;
    int64_t delay = 0, enter_time = 0;
    int rc = 0;
    int frame_size = 0;
    int frame_duration = (1.0e6 / av_q2d(s->framerate));
    int max_duration_delta = frame_duration / 2;
    int need_repeat = 0;
    opaque_shm_frame_t *o = (opaque_shm_frame_t *)malloc(sizeof(opaque_shm_frame_t));;
    o->s = s->reader;

    if (!shm_reader_is_ready(s->reader)) {
        rc = AVERROR(EOF);
        av_log(ctx, AV_LOG_INFO,
               "VIDEO memory not ready\n");

        goto out;
    }

    o->video_frame.pts = 0;
    enter_time = av_gettime();

    if(s->next_frame_read_time != AV_NOPTS_VALUE)
    {
        delay = s->next_frame_read_time - enter_time;
        if(delay > 0)
            usleep(delay);
        else
            av_log(ctx, AV_LOG_WARNING, "delay is negative. (%lu)", delay);
    }
    else
        s->next_frame_read_time = enter_time;

    s->next_frame_read_time = av_gettime() + (1.0e6 / av_q2d(s->framerate));

    need_repeat = 0;
    do
    {
        o->video_frame.pts = av_gettime();
        rc = shm_reader_lock_video(s->reader, &o->video_frame);

        if (rc < 0) {
            av_log(ctx, AV_LOG_ERROR, "Could not read video frame from shm at pts:%lu\n", o->video_frame.pts);
            rc = AVERROR(EAGAIN);
            goto out;
        }
        if(s->last_read_frame_ts == AV_NOPTS_VALUE)
        {
            s->last_read_frame_ts = o->video_frame.pts;
            s->current_shm_time = o->video_frame.pts;
        }
        else
        {
            s->current_shm_time += (1.0e6 / av_q2d(s->framerate));

            if(o->video_frame.pts == s->last_read_frame_ts )
            {
                if( (s->current_shm_time + max_duration_delta) >= (s->last_read_frame_ts + shm_reader_get_avg_frame_duration(s->reader)) )
                {
                    shm_reader_unlock_video(s->reader, &o->video_frame);
                    usleep(shm_reader_get_jitter(s->reader));
                    need_repeat = 1;
                }
            }
            else
            {
                s->last_read_frame_ts = o->video_frame.pts;
                s->current_shm_time = o->video_frame.pts;
                if(need_repeat)
                    need_repeat = 0;
            }
        }
    }
    while(need_repeat);

    av_log(ctx, AV_LOG_TRACE, "RET = %lu\n", o->video_frame.pts);

    frame_size = o->video_frame.height * o->video_frame.linesize[0] + \
        (o->video_frame.height / 2) * o->video_frame.linesize[1] +    \
        (o->video_frame.height / 2) * o->video_frame.linesize[2];

    rc = av_new_packet(pkt, frame_size);
    if (rc == 0) {
        av_log(ctx, AV_LOG_DEBUG, "pts: (%lu, %lu)\n", o->video_frame.pts, av_gettime());
        pkt->data = o->video_frame.data[0];
        pkt->pts = ++s->frames * (1.0e6 / av_q2d(s->framerate));
        pkt->size = frame_size;
        pkt->duration = 1.0e6 / av_q2d(s->framerate);
        pkt->buf->buffer->opaque = o;
        pkt->buf->buffer->free = &destruct_packet_with_locked_frame;
    }
    else {
        av_log(ctx, AV_LOG_ERROR,
               "Could not allocate memory for packet\n");
        rc = AVERROR(ENOMEM);
        goto out;
    }

    s->video_pts = o->video_frame.pts;
    return 0;

out:
    free(o);
    return rc;
}

static int read_video_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    ShmemInContext *s = ctx->priv_data;
    int64_t read_delay = 0;
    int rc = 0;
    int frame_size = 0;
    int64_t video_pts = 0;
    opaque_shm_frame_t *o = (opaque_shm_frame_t *)malloc(sizeof(opaque_shm_frame_t));
    o->s = s->reader;
    memset(&o->video_frame, 0, sizeof(shm_video_frame_t));

    if (!shm_reader_is_ready(s->reader)) {
        rc = AVERROR(EOF);
        av_log(ctx, AV_LOG_INFO,
               "VIDEO memory not ready\n");
        goto out;
    }

    if (s->realtime || s->threshold) {
        video_pts = shm_reader_get_video_pts(s->reader);
        goto realtime_read;
    }

    read_delay = 1.0e6/av_q2d(s->framerate) - (av_gettime() - s->video_prev_read);
    if (read_delay > 0) {
        usleep(read_delay);
    }

    if (s->video_prev_pts == AV_NOPTS_VALUE)
        video_pts = shm_reader_get_video_pts(s->reader) - s->video_delay;
    else
        video_pts = precise_pts(ctx);

realtime_read:
    o->video_frame.pts = video_pts;

    rc = shm_reader_lock_video(s->reader, &o->video_frame);
    if (rc < 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not read video frame from shm at pts:%lu\n", o->video_frame.pts);
        rc = AVERROR(EAGAIN);
        goto out;
    }

    if (s->video_prev_pts > o->video_frame.pts && 
        (av_gettime() - s->video_prev_read < shm_reader_get_avg_frame_duration(s->reader))) {
        shm_reader_unlock_video(s->reader, &o->video_frame);
        rc = AVERROR(EAGAIN);
        goto out;
    }

    frame_size = o->video_frame.height * o->video_frame.linesize[0] + \
        (o->video_frame.height / 2) * o->video_frame.linesize[1] +    \
        (o->video_frame.height / 2) * o->video_frame.linesize[2];

    rc = av_new_packet(pkt, frame_size);
    if (rc == 0) {
        av_log(ctx, AV_LOG_DEBUG, "pts: (%lu, %lu)\n", o->video_frame.pts, av_gettime());
        s->video_prev_read = av_gettime();
        s->video_prev_pts = o->video_frame.pts;

        pkt->data = o->video_frame.data[0];
        pkt->pts = ++s->frames * (1.0e6 / av_q2d(s->framerate));
        pkt->size = frame_size;

        pkt->buf->buffer->opaque = (void*)o;
        pkt->buf->buffer->free = destruct_packet_with_locked_frame;
    }
    else {
        av_log(ctx, AV_LOG_ERROR,
               "Could not allocate memory for packet\n");
        rc = AVERROR(ENODATA);
        goto out;
    }

    if (s->threshold) {
        int64_t tr = (s->video_prev_read - o->video_frame.pts) / 1000;
        if ((tr < s->threshold_min) || (s->threshold_max < tr)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Could not read video frame with provided threshold; got pts '%lu'(%lu))\n", o->video_frame.pts, tr);
            shm_reader_unlock_video(s->reader, &o->video_frame);
            rc = AVERROR(ENODATA);
            goto out;
        }
        av_log(ctx, AV_LOG_DEBUG,
               "Frame satisfies threshold\n");
    }

    return 0;

out:
    free(o);
    return rc;
}

static int read_audio_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    ShmemInContext *s = ctx->priv_data;

    int sample_rate = 0;
    int need_samples = 0;
    int rc = 0;
    int audio_size = 0;
    int sample_size = SHM_AUDIO_CHANNELS * sizeof(uint16_t);

    if (!shm_reader_is_ready(s->reader)) {
        return AVERROR(EOF);
    }

    sample_rate = shm_reader_audio_sampling_rate(s->reader);
    need_samples = 1024;//sample_rate * 100 / 1000;
    audio_size = need_samples * sample_size;

    if ((need_samples - shm_reader_available_samples_count(s->reader)) > 0) {
        return AVERROR(EAGAIN);
    }

    rc = av_new_packet(pkt, audio_size);
    if (rc != 0) {
        return rc;
    }

    if (s->audio_pts == AV_NOPTS_VALUE)
        s->audio_pts = av_gettime() - need_samples * 1.0e6 / sample_rate;

    if (shm_reader_query_audio(s->reader, need_samples, (int16_t *)pkt->data, &s->audio_pts ) != 0) {
        av_packet_unref(pkt);
        av_log(ctx, AV_LOG_ERROR,
               "Could not read audio frame for pts = %.6f\n", s->audio_pts/1.0e6);
        return AVERROR(EAGAIN);
    }

    pkt->size     = audio_size;
    pkt->pts      = s->audio_pts;
    pkt->duration = need_samples * 1.0e6 / sample_rate;

    s->audio_pts += pkt->duration;

    return 0;
}

static int shmem_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    AVStream *stream = ctx->streams[pkt->stream_index];
    ShmemInContext *s = ctx->priv_data;

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && s->reader) {
        if (s->realtime)
            return read_video_packet_realtime(ctx, pkt);
        else
            return read_video_packet(ctx, pkt);
    }
    else {
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && s->reader)
            return read_audio_packet(ctx, pkt);
    }

    return 0;
}

static int shmem_read_close(AVFormatContext *ctx)
{
    ShmemInContext *s = ctx->priv_data;

    av_log(ctx, AV_LOG_DEBUG, "shmem_read_close\n");
    if (s->reader) {
        if (!s->has_video || !s->has_audio || s->close_reader) {
            shm_reader_close(s->reader);
            return 0;
        }

        s->close_reader = 1;
    }

    return 0;
}

#define OFFSET(x) offsetof(ShmemInContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "framerate",       "Reading framerate",
      OFFSET(framerate),         AV_OPT_TYPE_VIDEO_RATE, {.str = "30"}, 1,  INT_MAX, DEC},
    { "force_framerate", "Force shm producer to write at the same framerate",
      OFFSET(force_framerate),   AV_OPT_TYPE_BOOL,       {.i64 = 0},    0,  1,       DEC},
    { "realtime",        "Read the latest frame from shared memory; framerate doesn't matter",
      OFFSET(realtime),          AV_OPT_TYPE_BOOL,       {.i64 = 0},    0,  1,       DEC},
    { "threshold",       "Set appropriate deviation from current time",
      OFFSET(threshold),         AV_OPT_TYPE_STRING,     {.str = NULL},   0,  0,       DEC},

    { "input", "", OFFSET(input), AV_OPT_TYPE_INT,   {.i64 = SHMEM_AV_INPUT}, 0, 3, DEC, "input"},
    { "all",   "", OFFSET(input), AV_OPT_TYPE_CONST, {.i64 = SHMEM_AV_INPUT}, 0, 3, DEC, "input"},
    { "video", "", OFFSET(input), AV_OPT_TYPE_CONST, {.i64 = SHMEM_V_INPUT},  0, 3, DEC, "input"},
    { "audio", "", OFFSET(input), AV_OPT_TYPE_CONST, {.i64 = SHMEM_A_INPUT},  0, 3, DEC, "input"},
    { NULL },
};

static const AVClass shmem_demuxer_class = {
    .class_name = "Epiphan shared memory indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_INPUT,
};

AVInputFormat ff_shmem_demuxer = {
    .name           = "sharedmemory,shmem",
    .long_name      = NULL_IF_CONFIG_SMALL("Epiphan shared memory a/v input"),
    .priv_data_size = sizeof(ShmemInContext),
    .read_header    = shmem_read_header,
    .read_packet    = shmem_read_packet,
    .read_close     = shmem_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &shmem_demuxer_class,
};
