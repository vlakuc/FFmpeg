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

#ifndef AVUTIL_CALCULATE_BITRATE_H
#define AVUTIL_CALCULATE_BITRATE_H

#include <stdint.h>

#define RING_BUFFER_SIZE 200

typedef struct {
    uint64_t total_size;
    uint64_t start_time;
    uint64_t prev_time;
    unsigned index;
    unsigned overflowed;
    struct {
        unsigned size;
        unsigned time_diff;
    } entries[RING_BUFFER_SIZE];
} AVBitrateContext;


/**
 * Record size and corresponding timestamp into context
 *
 * @param ctx  - AVBitrateContext context
 * @param size - data size to be used in bit rate calculation (in bytes)
 * @param current_time - timestamp (in mpst cases av_gettime() should be used)
 */
void av_fix_bitrate(AVBitrateContext **ctx, int size, int64_t current_time);

/**
 * Calculate bitrate value (bits/sec) using data recorded by av_fix_bitrate()
 *
 * @param ctx  - AVBitrateContext context
 * @param current_time - timestamp (in most cases av_gettime() should be used)
 */
unsigned av_calculate_bitrate(const AVBitrateContext *ctx, int64_t current_time);

#endif /* AVUTIL_CALCULATE_BITRATE_H */
