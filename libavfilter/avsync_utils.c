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
int get_average_color_of_image(AVFrame* frame, int width, int height, rect_t roi)
{
    int ls = frame->linesize[0];
    long brightness = 0;
    int x,y;
    int x0,y0, w,h;
    
    if(   roi.size.width  >  0
          && roi.size.height >  0
          && roi.origin.x    >= 0
          && roi.origin.y    >= 0 )
    {
        x0 = roi.origin.x;
        y0 = roi.origin.y;
        w  = roi.size.width;
        h  = roi.size.height;
    }
    else
    {
        // No roi.
        x0 = 0;
        y0 = 0;
        w  = width;
        h  = height;
    }

    // Check parameters safety.
    if( x0 + w > width || y0 + h > height )
    {
        av_log(NULL, AV_LOG_ERROR, "Invalid roi: %dx%d/%dx%d, image is %dx%d\n", x0, w, y0, h, width, height);
        return 0;
    }
	
    // Do the job.
    for(y=0; y<h; y++)
    {
        for(x=0; x<w; x++)
        {
            brightness += frame->data[0][ (y0+y)*ls + (x0+x) ];
        }
    }

    return (int)( (brightness/(w*h)+1)*100l/255l);
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

