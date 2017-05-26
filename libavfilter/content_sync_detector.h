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

#ifndef AVFILTER_CONTENT_SYNC_DETECTOR_H
#define AVFILTER_CONTENT_SYNC_DETECTOR_H

typedef enum { CONTENT_UNDEFINED=0, CONTENT_BLACK=1, CONTENT_WHITE=2 } content_state_t;

typedef struct {
    content_state_t state;
    int64_t         state_switch_time;
    unsigned int    threshold;
    unsigned int    last_value;
} content_track_t;

typedef struct {
    int nb_tracks;
    content_track_t* tracks;
} content_sync_detector_ctx_t;

void content_sync_detector_init(content_sync_detector_ctx_t* ctx, int threshold);
content_sync_detector_ctx_t* content_sync_detector_create(unsigned int nb_tracks, int threshold);
void content_sync_detector_destroy( content_sync_detector_ctx_t* ctx );

void content_sync_write( content_sync_detector_ctx_t* ctx, unsigned int track_idx, int64_t time, unsigned int value );
float content_sync_get_diff( content_sync_detector_ctx_t* ctx, unsigned int track_idx_a, unsigned int track_idx_b );

#endif /* AVFILTER_CONTENT_SYNC_DETECTOR_H */
