#include "libavformat/avformat.h"
#include "libavutil/intreadwrite.h" /* for AV_RL64 */
#include "internal.h"

#include "rawpkt.h"

static inline void read_tag_header(AVIOContext *pb, int64_t *tag, int *param, int *len)
{
    *tag = avio_rl64(pb);
    *param = avio_rb32(pb);        // reserved
    *len = avio_rb32(pb);    
}

static char * raw_read_string(AVIOContext *pb)
{
    int size;
    char *str;
    size = avio_rb32(pb);
    str = av_malloc(size+1);
    str[size] = 0;
    avio_read(pb,(unsigned char *)str,size);
    return str;
 }

static int raw_read_metadata(AVIOContext *pb, int len, int count, AVDictionary **metadata)
{
    int i;

    int64_t end = avio_tell(pb)+len;
    char *key, *val;
    for(i = 0; i < count; i++) {
        /* read key */
        key = raw_read_string(pb);
        /* read value */
        val = raw_read_string(pb);

        av_dict_set(metadata,key,val,AV_DICT_DONT_STRDUP_KEY|AV_DICT_DONT_STRDUP_VAL);
    }
    RAWTAG_SKIP(pb,end);

    return 0;
}

static int raw_read_codec(AVIOContext *pb, int len, AVCodecParameters *codec, int rev)
{
    int64_t end = avio_tell(pb)+len;
    // Common codec data
    codec->codec_id = avio_rb32(pb);
    codec->codec_type = avio_rb32(pb);  
    if(rev > 0) {
        codec->bit_rate = avio_rb64(pb);
   //     codec->bit_rate = 0; uncomment to test Azure
    }
    codec->format = avio_rb32(pb);
    codec->codec_tag = avio_rb32(pb);
    codec->bits_per_coded_sample = avio_rb32(pb);
    codec->bits_per_raw_sample = avio_rb32(pb);
    codec->profile = avio_rb32(pb);
    codec->level = avio_rb32(pb);


    if(codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        codec->sample_rate = avio_rb32(pb);
        codec->channels = avio_rb32(pb);
        codec->frame_size = avio_rb32(pb);
        codec->channel_layout = avio_rb32(pb);
        codec->block_align = avio_rb32(pb);
        codec->initial_padding = avio_rb32(pb);
        codec->trailing_padding = avio_rb32(pb);
        codec->seek_preroll = avio_rb32(pb);
    }

    if(codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        codec->width = avio_rb32(pb);
        codec->height = avio_rb32(pb);
        codec->sample_aspect_ratio.num = avio_rb32(pb);
        codec->sample_aspect_ratio.den = avio_rb32(pb);
        codec->field_order = avio_rb32(pb);
        codec->color_range = avio_rb32(pb);
        codec->color_primaries = avio_rb32(pb);
        codec->color_trc = avio_rb32(pb);
        codec->color_space = avio_rb32(pb);
        codec->chroma_location = avio_rb32(pb);
        codec->video_delay = avio_rb32(pb);
    }

    // Read codec extra data
    codec->extradata_size = avio_rb32(pb);
    if( codec->extradata_size == 0 ) {
        codec->extradata = NULL;
    } else {
        codec->extradata = av_mallocz(codec->extradata_size+8); /* codec probe frequently overshoot */
        if (!codec->extradata) {
            codec->extradata_size = 0;
            return -1;
        }
        avio_read(pb, codec->extradata, codec->extradata_size);
    }
    RAWTAG_SKIP(pb,end);
    return 0;
}

static int raw_read_streaminfo(AVIOContext *pb, int len, AVFormatContext *s)
{
    int64_t end = avio_tell(pb)+len;
    s->start_time_realtime = avio_rb64(pb);
    RAWTAG_SKIP(pb,end);
    return 0;
}

static int raw_read_trackinfo(AVIOContext *pb, int len, AVStream *st)
{
    int64_t tag;
    int size, i, param;
    int64_t end = avio_tell(pb)+len;

    st->avg_frame_rate.num = avio_rl32(pb);
    st->avg_frame_rate.den = avio_rl32(pb);

    while(avio_tell(pb) < end) {
        read_tag_header(pb,&tag,&param,&size);
        switch(tag) {
        case RAWHEADER_METADATA:
            raw_read_metadata(pb,size,param,&st->metadata);
            break;
        case RAWHEADER_CODECCTX:
            raw_read_codec(pb,size,st->codecpar, param & 0xff);
            break;
        default:
            av_log(st, AV_LOG_WARNING, "Unknown tag %"PRIx64" for track, size %u bytes\n",tag,size);
            for(i = 0; i < size;i++) avio_r8(pb);
        }
    }
    return 0;
}

static int rawpacket_read_header_internal(AVIOContext *pb, AVFormatContext *s)
{
    AVStream *st;
    int i,size,param;
    int64_t end;
    // Read raw  header signature
    int64_t  tag;
    read_tag_header(pb,&tag,&param,&size);

    if (tag != RAWHEADER_MAGIC)
        return -1;
    if (param != RAWPACKET_VERSION) {
        av_log(s, AV_LOG_ERROR, "Unsupported rawpacket format version (%d)\n", param);
        return -1;
    }
    
    end = avio_tell(pb)+size;
    
    while(avio_tell(pb) < end) {
        read_tag_header(pb,&tag,&param,&size);
        switch(tag) {
        case RAWHEADER_METADATA:
            raw_read_metadata(pb,size,param,&s->metadata);
            break;
        case RAWHEADER_STREAM:
            raw_read_streaminfo(pb,size,s);
            break;
        case RAWHEADER_TRACK:
            st = avformat_new_stream(s,NULL);
            avpriv_set_pts_info(st,64,1,1000000);
            raw_read_trackinfo(pb,size,st);
            break;
        default:
            av_log(s, AV_LOG_WARNING, "Unknown tag %"PRIx64" for stream, size %u bytes\n",tag,size);
            for(i = 0; i < size;i++) avio_r8(pb);
        }
    }
    return 0;

}

static int rawpacket_read_header(AVFormatContext *s)
{
    return rawpacket_read_header_internal(s->pb, s);
}

// Public API: read header
int ff_rawpacket_read_header(AVIOContext *pb, AVFormatContext** s)
{
    int res;
    AVFormatContext* ctx = avformat_alloc_context();
    if (!ctx)
        return AVERROR(ENOMEM);

    res = rawpacket_read_header_internal(pb, ctx);
    if (res < 0)
    {
        avformat_free_context(ctx);
        return res;
    }

    *s = ctx;
    return 0;
}

// Public API: read packet header
int ff_rawpacket_read_packet_header(AVIOContext *pb, AVPacket* pkt, int alloc)
{
    int param, pkt_size, hdr_size;
    int64_t tag, end;
    read_tag_header(pb,&tag,&param,&hdr_size);

    if (tag != RAWPACKET_MAGIC)
        return -1;

    end = avio_tell(pb)+hdr_size;

    pkt_size = avio_rb32(pb);

    if(alloc) {
        int err = av_new_packet(pkt, pkt_size);
        if(err < 0)
            return err;
    }
    else {
        pkt->size = pkt_size;
    }

    pkt->pts = avio_rb64(pb);
    pkt->dts = avio_rb64(pb);
    
    pkt->stream_index = avio_rb32(pb);
    pkt->flags = avio_rb32(pb);
    pkt->duration = avio_rb64(pb);
    pkt->pos = avio_rb64(pb);
    RAWTAG_SKIP(pb,end);
    return 0;
}

static int rawpacket_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    int size = ff_rawpacket_read_packet_header(pb,pkt,1);

    if (avio_feof(pb))
        return AVERROR_EOF;
    
    if(size < 0)
        return size;

    if ((unsigned)pkt->stream_index >= s->nb_streams) {
        av_log(s, AV_LOG_ERROR, "invalid stream index %d\n", pkt->stream_index);
        av_packet_unref(pkt);
        return -1;
    }
    /* and read data */
    pkt->pos = avio_tell(s->pb);
    avio_read(pb,pkt->data,pkt->size);
    return 0;
}

static int rawpacket_probe(AVProbeData *p)
{
    if(p->buf_size < 8)
        return AVPROBE_SCORE_RETRY;
    return AV_RL64(p->buf) ==  RAWHEADER_MAGIC?AVPROBE_SCORE_MAX + 1:0;
}

AVInputFormat ff_rawpacket_demuxer = {
    .name              = "rawpacket",
    .long_name         = "raw avpacket format",
    .extensions        = "avpkt",
    .priv_data_size    = 0,
    .read_probe     = rawpacket_probe,
    .read_header    = rawpacket_read_header,
    .read_packet    = rawpacket_read_packet,
    .read_close     = NULL,
    .read_seek      = NULL,
};

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
