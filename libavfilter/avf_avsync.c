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

#include "libavutil/timestamp.h"
#include "libavutil/time.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libswresample/swresample.h"

#include "libavcodec/avcodec.h"

#include "avfilter.h"
#include "internal.h"
#include "avsync_utils.h"
#include "content_sync_detector.h"

#if CONFIG_SYSINFO
#include <sysinfo/sysinfo.h>
#endif //CONFIG_SYSINFO


#define TYPE_ALL 2

typedef struct {
    int64_t pts;
    AVRational time_base;
} StreamInfo;

typedef struct {
    int64_t packet_counter;
    float avg_lipsync;
    float lipsync; // current value
    float norm_lipsync; // current normalized value
} AVSyncStat;

typedef struct {
    const AVClass *class;
    unsigned nb_streams[TYPE_ALL];
    int master_stream;
    char* output_file_str;
    FILE* output_file;
    int threshold;
    unsigned char compact_format;
    char* sysinfo_path;
    int64_t first_frame_time;
    content_sync_detector_ctx_t* csd_ctx;
    SwrContext** sw_resamplers;
    int* frame_values;
    StreamInfo* stream_info;
    AVSyncStat* avsync_stat;

} AVSyncContext;


#define OFFSET(x) offsetof(AVSyncContext, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption avsync_options[] = {
    { "video", "number of video streams to be analyzed", OFFSET(nb_streams[AVMEDIA_TYPE_VIDEO]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "v",     "number of video streams to be analyzed", OFFSET(nb_streams[AVMEDIA_TYPE_VIDEO]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "audio", "number of audio streams to be analyzed", OFFSET(nb_streams[AVMEDIA_TYPE_AUDIO]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "a",     "number of audio streams to be analyzed", OFFSET(nb_streams[AVMEDIA_TYPE_AUDIO]), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "master", "master stream index. Metrics of other streams are compared against master stream", OFFSET(master_stream), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS},
    { "m",      "master stream index. Metrics of other streams are compared against master stream", OFFSET(master_stream), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS},
    { "output", "output to given file or to stdout", OFFSET(output_file_str), AV_OPT_TYPE_STRING, { .str = "-" }, CHAR_MIN, CHAR_MAX, FLAGS},
    { "o",      "output to given file or to stdout", OFFSET(output_file_str), AV_OPT_TYPE_STRING, { .str = "-" }, CHAR_MIN, CHAR_MAX, FLAGS},
    { "threshold", "frame charactersistic threshold", OFFSET(threshold), AV_OPT_TYPE_INT, { .i64 = 10 }, 0, 100, FLAGS},
    { "t",         "frame charactersistic threshold", OFFSET(threshold), AV_OPT_TYPE_INT, { .i64 = 10 }, 0, 100, FLAGS},
    { "compact_format", "output only lipsync values",  OFFSET(compact_format),      AV_OPT_TYPE_BOOL,   {.i64 = 0    }, 0,  1, FLAGS },
    { "sysinfo", "output to sysinfo path (works with compact_format options only)", OFFSET(sysinfo_path), AV_OPT_TYPE_STRING, { .str = NULL }, CHAR_MIN, CHAR_MAX, FLAGS},
    { NULL }
};


AVFILTER_DEFINE_CLASS(avsync);


static void sysinfo_writer(AVFilterContext *ctx, const char* path, const char* value)
{
#if CONFIG_SYSINFO
    
    if (!path || !value ) {
        av_log(ctx, AV_LOG_ERROR, "path or value is not specified.\n");
    }
    else {
        sysinfo_set_string(path, value);
    }
#else
    av_log(ctx, AV_LOG_ERROR, "output to sysinfo is not supported.\n");
#endif //CONFIG_SYSINFO
}


static void print_frame_time(AVFilterContext* ctx)
{
    AVSyncContext *avs = ctx->priv;
    int64_t frame_time = av_gettime();

    if (avs->first_frame_time == AV_NOPTS_VALUE) {
        avs->first_frame_time = frame_time;
    }

    fprintf(avs->output_file, "%0.3f\t", (float)(frame_time - avs->first_frame_time) / AV_TIME_BASE);
}

static void print_timestamps(AVFilterContext *ctx, unsigned current_stream)
{
    AVSyncContext *avs = ctx->priv;
    char active_stream_label = '*';
    unsigned i;

    for (i = 0; i < ctx->nb_inputs; ++i) {
        fprintf(avs->output_file, "%c", current_stream == i ? active_stream_label : ' ');
        
        if (avs->stream_info[i].pts == AV_NOPTS_VALUE) {
            fprintf(avs->output_file, "-\t");
        }
        else { 
            fprintf(avs->output_file, "%0.3f\t", avs->stream_info[i].pts * avs->stream_info[i].time_base.num / (float)avs->stream_info[i].time_base.den);
        }
    }
}

static void print_frame_characteristic(AVFilterContext *ctx)
{
    AVSyncContext *avs = ctx->priv;
    unsigned i, comma_needed = 0;
    
    fprintf(avs->output_file, "values: ");

    for( i = 0; i < ctx->nb_inputs; ++i ) {
        if( comma_needed )
            fprintf(avs->output_file, ", ");
			
        fprintf(avs->output_file, "%d", avs->frame_values[i]);
			
        comma_needed = 1;
    }
    fprintf(avs->output_file, "\t");
}


static void print_lipsync(AVFilterContext *ctx)
{
    AVSyncContext *avs = ctx->priv;
    unsigned i;
    
    fprintf(avs->output_file, "lipsync:\t"); 
    
    for (i = 0; i < ctx->nb_inputs; i++) {
        if (i == avs->master_stream) continue;
        fprintf(avs->output_file, "[%d:%u]:%0.3f\t",
                avs->master_stream,
                i,
                avs->avsync_stat[i].lipsync);
    }
    fprintf(avs->output_file, "\n"); 
}

static void print_lipsync_compact(AVFilterContext *ctx)
{
    AVSyncContext *avs = ctx->priv;
    unsigned i;
    char* lipsync_str = NULL;
    unsigned char need_comma = 0;
    
    struct AVBPrint bprint;
    av_bprint_init(&bprint, 0, AV_BPRINT_SIZE_AUTOMATIC);

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (i == avs->master_stream) continue;
        if (need_comma) {
            av_bprintf(&bprint, ",");
        }
        av_bprintf(&bprint,  "%0.3f", avs->avsync_stat[i].norm_lipsync);
        need_comma = 1;
    }
    av_bprint_finalize(&bprint, &lipsync_str);
    sysinfo_writer(ctx, avs->sysinfo_path, lipsync_str);
}



static int get_loudness(AVSyncContext* ctx, AVFrame* frame, int index)
{
    int samples_count = frame->nb_samples;
    int loudness = 0;
    SwrContext* s = NULL;

    // Further processing in PCM 16bit Mono
    if (samples_count > 0) {
        int16_t*  samples = av_calloc(samples_count, sizeof(int16_t));                // Assume that thread stack is big enough to hold all frame samples
        if ( ctx->sw_resamplers[index] == NULL ) {
            uint64_t channel_layout = av_frame_get_channel_layout(frame);
            if (channel_layout == 0)
                channel_layout = frame->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
                
            s = swr_alloc_set_opts(
                NULL,                                 //Swr context, can be NULL
                AV_CH_LAYOUT_MONO,                    // output channel layout (AV_CH_LAYOUT_*)
                AV_SAMPLE_FMT_S16,                    // output sample format (AV_SAMPLE_FMT_*).
                av_frame_get_sample_rate(frame),      // output sample rate (frequency in Hz)
                channel_layout,                       // input channel layout (AV_CH_LAYOUT_*)
                frame->format,                        // input sample format (AV_SAMPLE_FMT_*).
                av_frame_get_sample_rate(frame),      // input sample rate (frequency in Hz)
                0,                                    // logging level offset
                NULL);                                // parent logging context, can be NULL
            if (s)
            {
                swr_init(s);
                ctx->sw_resamplers[index] = s;
            }
        }
            
        if ( ctx->sw_resamplers[index] ) {
            int16_t* samples_bufs[1] = { samples };         // Pointers to output buffer for swresample
            swr_convert(ctx->sw_resamplers[index], (uint8_t **)samples_bufs, samples_count, (const uint8_t**)frame->data, samples_count);
        }
        else
            memset(samples, 0, samples_count * sizeof(uint16_t));
            
        // Buffer is ready. Get metrics
        loudness = get_loudness_of_samples( samples, samples_count );
        av_free(samples);
    }
    return loudness;
}


static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    unsigned out_no = FF_OUTLINK_IDX(outlink);
    AVFilterLink *inlink = ctx->inputs[out_no];
    
    outlink->time_base = inlink->time_base;
    outlink->w                   = inlink->w;
    outlink->h                   = inlink->h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->format              = inlink->format;
    
    return 0;
}


static void update_avsync_stat( AVFilterContext *ctx )
{
    AVSyncContext *avs = ctx->priv;
    AVSyncStat *stat = avs->avsync_stat;


    for( unsigned i = 0; i < ctx->nb_inputs; ++i ) {
        if (i == avs->master_stream) continue;			
        stat[i].lipsync = content_sync_get_diff(avs->csd_ctx, i, avs->master_stream);
        stat[i].norm_lipsync = (fabs(LIPSYNC_UNDEFINED - stat[i].lipsync) < 0.001) ? stat[i].avg_lipsync : stat[i].lipsync;
        
        // cumulative average
        stat[i].avg_lipsync = (stat[i].norm_lipsync + stat[i].avg_lipsync * stat[i].packet_counter) / (stat[i].packet_counter + 1);
        stat[i].packet_counter++;
    }
}



static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *avf_ctx = inlink->dst;
    AVSyncContext *avs_ctx = avf_ctx->priv;
    int idx = FF_INLINK_IDX(inlink);
    int value = 0;
    
    av_assert0(idx < avf_ctx->nb_outputs);

    avs_ctx->stream_info[idx].pts = frame->pts;
    avs_ctx->stream_info[idx].time_base = inlink->time_base;
    
    if (avf_ctx->input_pads[idx].type == AVMEDIA_TYPE_VIDEO) {
        value = get_average_color_of_image(frame, frame->width, frame->height);
    }
    else {
        value = get_loudness(avs_ctx, frame, idx);
    }

    avs_ctx->frame_values[idx] = value;
    {
        int64_t decoded_pts = av_rescale_q(frame->pts, inlink->time_base, AV_TIME_BASE_Q );
        content_sync_write( avs_ctx->csd_ctx, idx, decoded_pts, avs_ctx->frame_values[idx] );
    }


    update_avsync_stat(avf_ctx);
    
    if (avs_ctx->compact_format) {
        print_lipsync_compact(avf_ctx);
    }
    else {
        print_frame_time(avf_ctx);
        print_timestamps(avf_ctx, idx);
        print_frame_characteristic(avf_ctx);
        print_lipsync(avf_ctx);
    }

    return ff_filter_frame(avf_ctx->outputs[idx], frame);
}


static void create_input_pad(AVFilterContext *ctx, enum AVMediaType type, unsigned index)
{
    AVFilterPad pad = {
        .type         = type,
        .filter_frame = filter_frame,
    };
    pad.name = av_asprintf("in:%c%d", "va"[type], index);
    av_log(ctx, AV_LOG_INFO, "create input pad: %s\n", pad.name);
    ff_insert_inpad(ctx, ctx->nb_inputs, &pad);
}

static void create_output_pad(AVFilterContext *ctx, enum AVMediaType type, unsigned index)
{
    AVFilterPad pad = {
        .type         = type,
        .config_props = config_output,
    };

    pad.name = av_asprintf("out:%c%d", "va"[type], index);
    av_log(ctx, AV_LOG_INFO, "create output pad: %s\n", pad.name);
    ff_insert_outpad(ctx, ctx->nb_outputs, &pad);
}


static av_cold int init(AVFilterContext *ctx)
{
    AVSyncContext *avs_ctx = ctx->priv;
    unsigned type, str, i;

    if (avs_ctx->nb_streams[AVMEDIA_TYPE_VIDEO] + avs_ctx->nb_streams[AVMEDIA_TYPE_AUDIO] < 2) {
        av_log(ctx, AV_LOG_ERROR, "at least 2 streams should be specified\n");
        return AVERROR(EINVAL);
    }
    
    for (type = 0; type < TYPE_ALL; type++) {
        for (str = 0; str < avs_ctx->nb_streams[type]; str++) {
            create_input_pad(ctx, type, str);
            create_output_pad(ctx, type, str);
        }
    }
    
    avs_ctx->first_frame_time = AV_NOPTS_VALUE;
    
    avs_ctx->sw_resamplers = av_calloc(ctx->nb_inputs, sizeof(SwrContext*));
    if (!avs_ctx->sw_resamplers) return AVERROR(ENOMEM);
    
    avs_ctx->frame_values = av_calloc(ctx->nb_inputs, sizeof(int));
    if (!avs_ctx->frame_values) return AVERROR(ENOMEM);
    
    avs_ctx->stream_info = av_calloc(ctx->nb_inputs, sizeof(StreamInfo));
    if (!avs_ctx->stream_info) return AVERROR(ENOMEM);
    
    for (i = 0; i < ctx->nb_inputs; i++) {
        avs_ctx->stream_info[i].pts = AV_NOPTS_VALUE;
    }

    avs_ctx->avsync_stat = av_calloc(ctx->nb_inputs, sizeof(AVSyncStat));
    if (!avs_ctx->avsync_stat) return AVERROR(ENOMEM);

    for (i = 0; i < ctx->nb_inputs; i++) {
        avs_ctx->avsync_stat[i].packet_counter = 0;
        avs_ctx->avsync_stat[i].avg_lipsync = 0.0;
        avs_ctx->avsync_stat[i].lipsync = 0.0;
    }
    
    if (ctx->nb_inputs <= avs_ctx->master_stream) {
        av_log(ctx, AV_LOG_ERROR, "index (%d) of master stream is out of range (0:%d)\n",
               avs_ctx->master_stream, ctx->nb_inputs);
        return AVERROR(EINVAL);
    }


    if (!avs_ctx->compact_format && avs_ctx->output_file_str) {
        if (!strcmp(avs_ctx->output_file_str, "-")) {
            avs_ctx->output_file = stdout;
        }
        else {
            avs_ctx->output_file = fopen(avs_ctx->output_file_str, "w");
            if (!avs_ctx->output_file) {
                int err = AVERROR(errno);
                char buf[128];
                av_strerror(err, buf, sizeof(buf));
                av_log(ctx, AV_LOG_ERROR, "Could not open stats file %s: %s\n",
                       avs_ctx->output_file_str, buf);
                return err;
            }
        }
    }
    
    avs_ctx->csd_ctx = content_sync_detector_create(ctx->nb_inputs,  avs_ctx->threshold);
    if (avs_ctx->csd_ctx == NULL)
        return AVERROR(ENOMEM);
    
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AVSyncContext *avs_ctx = ctx->priv;
    unsigned i;

    content_sync_detector_destroy(avs_ctx->csd_ctx);
    
    for (i = 0; i < ctx->nb_inputs; ++i) {
        swr_free(&avs_ctx->sw_resamplers[i]);
    }
    av_free(avs_ctx->sw_resamplers);
    av_free(avs_ctx->frame_values);
    av_free(avs_ctx->stream_info);
    av_free(avs_ctx->avsync_stat);

    for (i = 0; i < ctx->nb_inputs; ++i) {
        av_freep(&ctx->input_pads[i].name);
    }
    for (i = 0; i < ctx->nb_outputs; ++i) {
        av_freep(&ctx->output_pads[i].name);
    }

    if (avs_ctx->output_file && avs_ctx->output_file != stdout) {
        fclose(avs_ctx->output_file);
    }
}


static int query_formats(AVFilterContext *ctx)
{
    AVSyncContext *cat = ctx->priv;
    unsigned type, idx0 = 0, idx, str;
    AVFilterFormats *formats, *rates = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    int ret;

    for (type = 0; type < TYPE_ALL; type++) {
        unsigned nb_str = cat->nb_streams[type];
        for (str = 0; str < nb_str; str++) {
            idx = idx0;

            /* Set the output formats */
            formats = ff_all_formats(type);
            if ((ret = ff_formats_ref(formats, &ctx->outputs[idx]->in_formats)) < 0)
                return ret;

            if (type == AVMEDIA_TYPE_AUDIO) {
                rates = ff_all_samplerates();
                if ((ret = ff_formats_ref(rates, &ctx->outputs[idx]->in_samplerates)) < 0)
                    return ret;
                layouts = ff_all_channel_layouts();
                if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[idx]->in_channel_layouts)) < 0)
                    return ret;
            }

            /* Set the same formats for each corresponding input */
            if ((ret = ff_formats_ref(formats, &ctx->inputs[idx]->out_formats)) < 0)
                return ret;
            if (type == AVMEDIA_TYPE_AUDIO) {
                if ((ret = ff_formats_ref(rates, &ctx->inputs[idx]->out_samplerates)) < 0 ||
                    (ret = ff_channel_layouts_ref(layouts, &ctx->inputs[idx]->out_channel_layouts)) < 0)
                    return ret;
            }
            idx += ctx->nb_outputs;

            idx0++;
        }
    }
    return 0;
}

AVFilter ff_avf_avsync = {
    .name          = "avsync",
    .description   = NULL_IF_CONFIG_SMALL("Measure lipsync between audio and video streams"),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AVSyncContext),
    .inputs        = NULL,
    .outputs       = NULL,
    .priv_class    = &avsync_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS | AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
