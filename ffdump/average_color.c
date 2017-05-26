/*
 * Copyright (c) 2012 - 2017 Epiphan Systems Inc. All rights reserved.
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
//  average_color.c
//  ffdump
//
//  Created by Vadim Kalinsky on 12-04-03.
//

#include <stdio.h>

#include "average_color.h"

// Currently this is an average brightness (channel Y)
int get_average_color_of_image(AVFrame* frame, int width, int height, rect_t crop)
{
    int ls = frame->linesize[0];
    long brightness = 0;
	
    int x,y;
    int x0,y0, w,h;
	
    // Apply cropping (if any)
    if(   crop.size.width  >  0
          && crop.size.height >  0
          && crop.origin.x    >= 0
          && crop.origin.y    >= 0 )
    {
        x0 = width  * crop.origin.x    / 100;
        y0 = height * crop.origin.y    / 100;
        w  = width  * crop.size.width  / 100;
        h  = height * crop.size.height / 100;
    }
    else
    {
        // No cropping.
        x0 = 0;
        y0 = 0;
        w  = width;
        h  = height;
    }
	
    // Check parameters safety.
    if( x0 + w > width && y0 + h > height )
    {
        fprintf(stderr, "Invalid cropping: %dx%d/%dx%d, image is %dx%d\n", x0, w, y0, h, width, height);
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
