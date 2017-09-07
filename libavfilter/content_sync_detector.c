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

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include <limits.h>

#include "content_sync_detector.h"

    
void content_sync_detector_init( content_sync_detector_ctx_t* ctx, int threshold )
{
    
    int i;

    av_assert0(ctx != NULL);
    
    for( i=0; i<ctx->nb_tracks; i++ )
    {
        ctx->tracks[i].state             = CONTENT_UNDEFINED;
        ctx->tracks[i].state_switch_time = 0;
        ctx->tracks[i].threshold         = threshold;
        ctx->tracks[i].last_value        = UINT_MAX;
    }
}

content_sync_detector_ctx_t* content_sync_detector_create( unsigned int nb_tracks, int threshold )
{
    content_sync_detector_ctx_t* ptr = NULL;
    
    ptr = av_mallocz(sizeof(content_sync_detector_ctx_t));
    av_assert0(ptr != NULL);
    ptr->tracks = av_calloc(nb_tracks, sizeof(content_track_t));
    av_assert0(ptr->tracks != NULL);
    ptr->nb_tracks = nb_tracks;
    
    content_sync_detector_init(ptr, threshold);
    return ptr;
}

void content_sync_detector_destroy( content_sync_detector_ctx_t* ctx )
{
    if (ctx != NULL && ctx->tracks != NULL) {
        av_free(ctx->tracks);
        av_free(ctx);
    }
}

void content_sync_write( content_sync_detector_ctx_t* ctx, unsigned int track_idx, int64_t time, unsigned int value )
{
    content_track_t* track = NULL;

    av_assert0(ctx != NULL);
    av_assert0(track_idx < ctx->nb_tracks);
    
    track = &ctx->tracks[track_idx];

    if( track->last_value != UINT_MAX )
    {
        int what_happened = 0; // bit 1: last state, bit 0: current state
        
        if( track->last_value > track->threshold )
            what_happened |= 0x2;
        
        if( value > track->threshold )
            what_happened |= 0x1;
        
        // "10" - state changed to black, "01" - state changed to white
        
        if( what_happened == 0x1 || what_happened == 0x2 )
        {
            track->state             = what_happened==0x1 ? CONTENT_WHITE : CONTENT_BLACK;
            track->state_switch_time = time;
        }
    }

   
    track->last_value = value;
}

float content_sync_get_diff( content_sync_detector_ctx_t* ctx, unsigned int track_idx_a, unsigned int track_idx_b )
{
    content_track_t *track_a = NULL,
                    *track_b = NULL;

    av_assert0(ctx != NULL);
    av_assert0(track_idx_a < ctx->nb_tracks);
    av_assert0(track_idx_b < ctx->nb_tracks);
    
    track_a = &ctx->tracks[track_idx_a];
    track_b = &ctx->tracks[track_idx_b];
    
    if( track_a->state == track_b->state && track_a->state != CONTENT_UNDEFINED )
        return (track_a->state_switch_time - track_b->state_switch_time) / 1000000.0;
    else
        return LIPSYNC_UNDEFINED;
}
