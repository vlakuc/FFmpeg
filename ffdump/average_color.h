//
//  average_color.h
//  ffdump
//
//  Created by Vadim Kalinsky on 12-04-03.
//  Copyright (c) 2012 Epiphan Systems Inc. All rights reserved.
//

#ifndef ffdump_average_color_h
#define ffdump_average_color_h

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#include "ffdump.h"

int get_average_color_of_image(AVFrame* frame, int width, int height, rect_t crop);

#endif
