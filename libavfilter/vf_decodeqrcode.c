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

#include "avfilter.h"
#include "internal.h"

#include <quirc.h>



typedef struct {
    const AVClass *class;
    char* output_file_str;
    FILE* output_file;
} QRCodeContext;


static av_cold int init(AVFilterContext *ctx)
{
    QRCodeContext *qrc_ctx = ctx->priv;

    if (qrc_ctx->output_file_str) {
        if (!strcmp(qrc_ctx->output_file_str, "-")) {
            qrc_ctx->output_file = stdout;
        }
        else {
            qrc_ctx->output_file = fopen(qrc_ctx->output_file_str, "w");
            if (!qrc_ctx->output_file) {
                int err = AVERROR(errno);
                char buf[128];
                av_strerror(err, buf, sizeof(buf));
                av_log(ctx, AV_LOG_ERROR, "Could not open output file %s: %s\n",
                       qrc_ctx->output_file_str, buf);
                return err;
            }
        }
    }
    
    return 0;
}


static av_cold void uninit(AVFilterContext *ctx)
{
    QRCodeContext *qrc_ctx = ctx->priv;

    if (qrc_ctx->output_file && qrc_ctx->output_file != stdout) {
        fclose(qrc_ctx->output_file);
    }
}



static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}


static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    QRCodeContext *qrc_ctx = ctx->priv;
    int i;
    int num_codes;
    int err;
    uint8_t *buf;
    struct quirc *qr;
    
    qr = quirc_new();
    if (!qr) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate qr code decoder\n");
        return 1;
    }

    if (quirc_resize(qr, frame->width, frame->height) < 0) {
        av_log(ctx, AV_LOG_ERROR,"Failed to allocate video memory\n");
        return 1;
    }

    buf = quirc_begin(qr, NULL, NULL);
    
    for (i = 0; i < frame->height; ++i)
        memcpy(buf + i * frame->width, frame->data[0] + i * frame->linesize[0], frame->width);

    quirc_end(qr);

    num_codes = quirc_count(qr);
    av_log(ctx, AV_LOG_TRACE, "%d QR codes found on the frame\n", num_codes);

    if (num_codes) {
        for(i = 0; i < num_codes; ++i) {
            struct quirc_code code;
            struct quirc_data data;

            quirc_extract(qr, i, &code);
            err = quirc_decode(&code, &data);
            if (!err)
                fprintf(qrc_ctx->output_file, "%s\n", data.payload);
            else
                av_log(ctx, AV_LOG_TRACE, "error while decoding QR Code: %s\n", quirc_strerror(err));
        }
    }

    quirc_destroy(qr);
    
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static int config_props(AVFilterContext *ctx, AVFilterLink *link, int is_out)
{
    av_log(ctx, AV_LOG_INFO, "config %s time_base: %d/%d, frame_rate: %d/%d\n",
           is_out ? "out" : "in",
           link->time_base.num, link->time_base.den,
           link->frame_rate.num, link->frame_rate.den);

    return 0;
}

static int config_props_in(AVFilterLink *link)
{
    AVFilterContext *ctx = link->dst;
    return config_props(ctx, link, 0);
}

static int config_props_out(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    return config_props(ctx, link, 1);
}


#define OFFSET(x) offsetof(QRCodeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption decodeqrcode_options[] = {
    { "output", "output to given file or to stdout", OFFSET(output_file_str), AV_OPT_TYPE_STRING, { .str = "-" }, CHAR_MIN, CHAR_MAX, FLAGS},
    { "o",      "output to given file or to stdout", OFFSET(output_file_str), AV_OPT_TYPE_STRING, { .str = "-" }, CHAR_MIN, CHAR_MAX, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(decodeqrcode);


static const AVFilterPad decodeqrcode_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
        .config_props     = config_props_in,
    },
    { NULL }
};

static const AVFilterPad decodeqrcode_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_props_out,
    },
    { NULL }
};


AVFilter ff_vf_decodeqrcode = {
    .name        = "decodeqrcode",
    .description = NULL_IF_CONFIG_SMALL("Show textual information encoded in QR code symbol presented in the frame."),
    .init          = init,
    .uninit        = uninit,
    .inputs      = decodeqrcode_inputs,
    .outputs     = decodeqrcode_outputs,
    .query_formats = query_formats,
    .priv_class    = &decodeqrcode_class,
    .priv_size     = sizeof(QRCodeContext),
};
