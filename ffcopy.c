// ffcopy: Track extraction and file concatenation utility


#include "config.h"
#include "libavutil/ffversion.h"

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"
#include "libavutil/parseutils.h"
#include "cmdutils.h"

// Required by cmdutils:
const char program_name[] = "ffcopy";
const int program_birth_year = 2012;

// ffcopy configuration
static int debug_ts = 0;

// Single file output
typedef struct {
    AVFormatContext* oc;        // Output file
    int  nb_input_streams;      // Number of input streams 
    int* streams_map;           // Map input stream indexes to output ones
    int64_t ts_offset;          // Timestamp of the first frame
} output_context_t;

// API:
//  * output_open(...)
//  * output_write(context, packet)
//  * output_close(context)

// Auxiliary structure for precision adjacent inputs concatenation and time range extraction
typedef struct {
    int64_t last_dts;       // dts of the last packet (realtime timescale)
    int64_t next_dts;       // expected dts of the next packet (realtime timescale)
    int64_t dts_offset;     // dts offset per stream (realtime timescale) 
    
    // stream read state
    enum
    {
        INPUT_SKIP = 0,         // Skip incoming frames (dts < start_time)
        INPUT_WAIT_KEYFRAME,    // Ready to read - waiting for key frames (dts >= start_time)
        INPUT_READ,             // Read is in progress (dts >= start_time && dts+duration < end_time)
        INPUT_DONE              // Stop reading (dts + duration > end_time)
    } read_state;
    
} input_stream_t; 

// Multi-files input
typedef struct {
    
    int nb_inputs;                  // Number of inputs
    AVFormatContext** inputs;       // Input contexts
    int64_t start_time;             // Time range: start time. Can be AV_NOPTS_VALUE
    int64_t end_time;               // Time range: end time (start_time + duration). Can be AV_NOPTS_VALUE
    
    // Current state
    int input_index;                // Index of current file
    int64_t time_origin;            // Time origin (real world time) for the current file
    // Additional timing information to perform precision glue of adjacent continuous inputs
    input_stream_t* streams;   
    int nb_streams;                 // Number of input streams (the same as nb_inputs[0]->nb_streams)
    
    // For progress reporting
    int64_t first_time_origin;      // Time origin of the first input
    int64_t inputs_duration;        // Total duration for all inputs (mks)
} input_context_t;

// API:
// * input_open(...)
// * input_get_format_context(ctx)
// * packet input_read(ctx)
// * input_close(ctx)


// Concatenate files and extract tracks
typedef struct {
    int64_t read_frames;        // Read frames (for all tracks)
    int64_t read_ms;            // Read ms
    int64_t total_ms;           // Total duration (ms)
} copy_progress_t;

//  One multi-file input and multiple output files
typedef struct {
    input_context_t*    input;          // Input file(s)
    output_context_t**  outputs;        // Output files
    int                 nb_outputs;     // Number of outputs
    
    volatile int        processing;     // Process continue/interrupt flag

    void(*progress) (const copy_progress_t*);   // Copy progress callback
} copy_context_t;

// API:
// * copy_context_t* copy_allocate_context()
// * copy_free_context(ctx)
// * copy_new_input(ctx, ...)
// * copy_add_new_output(ctx, ...)
// * copy_close(ctx)
// * copy(ctx)


//////////////////////
// Utilities

static int compare_format_contexts(const AVFormatContext* c1, const AVFormatContext* c2)
{
    int i;

    assert(c1 != NULL);
    assert(c2 != NULL);

    if( c1->nb_streams != c2->nb_streams )
        return 0;

    for(i=0; i < c1->nb_streams; i++)
    {
        AVStream* s1 = c1->streams[i];
        AVStream* s2 = c2->streams[i];
        
        if (s1->codecpar->codec_id != s2->codecpar->codec_id)     return 0;
        if (av_cmp_q(s1->time_base, s2->time_base) != 0)    return 0;
    }
    
    return 1;
}


static int64_t get_timeorigin(AVFormatContext* ic)
{
    int64_t ret = 0;
    AVDictionaryEntry *timeorigin = av_dict_get(ic->metadata, "timeorigin", NULL, 0);
    if (timeorigin)
        ret = atoll(timeorigin->value);
    return ret;
}

// Get duration of input (maximum of all tracks)
static int64_t get_input_duration(AVFormatContext* ic)
{
    int64_t duration = 0;
    int i;

    if (ic->duration > 0)
        return ic->duration;
    
    for (i = 0; i < ic->nb_streams; i++)
    {
        int d = av_rescale_q(ic->streams[i]->duration, ic->streams[i]->time_base, AV_TIME_BASE_Q);
        if (d > duration)
            duration = d;
    }

    return duration;
}

//////////////////////////////
// Single file output

static output_context_t* output_open(
                        AVFormatContext* ic,         // Input context
                        const char* filename,        // Output filename
                        const AVDictionary* options, // An optional output file options. Can be null
                        int64_t timeorigin,          // Time of the first packet
                        const int* streams_map,      // Input to output stream mapping. Can be null for all-to-all
                        int  nb_streams_map)         // Number of entries in stream_map
{
    int ret = 0;
    int i;
    output_context_t* ctx;
    int nb_output_streams = 0;

    assert(ic != NULL);

    // Allocate context
    ctx = av_mallocz(sizeof(output_context_t));
    if (ctx == NULL)
        return NULL;
    ctx->ts_offset = AV_NOPTS_VALUE;

    // Input-to-output stream mapping:
    //  internal (output_context_t::streams_map)
    //   - index - index of input stream
    //   - value - index of output stream
    //  parameters (streams_map)
    //   - index - ignored
    //   - value - index of input stream to be used 
    
    ctx->nb_input_streams = ic->nb_streams;
    ctx->streams_map = av_mallocz(ctx->nb_input_streams * sizeof(int*));
    if (!ctx->streams_map)
    {
        av_free(ctx);
        return NULL;
    }

    if (streams_map == NULL || nb_streams_map == 0)        // Mapping is not specified. Use one-to-one mapping
    {
        for(i = 0; i < ctx->nb_input_streams; i++)
            ctx->streams_map[i] = i;
        nb_output_streams = ctx->nb_input_streams;
    }
    else
    {
        for(i = 0; i < ctx->nb_input_streams; i++)
            ctx->streams_map[i] = -1;
        
        for(i = 0; i < nb_streams_map; i++)
            if (streams_map[i] >= 0 && streams_map[i] < ctx->nb_input_streams)
                ctx->streams_map[streams_map[i]] = nb_output_streams++;
    }
    
    // Create output context based on input and streams map
    avformat_alloc_output_context2(&ctx->oc, NULL, NULL, filename);
    if (!ctx->oc) 
    {
        av_log(NULL, AV_LOG_FATAL, "Could not create output context\n");
        
        av_free(ctx->streams_map);
        av_free(ctx);
        return NULL;
    }
        
    for (i = 0; i < nb_output_streams; i++)
    {
        AVStream *in_stream = NULL;
        AVStream *out_stream = NULL;

        // Map output stream to input
        int j;
        for (j = 0; j < ic->nb_streams; j++) 
        {
            if (ctx->streams_map[j] == i)
            {
                in_stream = ic->streams[j];
                break;
            }
        }
        
        if (!in_stream)
            continue;
        
        out_stream = avformat_new_stream(ctx->oc, NULL);
        if (!out_stream)
        {
            av_log(NULL, AV_LOG_FATAL, "Failed allocating output stream\n");
            ret = -1;
            break;
        }

        out_stream->time_base = in_stream->time_base;
        out_stream->sample_aspect_ratio = in_stream->sample_aspect_ratio;
        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
        if (ret < 0) 
        {
            av_log(NULL, AV_LOG_FATAL, "Failed to copy codec parameters from input to output stream\n");
            break;
        }
        out_stream->codecpar->codec_tag = 0;

    }    
    
    if (ret == 0 && !(ctx->oc->oformat->flags & AVFMT_NOFILE)) 
    {
        AVDictionary* opts = NULL;
        av_dict_copy(&opts, (AVDictionary*)options, 0);
        ret = avio_open2(&ctx->oc->pb, filename, AVIO_FLAG_WRITE, NULL, &opts);
        av_dict_free(&opts);
        if (ret < 0) 
            av_log(NULL, AV_LOG_FATAL, "Could not open output file '%s'\n", filename);
    }
    
    if (ret == 0)
    {
        // Write header
        time_t creation_time;
        int64_t creation_ts;
        char cretation_time_tag[sizeof "2014-09-18T09:08:07Z"];
        char time_origin_tag[21];
        static const AVRational AV_TIME_SECONDS_Q = {1,1};
        AVDictionary* opts = NULL;

        
        // Copy all metadata from input
        // TODO: adjust 'comments', because this field is used to store information about tracks
        av_dict_copy(&ctx->oc->metadata, ic->metadata, 0);
        
        // Setup creation time and timeorigin
        ctx->ts_offset = timeorigin;
        creation_ts = (ctx->ts_offset != AV_NOPTS_VALUE) ? ctx->ts_offset : av_gettime();
        
        // Epiphan specific: timeorigin
        snprintf( time_origin_tag, sizeof(time_origin_tag), "%" PRId64, creation_ts );
        av_dict_set(&ctx->oc->metadata, "timeorigin", time_origin_tag, 0);
        
        // Produce creation time from timeorigin as number of seconds since 01.01.1970
        //  Modern muxers take creation timestamp from metadata (ISO 8601)
        creation_time = av_rescale_q(creation_ts, AV_TIME_BASE_Q, AV_TIME_SECONDS_Q);
        ctx->oc->start_time_realtime = creation_time;
        strftime(cretation_time_tag, sizeof (cretation_time_tag), "%FT%TZ", gmtime(&creation_time));
        av_dict_set(&ctx->oc->metadata, "creation_time", cretation_time_tag, 0);

        if( !strcmp(ctx->oc->oformat->name, "mov")  )
            av_dict_set(&opts, "use_editlist", "1", 0);
        
        // Write header
        av_dict_copy(&opts, (AVDictionary*)options, 0);
        ret = avformat_write_header(ctx->oc, &opts);
        av_dict_free(&opts);
    }
    
    
    if (ret < 0)
    {
        avformat_free_context(ctx->oc);
        av_free(ctx->streams_map);
        av_free(ctx);
        return NULL;
    }

    return ctx;
}
// Write next frame to output
//  Packet timebase is 1/1M in real world timescale
static int output_write(output_context_t* ctx, const AVPacket* p)
{
    int res;
    int stream_index;
    AVStream* ost;
    AVPacket pkt;

    assert(ctx != NULL);
    assert(p != NULL);
    
    // Skip unmatched streams
    if (p->stream_index > ctx->nb_input_streams || ctx->streams_map[p->stream_index] < 0)
        return 0;
    
    // TODO: check DTS, PTS, etc
    
    // Make a copy because we are going to modify some packet attributes
    if (av_copy_packet(&pkt, (AVPacket*)p) < 0)
        return -1;

    stream_index = ctx->streams_map[p->stream_index];
    ost = ctx->oc->streams[stream_index];

    pkt.stream_index = stream_index;
    pkt.dts = av_rescale_q(pkt.dts - ctx->ts_offset, AV_TIME_BASE_Q, ost->time_base);
    pkt.pts = av_rescale_q(pkt.pts - ctx->ts_offset, AV_TIME_BASE_Q, ost->time_base);
    pkt.duration = av_rescale_q(pkt.duration, AV_TIME_BASE_Q, ost->time_base);

    
    if (debug_ts) 
    {
        av_log(NULL, AV_LOG_INFO, "output <- #%02d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s pkt_duration:%s pkt_duration_time:%s size:%d\n",
                pkt.stream_index,
                av_get_media_type_string(ost->codecpar->codec_type),
                av_ts2str(pkt.pts), av_ts2timestr(pkt.pts, &ost->time_base),
                av_ts2str(pkt.dts), av_ts2timestr(pkt.dts, &ost->time_base),
                av_ts2str(pkt.duration), av_ts2timestr(pkt.duration, &ost->time_base),
                pkt.size);
    }

    
    // It's all set. Write the packet
    res = av_interleaved_write_frame(ctx->oc, &pkt);
    
    av_packet_unref(&pkt);
    
    return res;
}

// Close output context
static int output_close(output_context_t** ctx)
{
    if (ctx && *ctx)
    {
        output_context_t* c = *ctx;
        if (c->oc)
        {
            av_write_trailer(c->oc);
            if (c->oc->flags & AVFMT_NOFILE)
                avio_closep(&c->oc->pb);
            avformat_free_context(c->oc);
            c->oc = NULL;
        }

        if (c->streams_map)
        {
            av_freep(&c->streams_map);
            c->nb_input_streams = 0;
        }
        
        av_freep(ctx);
    }

    return 0;
}


//////////////////////////////
// Multi-files input

// Allocate and open inputs
static input_context_t* input_open(
                            const char** filenames,         // Names of input files
                            int nb_files)                   // Number of input files
{
    int ret;
    int i;
    int64_t last_timeorigin = 0;    // Timeorigin verification
    input_context_t* ctx;

    if (nb_files < 1)
        return NULL;

    ctx = av_mallocz(sizeof(input_context_t));
    if (!ctx)
        return NULL;
    
    ctx->nb_inputs = nb_files;
    ctx->inputs = av_mallocz(sizeof(AVFormatContext*) * nb_files);
    if (!ctx->inputs)
    {
        av_free(ctx);
        return NULL;
    }

    ctx->start_time = AV_NOPTS_VALUE;
    ctx->end_time = AV_NOPTS_VALUE;
    ctx->input_index = -1;
    for (i = 0; i < nb_files; i++)
    {
        if ((ret = avformat_open_input(&ctx->inputs[i], filenames[i], NULL, NULL)) < 0)
        {
            av_log(NULL, AV_LOG_FATAL, "Could not open input file '%s'\n", filenames[i]);
            break;
        }
        
        // Epiphan's timeorigin attribute is required if there are more then one input files
        if (nb_files > 1)
        {
            int64_t timeorigin = get_timeorigin(ctx->inputs[i]);
            if (timeorigin == 0)
            {
                ret = -1;
                av_log(NULL, AV_LOG_FATAL, "Timeorigin attribute is missing for file '%s'\n", filenames[i]);
                break;
            }
            if (timeorigin < last_timeorigin)
            {
                ret = -1;
                av_log(NULL, AV_LOG_FATAL, "Input files are not sorted by timeorigin attribute. File '%s'\n", filenames[i]);
                break;
            }
            last_timeorigin = timeorigin;
        }

        // Get an extended information from the first file, because their stream will be used to build output context
        if (i == 0 && avformat_find_stream_info(ctx->inputs[i], 0) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to retrieve input stream information for file '%s'\n", filenames[i]);
            break;
        }
        
        // All files must be in same format
        if (i > 0 && compare_format_contexts(ctx->inputs[0], ctx->inputs[i]) == 0)
        {
            ret = -1;
            av_log(NULL, AV_LOG_FATAL, "One or more input files have different formats. File '%s'\n", filenames[i]);
            break;
        }
    }
    
    if (ret == 0)
    {
        // Estimate duration of the sequence
        AVFormatContext* first_input = ctx->inputs[0];
        AVFormatContext* last_input = ctx->inputs[ctx->nb_inputs - 1];
        ctx->first_time_origin = get_timeorigin(first_input);
        ctx->inputs_duration = get_timeorigin(last_input) - ctx->first_time_origin + get_input_duration(last_input);
        
        ctx->nb_streams = ctx->inputs[0]->nb_streams;
        ctx->streams = av_mallocz(sizeof(input_stream_t) * ctx->nb_streams);
        if (ctx->streams)
        {
            for (i = 0; i < ctx->nb_streams; i++)
            {
                input_stream_t* s = &ctx->streams[i];
                s->last_dts = AV_NOPTS_VALUE;
                s->next_dts = AV_NOPTS_VALUE;
                s->dts_offset = AV_NOPTS_VALUE;
            }
        }
        else
            ret = -1;
    }

    if (ret < 0)    // Failure cleanup
    {
        for (i = 0; i < nb_files; i++)
            if (ctx->inputs[i])
                avformat_close_input(&ctx->inputs[i]);
        av_free(ctx->inputs);
        av_free(ctx->streams);
        av_free(ctx);
        return NULL;
    }

    return ctx;
}                                    

static AVFormatContext* input_get_format_context(input_context_t* ctx)
{
    if (ctx && ctx->inputs && ctx->nb_inputs > 0)
        return ctx->inputs[0];

    return NULL;
}

// Read one packet from input files
//  Caller is responsible to free packet data (av_packet_unref) and free packet structure (av_free)
static AVPacket* input_read(input_context_t* ctx)
{
    assert(ctx != NULL);
    
    while (ctx->input_index < ctx->nb_inputs)
    {
        int i;
        if ( ctx->input_index >= 0 )
        {
            AVFormatContext* ic = ctx->inputs[ctx->input_index];
            AVPacket* pkt = av_malloc(sizeof(AVPacket));
            if (!pkt)
                return NULL;
            av_init_packet(pkt);
            
            if (av_read_frame(ic, pkt) >= 0)
            {
                // Convert timeline to realtime world timescale
                int stream_index = pkt->stream_index;
                AVStream* ist = ic->streams[stream_index];
                input_stream_t* s = &ctx->streams[stream_index];   // Additional stream timings

                if (pkt->pts == AV_NOPTS_VALUE)
                    pkt->pts = pkt->dts;            // Although PTS does not make sense for remuxing, some muxers still wanted it.
                
                if (debug_ts) 
                {
                    av_log(NULL, AV_LOG_INFO, "input  -> #%02d type:%s pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s pkt_duraion:%s pkt_duration_time:%s size:%d\n",
                        pkt->stream_index, av_get_media_type_string(ist->codecpar->codec_type),
                        av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &ist->time_base),
                        av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, &ist->time_base),
                        av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, &ist->time_base),
                        pkt->size);
                }
                
                pkt->dts = ctx->time_origin + av_rescale_q(pkt->dts, ist->time_base, AV_TIME_BASE_Q);
                pkt->pts = ctx->time_origin + av_rescale_q(pkt->pts, ist->time_base, AV_TIME_BASE_Q);
                pkt->duration = av_rescale_q(pkt->duration, ist->time_base, AV_TIME_BASE_Q);
                
                
                // Accuracy glue of two adjacent inputs
                //  Since a concatenation requires Epiphan's extension (timeorigin attribute)
                //  we can rely on implementation details of Epiphan's recorder.
                //  To compensate gaps in audio/video empty/silence frames is added only when gap is longer then frame
                //  and one empty/silence frame is the exactly same duration.
                //  So, we can assume that when an overlapped frame (dts between last_dts and next_dst and dts+duration more the next_dts)
                //  is received, it means that it is actually an next frame and we have to adjust its timestamp to next_dts.
                //
                //               File 1:         File 2:
                //            0         1      1       2         3  
                //            012345678901234  2345678901234567890 
                //  stream 1  [1---][2---]     [3---][4---]    
                //  stream 2  [1--][2--][3--]  [4--][5--][6--]
                //
                //  File 1: 00 - 14, File 2: 12 - 26
                //  Let's take stream #2. On glue files, an expected next_dts for packet #4 is 15. But all streams start with the same dts 12
                //   and demuxer returns packet #4 with dts 12. So, packet #3 (10-14) and #4 (12-16) are overlapped. To provide continuous 
                //   stream we need to adjust dts for packet #4 and all further packets (within this file) to +3.
                //  All fake packets, with dts equal or less then last_dts should be discarded. 

                if (s->last_dts != AV_NOPTS_VALUE)
                {
                    if (pkt->dts <= s->last_dts)
                    {
                        // Get packet definitely from the past. It is a padding packet.
                        //  We never expect to skip real video packet, especially with key frame
                        if (pkt->size > 0 && ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                        {
                            if (pkt->flags & AV_PKT_FLAG_KEY)
                                av_log(NULL, AV_LOG_ERROR,   "Packet for video key frame was dropped. stream: %02d pkt_dts:%s last_dts:%s\n", pkt->stream_index, av_ts2str(pkt->dts), av_ts2str(s->last_dts));
                            else
                                av_log(NULL, AV_LOG_WARNING, "Packet for video frame was dropped. stream: %02d pkt_dts:%s last_dts:%s\n", pkt->stream_index, av_ts2str(pkt->dts), av_ts2str(s->last_dts));
                        }
                        
                        av_packet_unref(pkt);
                        av_free(pkt);
                        continue;
                    }

                    if (s->dts_offset == AV_NOPTS_VALUE)
                    {
                        // Beginning of the new input file in sequence
                        assert(pkt->duration > 0);

                        // Initialize dts offset only when the first packet for next input is received
                        // One more null packets might be added at the begging of file.
                        // Assume that all frames are the same duration (real and null)
                        if (pkt->dts > s->next_dts)
                        {
                            int64_t skip = (pkt->dts - s->next_dts) / pkt->duration;    // Number of null packets
                            s->last_dts += skip * pkt->duration;
                            s->next_dts += skip * pkt->duration;
                        }
                        
                        if (pkt->dts > s->last_dts && pkt->dts <= s->next_dts && (pkt->dts + pkt->duration) > s->next_dts)
                        {
                            s->dts_offset = s->next_dts - pkt->dts;
                        }
                        else
                        {
                            s->dts_offset = 0;     // TODO: cannot determine offset ?
                        }

                        if (debug_ts)
                        {
                            av_log(NULL, AV_LOG_INFO, "input  :: #%02d type:%s input: %03d position_time:%s dts_offset:%s dts_offset_time:%s dts_overlap:%s\n",
                                pkt->stream_index, av_get_media_type_string(ist->codecpar->codec_type),
                                ctx->input_index, av_ts2timestr(pkt->dts - ctx->first_time_origin, &AV_TIME_BASE_Q),
                                av_ts2str(s->dts_offset), av_ts2timestr(s->dts_offset, &AV_TIME_BASE_Q),
                                av_ts2str(pkt->duration - s->dts_offset));
                        }
                    }

                    // Sub-frame dts adjustment
                    pkt->dts += s->dts_offset;
                    pkt->pts += s->dts_offset;
                }
                else
                {
                    s->dts_offset = 0;              // For the first file an offset is unknown
                }

                // After all adjustments check the time range (start_time .. end_time) and update read state
                {
                    int stop_reading = 0;
                    int skip_frame = 1;
                    switch(s->read_state)
                    {
                    case INPUT_SKIP:
                        if (ctx->start_time != AV_NOPTS_VALUE  && pkt->dts < ctx->start_time)
                            break;
                        s->read_state = INPUT_WAIT_KEYFRAME;        // No break here
                    
                    case INPUT_WAIT_KEYFRAME:
                        if ((pkt->flags & AV_PKT_FLAG_KEY) == 0)
                            break;
                        s->read_state = INPUT_READ;                 // No break here
                    
                    case INPUT_READ:
                        if (ctx->end_time == AV_NOPTS_VALUE || (pkt->dts + pkt->duration) < ctx->end_time)
                        {
                            skip_frame = 0;                         // Take this packet
                            break;
                        }
                        s->read_state = INPUT_DONE;                 // No break here        
                    
                    case INPUT_DONE:
                        {
                            int done = 0;                           // Check that all streams are done and stop reading at all
                            for(i = 0; i < ctx->nb_streams; i++)
                                if (ctx->streams[i].read_state == INPUT_DONE)
                                    done++;
                            stop_reading = (done == ctx->nb_streams) ? 1 : 0;
                        }
                        break;
                    }
                    
                    if (skip_frame)
                    {
                        // Free the packet
                        av_packet_unref(pkt);
                        av_free(pkt);
                        if (stop_reading)
                            return NULL;                            // end_time for every stream is reached
                        continue;                       
                    }
                }
                    
                
                s->last_dts = pkt->dts;
                s->next_dts = pkt->dts + pkt->duration; // Expected timestamp for the next frame
                
                return pkt;
            }
            
            av_free(pkt);
        }
        
        // Go to the next input file
        ctx->input_index++;
        if (ctx->input_index >= ctx->nb_inputs)
            break;
        
        // Get extended streams information
        //  For the first file in the sequence avformat_find_stream_info already called in input_open
        if (ctx->input_index > 0 && avformat_find_stream_info(ctx->inputs[ctx->input_index], 0) < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to retrieve input stream information for file '%s'\n", ctx->inputs[ctx->input_index]->filename);
            break;
        }
        
        ctx->time_origin = get_timeorigin(ctx->inputs[ctx->input_index]);
        
        // TODO: Fast seek (skip n-1 files before start_time)
        
        
        // We need to re-calculate dts offset for the next file
        for(i = 0; i < ctx->nb_streams; i++)
            ctx->streams[i].dts_offset = AV_NOPTS_VALUE;
    }
    
    return NULL;
}

// Get timeorigin of the sequence input files and taking into account skip time (start_time)
static int64_t input_get_timeorigin(input_context_t* ctx)
{
    assert(ctx != NULL);
    
    if (ctx->nb_inputs == 0)
        return 0;
    
    if (ctx->start_time != AV_NOPTS_VALUE)
        return ctx->start_time;
    
    return get_timeorigin(ctx->inputs[0]);
}

// Set time range for the whole input sequence
//  if start_time is less then one year - assume that it is a relative time, otherwise - absolute
//  Both start_time and duration can be AV_NOPTS_VALUE
static void input_set_timerange(input_context_t* ctx, int64_t start_time, int64_t duration)
{
    int64_t inputs_start_time;      // Actual start time
    int64_t inputs_end_time;        // Actual end time
    
    assert(ctx != NULL);
    assert(ctx->nb_inputs >= 0);
    
    ctx->start_time = ctx->end_time = AV_NOPTS_VALUE;
    inputs_start_time = get_timeorigin(ctx->inputs[0]);
    inputs_end_time   = get_timeorigin(ctx->inputs[ctx->nb_inputs-1]) + get_input_duration(ctx->inputs[ctx->nb_inputs-1]);
    
    av_log(NULL, AV_LOG_VERBOSE, "Input start time: %s, end time: %s, duration: %s second(s)\n", av_ts2str(inputs_start_time),  av_ts2str(inputs_end_time), av_ts2timestr(inputs_end_time-inputs_start_time, &AV_TIME_BASE_Q));
    
    if (start_time != AV_NOPTS_VALUE && start_time >= 0)
    {
        // Accept negative duration (only if start time is specified)
        if (duration != AV_NOPTS_VALUE && duration < 0)
        {
            duration = -duration;
            if (duration < start_time)
            {
                start_time -= duration;
            }
            else
            {
                duration = start_time;
                start_time = 0;
            }
            
            av_log(NULL, AV_LOG_INFO, "Negative duration was specified. Start time and duration have been adjusted. Start time: %s, duration: %s second(s)\n",
                av_ts2str(start_time), av_ts2timestr(duration, &AV_TIME_BASE_Q));
        }
        
        if (start_time >= 0 && start_time < 31536000000000LL)
        {
            ctx->start_time = get_timeorigin(ctx->inputs[0]) + start_time;      // First timeorigin + relative offset
        }
        else
        {
            if (start_time < inputs_start_time)     // Absolute timestamp from the past. Adjust start time and reduce duration
            {
                if (duration != AV_NOPTS_VALUE)   
                {
                    duration -= (inputs_start_time - start_time);
                    if (duration < 0)
                        duration = 0;               // start_time + duration is still less then sequence start time. Nothing to extract
                    
                    av_log(NULL, AV_LOG_INFO, "Start time (%s) was earlier then time of the first file (%s). Start time and duration have been adjusted. New duration is %s second(s)\n",
                        av_ts2str(start_time), av_ts2str(inputs_start_time), av_ts2timestr(duration, &AV_TIME_BASE_Q));
                }
                else
                {
                    av_log(NULL, AV_LOG_INFO, "Start time (%s) was earlier then time of the first file (%s). Start time has been adjusted\n",
                        av_ts2str(start_time), av_ts2str(inputs_start_time));
                }
                ctx->start_time = inputs_start_time;
            }
            else
            {
                ctx->start_time = start_time;
                if (start_time > inputs_end_time)
                    av_log(NULL, AV_LOG_WARNING, "Start time (%s) is after the end of the input files (%s). Nothing will be extracted\n", av_ts2str(start_time), av_ts2str(inputs_end_time));
            }
        } 
    }
    if (duration != AV_NOPTS_VALUE && duration >= 0)
    {
        if (ctx->start_time != AV_NOPTS_VALUE)
            ctx->end_time = ctx->start_time + duration;
        else
            ctx->end_time = inputs_start_time + duration;
    }
    
    // adjust first_time_origin and inputs_duration based on new start_time and end_time values
    ctx->first_time_origin = (ctx->start_time != AV_NOPTS_VALUE) 
                                ? ctx->start_time 
                                : inputs_start_time;
    ctx->inputs_duration = (ctx->end_time == AV_NOPTS_VALUE ) 
                                ? (inputs_end_time - ctx->first_time_origin)        
                                : (ctx->end_time < inputs_end_time) 
                                    ? (ctx->end_time - ctx->first_time_origin) 
                                    : (inputs_end_time - ctx->first_time_origin);
    if (ctx->inputs_duration < 0)
        ctx->inputs_duration = 0;
    
    av_log(NULL, AV_LOG_VERBOSE, "Extract %s second(s) starting from %s\n", av_ts2timestr(ctx->inputs_duration, &AV_TIME_BASE_Q), av_ts2str(ctx->first_time_origin));
}

// Seek to the first video key frame within specified streams
static int64_t input_skip_to_keyframe(input_context_t* ctx,     // Input context
                                      const int* streams_map,   // Input to output stream mapping. Can be null for all-to-all
                                      int  nb_streams_map)      // Size of stream map.
{
    AVFormatContext* ic;
    AVPacket pkt;
    int64_t start_time = AV_NOPTS_VALUE;
    int start_stream_index = -1;
    
    assert(ctx != NULL);
    assert(ctx->nb_inputs >= 0);

    // Take into account only the first input file in sequence
    ic = ctx->inputs[0];
    av_init_packet(&pkt);
    while(start_time == AV_NOPTS_VALUE && av_read_frame(ic, &pkt) >= 0)
    {
        int skip = 0;
        int stream_index = pkt.stream_index;
        AVStream* ist = ic->streams[stream_index];
        // Check stream index against the provided stream map
        if (streams_map != NULL && nb_streams_map > 0)
        {
            int i;
            skip = 1;
            for(i = 0; i < nb_streams_map; i++)
            {
                if (streams_map[i] == stream_index)
                {
                    skip = 0;
                    break;
                }
            }
        }
        
        // Check for no-empty video key frame
        if (!skip)
            skip = !(ist->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                     pkt.flags & AV_PKT_FLAG_KEY &&
                     pkt.size > 0);
                     
        // Found first video key frame
        if (!skip)
        {
            start_time = pkt.dts;
            start_stream_index = stream_index;
            av_log(NULL, AV_LOG_VERBOSE, "Found video key frame at stream %d. dts:%s (%s)\n", start_stream_index, av_ts2str(start_time), av_ts2timestr(start_time, &ist->time_base));
        }
        
        av_packet_unref(&pkt);
    }
    
    // Rewind file to the beginning or to the found video keyframe
    av_seek_frame(ic, start_stream_index, (start_time != AV_NOPTS_VALUE) ? start_time : 0,  AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
    
    if (start_stream_index >= 0 && start_time != AV_NOPTS_VALUE)
        start_time = av_rescale_q(start_time, ic->streams[start_stream_index]->time_base, AV_TIME_BASE_Q);
    
    return start_time;
}

static int input_close(input_context_t** ctx)
{
    if (ctx && *ctx)
    {
        input_context_t* c = *ctx;
        if(c->inputs)
        {
            int i;
            for( i = 0; i < c->nb_inputs; i++)
            {
                if (c->inputs[i])
                    avformat_close_input(&c->inputs[i]);
            }
            av_freep(&c->inputs);
        }
        av_freep(&c->streams);

        av_freep(ctx);
    }
    
    return 0;
}


////////////////////////////////////////////////////////
// Concatenate files and extract tracks

static copy_context_t* copy_allocate_context(void)
{
    copy_context_t* ctx = av_mallocz(sizeof(copy_context_t));
    if (!ctx)
        return NULL;

    ctx->processing = 1;
    return ctx;
}

static void copy_free_context(copy_context_t** ctx)
{
    if (ctx && *ctx)
    {
        av_freep(ctx);
    }
}

// Set a new input for copy and replace the previous one
//  Previous input will be destroyed
static input_context_t* copy_new_input(
                            copy_context_t* ctx,
                            const char** filenames,       // Names of input files
                            int nb_files)                 // Number of input files
{
    assert(ctx != NULL);

    input_close(&ctx->input);
    ctx->input = input_open(filenames, nb_files);

    return ctx->input;
};

// Create a new output and add it to copy context
static output_context_t* copy_add_new_output(
                            copy_context_t* ctx,
                            const char* filename,           // Output filename
                            const AVDictionary* options,    // An optional output file options. Can be null
                            const int* streams_map,         // Input to output stream mapping. Can be null for all-to-all
                            int  nb_streams_map)            // Number of entries in stream_map
{
    output_context_t* oc;
    
    assert(ctx != NULL);
    if (ctx->input == NULL)
        return NULL;
    
    oc = output_open(input_get_format_context(ctx->input), 
                     filename, 
                     options, 
                     input_get_timeorigin(ctx->input),      // Expected start time of the first read packet
                     streams_map, 
                     nb_streams_map);
    if (oc == NULL)
        return NULL;

    ctx->outputs = av_realloc_array(ctx->outputs, ctx->nb_outputs + 1, sizeof(output_context_t*));
    ctx->outputs[ctx->nb_outputs++] = oc;

    return oc;
}

static int copy_close(copy_context_t* ctx)
{
    int i;

    assert(ctx != NULL);
    
    input_close(&ctx->input);
    for (i = 0; i < ctx->nb_outputs; i++)
        output_close(&ctx->outputs[i]);
    av_freep(&ctx->outputs);
    ctx->nb_outputs = 0;
    
    return 0;
}


// Copy frames from input to output
static int copy(copy_context_t* ctx)
{
    int ret = 0;
    copy_progress_t copy_progress = { 0 };


    assert(ctx != NULL);
    assert(ctx->input != NULL);
    
    copy_progress.total_ms = ctx->input->inputs_duration / 1000;

    while(ctx->processing)
    {
        // Timescale: 1/1M in real world time
        int i;
        int64_t read_duration;
        AVPacket* pkt;
        
        ret = 0;
        pkt = input_read(ctx->input);
        if (pkt == NULL)
            break;          // All packets have been read

        // Grab some packets metrics for future use
        read_duration = pkt->dts - ctx->input->first_time_origin;    // Relative time from the begging of the first input

        // Send packet to one or more outputs
        for (i = 0; i < ctx->nb_outputs && ctx->processing; i++)
            if ((ret = output_write(ctx->outputs[i], pkt)) < 0)
                break;

        av_packet_unref(pkt);
        av_free(pkt);
        
        if (ret < 0)
            break;
        
        if (ctx->progress)
        {
            read_duration /= 1000;
            if (read_duration > copy_progress.read_ms)
                copy_progress.read_ms = read_duration;
            if (copy_progress.read_ms > copy_progress.total_ms)
                copy_progress.read_ms = copy_progress.total_ms;
            
            copy_progress.read_frames++;
            
            ctx->progress(&copy_progress);
        }
    }
    
    return ret;
}

///////////////////////////
// Dump track names
static void dump_track_names(AVFormatContext* ic)
{
    int i;
    char** names;
    AVDictionaryEntry* tag;

    if (ic->nb_streams == 0)
        return;
    
    names = av_mallocz(ic->nb_streams*sizeof(char*));
 
    // Extract track names from 'comment' metadata
    tag = av_dict_get(ic->metadata, "comment", NULL, 0);
    if (tag != NULL)
    {
        char* pc = tag->value;
        int n = 0;
        while (*pc && n < ic->nb_streams)
        {
            char* pd = strchr(pc, '|');
            if (pd)
                *pd = '\0';

            names[n] = pc;

            if (pd == NULL)
                break;

            pc = pd + 1;
            n++;
        }
    }

    for (i = 0; i<ic->nb_streams; i++)
    {
        char default_name[32];
        char* pn = names[i];
        if (pn == NULL)
        {
            snprintf(default_name, sizeof(default_name), "Track %d", i + 1);
            pn = default_name;
        }
        printf("%d: %s\n", i + 1, pn);
    }

    av_free(names);
}


///////////////////////////

// Application settings
static int show_track_names = 0;
static int hide_progress = 0;               // Hide progress
static char* progress_filename = NULL;      // Progress filename (optional)

static int tracks_map[256];                 // Up to 256 tracks. Even that AVI supports no more then 100.
static int tracks_map_size = 0;

static const char* input_files[2048];       // Up to 2K input files
static int input_files_count = 0;
static const char* output_file = NULL;
static int64_t start_time = AV_NOPTS_VALUE; // Relative or absolute start time (mks)
static int64_t duration = AV_NOPTS_VALUE;   // Duration (mks)
static int skip_to_vkeyframe = 0;           // Start from the first video keyframe

// Files: <input 1> [input N] <output>
static void opt_files(void *optctx, const char *filename)
{
    if (input_files_count == 0)
    {
        input_files[input_files_count++] = filename;        // First file - input
    }
    else if (output_file == NULL)
    {
        output_file = filename;                             // Second file - output
    }        
    else
    {
        if (input_files_count < sizeof(input_files)/sizeof(input_files[0]))
        {
            input_files[input_files_count++] = output_file;
            output_file = filename;
        }
    }
}

// Comma-separated tracks map
static int opt_tracks_map(void *optctx, const char *opt, const char *arg)
{
    char* mapstr = av_strdup(arg);
    char* pm = mapstr;
    while( *pm && tracks_map_size < sizeof(tracks_map)/sizeof(tracks_map[0]))
    {
        char* pd = strchr(pm, ',');
        if( pd )
            *pd = '\0';
                
        tracks_map[tracks_map_size++] = atoi(pm);
             
        if ( !pd )
            break;
        pm = pd + 1;
    }
    av_free(mapstr);
    
    return 0;
}

static int opt_seek(void *optctx, const char *opt, const char *arg)
{
    // Start offset might 
    //   be relative (starts with +) 
    //   or absolute ([{YYYY-MM-DD|YYYYMMDD}[T|t| ]]{{HH:MM:SS[.m...]]]}|{HHMMSS[.m...]]]}}[Z]) in UTC. If year is missing - take the current year
    if (arg[0] == '+')
        start_time = parse_time_or_die(opt, arg + 1, 1);        // Relative
    else
        start_time = parse_time_or_die(opt, arg, 0);            // Absolute
    return 0;
}

static int opt_duration(void *optctx, const char *opt, const char *arg)
{
    duration = parse_time_or_die(opt, arg, 1);
    return 0;
}


static void show_usage(void)
{
    av_log(NULL, AV_LOG_INFO, "usage: %s [options] input_file1 [input_fileN] [output_file] \n", program_name);
    av_log(NULL, AV_LOG_INFO, "  dump tracks:             %s -show_tracks input_file1 \n", program_name);
    av_log(NULL, AV_LOG_INFO, "  extract and concatenate: %s [-tracks <map>] input_file1 [input_fileN] output_file\n", program_name);
    av_log(NULL, AV_LOG_INFO, "\n");
}


static int show_app_help(void *optctx, const char *opt, const char *arg)
{
    show_help_default("","");
    return 0;
}

static const OptionDef options[] = {
    { "h"               , OPT_EXIT, {.func_arg = show_app_help}, "show help"},
    { "?"               , OPT_EXIT, {.func_arg = show_app_help}, "show help"},
    { "help"            , OPT_EXIT, {.func_arg = show_app_help}, "show help"},
    { "-help"           , OPT_EXIT, {.func_arg = show_app_help}, "show help"},
    { "show_tracks"     , OPT_BOOL, { &show_track_names }, "show track names" },
    { "tracks"          , HAS_ARG,  {.func_arg = opt_tracks_map}, "tracks to be extracted", "map"},
    { "ss"              , HAS_ARG,  {.func_arg = opt_seek }, "seek to a given position (relative or absolute time in UTC)", "pos" },
    { "t"               , HAS_ARG,  {.func_arg = opt_duration }, "extract  \"duration\" seconds of audio/video", "duration" },
    { "no_progress"     , OPT_BOOL, { &hide_progress }, "hide progress indicator" },
    { "progress_file"   , HAS_ARG | OPT_STRING,  { &progress_filename }, "write progress into file", "file" },
    { "loglevel"        , HAS_ARG,  {.func_arg = opt_loglevel}, "set logging level", "loglevel" },
    { "v"               , HAS_ARG,  {.func_arg = opt_loglevel}, "set logging level", "loglevel" },
    { "debugts"         , OPT_BOOL | OPT_EXPERT, { &debug_ts }, "debug timestamps" },
    { "sk"              , OPT_BOOL | OPT_EXPERT, { &skip_to_vkeyframe}, "Seek to the first video key frame" },
    { NULL, }
};

void show_help_default(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:", 0, OPT_EXPERT, 0);

}

//////////////////
static void copy_progress(const copy_progress_t* p)
{
    static int last_percentage = 0;
    
    int total_sec = p->total_ms / 1000; 
    int read_sec  = p->read_ms / 1000;
    
    if (total_sec > 0 )
    {
        int percentage = (int)((read_sec * 100.0f + 0.5f) / total_sec);
        if (percentage != last_percentage)
        {
            printf( "\r%d of %d s, %d%%  ", read_sec, total_sec, percentage );
            
            if (progress_filename && *progress_filename)
            {
                FILE* pfd = fopen( progress_filename, last_percentage ? "r+" : "w" );
                if( pfd )
                {
                    fseek(pfd, 0, SEEK_SET);
                    fprintf( pfd, "%8d:%3d", getpid(), percentage );
                    fclose(pfd);
                }
            }
            last_percentage = percentage;
        }
    }
}


int main(int argc, char * argv[])
{
    int ret = -1;
    
    av_log_set_level(AV_LOG_ERROR);
        parse_loglevel(argc, argv, options);
    av_register_all();
    
    init_opts();
    parse_options(NULL, argc, argv, options, opt_files);
    
    if (show_track_names)   // Dump track names mode
    {
        input_context_t* input;
        if (input_files_count == 0)
        {
           av_log(NULL, AV_LOG_FATAL, "An input file must be specified\n");
           return 1;
        }

        input = input_open(&input_files[0], 1);
        if (!input)
            return 1;
        
        dump_track_names(input_get_format_context(input));
        input_close(&input);
        return 0;
        
    }
    
    // Concatenate and extraction mode
    {
        copy_context_t* copy_ctx = NULL;
        ret = 0;
        
        // Extract and concatenate mode
        if (input_files_count == 0)
        {
            av_log(NULL, AV_LOG_FATAL, "At least one input file and one output file must be specified\n");
            return 1;
        }
        if (output_file == NULL)
        {
            av_log(NULL, AV_LOG_FATAL, "An output file must be specified\n");
            return 1;
        }
    
        copy_ctx = copy_allocate_context();
        if (!copy_ctx)
        {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate copy context\n");
            return 1;
        }
        
        if (!hide_progress)
        {
            copy_ctx->progress = &copy_progress;
            printf("Checking input files...\n");
        }
            
        if (copy_new_input(copy_ctx, &input_files[0], input_files_count))
        {
            // Add only one output
            output_context_t* output;
            const char* output_filename = (strcmp(output_file, "-") == 0) ? "pipe:" : output_file;

            if (skip_to_vkeyframe)
                start_time = input_skip_to_keyframe(copy_ctx->input, &tracks_map[0], tracks_map_size);   // Seek to the first video keyframe
            
            input_set_timerange(copy_ctx->input, start_time, duration);                       // Setup an optional extraction timerange
            
            output = copy_add_new_output(copy_ctx, output_filename, NULL, &tracks_map[0], tracks_map_size);
            if (output)
            {
                ret = copy(copy_ctx);     // Do concatenate file / extract tracks
                if (!hide_progress)
                    printf("\n");
            }
        }
        
        // Cleanup
        if (progress_filename && *progress_filename)
            unlink(progress_filename);
        
        copy_close(copy_ctx);               // Close input and output contexts here
        copy_free_context(&copy_ctx);
    }

    return (ret < 0) ? 1 : 0;
}


