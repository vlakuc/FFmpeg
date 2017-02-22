#ifndef AVSYNC_UTILS_H
#define AVSYNC_UTILS_H

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>


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

int get_average_color_of_image(AVFrame* frame, int width, int height, rect_t roi);

int get_loudness_of_samples( int16_t* samples, int count);

#endif /* AVSYNC_UTILS_H */
