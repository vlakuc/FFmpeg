/*
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

#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "libavutil/timestamp.h"
#include "libavutil/time.h"
#include "libavutil/bprint.h"

#include "avfilter.h"
#include "internal.h"

#if CONFIG_SYSINFO
#include <sysinfo/sysinfo.h>
#endif //CONFIG_SYSINFO


/**
 * How to generate simple avsync pattern:
 *  ffmpeg -hide_banner -y                        \
 *    -f lavfi -i "color=c=white:s=hd720:rate=30"                       \
 *    -f lavfi -i sine="frequency=8000:sample_rate=48000:samples_per_frame=240" \
 *     -vf "[v]drawbox=enable='lt(mod(t,10),5)':color=invert:t=max;\
 *     -af "[a]volume=15dB[preamp];[preamp]volume=enable='lt(mod(t,10),5):volume=0" \
 *     -c:v libx264 -c:a aac -t 5:00 -f mp4 patten.mp4
 */

//////////////////////////////////////////////////////////////////
// Simple avsync detector





typedef enum ThresholdDetectionAlgorithm {
    TDA_AM,         // arithmetic mean algorithm
    TDA_DB,         // density based algorithm
    TDA_COUNT
} ThresholdDetectionAlgorithm;


enum {
    THRESHOLD_DEFAULT_RANGE = 100,
    THRESHOLD_NB_BUCKETS = 20,
    THRESHOLD_MIN_PACKET_COUNT = 10,
} ThresholdDetectorContants;


typedef struct ThresholdDetectionContext
{
    void* priv;
    void  (*create)(struct ThresholdDetectionContext*, int);
    void  (*destroy)(struct ThresholdDetectionContext* ctx);
    void  (*update)(struct ThresholdDetectionContext* ctx, float value);
    float (*get_threshold)(struct ThresholdDetectionContext* ctx);
    int   (*is_detected)(struct ThresholdDetectionContext* ctx);
} ThresholdDetectionContext;


typedef struct
{
    int range;
    int nb_buckets;
    int bucket_size;
    unsigned int* buckets;
} DensityBasedThresholdDetector;

static void td_db_create(struct ThresholdDetectionContext* c, int nb_buckets)
{
    DensityBasedThresholdDetector* ctx;
    
    av_assert0(c != NULL);
    av_assert0(nb_buckets >= 2);
    av_assert0(nb_buckets <= THRESHOLD_DEFAULT_RANGE);


    ctx = av_mallocz(sizeof(DensityBasedThresholdDetector));
    av_assert0(ctx != NULL);


    ctx->buckets = av_mallocz_array(nb_buckets, sizeof(unsigned int));
    av_assert0(ctx->buckets != NULL);
    
    ctx->range = THRESHOLD_DEFAULT_RANGE;
    ctx->nb_buckets = nb_buckets;
    ctx->bucket_size = ctx->range / ctx->nb_buckets;

    c->priv = ctx;
}

static void td_db_destroy(ThresholdDetectionContext* c)
{
    if (c != NULL && c->priv != NULL) {
        DensityBasedThresholdDetector* ctx = c->priv;
        av_free(ctx->buckets);
        av_free(ctx);
    }
}

static void td_db_update(ThresholdDetectionContext* c, float value)
{
    DensityBasedThresholdDetector* ctx;
    int i;

    av_assert0(c != NULL);
    av_assert0(c->priv != NULL);

    ctx = c->priv;
    
    for (i = 0; i < ctx->nb_buckets && ((value * ctx->range) >= (i + 1) * ctx->bucket_size); ++i) {}
    ctx->buckets[i]++;
}


static inline int find_index_of_max_element(DensityBasedThresholdDetector* ctx, int exclude)
{
    int max = 0, index = -1;
    for (int i = 0; i < ctx->nb_buckets; ++i) {
        if (ctx->buckets[i] > max && i != exclude) {
            max = ctx->buckets[i];
            index = i;
        }
    }
    return index;
}

static float td_db_get_threshold(ThresholdDetectionContext* c)
{
    DensityBasedThresholdDetector* ctx;
    int first, second;
    int middle_bucket_idx;
    
    av_assert0(c != NULL);
    av_assert0(c->priv != NULL);

    ctx = c->priv;
    first = find_index_of_max_element(ctx, INT_MAX);
    second = find_index_of_max_element(ctx, first);

    middle_bucket_idx = (first > second ? second + abs(first - second) : first + abs(first - second)) / 2;

    return (float)(middle_bucket_idx * ctx->bucket_size) / ctx->range;
}

static int td_db_is_detected(ThresholdDetectionContext* c)
{
    DensityBasedThresholdDetector* ctx;
    int first, second;
    av_assert0(c != NULL);
    av_assert0(c->priv != NULL);

    ctx = c->priv;

    first = find_index_of_max_element(ctx, INT_MAX);
    if (first < 0) return 0;
    second = find_index_of_max_element(ctx, first);
    if (second < 0) return 0;
    return 1;
}


static ThresholdDetectionContext* tdc_db_create(int nb_buckets)
{
    ThresholdDetectionContext* ctx = av_malloc(sizeof(ThresholdDetectionContext));
    ctx->create = td_db_create;
    ctx->destroy = td_db_destroy;
    ctx->update = td_db_update;
    ctx->get_threshold = td_db_get_threshold;
    ctx->is_detected = td_db_is_detected;
    
    ctx->create(ctx, nb_buckets);
    return ctx;
}

static void tdc_db_destroy(ThresholdDetectionContext** ctx)
{
    if (ctx != NULL && *ctx != NULL) {
        (*ctx)->destroy(*ctx);
        av_freep(ctx);
    }
}


typedef struct ArithmeticMeanThresholdDetector
{
    float threshold;
    unsigned int count;
} ArithmeticMeanThresholdDetector;


static void td_am_create(struct ThresholdDetectionContext* c, int e)
{
    ArithmeticMeanThresholdDetector* ctx;

    av_assert0(c != NULL);
    
    ctx = av_mallocz(sizeof(ArithmeticMeanThresholdDetector));
    c->priv = ctx;
}

static void td_am_destroy(struct ThresholdDetectionContext* c)
{
    if (c != NULL && c->priv != NULL) {
        av_free(c->priv);
    }
}

static void td_am_update(struct ThresholdDetectionContext* c, float value)
{
    ArithmeticMeanThresholdDetector* ctx;

    av_assert0(c != NULL);
    av_assert0(c->priv != NULL);
    
    ctx = c->priv;
    ctx->count++;
    ctx->threshold += value;
}


static float td_am_get_threshold(ThresholdDetectionContext* c)
{
    ArithmeticMeanThresholdDetector* ctx;
    av_assert0(c != NULL);
    av_assert0(c->priv != NULL);

    ctx = c->priv;
    return ctx->threshold / ctx->count;
}


static int td_am_is_detected(ThresholdDetectionContext* c)
{
    ArithmeticMeanThresholdDetector* ctx;

    av_assert0(c != NULL);
    av_assert0(c->priv != NULL);

    ctx = c->priv;
    return ctx->count > THRESHOLD_MIN_PACKET_COUNT;
}


static ThresholdDetectionContext* tdc_am_create(void)
{
    ThresholdDetectionContext* ctx = av_mallocz(sizeof(ThresholdDetectionContext));
    ctx->create = td_am_create;
    ctx->destroy = td_am_destroy;
    ctx->update = td_am_update;
    ctx->get_threshold = td_am_get_threshold;
    ctx->is_detected = td_am_is_detected;
    ctx->create(ctx, 0);
    return ctx;
}

static void tdc_am_destroy(ThresholdDetectionContext** ctx)
{
    if (ctx != NULL && *ctx != NULL) {
        (*ctx)->destroy(*ctx);
        av_freep(ctx);
    }
}


static ThresholdDetectionContext* tdc_create(enum ThresholdDetectionAlgorithm algo)
{
    switch (algo) {
    case TDA_DB: return tdc_db_create(THRESHOLD_NB_BUCKETS);
    case TDA_AM: 
    default:
        return tdc_am_create();
    }
    return NULL;
}


static void tdc_destroy(enum ThresholdDetectionAlgorithm algo, ThresholdDetectionContext** ctx)
{
    switch (algo) {
    case TDA_DB: return tdc_db_destroy(ctx);
    case TDA_AM: 
    default:
        return tdc_am_destroy(ctx);
    }
}





typedef struct {
    int             state;      // 0 - below threshold, 1 - above threshold
    float           threshold;  
    int64_t         ccts;       // Content Change TimeStamp
    int64_t         pts;        // Last frame timestamp
    float           value;      // Last frame content value (0.0 .. 1.0)

    int64_t         avsync;     // av sync value in microseconds

    ThresholdDetectionContext* tdc;

} AVSyncTrack;


static void init_avsync_track(AVSyncTrack* track, float threshold)
{
    track->state = 0;
    track->threshold = threshold;
    track->ccts = AV_NOPTS_VALUE;
    track->pts  = AV_NOPTS_VALUE;
    track->value = 0.0;
    track->avsync = 0;
    track->tdc = NULL;
}

// Possible return value bits for the update_track_avsync_value/update_avsync_content
#define AVSYNC_STATE_CHANGED (1 << 0)
#define AVSYNC_VALUE_UPDATED (1 << 1)
#define AVSYNC_VALUE_CHANGED (1 << 2)

static int update_track_avsync_value(const AVSyncTrack* master_trk, AVSyncTrack* trk) {
    int result = 0;
    int64_t value;
    
    if (master_trk == trk)
        return 0;
    if (master_trk->ccts == AV_NOPTS_VALUE || trk->ccts == AV_NOPTS_VALUE)
        return 0;
    if (master_trk->state != trk->state)
        return 0;
    
    result = AVSYNC_VALUE_UPDATED;
    value = master_trk->ccts - trk->ccts;
    if (trk->avsync != value)
        result |= AVSYNC_VALUE_CHANGED;
    trk->avsync = value;
    return result;
}

static int update_avsync_content(AVSyncTrack* tracks, unsigned nb_tracks, unsigned master_idx, float value, int64_t pts, unsigned idx)
{
    AVSyncTrack* track = NULL;
    int result = 0;

    av_assert0(idx < nb_tracks);
    av_assert0(master_idx < nb_tracks);

    track = &tracks[idx];

    if (track->ccts == AV_NOPTS_VALUE || ((value > track->threshold) != track->state)) {
        track->state = value > track->threshold;
        track->ccts  = pts;
        result = AVSYNC_STATE_CHANGED;
    }
    track->value = value;
    track->pts = pts;

    if (result) {
        const AVSyncTrack* master_track = &tracks[master_idx];
        if (idx == master_idx) {
            unsigned i;
            for(i = 0; i < nb_tracks; i++)
                result |= update_track_avsync_value(master_track, &tracks[i]);
        } else {
            result |= update_track_avsync_value(master_track, track);
        }
    }

    return result;
}

// Calculate frame content value
// Video: YUV 8bit, average brightness
// Audio: PCM 16 bit mono, average RMS
static float get_frame_content_value(const AVFrame* frame, enum AVMediaType type)
{
    int64_t value = 0;
    
    av_assert0(frame != NULL);
    av_assert0(frame->data[0] != 0);
    

    if (type == AVMEDIA_TYPE_VIDEO) {
        const uint8_t* p = frame->data[0];
        int x, y;
        for (y = 0; y < frame->height; y++) {
            for(x = 0; x < frame->width; x++)
                value += p[x];
            p += frame->linesize[0];
        }
        return (value/(frame->width*frame->height))/255.0f;
    
    } else if (type == AVMEDIA_TYPE_AUDIO) {
        const int16_t* p = (const int16_t*)frame->data[0];
        int i;
        for(i = 0; i < frame->nb_samples; ++i, ++p)
            value += *p * *p;
        return sqrt(value/frame->nb_samples)/32768.0f;
    
    } else {
        return 0.0f;
    }
}

//////////////////////////////////////////////////////////////////
// Output

// Print ffdump-style detailed information
// realtime timestamp, stream timestamps, stream value, avsync values
static void print_ffdump_line(FILE* output, int64_t rts, const AVSyncTrack* tracks, unsigned nb_tracks, unsigned master_idx, unsigned idx)
{
    unsigned i;

    // Real timestamp
    fprintf(output, "%0.3f\t", (float)rts/AV_TIME_BASE);
    
    // Tracks timestamps
    for(i = 0; i < nb_tracks; i++) {
        int64_t pts = tracks[i].pts;
        fprintf(output, "%c", (idx == i) ? '*' : ' ');
        if (pts == AV_NOPTS_VALUE)
            fprintf(output, "-\t");
        else
            fprintf(output, "%0.3f\t", (float)pts/AV_TIME_BASE);
    }
    
    // Tracks values
    fprintf(output, "values: ");
    for(i = 0; i < nb_tracks; i++) {
        if (i > 0)
            fprintf(output, ", ");
        fprintf(output, "%0.3f", tracks[i].value);
    }
    fprintf(output,"\t");

    // avsync values
    fprintf(output, "lipsync:\t"); 
    for (i = 0; i < nb_tracks; i++) {
        if (i != master_idx)
            fprintf(output, "[%d:%u]:%0.3f\t",
                    master_idx, i,
                    (float)tracks[i].avsync/AV_TIME_BASE);
    }

    // Done
    fprintf(output, "\n");
}

// Comma separated values
//  timestamp, values, avsync values
static void print_csv_header(FILE* output, const AVSyncTrack* tracks, unsigned nb_tracks, unsigned master_idx)
{
    unsigned i;
    fprintf(output, "timestamp");
    for(i = 0; i < nb_tracks; i++)
        fprintf(output, ",value%d", i);
    for(i = 0; i < nb_tracks; i++)
        if (i != master_idx)
            fprintf(output, ",avsync%d:%d", master_idx, i);
    fprintf(output, "\n");
}

static void print_csv_line(FILE* output, const AVSyncTrack* tracks, unsigned nb_tracks, unsigned master_idx, unsigned idx)
{
    // TODO: wait for all streams get ready

    unsigned i;
    fprintf(output, "%s", av_ts2str(tracks[idx].pts));
    for(i = 0; i < nb_tracks; i++)
        fprintf(output, ",%0.3f", tracks[i].value);
    for(i = 0; i < nb_tracks; i++)
        if (i != master_idx)
            fprintf(output, ",%d", (int)tracks[i].avsync);
    fprintf(output, "\n");
}

#if CONFIG_SYSINFO
static void print_sysinfo_status(const char* name, const char* status)
{
    sysinfo_set_string(name, status);
}

// Sysinfo: just avsync values
static void print_sysinfo_line(const char* name, const AVSyncTrack* tracks, unsigned nb_tracks, unsigned master_idx, unsigned idx)
{
    // TODO: wait for all streams get ready
    
    if (nb_tracks == 2) {
        // Popular case - only two tracks
        //  master_idx in this case is whether 0 or 1, so operator ! can be used to invert it
        sysinfo_set_integer(name, tracks[!master_idx].avsync);
    } else {
        // All other cases
        unsigned char need_comma = 0;
        unsigned i;
        char* value = NULL;
        struct AVBPrint buf;
        av_bprint_init(&buf, 0, AV_BPRINT_SIZE_AUTOMATIC);
        for (i = 0; i < nb_tracks; i++) {
            if (i == master_idx)
                continue;
            if (need_comma)
                av_bprintf(&buf, ",");
            av_bprintf(&buf,  "%" PRId64, tracks[i].avsync);
            need_comma = 1;
        }

        if(av_bprint_finalize(&buf, &value) == 0) {
            sysinfo_set_string(name, value);
            av_free(value);
        }
    }
}
#endif //CONFIG_SYSINFO

//////////////////////////////////////////////////////////////////
// Filter

typedef enum {
    OFMT_FFDUMP,    // ffdump-style format
    OFMT_CSV,       // CSV: time, values, avsync
#if CONFIG_SYSINFO
    OFMT_SYSINFO,   // sysinfo
#endif //CONFIG_SYSINFO
    OFMT_COUNT      // Number of output formats
} OutputFormat;


#define MAX_STREAMS_COUNT 64
#define TYPE_ALL 2
typedef struct {
    const AVClass *class;
    unsigned nb_streams[TYPE_ALL];

    int output_format;
    char* output_name;
    
    FILE* output_file;

    int64_t training;           // Training mode duration in microseconds, 0 - processing mode
    int64_t first_rts;          // Receive time of the very first packet
    char* threshold_values;     // Comma-separated threshold values or 'auto'
    int64_t training_duration;  // Threshold detection duration (microseconds)
    int threshold_detection_algo;
    
    unsigned nb_tracks;
    AVSyncTrack* tracks;
    unsigned master_idx;        // Master track index

    unsigned nb_frames;         // Number of processed frames

} AVSyncContext;

#define OFFSET(x) offsetof(AVSyncContext, x)
#define VFLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
#define AFLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
#define FLAGS  VFLAGS | AFLAGS

static const AVOption avsync_options[] = {
    { "v",     "number of video streams to be analyzed", OFFSET(nb_streams[AVMEDIA_TYPE_VIDEO]), AV_OPT_TYPE_INT,    { .i64 = 1 },   0,        31, VFLAGS },
    { "a",     "number of audio streams to be analyzed", OFFSET(nb_streams[AVMEDIA_TYPE_AUDIO]), AV_OPT_TYPE_INT,    { .i64 = 1 },   0,        31, AFLAGS },
    { "m",     "master stream index",                    OFFSET(master_idx),                     AV_OPT_TYPE_INT,    { .i64 = 0 },   0,        63, FLAGS },
    { "o",     "output file name",                       OFFSET(output_name),                    AV_OPT_TYPE_STRING, { .str = "-" }, CHAR_MIN, CHAR_MAX, FLAGS},
    { "of",    "output format",                          OFFSET(output_format),                  AV_OPT_TYPE_INT, { .i64 = OFMT_FFDUMP }, 0, OFMT_COUNT-1, FLAGS, "of" },
    {   "ffdump",   "ffdump-style detailed output",      0,  AV_OPT_TYPE_CONST, {.i64=OFMT_FFDUMP},     0, 0, FLAGS, "of" },
    {   "csv",      "csv values and avsync",             0,  AV_OPT_TYPE_CONST, {.i64=OFMT_CSV},        0, 0, FLAGS, "of" },
#if CONFIG_SYSINFO
    {   "sysinfo",  "avsync value in sysinfo",           0,  AV_OPT_TYPE_CONST, {.i64=OFMT_SYSINFO},    0, 0, FLAGS, "of" },
#endif //CONFIG_SYSINFO    
    { "t",     "streams threshold values (comma separated)", OFFSET(threshold_values),          AV_OPT_TYPE_STRING,   { .str = "0.5" }, CHAR_MIN, CHAR_MAX, FLAGS},
    { "tdd",   "threshold detection duration",           OFFSET(training_duration),             AV_OPT_TYPE_DURATION, { .i64 = 60000000LL }, 10000000LL,  60000000LL, FLAGS},
    { "tda",   "threshold detection algorithm",          OFFSET(threshold_detection_algo),      AV_OPT_TYPE_INT, { .i64 = TDA_DB }, 0, TDA_COUNT-1, FLAGS, "tda" },
    {   "am",  "arithmetic mean",                       0,  AV_OPT_TYPE_CONST, {.i64=TDA_AM},     0, 0, FLAGS, "tda" },
    {   "den", "density based",                         0,  AV_OPT_TYPE_CONST, {.i64=TDA_DB},     0, 0, FLAGS, "tda" },
    { NULL }
};
AVFILTER_DEFINE_CLASS(avsync);

static void print_status(AVSyncContext* av_unused(avs_ctx), const char* av_unused(status))
{
#if CONFIG_SYSINFO
    if (avs_ctx->output_format == OFMT_SYSINFO) {
        print_sysinfo_status(avs_ctx->output_name, status);
    }
#endif //CONFIG_SYSINFO
}

// Print frame information
static void print_frame(AVSyncContext* avs_ctx, unsigned frame_idx, int64_t rts, int idx, int result)
{
    switch(avs_ctx->output_format) {
        // FFDUMP - report every frame
        case OFMT_FFDUMP:
            print_ffdump_line(avs_ctx->output_file, rts, avs_ctx->tracks, avs_ctx->nb_tracks, avs_ctx->master_idx, idx);
            break;
        // CSV - only when value is updated
        case OFMT_CSV:
            if (frame_idx == 0)
                print_csv_header(avs_ctx->output_file, avs_ctx->tracks, avs_ctx->nb_tracks, avs_ctx->master_idx);
            if ((result & (AVSYNC_STATE_CHANGED | AVSYNC_VALUE_UPDATED)) == (AVSYNC_STATE_CHANGED | AVSYNC_VALUE_UPDATED))
                print_csv_line(avs_ctx->output_file, avs_ctx->tracks, avs_ctx->nb_tracks, avs_ctx->master_idx, idx);
            break;
#if CONFIG_SYSINFO
        // Sysinfo - only when value is updated
        case OFMT_SYSINFO:
            if (result & (AVSYNC_STATE_CHANGED | AVSYNC_VALUE_UPDATED) == (AVSYNC_STATE_CHANGED | AVSYNC_VALUE_UPDATED))
                print_sysinfo_line(avs_ctx->output_name, avs_ctx->tracks, avs_ctx->nb_tracks, avs_ctx->master_idx, idx);
            break;
#endif //CONFIG_SYSINFO            
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    int64_t rts = av_gettime();
    AVFilterContext *ctx = inlink->dst;
    AVSyncContext *avs_ctx = ctx->priv;
    int first_frame = avs_ctx->first_rts == AV_NOPTS_VALUE;
    float value = get_frame_content_value(frame, inlink->type);
    int64_t pts = av_rescale_q(frame->pts, inlink->time_base, AV_TIME_BASE_Q);
    int idx = FF_INLINK_IDX(inlink);

    if (first_frame)
        avs_ctx->first_rts = rts;
    rts -= avs_ctx->first_rts;

    if (avs_ctx->training == 0) {
        // Processing mode
        int result = update_avsync_content(avs_ctx->tracks, avs_ctx->nb_tracks, avs_ctx->master_idx, value, pts, idx);
        if (result < 0)
            return result;

        print_frame(avs_ctx, avs_ctx->nb_frames, rts, idx, result);
        avs_ctx->nb_frames++;
    } else {
        if (pts >= avs_ctx->training) {
            unsigned i;
            for(i = 0; i < avs_ctx->nb_tracks; i++) {
                AVSyncTrack* track = &avs_ctx->tracks[i];
                if (!track->tdc->is_detected(track->tdc)) {
                    av_log(avs_ctx, AV_LOG_ERROR, "No enough frames to determine threshold for the stream %d\n", i);
                    return AVERROR(EINVAL);
                }
                
                track->threshold = track->tdc->get_threshold(track->tdc);
                av_log(avs_ctx, AV_LOG_DEBUG, "Calculated threshold %0.3f for the stream %d\n", track->threshold, i);
            }
            avs_ctx->training = 0;
        } else {
            AVSyncTrack* track = &avs_ctx->tracks[idx];
            if (first_frame) {
                // TODO: start training
                unsigned i = 0;
                for(; i < avs_ctx->nb_tracks; i++) {
                    AVSyncTrack* t = &avs_ctx->tracks[i];
                    t->tdc = tdc_create(avs_ctx->threshold_detection_algo);
                }
                av_log(avs_ctx, AV_LOG_DEBUG, "Threshold detection started for %d second(s)\n", (int)(avs_ctx->training/AV_TIME_BASE));
                print_status(avs_ctx, "training");
            }
            track->tdc->update(track->tdc, value);
        }
    }

    av_frame_free(&frame);
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    return 0;
}

static av_cold int init(AVFilterContext *ctx) 
{
    AVSyncContext *avs_ctx = ctx->priv;
    unsigned type;
    unsigned i;
    float thresholds[MAX_STREAMS_COUNT] = {0.0f};


    if (avs_ctx->nb_streams[AVMEDIA_TYPE_VIDEO] + avs_ctx->nb_streams[AVMEDIA_TYPE_AUDIO] < 2) {
        av_log(avs_ctx, AV_LOG_ERROR, "at least 2 streams should be specified\n");
        return AVERROR(EINVAL);
    }

    if (avs_ctx->nb_streams[AVMEDIA_TYPE_VIDEO] + avs_ctx->nb_streams[AVMEDIA_TYPE_AUDIO] > MAX_STREAMS_COUNT) {
        av_log(avs_ctx, AV_LOG_ERROR, "too many streams\n");
        return AVERROR(EINVAL);
    }
    
    if (avs_ctx->output_name == NULL || *avs_ctx->output_name == 0) {
        av_log(avs_ctx, AV_LOG_ERROR, "Missing output file name\n");
        return AVERROR(EINVAL);
    }

#if CONFIG_SYSINFO
    if (avs_ctx->output_format == OFMT_SYSINFO) {
        if (strcmp(avs_ctx->output_name, "-") == 0) {
            av_log(avs_ctx, AV_LOG_ERROR, "'-' is not a valid output name for sysinfo\n");
            return AVERROR(EINVAL);
        }
    }
#endif //CONFIG_SYSINFO

    if (avs_ctx->output_format == OFMT_FFDUMP 
     || avs_ctx->output_format == OFMT_CSV) {
        if (strcmp(avs_ctx->output_name, "-") == 0) {
            avs_ctx->output_file = stdout;
        } else {
            avs_ctx->output_file = fopen(avs_ctx->output_name, "w");
            if (!avs_ctx->output_file) {
                int err = AVERROR(errno);
                char buf[128];
                av_strerror(err, buf, sizeof(buf));
                av_log(avs_ctx, AV_LOG_ERROR, "Could not open output file %s: %s\n", avs_ctx->output_name, buf);
                return err;
            }
        }
    }

    for (type = 0; type < TYPE_ALL; type++) {
        unsigned i;
        for (i = 0; i < avs_ctx->nb_streams[type]; i++) {
            AVFilterPad pad = {
                .type         = type,
                .filter_frame = filter_frame,
                .config_props = config_input,
            };
            pad.name = av_asprintf("in:%c%d", "va"[type], i);
            av_log(avs_ctx, AV_LOG_DEBUG, "create input pad: %s\n", pad.name);
            ff_insert_inpad(ctx, ctx->nb_inputs, &pad);
        }
    }

    if (avs_ctx->master_idx >= ctx->nb_inputs) {
        av_log(avs_ctx, AV_LOG_ERROR, "index (%d) of master stream is out of range (0:%d)\n", avs_ctx->master_idx, ctx->nb_inputs);
        return AVERROR(EINVAL);    
    }

    // Parse threshold values
    //  - auto - training mode, duration is specified in 
    //  - single value - use for all tracks
    //  - comma separated values
    if (av_strcasecmp(avs_ctx->threshold_values, "auto") == 0) {
        avs_ctx->training = avs_ctx->training_duration;
    } else {
        char* p;
        char* s = avs_ctx->threshold_values;
        i = 0;
        while((p = av_strtok(s, ",", &s)) != NULL) {
            float t = atof(p);
            if (i >= ctx->nb_inputs) {
                av_log(avs_ctx, AV_LOG_ERROR, "too many (%d) threshold value(s) are provided\n", i);
                return AVERROR(EINVAL);    
            }

            if (t < 0.0f || t > 1.0f) {
                av_log(avs_ctx, AV_LOG_ERROR, "threshold value %s for stream %d is out of range (0..1)\n", p, i);
                return AVERROR(EINVAL);    
            }  
            thresholds[i] = t;
            i++;
        }
        if (i == 1) {
            for(; i < ctx->nb_inputs; i++)
                thresholds[i] = thresholds[0];
        } else if (i < ctx->nb_inputs) {
            av_log(avs_ctx, AV_LOG_ERROR, "expected %d threshold value(s), %d given\n", ctx->nb_inputs, i);
            return AVERROR(EINVAL);    
        }
    }


    avs_ctx->tracks = av_mallocz_array(ctx->nb_inputs, sizeof(AVSyncTrack));
    if (!avs_ctx->tracks)
        return AVERROR(ENOMEM);
    avs_ctx->nb_tracks = ctx->nb_inputs;
    for(i = 0; i < avs_ctx->nb_tracks; i++)
        init_avsync_track(&avs_ctx->tracks[i], thresholds[i]);

    avs_ctx->first_rts = AV_NOPTS_VALUE;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx) 
{
    AVSyncContext *avs_ctx = ctx->priv;
    unsigned i;

    for(i = 0; i < avs_ctx->nb_tracks; i++) {
        AVSyncTrack* track = &avs_ctx->tracks[i];
        tdc_destroy(avs_ctx->threshold_detection_algo, &track->tdc);
    }
    
    av_freep(&avs_ctx->tracks);

    for (i = 0; i < ctx->nb_inputs; ++i) {
        av_freep(&ctx->input_pads[i].name);
    }

    if (avs_ctx->output_file && avs_ctx->output_file != stdout) {
        fclose(avs_ctx->output_file);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pixel_fmts[] = { 
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_NONE };
    
    static const enum AVSampleFormat sample_fmts[] = { 
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P, 
        AV_SAMPLE_FMT_NONE };
    
    static const int64_t channel_layouts[] = { 
        AV_CH_LAYOUT_MONO, 
        -1};

    AVFilterFormats* pixel_formats = NULL;
    AVFilterFormats* sample_formats = NULL;
    AVFilterChannelLayouts* layouts = NULL;
    AVFilterFormats* samplerates = NULL;

    int i;
    int ret;

    for(i = 0; i < ctx->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];
        if (inlink) {
            if (inlink->type == AVMEDIA_TYPE_VIDEO) {
                // Set video formats
                if (!pixel_formats)
                    pixel_formats = ff_make_format_list(pixel_fmts);
                if ((ret = ff_formats_ref(pixel_formats, &inlink->out_formats)) < 0)
                    return ret;
            } else if (inlink->type == AVMEDIA_TYPE_AUDIO) {
                // Set audio formats
                if (!sample_formats)
                    sample_formats = ff_make_format_list(sample_fmts);
                if ((ret = ff_formats_ref(sample_formats, &inlink->out_formats)) < 0)
                    return ret;
                
                if (!layouts)
                    layouts = avfilter_make_format64_list(channel_layouts);
                if ((ret = ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts)) < 0)
                    return ret;

                if (!samplerates)
                    samplerates = ff_all_samplerates();
                if ((ret = ff_formats_ref(samplerates, &inlink->out_samplerates)) < 0)
                    return ret;
            }
        }
    }

    return 0;
}

AVFilter ff_avsink_avsync2 = {
    .name          = "avsync2",
    .description   = NULL_IF_CONFIG_SMALL("Measure lipsync between audio and video streams"),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AVSyncContext),
    .inputs        = NULL,
    .outputs       = NULL,
    .priv_class    = &avsync_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
