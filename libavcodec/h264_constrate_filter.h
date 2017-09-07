/*
 * H.264 Bitstream Level Constant Video Frame Rate Filter
 *
 * Copyright (c) 2017 Epiphan Systems Inc. All rights reserved.
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

#ifndef AVCODEC_H264_CONSTRATE_FILTER_H
#define AVCODEC_H264_CONSTRATE_FILTER_H

#include "avcodec.h"
#include "h264_ps.h"

typedef struct H264ConstRateContext {
    H264ParamSets paramsets;
    const SPS*    sps;
    const PPS*    pps;
    int           is_avc;
    int           nal_length_size;
    const void*   slice_data;
    int           slice_data_size;
    int64_t       duration;             ///< single frame duration
    AVRational    time_base;
    void*         log_ctx;              ///< logging context

    // context
    AVPacket      pkt;                  ///< Packet from send_packet
    int64_t       last_dts;
    int           num_skip_frames;      ///< Number of skip frames to be inserted
    int           skip_frame_num;
    int64_t       skip_frame_dts;
    int           last_frame_num;       ///< Frame number of last received frame (adjusted)
    int           frame_num_adj;        ///< Frame number adjustment for the remained frames in current GOP (0 - do not adjust)

} H264ConstRateContext;

/** 
 * Allocate and initialize new H264 Constant Rate Filter
 */
int av_h264_constrate_create(H264ConstRateContext** filter, const AVCodecParameters* par, AVRational time_base, AVRational frame_rate, void* log_ctx);

/*
 * Free H264 Constant Rate Filter and its resources
 */
void av_h264_constrate_free(H264ConstRateContext** filter);

/*
 * Send packet to the filter
 *  After av_h264_constrate_send_packet caller should call av_h264_constrate_receive_packet 
 *  until it returns EAGAIN or an error. EAGAIN means that filter is drain and another packet should be provided
 */
int av_h264_constrate_send_packet(H264ConstRateContext* filter, AVPacket* pkt);

/*
 * Retrieve packet from the filter. 
 *  Returned packet is either original (passed through av_h264_constrate_send_packet)
 *  or skip frame. In any case caller is responsible to release packet by calling av_packet_unref
 * 
 *  Returns EAGAIN if there is not more packets in filter and av_h264_constrate_send_packet should be called
 */ 
int av_h264_constrate_receive_packet(H264ConstRateContext* filter, AVPacket* pkt);


#endif /* AVCODEC_H264_CONSTRATE_FILTER_H */

