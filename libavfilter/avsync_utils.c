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

