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

#include <libavutil/log.h>

#include "avsync_utils.h"


// Currently this is an average brightness (channel Y)
int get_average_color_of_image(AVFrame* frame, int width, int height)
{
    int ls = frame->linesize[0];
    long brightness = 0;
    int x,y;

    for(y=0; y<height; y++)
    {
        for(x=0; x<width; x++)
        {
            brightness += frame->data[0][ y*ls + x ];
        }
    }

    return (int)( (brightness/(width*height)+1)*100l/255l);
}

// Input: PCM 16bit, mono
// Returns average signal level in range 0..100
int get_loudness_of_samples( int16_t* samples, int count)
{    
    int64_t vol = 0;
    int c = count;
    for( ; c > 0; --c, ++samples)
        vol += abs(*samples);
	
    return (int)(vol*100/32768/count);
}    

