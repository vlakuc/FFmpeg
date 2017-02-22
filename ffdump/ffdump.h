/*
 *  ffdump.h
 *  vgagrid_sources
 *
 *  Created by Vadim Kalinsky on 6/28/10.
 *  Copyright 2010 Epiphan Systems Inc. All rights reserved.
 *
 */

#ifndef FFDUMP_H_INCLUDED
#define FFDUMP_H_INCLUDED

#include <stdio.h>

#include <libavformat/avformat.h>
#include <libavformat/url.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavutil/fifo.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>

typedef struct {
    int x;
    int y;
} point_t;

typedef struct {
    int width;
    int height;
} rect_size_t;

typedef struct {
    point_t origin;
    rect_size_t size;
} rect_t;

#define MAKE_POINT(x,y)     (point_t){x,y}
#define MAKE_RECT_SIZE(w,h) (rect_size_t){w,h}
#define MAKE_RECT(x,y,w,h)  (rect_t){MAKE_POINT(x,y), MAKE_RECT_SIZE(w,h)}


#endif // FFDUMP_H_INCLUDED