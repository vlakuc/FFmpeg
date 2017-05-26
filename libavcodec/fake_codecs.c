/*
 * Copyright (c) 2010 - 2017 Epiphan Systems Inc. All rights reserved.
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

/*
 Fake codecs for hardware TI codecs on daVinci
 */

#include "avcodec.h"

// Needed just for not-null AVCodec::encode2 pointer
static int fake_encode(AVCodecContext *ctx, AVPacket *pkt, const AVFrame *frame, int *got_packet)
{
	av_log( ctx, AV_LOG_FATAL, "Don't use these virtual encoders!" );
	return -1;
}

#ifdef CONFIG_FAKE_LIBX264_ENCODER
static const AVClass fake_libx264_class = {
    .class_name = "fake_libx264",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_fake_libx264_encoder = {
	.name           = "libx264",
	.type           = AVMEDIA_TYPE_VIDEO,
	.id             = AV_CODEC_ID_H264,
	.priv_data_size = 0,
	.init           = NULL,
	.encode2        = fake_encode,
	.close          = NULL,
	.capabilities   = CODEC_CAP_DELAY,
	.pix_fmts       = (enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
	.long_name      = NULL_IF_CONFIG_SMALL("Fake libx264 codec"),
    .priv_class       = &fake_libx264_class,
};
#endif

#ifdef CONFIG_FAKE_LIBMP3LAME_ENCODER
static const AVClass fake_libmp3lame_class = {
    .class_name = "fake_libmp3lame",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_fake_libmp3lame_encoder = {
	.name           = "libmp3lame",
	.type           = AVMEDIA_TYPE_AUDIO,
	.id             = AV_CODEC_ID_MP3,
	.priv_data_size = 0,
	.init           = NULL,
	.encode2        = fake_encode,
	.close          = NULL,
	.capabilities   = CODEC_CAP_DELAY,
	.sample_fmts    = (enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
	.long_name      = NULL_IF_CONFIG_SMALL("Fake libmp3lame codec"),
    .priv_class       = &fake_libmp3lame_class,
};
#endif

#ifdef CONFIG_FAKE_LIBFAAC_ENCODER
static const AVClass fake_libfaac_class = {
    .class_name = "fake_libfaac",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_fake_libfaac_encoder = {
	.name           = "libfaac",
	.type           = AVMEDIA_TYPE_AUDIO,
	.id             = AV_CODEC_ID_AAC,
	.priv_data_size = 0,
	.init           = NULL,
	.encode2        = fake_encode,
	.close          = NULL,
	.capabilities   = CODEC_CAP_DELAY,
	.sample_fmts    = (enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
	.long_name      = NULL_IF_CONFIG_SMALL("Fake libfaac codec"),
    .priv_class       = &fake_libfaac_class,
};
#endif
