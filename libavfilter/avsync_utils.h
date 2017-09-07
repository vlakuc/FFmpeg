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

#ifndef AVFILTER_AVSYNC_UTILS_H
#define AVFILTER_AVSYNC_UTILS_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

int get_average_color_of_image(AVFrame* frame, int width, int height);

int get_loudness_of_samples( int16_t* samples, int count);

#endif /* AVFILTER_AVSYNC_UTILS_H */
