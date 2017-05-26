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

#include "time.h"
#include "mem.h"
#include "calculate_bitrate.h"


unsigned av_calculate_bitrate(const AVBitrateContext *ctx, int64_t current_time)
{
    if (ctx != NULL && ctx->total_size) {
        const int64_t start_time = ((current_time - ctx->prev_time) / 1000) + ctx->start_time;
        if (start_time)
            return ctx->total_size * 1000 * 8 / start_time;
    }
    return 0;
}


void av_fix_bitrate(AVBitrateContext **ctx, int size, int64_t current_time)
{
    if (*ctx == NULL) {
        *ctx = (AVBitrateContext *)av_malloc(sizeof(AVBitrateContext));
        memset((void*)*ctx, 0, sizeof(AVBitrateContext));
    } else {
        const int64_t time_diff = (current_time - (*ctx)->prev_time) / 1000;
        if ((*ctx)->overflowed) {
            (*ctx)->total_size -= (*ctx)->entries[(*ctx)->index].size;
            (*ctx)->start_time -= (*ctx)->entries[(*ctx)->index].time_diff;
        }
        (*ctx)->total_size += size;
        (*ctx)->start_time += time_diff;
        (*ctx)->entries[(*ctx)->index].size = size;
        (*ctx)->entries[(*ctx)->index].time_diff = time_diff;
        if (++(*ctx)->index >= RING_BUFFER_SIZE) {
            (*ctx)->index = 0;
            (*ctx)->overflowed = 1;
        }   
    }
    (*ctx)->prev_time = current_time;
}

