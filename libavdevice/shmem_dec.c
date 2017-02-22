#include "config.h"

#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <dirent.h>
#include <libshm.h>

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

typedef struct ShmemContext_ {
    AVClass *class;

    AVRational framerate;
    int        force_framerate;
    int        input;
    int        realtime;
    char*      threshold;
    int        threshold_min;
    int        threshold_max;

    int64_t    video_pts;
    int        has_video;

    int64_t    audio_pts;
    int        has_audio;

    shm_reader_t* reader;
} ShmemContext;

typedef struct opaque_stuff {
    shm_video_frame_t video_frame;
    ShmemContext* s;
} opaque_stuff_t;

static void destruct_packet_with_locked_frame(void *opaque, uint8_t *data)
{
    opaque_stuff_t *o = (opaque_stuff_t *)opaque;

    shm_reader_unlock_video(o->s->reader, &o->video_frame);
    free(o);
}

static int open_video_stream(AVFormatContext *ctx, const char *shm_name)
{
    AVStream *stream = NULL;
    ShmemContext *s = ctx->priv_data;
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

    s->video_pts = AV_NOPTS_VALUE;
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
    ShmemContext *s = ctx->priv_data;
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
    stream->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
    stream->codecpar->sample_rate = sample_rate;
    stream->codecpar->channels    = SHM_AUDIO_CHANNELS;

    av_log(ctx, AV_LOG_INFO,
           "AUDIO codec:AV_CODEC_ID_PCM_S16LE sample_rate:%d channels:%d\n", stream->codecpar->sample_rate, stream->codecpar->channels);

    return 0;
}

static int parse_threshold(AVFormatContext *ctx)
{
    ShmemContext *s = ctx->priv_data;

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
    ShmemContext *s = ctx->priv_data;
    int rc = 0, rc2 = 0;
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

    switch (s->input) {
    case SHMEM_V_INPUT:
        rc = open_video_stream(ctx, shm_name);
        break;
    case SHMEM_A_INPUT:
        rc = open_audio_stream(ctx, shm_name);
        break;
    default:
        rc = open_video_stream(ctx, shm_name);
        rc2 = open_audio_stream(ctx, shm_name);
        if (rc && rc2) {
            rc = AVERROR(EIO);
            goto out;
        }
        else
            return 0;
    }

    return rc;

out:
    av_free(shm_name);
    return rc;
}

static int read_video_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    ShmemContext *s = ctx->priv_data;
    int64_t delay = 0, enter_time = 0;
    int rc = 0;
    opaque_stuff_t *o = (opaque_stuff_t *)malloc(sizeof(shm_video_frame_t));;
    o->s = s;

    if (!shm_reader_is_ready(s->reader)) {
        rc = AVERROR(EAGAIN);
        goto out;
    }

    if (s->video_pts == AV_NOPTS_VALUE) {
        s->video_pts = av_gettime();
        if (!s->realtime)
            s->video_pts -= 2.0e6 / av_q2d(s->framerate);
    }

    if (s->realtime) {
        while (1) {
            s->video_pts = av_gettime();
            o->video_frame.pts = s->video_pts;
            rc = shm_reader_lock_video(s->reader, &o->video_frame);
            if (rc >= 0) {
                if (o->video_frame.pts > s->video_pts) {
                    break;
                } else {
                    struct timespec t = {0, 1000};
                    shm_reader_unlock_video(s->reader, &o->video_frame);
                    nanosleep(&t, &t);
                }
            }
        }
    } else {
        enter_time = av_gettime();
        delay = 1.0e6 / av_q2d(s->framerate);
        usleep(delay);
        o->video_frame.pts = s->video_pts;
        rc = shm_reader_lock_video(s->reader, &o->video_frame);
    }

    if (rc >= 0) {
        int frame_size = o->video_frame.height * o->video_frame.linesize[0] + \
            (o->video_frame.height / 2) * o->video_frame.linesize[1] + \
            (o->video_frame.height / 2) * o->video_frame.linesize[2];

        rc = av_new_packet(pkt, frame_size);
        if (rc == 0) {
            pkt->data = o->video_frame.data[0];
            pkt->pts = o->video_frame.pts;
            pkt->size = frame_size;
            pkt->duration = shm_reader_get_avg_frame_duration(s->reader);
            pkt->buf->buffer->opaque = o;
            pkt->buf->buffer->free = &destruct_packet_with_locked_frame;
        }

        if (s->threshold) {
            if ((o->video_frame.pts < s->threshold_min) || (s->threshold_max < o->video_frame.pts)) {
                rc = AVERROR(ENODATA);
            }
        }
    } else {
        av_log(ctx, AV_LOG_ERROR,
               "Could not read video frame from shm\n");
        rc = AVERROR(EAGAIN);
        goto out;
    }

    s->video_pts += av_gettime() - enter_time;

    return 0;

out:
    free(o);
    return rc;
}

static int read_audio_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    /*DEFAULT_AUDIO_GRAB_DURATION = 100*/
    ShmemContext *s = ctx->priv_data;

    int sample_rate = 0;
    int need_samples = 0;
    int rc = 0;
    int64_t pts = 0;
    int64_t current_time = av_gettime();
    int audio_size = 0;
    int sample_size = SHM_AUDIO_CHANNELS * sizeof(uint16_t);

    if (!shm_reader_is_ready(s->reader)) {
        rc = AVERROR(EAGAIN);
        goto out;
    }

    sample_rate = shm_reader_audio_sampling_rate(s->reader);
    need_samples = 1024;//sample_rate * 100 / 1000;
    audio_size = need_samples * sample_size;

    while (1) {
        if ((need_samples - shm_reader_available_samples_count(s->reader)) > 0) {
            struct timespec t = {0, 1000};
            nanosleep(&t, &t);
        } else {
            break;
        }
    }


    if ((need_samples - shm_reader_available_samples_count(s->reader)) > 0)
    {
        rc = AVERROR(EAGAIN);
        goto out;
    }

    if (av_new_packet(pkt, audio_size) < 0) {
        return AVERROR(EIO);
    }

    if (s->audio_pts == AV_NOPTS_VALUE)
        s->audio_pts = shm_reader_get_audio_pts(s->reader) - need_samples * 1.0e6 / sample_rate;

    if (shm_reader_query_audio(s->reader, need_samples, (int16_t *)pkt->data, &s->audio_pts ) != 0) {
        av_packet_unref(pkt);
        av_log(ctx, AV_LOG_ERROR,
               "Could not read audio frame for pts = %.6f\n", pts/1.0e6);
        rc = AVERROR(EAGAIN);
    }

    pkt->size     = audio_size;
    pkt->pts      = s->audio_pts;
    pkt->duration = need_samples * 1.0e6 / sample_rate;

    s->audio_pts += pkt->duration;

out:
    return rc;
}

static int shmem_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    AVStream *stream = ctx->streams[pkt->stream_index];
    ShmemContext *s = ctx->priv_data;

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && s->reader)
        return read_video_packet(ctx, pkt);
    else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && s->reader)
        return read_audio_packet(ctx, pkt);

    return 0;
}

static int shmem_read_close(AVFormatContext *ctx)
{
    ShmemContext *s = ctx->priv_data;

    if (s->reader)
        shm_reader_close(s->reader);

    return 0;
}

static int shmem_get_device_list(AVFormatContext *ctx, AVDeviceInfoList *device_list)
{
    DIR *dir;
    struct dirent *entry;
    AVDeviceInfo *device = NULL;
    int rc = 0;

    dir = opendir("/dev/shm");
    if (!dir) {
        rc = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "Couldn't open the directory: %s\n", av_err2str(rc));
        return rc;
    }

    while ((entry = readdir(dir))) {
        snprintf(ctx->filename, sizeof(ctx->filename), "/dev/shm/%s", entry->d_name);
//        dump_shmem(ctx, entry->d_name);
        device = av_mallocz(sizeof(AVDeviceInfo));
        if (!device) {
            rc = AVERROR(ENOMEM);
            goto fail;
        }

        device->device_name =        av_strdup(entry->d_name);
        device->device_description = av_strdup("shared memory");
        if (!device->device_name || !device->device_description) {
            rc = AVERROR(ENOMEM);
            goto fail;
        }

        if ((rc = av_dynarray_add_nofree(&device_list->devices,
                                         &device_list->nb_devices, device)) < 0)
            goto fail;

        continue;

      fail:
        if (device) {
            av_freep(&device->device_name);
            av_freep(&device->device_description);
            av_freep(&device);
        }
        break;
    }
    closedir(dir);
    return rc;
}


#define OFFSET(x) offsetof(ShmemContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "framerate",       "Reading framerate",
      OFFSET(framerate),         AV_OPT_TYPE_VIDEO_RATE, {.str = "30"}, 1,  INT_MAX, DEC},
    { "force_framerate", "Force shm producer to write at the same framerate",
      OFFSET(force_framerate),   AV_OPT_TYPE_BOOL,       {.i64 = 0},    0,  1,       DEC},
    { "realtime",        "Read the latest frame from shared memory; framerate doesn't matter",
      OFFSET(realtime),          AV_OPT_TYPE_BOOL,       {.i64 = 0},    0,  1,       DEC},
    { "threshold",       "Set appropriate deviation from current time",
      OFFSET(threshold),         AV_OPT_TYPE_STRING,     {.str = ""},   0,  0,       DEC},

    { "input", "", OFFSET(input), AV_OPT_TYPE_INT,   {.i64 = SHMEM_AV_INPUT}, 0, 3, DEC, "input"},
    { "all",   "", OFFSET(input), AV_OPT_TYPE_CONST, {.i64 = SHMEM_AV_INPUT}, 0, 3, DEC, "input"},
    { "video", "", OFFSET(input), AV_OPT_TYPE_CONST, {.i64 = SHMEM_V_INPUT},  0, 3, DEC, "input"},
    { "audio", "", OFFSET(input), AV_OPT_TYPE_CONST, {.i64 = SHMEM_A_INPUT},  0, 3, DEC, "input"},
    { NULL },
};

static const AVClass shmem_class = {
    .class_name = "Epiphan shared memory indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT | AV_CLASS_CATEGORY_DEVICE_AUDIO_INPUT,
};

AVInputFormat ff_shmem_demuxer = {
    .name           = "sharedmemory,shmem",
    .long_name      = NULL_IF_CONFIG_SMALL("Epiphan shared memory a/v input"),
    .priv_data_size = sizeof(ShmemContext),
    .read_header    = shmem_read_header,
    .read_packet    = shmem_read_packet,
    .read_close     = shmem_read_close,
    .get_device_list = shmem_get_device_list,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &shmem_class,
};
