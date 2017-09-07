/*
 * H.264 Bitstream Level Constant Video Frame Rate Filter
 *
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

 /* 
  * Pre-calculated slice data 
  */

#ifndef AVCODEC_H264_CONSTRATE_FILTER_DATA_H
#define AVCODEC_H264_CONSTRATE_FILTER_DATA_H

typedef struct H264SkipSliceData {
    int             width;
    int             height;
    const void*     data;
    int             size;
} H264SkipSliceData;

const H264SkipSliceData* ff_find_h264_skipslice_data(int width, int height);

#endif /* AVCODEC_H264_CONSTRATE_FILTER_DATA_H */

