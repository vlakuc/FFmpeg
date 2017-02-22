#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#include <qrencode.h>

typedef struct QRCodeContext {
    const AVClass *class;
    int x, y;
    int thickness;
    int margin;
    int vsub, hsub;
    int have_alpha;
} QRCodeContext;


static av_cold int init(AVFilterContext *ctx)
{
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    QRCodeContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->have_alpha = desc->flags & AV_PIX_FMT_FLAG_ALPHA;

    return 0;
}

static void update_row(unsigned char *row[4], AVFrame *frame, int line, int sub)
{
    int plane;
    row[0] = frame->data[0] + line * frame->linesize[0];
    row[3] = frame->data[3] + line * frame->linesize[3];
    for (plane = 1; plane < 3; plane++)
        row[plane] = frame->data[plane] +
            frame->linesize[plane] * (line >> sub);
}


static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    QRCodeContext *s = inlink->dst->priv;
    int x, y = 0, xb = s->x, yb = s->y, yy, current_line;
    unsigned char *row[4];
    QRcode* qrcode;
    char buf[128];
    unsigned char *p, * prev_row;
    unsigned int margin, real_width;
    
    sprintf(buf, "%" PRId64, frame->pts);
    qrcode = QRcode_encodeString(buf, 0, QR_ECLEVEL_H, QR_MODE_8, 0);
    p = qrcode->data;

    margin = s->margin;
    real_width = (qrcode->width + margin * 2) * s->thickness;
    current_line = s->thickness * margin;
    
    if ((frame->width < real_width + xb)
        || (frame->height < real_width + yb))
        return AVERROR(EINVAL);
    
    // top margin
    for (y = yb; y < yb + s->thickness  * margin; y++)
    {
        update_row(row, frame, y, s->vsub);
        memset(row[0] + xb, 255, real_width);
    }

    // QR code with left right margins
    current_line += yb;
    for (y = 0; y < qrcode->width; y++)
    {
        update_row(row, frame, current_line++, s->vsub);
        
        // left margin
        memset(row[0] + xb, 255, margin * s->thickness);

        // right margin
        memset(row[0] + xb + real_width - margin * s->thickness, 255, margin * s->thickness);

        for (x = 0; x < qrcode->width; x++)
        {
            memset(row[0] + xb + margin * s->thickness + x * s->thickness, (*p & 1) ? 0 : 255, s->thickness);
            p++;
        }

        prev_row = row[0] + xb;
        for (yy = 1; yy < s->thickness; yy++)
        {
            update_row(row, frame, current_line++, s->vsub);
            memcpy(row[0] + xb, prev_row, real_width);
        }
    }

    // bottom margin
    for (y = yb + real_width - s->thickness * margin; y < yb + real_width; y++)
    {
        update_row(row, frame, y, s->vsub);
        memset(row[0] + xb, 255, real_width);
    }

    QRcode_free(qrcode);
    
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

#define OFFSET(x) offsetof(QRCodeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption drawqrcode_options[] = {
    { "x",         "set horizontal position of the left box edge", OFFSET(x),    AV_OPT_TYPE_INT, { .i64=0 },       0, INT_MAX, FLAGS },
    { "y",         "set vertical position of the top box edge",    OFFSET(y),    AV_OPT_TYPE_INT, { .i64 = 0 },   0, INT_MAX, FLAGS },
    { "thickness", "set the box thickness",                        OFFSET(thickness),    AV_OPT_TYPE_INT, { .i64= 3 },       1, INT_MAX, FLAGS },
    { "t",         "set the box thickness",                        OFFSET(thickness),    AV_OPT_TYPE_INT, { .i64= 3 },       1, INT_MAX, FLAGS },
    { "margin",    "top/bottom and left/right margins size",       OFFSET(margin),    AV_OPT_TYPE_INT, { .i64= 5 },       1, INT_MAX, FLAGS },
    { "m",         "top/bottom and left/right margins size",       OFFSET(margin),    AV_OPT_TYPE_INT, { .i64= 5 },       1, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drawqrcode);

static const AVFilterPad drawqrcode_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad drawqrcode_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_drawqrcode = {
    .name          = "drawqrcode",
    .description   = NULL_IF_CONFIG_SMALL("Draw a QR code."),
    .priv_size     = sizeof(QRCodeContext),
    .priv_class    = &drawqrcode_class,
    .init          = init,
    .query_formats = query_formats,
    .inputs        = drawqrcode_inputs,
    .outputs       = drawqrcode_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};


