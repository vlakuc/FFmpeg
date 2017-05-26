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
 *  ffdump.h
 *  vgagrid_sources
 *
 *  Created by Vadim Kalinsky on 6/28/10.
 *
 */

#ifndef FFDUMP_FFDUMP_H
#define FFDUMP_FFDUMP_H

#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavformat/url.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavutil/fifo.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>

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


#endif
