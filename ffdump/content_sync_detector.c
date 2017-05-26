/*
 * Copyright (c) 2013 - 2017 Epiphan Systems Inc. All rights reserved.
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

//
//  content_sync_detector.c
//  ffdump
//
//  Created by Vadim Kalinsky on 2013-11-29.
//
//

#include <stdio.h>
#include <limits.h>

#include "content_sync_detector.h"

void content_sync_detector_init( content_sync_detector_ctx_t* ctx )
{
    int i;
    for( i=0; i<sizeof(ctx->tracks)/sizeof(ctx->tracks[0]); i++ )
    {
        ctx->tracks[i].state             = CONTENT_UNDEFINED;
        ctx->tracks[i].state_switch_time = 0;
        ctx->tracks[i].threshold         = 10;
        ctx->tracks[i].last_value        = UINT_MAX;
    }
}

void content_sync_detector_destroy( content_sync_detector_ctx_t* ctx )
{
}

void content_sync_write( content_sync_detector_ctx_t* ctx, unsigned int track_idx, int64_t time, unsigned int value )
{
    if( track_idx > sizeof(ctx->tracks)/sizeof(ctx->tracks[0]) )
    {
        fprintf( stderr, "Content track id %d is bigger than max %ld\n", track_idx, sizeof(ctx->tracks)/sizeof(ctx->tracks[0]) );
        return;
    }
    
    content_track_t* track = &ctx->tracks[track_idx];

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
    content_track_t* track_a = &ctx->tracks[track_idx_a];
    content_track_t* track_b = &ctx->tracks[track_idx_b];
    
    if( track_a->state == track_b->state && track_a->state != CONTENT_UNDEFINED )
        return (track_a->state_switch_time - track_b->state_switch_time) / 1000000.0;
    else
        return 1110.0f;
}
