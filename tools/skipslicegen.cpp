// Skip Frame Data generator
//  Used to generate skip slice data for contant video frame rate filter (libavcodec/h264_constrate_filter)
//  for the given resolution.
//  Encoding settings are based on real pipeline configuration.
//
//  How it works:
//   encode one frame, decode it and encode again. It should have all skip macroblocks.
//

//  Build:
//    g++ --std=c++14 -lavcodec -lavutil skipslicegen.cpp -o skipslicegen

//  Usage: skipslicegen <resolution>
//   and pasted output into libavcodec/h264_constrate_filter_data.c


///////////////////////////////////////////
// ffmpeg includes
#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/parseutils.h"
}

#include <stdexcept>
#include <memory>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

// Resource handling helpers
template<typename T, typename Deleter>
auto make_resource_ptr(T *p, Deleter deleter) {
    return std::move(std::unique_ptr<T, Deleter>(p, deleter));
}

template<typename T>
auto make_resource_ptr(T *p, void (*deleter)(T**) ) {
    return make_resource_ptr(p, [=](T* ptr) { deleter(&ptr); });
}

static AVDictionary* create_dictionary(std::initializer_list<std::pair<const char*, const char*>> l) {
    AVDictionary* dict = nullptr;
    for(auto& v : l)
        av_dict_set(&dict, std::get<0>(v), std::get<1>(v), AV_DICT_APPEND);
    return dict;
}



static auto create_picture(int w, int h) {

    auto frame = make_resource_ptr(av_frame_alloc(), av_frame_free);
    if (!frame)
        throw std::runtime_error("Failed to allocate picture");

    frame->width = w;
    frame->height = h;
    frame->format = AV_PIX_FMT_YUV420P;

    int ret = av_frame_get_buffer(frame.get(), 32);
    if (ret < 0)
        throw std::runtime_error("Failed to allocate picture buffers");

    memset(frame->data[0], 16,  frame->linesize[0] * h);
    memset(frame->data[1], 128, frame->linesize[1] * h/2);
    memset(frame->data[2], 128, frame->linesize[2] * h/2);

    frame->pts = 0;

    return frame;
}

static auto create_encoder(int w, int h) {
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        throw std::runtime_error("H.264 encoder is not found");

    auto c = make_resource_ptr(avcodec_alloc_context3(codec), avcodec_free_context);
    if (!c)
        throw std::runtime_error("Failed to allocate encoder context");

    auto avctx = c.get();
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->width = w;
    avctx->height = h;
    avctx->time_base = {1, 1};
    avctx->thread_count = 1;

    avctx->gop_size = 2;
    avctx->max_b_frames = 0;
    avctx->profile = 77;
    avctx->qmin = 11;
    avctx->qmax = 11;

    AVDictionary* opts = create_dictionary({
        { "refs",         "1"                },
        { "tune",         "zerolatency"      },
        { "me_method",    "zero"             },
        { "cmp",          "zero"             },
        { "trellis",      "0"                },
        { "subq",         "0"                },
        { "nal-hrd",      "none"             },
        { "rc-lookahead", "1"                },
        { "flags",        "+global_header"   },
        { "x264-params",  "repeat-headers=1" }
   });

    int ret = avcodec_open2(avctx, codec, &opts);
    av_dict_free(&opts);

    if (ret < 0)
        throw std::runtime_error("Cannot open encoder");


    return c;
}

static auto create_decoder() {
    AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec)
        throw std::runtime_error("H.264 decoder is not found");

    auto c = make_resource_ptr(avcodec_alloc_context3(codec), avcodec_free_context);
    if (!c)
        throw std::runtime_error("Failed to allocate decoder context");

    auto avctx = c.get();
    avctx->thread_count = 1;

    if (avcodec_open2(avctx, codec, nullptr) < 0)
        throw std::runtime_error("Cannot open decoder");

    return c;
}


// Encode one frame
static auto encode_frame(AVCodecContext* enc, const AVFrame* frame) {
    if(avcodec_send_frame(enc, frame) < 0)
        throw std::runtime_error("Failed to send frame to encoder");

    auto pkt = make_resource_ptr(av_packet_alloc(), av_packet_free);
    if (avcodec_receive_packet(enc, pkt.get()) < 0)
        throw std::runtime_error("Failed to get packet from encoder");

    return pkt;
}

// Decode one frame
static auto decode_frame(AVCodecContext* dec, const AVPacket* pkt) {

    if (avcodec_send_packet(dec, pkt) < 0)
        throw std::runtime_error("Failed to send packet to decoder");

    auto frame = make_resource_ptr(av_frame_alloc(), av_frame_free);
    if (avcodec_receive_frame(dec, frame.get()) < 0)
        throw std::runtime_error("Failed to get frame to decoder");

    return frame;
}


static auto encode_skip_slice(const AVFrame* pict) {

    auto enc = create_encoder(pict->width, pict->height);
    auto dec = create_decoder();

    // Encode frame => decode it => encode it again
    auto pkt = encode_frame(enc.get(), pict);

    auto decoded = decode_frame(dec.get(), pkt.get());
    decoded->pts = 1;
    decoded->key_frame = 0;
    decoded->pict_type = AV_PICTURE_TYPE_P;

    return encode_frame(enc.get(), decoded.get());
}

static auto get_slice_data(const AVPacket* pkt) {
    auto data = pkt->data;
    auto size = pkt->size;

    // For give encoding settings slice data offset is always 8 bytes:
    //  - 4 - NAL size len
    //  - 1 - NAL header
    //  - 3 - Slice header
    if (size < 8)
        throw std::runtime_error("Slice data is not found");

    data += 8;
    size -= 8;

    return std::vector<uint8_t>(data, data + size);
}

// usage: app <resolution>
int main(int argc, char** argv) {

    try {
        avcodec_register_all();
        av_log_set_level(AV_LOG_ERROR);

        if (argc < 2)
            throw std::runtime_error("Missing resolution");

        int w = 0, h = 0;
        if (av_parse_video_size(&w, &h, argv[1]) < 0)
            throw std::runtime_error("Invalid resolution");

        // Encode slice here
        auto pict = create_picture(w, h);
        auto pkt = encode_skip_slice(pict.get());
        auto data = get_slice_data(pkt.get());

        // Dump data in hex format
//        for(auto d : data)
//            std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << int(d) << ' ';


        // Dump in format for libavcodec/h264_constrate_filter_data.c
        std::cout << "static const uint8_t slice_data_"<< w << 'x' << h <<"[] = {";
        auto len = data.size();
        for(int i = 0; i < len; i++) {
            if (i % 32 == 0)
                std::cout << "\n   ";

            std::cout << " 0x" << std::hex << std::setw(2) << std::setfill('0') << int(data[i]);
            if (i < len-1)
                std::cout << ',';
        }
        std::cout << "};\n";



    } catch( std::exception& e) {
        std::cerr <<  e.what() << std::endl;
        return 1;
    }

    return 0;
}