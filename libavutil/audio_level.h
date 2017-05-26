/*
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

/**
 * @file
 * Audio level calculation functions
 */

#ifndef AVUTIL_AUDIO_LEVEL_H
#define AVUTIL_AUDIO_LEVEL_H

#include "avutil.h"
#include "samplefmt.h"
#include "frame.h"

typedef struct AVAudioLevel {
    float rms[AV_NUM_DATA_POINTERS];
    float max[AV_NUM_DATA_POINTERS];
} AVAudioLevel;

int av_frame_audio_level_calc(const AVFrame *frame, AVAudioLevel *result);

// Calculate audio level for non-planar formats
int av_audio_level_calc(const uint8_t* samples, size_t nsamples, size_t nchannels, enum AVSampleFormat format, AVAudioLevel *result);

static inline int av_audio_level_calc_s16(const int16_t* samples, size_t nsamples, size_t nchannels, AVAudioLevel *result)
{
    return av_audio_level_calc((const uint8_t*)samples, nsamples, nchannels, AV_SAMPLE_FMT_S16, result);
}


#endif /* AVUTIL_AUDIO_LEVEL_H */
