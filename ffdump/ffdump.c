/*
 *  ffdump.c
 *
 *  Created by Vadim Kalinsky on 6/28/10.
 *  Copyright 2012 Epiphan Systems Inc. All rights reserved.
 *
 */

#include "ffdump.h"
#include "average_color.h"
#include "content_sync_detector.h"
#include "compare_pictures.h"

#include "ffdump_opt.h"

static content_sync_detector_ctx_t content_sync_detector_ctx;
static compare_pict_ctx_t* compare_pict_ctx;

typedef struct {
	char type_s[10];
	int64_t prev_pts;
	int64_t prev_dts;
	int frame_number;
} stream_info_t;

// Frame reading context
#define MAX_STREAMS 64
static int64_t       first_frame_time = AV_NOPTS_VALUE;
static int           frame_values[MAX_STREAMS];
static int           ref_pictures_found[MAX_STREAMS];
static AVFrame*      last_frames[MAX_STREAMS];
static stream_info_t stream_info[MAX_STREAMS];
static int64_t       time_origin;
static SwrContext*   sw_resamplers[MAX_STREAMS];



typedef struct {
    AVFormatContext *fmt_ctx;
    AVCodecContext  **dec_ctx;
    
} input_context_t;


static void pts2a(int64_t pts, char* buffer)
{
    if(pts==AV_NOPTS_VALUE)
        strcpy(buffer, " - ");
    else
        sprintf(buffer, "%ld", pts);
}

static int dump_format2(input_context_t* ctx)
{
    int i;
    AVFormatContext* ic = ctx->fmt_ctx;
    switch(ic->nb_streams)
    {
    case 0:
        printf("There are no streams\n");
        return -2;
			
    case 1:
        printf("There is one stream\n");
        break;
			
    default:
        printf("There are %d streams\n", ic->nb_streams);
        break;
    }
	
    if(ic->nb_streams > MAX_STREAMS)
    {
        printf("max stream number is %d, exiting\n", MAX_STREAMS);
        return -1;
    }
	
    for(i=0;i<ic->nb_streams;i++)
    {
        printf("##### stream %d #####\n", i);
        AVCodecContext *enc = ctx->dec_ctx[i];
		
        if(enc->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            printf("type: video\n");
            printf("codec: %s (id=%d)\n", enc->codec->name, enc->codec_id);
            printf("\tframerate: %d/%d\n", enc->framerate.num, enc->framerate.den);
            printf("\tticks per frame: %d\n", enc->ticks_per_frame);
            printf("\tframesize: %d x %d\n", enc->width, enc->height);
            printf("\tGOP size: %d\n", enc->gop_size);
            printf("\tblock_align: %d\n", enc->block_align);
			
            printf("container:\n");
            printf("\tframe rate: %d/%d\n", ic->streams[i]->r_frame_rate.num, ic->streams[i]->r_frame_rate.den);
            printf("\ttime_base: %d/%d\n", ic->streams[i]->time_base.num, ic->streams[i]->time_base.den);
            printf("\tstart time: %ld\n", ic->streams[i]->start_time);
            printf("\tduration: %s\n", av_ts2str(ic->streams[i]->duration));
        }
        else if(enc->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            printf("type: audio\n");
            AVCodec* c = avcodec_find_decoder(enc->codec_id);
            printf("codec: %s (id=%d)\n", c==NULL?"unknown":c->name, enc->codec_id);
            printf("\tframerate: %d/%d\n", enc->framerate.num, enc->framerate.den);
            printf("\tsample rate: %d\n", enc->sample_rate);
            printf("\tchannels: %d\n", enc->channels);
            printf("\tblock_align: %d\n", enc->block_align);
			
            printf("container:\n");
            printf("\ttime_base: %d/%d\n", ic->streams[i]->time_base.num, ic->streams[i]->time_base.den);
            printf("\tstart time: %ld\n", ic->streams[i]->start_time);
            printf("\tduration: %s\n", av_ts2str(ic->streams[i]->duration));
        }
		
        printf("\n");
    };
	
    if( ic->metadata != NULL )
    {
        printf( "metadata:\n" );
        AVDictionaryEntry* tag = NULL;
        for( i=0; ; i++ )
        {
            tag = av_dict_get(ic->metadata, "", tag, AV_DICT_IGNORE_SUFFIX);
            if( tag != NULL )
                printf( "%s=%s\n", tag->key, tag->value );
            else
                break;
        }
    }
	
    return 0;
}


// Input: PCM 16bit, mono
// Returns average signal level in range 0..100
static int get_loudness_of_samples( int16_t* samples, int count)
{    
    int64_t vol = 0;
    int c = count;
    for( ; c > 0; --c, ++samples)
        vol += abs(*samples);
	
    return (int)(vol*100/32768/count);
}    

static int64_t decodeVideoFrame(input_context_t* ctx, AVPacket* pkt)
{
    AVFrame* frame = av_frame_alloc();
    int got_picture = 0;
    AVFormatContext* ic = ctx->fmt_ctx;
    avcodec_decode_video2(ctx->dec_ctx[pkt->stream_index], frame, &got_picture, pkt);
	
    int64_t pts = AV_NOPTS_VALUE;
    
    if(got_picture)
    {
        pts = frame->pts;
        
        frame_values[pkt->stream_index] = get_average_color_of_image(frame, ic->streams[pkt->stream_index]->codecpar->width, ic->streams[pkt->stream_index]->codecpar->height, crop);
        
        int64_t decoded_pts = av_rescale_q(pts, ic->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q );
        content_sync_write( &content_sync_detector_ctx, pkt->stream_index, decoded_pts, frame_values[pkt->stream_index] );
        
        frame->format = ctx->dec_ctx[pkt->stream_index]->pix_fmt;
        
        if( compare_pict_ctx )
            ref_pictures_found[pkt->stream_index] = cpc_find(compare_pict_ctx, frame, crop);
    }
    else
    {
        fprintf(stderr, "can't decode frame\n");
    }
	
    av_frame_free(&frame);
    
    if(last_frames[pkt->stream_index] != NULL)
        av_free(last_frames[pkt->stream_index]);
	
    last_frames[pkt->stream_index] = frame;
    
    return pts;
}

static int64_t decodeAudioFrame(input_context_t* ctx, AVPacket* pkt)
{
    AVFrame* frame = av_frame_alloc();
    AVFormatContext *ic = ctx->fmt_ctx;
    AVCodecContext* cctx = ctx->dec_ctx[pkt->stream_index];
    
    int64_t pts = AV_NOPTS_VALUE;
    int got_frame = 0;
    int ds = avcodec_decode_audio4(cctx, frame, &got_frame, pkt);
    if( got_frame && ds > 0 )
    {
        int samples_count = frame->nb_samples;
        int loudness = 0;
        pts = frame->pts;
        
        // Further processing in PCM 16bit Mono
        if (samples_count > 0)
        {
            int16_t  samples[samples_count];                // Assume that thread stack is big enough to hold all frame samples
            int16_t* samples_bufs[1] = { samples };         // Pointers to output buffer for swresample
            if ( sw_resamplers[pkt->stream_index] == NULL )
            {
                uint64_t channel_layout = cctx->channel_layout;
                if (channel_layout == 0)
                    channel_layout = cctx->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
                
                SwrContext* s = swr_alloc_set_opts(
                    NULL, //Swr context, can be NULL
                    AV_CH_LAYOUT_MONO,      // output channel layout (AV_CH_LAYOUT_*)
                    AV_SAMPLE_FMT_S16,      // output sample format (AV_SAMPLE_FMT_*).
                    cctx->sample_rate,      // output sample rate (frequency in Hz)
                    channel_layout,         // input channel layout (AV_CH_LAYOUT_*)
                    cctx->sample_fmt,       // input sample format (AV_SAMPLE_FMT_*).
                    cctx->sample_rate,      // input sample rate (frequency in Hz)
                    0,                      // logging level offset
                    NULL);                  // parent logging context, can be NULL
                if (s)
                {
                    swr_init(s);
                    sw_resamplers[pkt->stream_index] = s;
                }
            }
            
            if ( sw_resamplers[pkt->stream_index] )
                swr_convert(sw_resamplers[pkt->stream_index], (uint8_t **)samples_bufs, samples_count, (const uint8_t**)frame->data, samples_count);
            else
                memset(samples, 0, samples_count * sizeof(uint16_t));
            
            // Buffer is ready. Get metrics
            loudness = get_loudness_of_samples( samples, samples_count );
        }
        frame_values[pkt->stream_index] = loudness;

        int64_t decoded_pts = av_rescale_q(frame->pts, ic->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q );
        content_sync_write( &content_sync_detector_ctx, pkt->stream_index, decoded_pts, frame_values[pkt->stream_index] );
    }
    
    av_free(frame);
    
    return pts;
}

static void printTimestamps(AVFormatContext* ic, AVPacket* pkt)
{
    int64_t frame_time = av_gettime();
	
    char s_pts[15];
    char s_dts[15];
    pts2a(pkt->pts, s_pts);
    pts2a(pkt->dts, s_dts);
	
    int64_t pts_delta = pkt->pts - stream_info[pkt->stream_index].prev_pts;
    char s_pts_delta[15] = "\t";
	
    if(stream_info[pkt->stream_index].prev_pts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE)
        sprintf(s_pts_delta, "%+0.3f", pts_delta * ic->streams[pkt->stream_index]->time_base.num / (float)ic->streams[pkt->stream_index]->time_base.den );
	
    if( first_frame_time == AV_NOPTS_VALUE )
        first_frame_time = frame_time;
	
    if( s_real_time )
        first_frame_time = 0;
	
    printf( "%0.3f\t", (frame_time - first_frame_time) / 1000000.0 );
	
    int i;
    for(i=0; i < ic->nb_streams; i++)
    {
        AVStream* st = ic->streams[i];
		
        if( s_compact )
        {
            if( pkt->stream_index != i )
                continue;
        }
        else
        {
            if( pkt->stream_index == i )
                printf("*");
            else
                printf(" ");
        }
		
        int64_t vis_pts = ( pkt->stream_index==i ? pkt->pts : stream_info[i].prev_pts );
		
        if( ! ( s_real_time && time_origin == 0 ) )
            vis_pts -= st->start_time;
		
        if( pkt->pts != AV_NOPTS_VALUE && stream_info[i].prev_pts != AV_NOPTS_VALUE )
        {
            if( s_real_time )
            {
                vis_pts = time_origin + av_rescale_q(vis_pts, ic->streams[i]->time_base, AV_TIME_BASE_Q );
                printf( "%0.3f\t", vis_pts / 1000000.0 );
            }
            else
            {
                printf( "%0.3f\t", vis_pts * st->time_base.num / (float)st->time_base.den );
            }
			
            if( s_show_rawtimestamps )
                printf( "%ld\t", vis_pts );
			
            if( s_show_deltas )
                printf("%s\t", s_pts_delta );
        }
        else
            printf( "-\t" );
    }
}

static void dumpSingleFrame(input_context_t* ctx, AVPacket* pkt)
{
    AVFormatContext* ic = ctx->fmt_ctx;
    if( pkt->pts == AV_NOPTS_VALUE )
        pkt->pts = pkt->dts;
	
    unsigned i;
	
    if(!s_need_decode)
    {
        printTimestamps(ic, pkt);
        stream_info[pkt->stream_index].prev_pts = pkt->pts;
        stream_info[pkt->stream_index].prev_dts = pkt->dts;
    }
    else
    {
        if( ic->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO )
            pkt->pts = pkt->dts = decodeVideoFrame(ctx, pkt);
        else if( ic->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO )
            pkt->pts = pkt->dts = decodeAudioFrame(ctx, pkt);
		
        printTimestamps(ic, pkt);
        stream_info[pkt->stream_index].prev_pts = pkt->pts;
        stream_info[pkt->stream_index].prev_dts = pkt->dts;
        
        printf("values: ");
        int comma_needed = 0;
        for( i=0;i<ic->nb_streams;i++ )
        {
            if( s_compact && pkt->stream_index != i )
                continue;
			
            if( comma_needed )
                printf(", ");
			
            printf("%d", frame_values[i]);
			
            comma_needed = 1;
        }
        printf("\t");
		
        printf("refpictures: ");
        comma_needed = 0;
        for( i=0; i<ic->nb_streams; i++ )
        {
            if( comma_needed )
                printf(", ");
            
            if( ref_pictures_found[i] == -1 )
                printf("-");
            else
                printf("%d", ref_pictures_found[i]);
            
            comma_needed = 1;
        }
        printf("\t");
        
        if( s_measure_lipsync_track_a != -1 && s_measure_lipsync_track_b != -1 )
        {
            printf( "lipsync:%0.3f\t", content_sync_get_diff(&content_sync_detector_ctx, s_measure_lipsync_track_a, s_measure_lipsync_track_b) );
        }
    }
	
    printf("\n");
	
    stream_info[pkt->stream_index].frame_number++;
}

static int64_t getTimeOrigin(AVFormatContext* ic)
{
    int64_t the_origin = 0;
    
    if( ic->pb )
    {
        if( ic->pb->seekable )
        {
            AVDictionaryEntry* tag = av_dict_get(ic->metadata, "TimeOrigin", NULL, 0);
            if( tag )
                the_origin = atoll(tag->value);
        }
        else
        {
            if( !strcmp(ic->iformat->name, "rawpacket"))
                the_origin = ic->start_time_realtime;
        }
    }
    
    return the_origin;
}

static void dumpFrames(input_context_t* ctx)
{
    AVFormatContext* ic = ctx->fmt_ctx;
    // Read frames
    int i;
        
    for(i=0; i<MAX_STREAMS; i++)
    {
        memset(&stream_info[i], 0, sizeof(stream_info[i]));
        stream_info[i].prev_pts = stream_info[i].prev_dts = AV_NOPTS_VALUE;
    }
	
    unsigned n;
	
    time_origin = getTimeOrigin(ic);
	
    for(i=0;i<ic->nb_streams;i++)
    {
        switch(ic->streams[i]->codecpar->codec_type)
        {
        case AVMEDIA_TYPE_VIDEO:
            strcpy(stream_info[i].type_s, "video");
            break;
				
        case AVMEDIA_TYPE_AUDIO:
            strcpy(stream_info[i].type_s, "audio");
            break;
				
        case AVMEDIA_TYPE_SUBTITLE:
            strcpy(stream_info[i].type_s, "subtitle");
            break;
				
        default:
            strcpy(stream_info[i].type_s, "other");
            break;
        }
    }
	
    for(n=0; ; n++)
    {
        AVPacket pkt;
        av_init_packet(&pkt);
		
        if(s_read_frames)
        {
            if( av_read_frame(ic, &pkt) < 0 )
                return;
			
            dumpSingleFrame(ctx, &pkt);
        }
        else
        {
            if(av_read_frame(ic, &pkt) < 0)
                return;
			
            printf("packet %d\tstreamid=%d, pts=%s, dts=%s, size=%d\n",
                   n,
                   pkt.stream_index, av_ts2str(pkt.pts), av_ts2str(pkt.dts), pkt.size);
        }
		
        fflush( stdout );
		
        av_packet_unref(&pkt);
    }
}

static AVCodecContext* openCodec(AVStream* stream)
{
    AVCodecContext* cctx = NULL;
    do
    {
        int ret = 0;
        AVCodec* codec = NULL;

       
        cctx = avcodec_alloc_context3(NULL);
        if (!cctx)
        {
            fprintf(stderr, "can't allocate codec context\n");
            break;
        }
            
        ret = avcodec_parameters_to_context(cctx, stream->codecpar);
        if (ret < 0)
        {
            fprintf(stderr, "can't convert codec parameters to codec context\n");
            break;
        }

        codec = avcodec_find_decoder(stream->codecpar->codec_id);
		
        if(codec == NULL)
        {
            fprintf(stderr, "can't find codec id=%d\n", stream->codecpar->codec_id);
            break;
        }
        
        if( avcodec_open2(cctx, codec, NULL) < 0 )
        {
            fprintf(stderr, "can't open codec %s\n", codec->name);
            break;
        }

        return cctx;
    }
    while (0);
        
    if (cctx)
        avcodec_free_context(&cctx);
    return NULL;
}


static int openCodecs(input_context_t* ic)
{
    unsigned i;
    short do_cleanup = 0;
    
    ic->dec_ctx = av_mallocz_array(ic->fmt_ctx->nb_streams, sizeof(*ic->dec_ctx));
    
    for(i=0; i<ic->fmt_ctx->nb_streams; i++)
    {
        ic->dec_ctx[i] = openCodec(ic->fmt_ctx->streams[i]);
        if (ic->dec_ctx[i] == NULL)
        {
            do_cleanup = 1;
            break;
        }
    }

    if (do_cleanup)
    {
        for(i=0; i<ic->fmt_ctx->nb_streams; i++)
        {
            av_free(ic->dec_ctx[i]);
        }
        av_free(ic->dec_ctx);
    }
    
    return 1;
}

int main(int argc, char **argv)
{
    int i;
    
    // Initialize ffmpeg
    av_register_all();
    avformat_network_init();
    
    ffdump_parse_options(argc, argv);
    
    if( nb_compare_pict || s_comparator_learning_mode )
    {
        s_dump_frames = 1;
        s_need_decode = 1;
        s_read_frames = 1;
        
        compare_pict_ctx = cpc_alloc();
        
        if( s_comparator_learning_mode )
            cpc_set_learn_mode(compare_pict_ctx, 1);
        
        if( nb_compare_pict )
        {
            int i;
            for( i=0; i<nb_compare_pict; i++ )
            {
                cpc_add_file(compare_pict_ctx, compare_pict_filenames[i]);
                av_free(compare_pict_filenames[i]);
            }
            
            av_free(compare_pict_filenames);
        }
    }
    
    for( i=0; i<MAX_STREAMS; i++ )
    {
        ref_pictures_found[i] = -1;
    }
    
    if( s_input_file_name == NULL || argc < 2 )
    {
        fprintf( stderr, "Usage: %s <options> <input stream>\n", argv[0] );
        show_help_default(0,0);
        return -1;
    }
    
    if( s_measure_lipsync_track_a != -1 && s_measure_lipsync_track_b != -1 )
        content_sync_detector_init(&content_sync_detector_ctx);
    
    int res;
    AVInputFormat* fmt = NULL;
    input_context_t ic;
    memset(&ic, 0, sizeof(ic));
    
    // Use user's input format if needed
    if(s_forced_input_format)
    {
        fmt = av_find_input_format(s_forced_input_format);
        if( fmt == NULL )
        {
            fprintf(stderr, "Could not find input format \"%s\"\n", s_forced_input_format);
            return -1;
        }
    }
    
    // Open file
    res = avformat_open_input(&ic.fmt_ctx, s_input_file_name, fmt, NULL);
    if(res<0)
    {
        if(!s_forced_input_format)
        {
            printf("can't open file %s\n", s_input_file_name);
            return -1;
        }
    }
    else
        fmt = ic.fmt_ctx->iformat;
    
    // Get stream info
    res = avformat_find_stream_info(ic.fmt_ctx, NULL);
    if(res<0)
    {
        printf("can't find stream info %s\n", s_input_file_name);
        return -1;
    }
    
    // Open codecs
    if( ! openCodecs(&ic) )
        return -1;
    
    // Print file info
    if( ! s_dump_frames )
        if( dump_format2(&ic) < 0 )
            return -1;
    
    if(s_dump_frames)
        dumpFrames(&ic);
	
    // Clean-up
    for(i=0; i<MAX_STREAMS; i++ )
    {
        swr_free(&sw_resamplers[i]);
    }

    for (i = 0; i < ic.fmt_ctx->nb_streams; i++)
    {
        avcodec_free_context(&ic.dec_ctx[i]);
    }

    av_free(ic.dec_ctx);

    avformat_close_input(&ic.fmt_ctx);
    
    return 0;
}

