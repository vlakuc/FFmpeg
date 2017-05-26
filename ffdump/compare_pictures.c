/*
 * Copyright (c) 2014 - 2017 Epiphan Systems Inc. All rights reserved.
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
//  compare_pictures.c
//  ffdump
//
//  Created by Vadim Kalinsky on 2014-10-08.
//
//

#include "compare_pictures.h"

#include <libswscale/swscale.h>
#include "libavutil/imgutils.h"


#define MAX_PATH 255
#define MAX_COMPARE_PICTURES 30 // Description of --comparator-learning-mode param contains this number.
#define MAX_DIFF_PICTURES 5
#define PIXEL_DIFF_THRESHOLD 30

struct compare_pict_ctx_t {
    int learn_mode;
    AVFrame* reference_pictures[MAX_COMPARE_PICTURES];
    AVFrame* diff_pictures[MAX_DIFF_PICTURES];
};

compare_pict_ctx_t* cpc_alloc(void)
{
    compare_pict_ctx_t* ctx = av_malloc(sizeof(compare_pict_ctx_t));
    memset(ctx, 0, sizeof(*ctx));
    
    return ctx;
}

void cpc_free( compare_pict_ctx_t* ctx )
{
    int i;
    for( i=0; i<MAX_COMPARE_PICTURES; i++ )
        if( ctx->reference_pictures[i] != NULL )
        {
            av_freep(&ctx->reference_pictures[i]->data[0]);
            av_frame_free(&ctx->reference_pictures[i]);
        }
    
    for( i=0; i<MAX_DIFF_PICTURES; i++ )
        if( ctx->diff_pictures[i] != NULL )
        {
            av_freep(&ctx->diff_pictures[i]->data[0]);
            av_frame_free(&ctx->diff_pictures[i]);
        }
    
    av_free(ctx);
}

void cpc_set_learn_mode( compare_pict_ctx_t* ctx, int enabled )
{
    ctx->learn_mode = enabled;
}

static int find_free_picture_slot( compare_pict_ctx_t* ctx )
{
    int i;
    for( i=0; i<MAX_COMPARE_PICTURES; i++ )
    {
        if( ctx->reference_pictures[i] == NULL )
            return i;
    }

    return -1;
}

static AVFrame* scale_frame( const AVFrame* frame, int width, int height, enum AVPixelFormat pix_fmt )
{
    AVFrame* new_frame = av_frame_alloc();
    uint8_t* new_frame_data = av_malloc(av_image_get_buffer_size(pix_fmt, width, height, 1));
    av_image_fill_arrays(new_frame->data, new_frame->linesize, new_frame_data, pix_fmt, width, height, 1);
    new_frame->width  = width;
    new_frame->height = height;
    new_frame->format = pix_fmt;
    
    struct SwsContext* sws_context = sws_getContext(frame->width, frame->height, frame->format,
                                                    new_frame->width, new_frame->height, new_frame->format, 0, NULL, NULL, NULL);
    sws_scale(sws_context,
              (const uint8_t* const*)frame->data,     // srcSlice
              frame->linesize, // srcStride
              0,               // srcSliceY
              frame->height,   // srcSliceH
              new_frame->data, // dst
              new_frame->linesize // dstStride
              );
    
    sws_freeContext(sws_context);
    
    return new_frame;
}

int cpc_add_file( compare_pict_ctx_t* ctx, const char* filename )
{
    int frame_idx = -1;
    int res;
    
    AVFormatContext* ic = NULL;
    AVCodecContext* cdc = NULL;
    
    frame_idx = find_free_picture_slot(ctx);
    
    if( frame_idx < 0 )
    {
        fprintf(stderr, "Max number (%d) of reference pictures reached\n", MAX_COMPARE_PICTURES);
        goto err;
    }

    res = avformat_open_input(&ic, filename, NULL, NULL);
    
    if( res < 0 )
    {
        fprintf(stderr, "Could not open %s\n", filename);
        goto err;
    }
    
    res = avformat_find_stream_info(ic, NULL);
    
    if( res < 0 )
    {
        fprintf(stderr, "Could not find stream info for %s\n", filename);
        goto err;
    }
    
    AVCodec* codec = avcodec_find_decoder( ic->streams[0]->codecpar->codec_id );
    
    if( codec == NULL )
    {
        fprintf(stderr, "Could not find codec with id %d\n", ic->streams[0]->codecpar->codec_id);
        goto err;
    }
    
    cdc = avcodec_alloc_context3(codec);
    res = avcodec_parameters_to_context(cdc, ic->streams[0]->codecpar);
    if ( res < 0 )
    {
        fprintf(stderr, "Could not convert codec parameters to codec context\n");
        goto err;
    }
    
    res = avcodec_open2(cdc, codec, NULL);
    
    if( res < 0 )
    {
        fprintf(stderr, "Could not open codec %s\n", codec->name);
        goto err;
    }
    
    AVPacket pkt;
    av_read_frame(ic, &pkt);
    
    AVFrame* frame = av_frame_alloc();
    int got_picture = 0;
    avcodec_decode_video2(cdc, frame, &got_picture, &pkt);
    
    ctx->reference_pictures[frame_idx] = scale_frame(frame, frame->width, frame->height, AV_PIX_FMT_RGB24);
    
    av_frame_free(&frame);
    
    return 0;
    
err:
    if( ic )
        avformat_free_context(ic);
    
    if( cdc )
        avcodec_free_context(&cdc);
    
    return -1;
}

static int save_frame( AVFrame* frame, const char* filename )
{
    AVFrame* frame_to_save = frame;
    
    int res = 0;
    
    AVCodecContext* out_cd_ctx;
    
    AVOutputFormat* fmt = av_guess_format(NULL, filename, NULL);
    if( !fmt )
    {
        fprintf(stderr, "Could not find format for %s\n", filename);
        goto err;
    }
    
    enum AVCodecID codec_id = av_guess_codec(fmt, NULL, filename, NULL, AVMEDIA_TYPE_VIDEO);
    
    AVCodec* codec = avcodec_find_encoder(codec_id);
    if(!codec)
    {
        fprintf(stderr, "Could not find encoder for %s\n", filename);
        goto err;
    }
    
    if( frame->format != codec->pix_fmts[0] )
        frame_to_save = scale_frame(frame, frame->width, frame->height, codec->pix_fmts[0]);
    
    out_cd_ctx = avcodec_alloc_context3(codec);
    out_cd_ctx->width         = frame_to_save->width;
    out_cd_ctx->height        = frame_to_save->height;
    out_cd_ctx->pix_fmt       = frame_to_save->format;
    out_cd_ctx->time_base.num = 1;
    out_cd_ctx->time_base.den = 25;
    out_cd_ctx->bit_rate      = 4000000;
    out_cd_ctx->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    
    if(avcodec_open2(out_cd_ctx, codec, NULL) != 0)
    {
        fprintf(stderr, "Could not open %s encoder\n", codec->name);
        goto err;
    }
    
    // Could use fopen/fwrite/fclose instead of ByteIOContext
    AVIOContext *pb = NULL;
    if( avio_open(&pb, filename, AVIO_FLAG_WRITE) < 0 )
    {
        fprintf(stderr, "Could not open output file %s\n", filename);
        goto err;
    }
    
    AVPacket pkt;
    av_new_packet(&pkt, frame_to_save->width*frame_to_save->height*4*1.5);
    int got_packet = 0;
    
    int enc_result = avcodec_encode_video2(out_cd_ctx, &pkt, frame_to_save, &got_packet);
    if( enc_result < 0 )
        fprintf(stderr, "Encoding failed");
    
    avio_write(pb, pkt.data, pkt.size);
    avio_flush(pb);
    
    goto end;
    
err:
    res = -1;
    
end:
    if( out_cd_ctx )
    {
        avcodec_close(out_cd_ctx);
        av_free(out_cd_ctx);
    }
    
    if( pb )
        avio_close(pb);
    
    av_packet_unref(&pkt);
    
    if( frame_to_save != frame )
    {
        av_freep(&frame_to_save->data[0]);
        av_frame_free(&frame_to_save);
    }
    
    return res;
}


// Return values of these two:
//   - if frames equal: NULL
//   - if NOT equal: AVFrame with grayscaled content of frame "a" with red dots in "failed" pixels.

// Compares frames which have same height, width and pixel format.
static AVFrame* get_frame_diff( const AVFrame* a, const AVFrame* b, rect_t crop )
{
    int x,y;
    int x0,y0, w,h;
    
    int res = 0;
    
    AVFrame* diff_frame = NULL;
    
    // Apply cropping (if any)
    if(   crop.size.width  >  0
       && crop.size.height >  0
       && crop.origin.x    >= 0
       && crop.origin.y    >= 0 )
    {
        x0 = a->width  * crop.origin.x    / 100;
        y0 = a->height * crop.origin.y    / 100;
        w  = a->width  * crop.size.width  / 100;
        h  = a->height * crop.size.height / 100;
    }
    else
    {
        // No cropping.
        x0 = 0;
        y0 = 0;
        w  = a->width;
        h  = a->height;
    }
    
    // Check parameters safety.
    if( x0 + w > a->width && y0 + h > a->height )
    {
        fprintf(stderr, "Invalid cropping: %dx%d/%dx%d, image is %dx%d\n", x0, w, y0, h, a->width, a->height);
        return 0;
    }
    
    // Prepare difference frame
    diff_frame = av_frame_alloc();
    diff_frame->format = AV_PIX_FMT_RGB24;
    diff_frame->width = a->width;
    diff_frame->height = a->height;
    size_t frame_data_size = av_image_get_buffer_size(diff_frame->format, diff_frame->width, diff_frame->height, 1);
    uint8_t* frame_data = av_mallocz(frame_data_size);    
    av_image_fill_arrays(diff_frame->data, diff_frame->linesize, frame_data, diff_frame->format, diff_frame->width, diff_frame->height, 1);
    
    // Do the job.
    for(y=0; y<h; y++)
    {
        for(x=0; x<w; x++)
        {
            int a_offset = (y0+y)*a->linesize[0] + (x0+x)*3;
            int b_offset = (y0+y)*b->linesize[0] + (x0+x)*3;
            
            uint8_t ar = a->data[0][ a_offset + 0 ];
            uint8_t ag = a->data[0][ a_offset + 1 ];
            uint8_t ab = a->data[0][ a_offset + 2 ];
            
            uint8_t br = b->data[0][ b_offset + 0 ];
            uint8_t bg = b->data[0][ b_offset + 1 ];
            uint8_t bb = b->data[0][ b_offset + 2 ];
            
            int diff_r = abs( ar - br );
            int diff_g = abs( ag - bg );
            int diff_b = abs( ab - bb );
            
            int pix_offset = (y0+y)*diff_frame->linesize[0] + (x0+x)*3;
            diff_frame->data[0][pix_offset+0] =
            diff_frame->data[0][pix_offset+1] =
            diff_frame->data[0][pix_offset+2] = (ar+ag+ab)/3;
            
            if( diff_r > PIXEL_DIFF_THRESHOLD || diff_g > PIXEL_DIFF_THRESHOLD || diff_b > PIXEL_DIFF_THRESHOLD )
            {
                diff_frame->data[0][pix_offset+0] = 255;
                res = 1;
            }
        }
    }
    
    if( res == 0 )
    {
        av_freep(&diff_frame->data[0]);
        av_frame_free(&diff_frame);
    }
    
    return diff_frame;
}

// Scales/converts frame a and compares using function above.
static AVFrame* get_frame_diff_smart( AVFrame* a, const AVFrame* b, rect_t crop )
{
    AVFrame* new_a = a;
    
    if( a->width != b->width || a->height != b->height || a->format != b->format )
        new_a = scale_frame(a, b->width, b->height, b->format);
    
    AVFrame* res = get_frame_diff(new_a, b, crop);
    
    if( new_a != a )
    {
        av_freep(&new_a->data[0]);
        av_frame_free(&new_a);
    }
    
    return res;
}

static void add_diff_frame(compare_pict_ctx_t* ctx, AVFrame* frame, rect_t crop)
{
    int i;
    for( i=0; i<MAX_DIFF_PICTURES; i++ )
    {
        if( ctx->diff_pictures[i] )
        {
            AVFrame* diff_diff_frame = get_frame_diff(frame, ctx->diff_pictures[i], crop);
            if( diff_diff_frame )
            {
                av_freep(&diff_diff_frame->data[0]);
                av_frame_free(&diff_diff_frame);
            }
            else
                break; // This frame is already in the list
        }
        else // The diff is not found
        {
            ctx->diff_pictures[i] = av_frame_clone(frame);
            
            char filename[255];
            sprintf(filename, "/tmp/diff_%03d.png", i);
            save_frame(frame, filename);
            break;
        }
    }
}

int cpc_find( compare_pict_ctx_t* ctx, AVFrame* f, rect_t crop )
{
    int i;
    AVFrame* diff_frame = NULL;
    for( i=0; i<MAX_COMPARE_PICTURES; i++ )
    {
        if( ctx->reference_pictures[i] )
        {
            diff_frame = get_frame_diff_smart( f, ctx->reference_pictures[i], crop );
            if( diff_frame == NULL )
                return i+1;
        }
    }
    
    if( ctx->learn_mode )
    {
        int frame_idx = find_free_picture_slot(ctx);
        
        if( frame_idx >= 0 )
        {
            ctx->reference_pictures[frame_idx] = scale_frame(f, f->width, f->height, AV_PIX_FMT_RGB24);
            
            char name[255];
            sprintf(name, "/tmp/reference_%d.png", frame_idx+1);
            save_frame(f, name);
        }
        else
        {
            fprintf(stderr, "Max number (%d) of reference pictures reached\n", MAX_COMPARE_PICTURES);
        }
    }
    else
    {
        add_diff_frame(ctx, diff_frame, crop);
        av_freep(&diff_frame->data[0]);
        av_frame_free(&diff_frame);
    }
    
    return -1;
}
