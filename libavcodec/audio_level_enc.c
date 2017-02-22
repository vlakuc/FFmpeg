#include "libavutil/channel_layout.h"
#include "libavutil/audio_level.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "bytestream.h"

#define PKT_HEADER_SIZE 2

typedef struct AudioLevelEncContext {
    AVAudioLevel audio_level;
    uint8_t layout_code;
    uint8_t pkt_size;
    uint64_t duration;
} AudioLevelEncContext;

static av_cold int audio_level_encode_close(AVCodecContext *avctx)
{
    return 0;
}

static av_cold int audio_level_encode_init(AVCodecContext *avctx)
{
    AudioLevelEncContext *s = avctx->priv_data;
    int ret;
    /* number of channels */
    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "encoding %d channel(s) is not allowed\n", avctx->channels);
        ret = AVERROR(EINVAL);
        goto error;
    }
    /* layout */
    if (avctx->channel_layout == AV_CH_LAYOUT_MONO) {
        s->layout_code = 0x01;
        s->pkt_size = PKT_HEADER_SIZE + 2*sizeof(float);
    }
    else if (avctx->channel_layout == AV_CH_LAYOUT_STEREO) {
        s->layout_code = 0x02;
        s->pkt_size = PKT_HEADER_SIZE + 4*sizeof(float);
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "Only mono and stereo layout for audio_level encodder is supported\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    avctx->frame_size = (int)avctx->sample_rate*s->duration/1e6;

    /* Set decoder specific info */
    avctx->extradata_size = 0;
    return 0;
error:
    audio_level_encode_close(avctx);
    return ret;
}

static int audio_level_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    AudioLevelEncContext *s = avctx->priv_data;
    AVAudioLevel audio_level;
    int ret, i;
    uint8_t *packet_data_ptr;

    if (!frame)
        return 0;

    if ((ret = av_frame_audio_level_calc(frame, &audio_level)) < 0)
        return ret;

    if (ret = ff_alloc_packet2(avctx, avpkt, s->pkt_size, 0) < 0)
        return ret;
    //Bitstream format MD-5461
    packet_data_ptr = avpkt->data;
    *packet_data_ptr++ = s->layout_code;
    *packet_data_ptr++ = 0x01;              // Number of sub-bands (reserved for the future use)
    for (i = 0; i < avctx->channels; i++) {
        bytestream_put_be32(&packet_data_ptr, (int32_t)(audio_level.rms[i]*1000));
        bytestream_put_be32(&packet_data_ptr, (int32_t)(audio_level.max[i]*1000));
    }
    avpkt->size = s->pkt_size;
    avpkt->pts = frame->pts;
    *got_packet_ptr = 1;
    return 0;
}

static const uint64_t audio_level_channel_layouts[] = {
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    0
};

#define FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM
static const AVOption audio_level_options[] = {
    {"duration", "audio level period duration in microseconds", offsetof(AudioLevelEncContext, duration), AV_OPT_TYPE_INT64, {.i64 = 100000}, 0, INT_MAX, FLAGS},
    {NULL}
};

static const AVClass audio_level_enc_class = {
    "AUDIO_LEVEL encoder",
    av_default_item_name,
    audio_level_options,
    LIBAVUTIL_VERSION_INT,
};

AVCodec ff_audio_level_encoder = {
    .name           = "audio_level_enc",
    .long_name      = NULL_IF_CONFIG_SMALL("audio level rms serialize encoder"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_AUDIO_LEVEL,
    .priv_data_size = sizeof(AudioLevelEncContext),
    .init           = audio_level_encode_init,
    .encode2        = audio_level_encode_frame,
    .close          = audio_level_encode_close,
    .capabilities   = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .channel_layouts = audio_level_channel_layouts,
    .priv_class     = &audio_level_enc_class,
};
