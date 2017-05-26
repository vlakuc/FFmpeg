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


typedef struct {
    int x;
    int y;
} point_t;

typedef struct {
    int width;
    int height;
} rect_size_t;

typedef struct {
    point_t origin;
    rect_size_t size;
} rect_t;

#define MAKE_POINT(x,y)     (point_t){x,y}
#define MAKE_RECT_SIZE(w,h) (rect_size_t){w,h}
#define MAKE_RECT(x,y,w,h)  (rect_t){MAKE_POINT(x,y), MAKE_RECT_SIZE(w,h)}

int get_average_color_of_image(AVFrame* frame, int width, int height, rect_t roi);

int get_loudness_of_samples( int16_t* samples, int count);

#endif /* AVFILTER_AVSYNC_UTILS_H */
