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

#ifndef AVUTIL_RATE_COUNTER_H
#define AVUTIL_RATE_COUNTER_H

#include <stddef.h>
#include <inttypes.h>

typedef struct AVRateCounter AVRateCounter;

/**
 * Allocate rate counter context
 *
 * @param window_size - number of measurements stored in context
 *
 * @return pinter to allocated AVRateCounter context or NULL
 */
AVRateCounter *av_rate_counter_alloc(size_t window_size);

/**
 * Free rate counter context
 *
 * @param ctx - AVRateCounter context
 *
 */
void av_rate_counter_destroy(AVRateCounter* ctx);

/**
 * Add timestamp to context
 *
 * @param ctx  - AVRateCounter context
 * @param tick - timestamp to be used in following measurements
 */
void av_rate_counter_add_tick(AVRateCounter* ctx, int64_t tick);

/**
 * Add current timestamp returned by av_gettime() to context
 *
 * @param ctx - AVRateCounter context
 *
 */
void av_rate_counter_add_tick_now(AVRateCounter* ctx);

/**
 * Return rate calculated on whole window_size;
 *
 * @param ctx - AVRateCounter context
 *
 */
double av_rate_counter_get(AVRateCounter* ctx);

/**
 * Return rate calculated for last msec interval
 *
 * @param ctx - AVRateCounter context
 * @param interval in msec
 */
double av_rate_counter_get_interval(AVRateCounter* ctx, uint64_t interval);

/**
 * Reset rate counter context
 *
 * @param ctx - AVRateCounter context
 */
void av_rate_counter_reset(AVRateCounter* ctx);

#endif /* AVUTIL_RATE_COUNTER_H */
