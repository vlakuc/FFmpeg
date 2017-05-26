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

#include "audio_level.h"
#include "avassert.h"
#include "channel_layout.h"

// Convert to dB with lower limit -100dB
static float db_value(float v)
{
    return (v > 0.00001f) ? 20.0f * log10f(v) : -100.0f;
}

// RMS and Peak values in dBFS scale
// AV_SAMPLE_FMT_S16
static void get_level_values_s16(const int16_t* samples, size_t nsamples, size_t nchannels, float* rms_values, float* max_values)
{
    int16_t m[AV_NUM_DATA_POINTERS] = {0}; // peak values
    size_t i;
    
    for (i = 0; i < nsamples*nchannels; i++, samples++) {
        rms_values[i%nchannels] += (*samples/32768.0f) * (*samples/32768.0f);
        m[i%nchannels] = FFMAX(*samples, m[i%nchannels]);
    }
    for (i = 0; i < nchannels; i++) {
        rms_values[i] = db_value(sqrt(rms_values[i]/nsamples));
        max_values[i] = db_value(m[i]/32768.0f);
    }
}

// TODO: AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_FLT

int av_frame_audio_level_calc(const AVFrame *frame, AVAudioLevel *result)
{
    av_assert0(frame != NULL);
    av_assert0(result != NULL);

    // TODO: remove this limitation.
    if ((frame->channel_layout != AV_CH_LAYOUT_MONO) && (frame->channel_layout != AV_CH_LAYOUT_STEREO)) {
        av_log(NULL, AV_LOG_ERROR, "Only mono and stereo layout for audio_level_calc is supported\n");
        return AVERROR_INVALIDDATA;
    }
    
    // Since only S16 format is supported, call directly av_audio_level_calc
    av_assert0(frame->data[0] != NULL);
    return av_audio_level_calc(frame->data[0], frame->nb_samples, frame->channels, frame->format, result);
}

// Calculate audio level for non-planar formats
int av_audio_level_calc(const uint8_t* samples, size_t nsamples, size_t nchannels, enum AVSampleFormat format, AVAudioLevel *result)
{
    size_t i;
    av_assert0(result != NULL);
    if ((!nchannels) || (!nsamples)) {
        for (i = 0; i < AV_NUM_DATA_POINTERS ; i++) {
            result->rms[i] = -100.0;
            result->max[i] = -100.0;
        }
        return 0;
    }

    memset(result->max, 0, sizeof(result->max));
    memset(result->rms, 0, sizeof(result->rms));

    switch(format)
    {
        case AV_SAMPLE_FMT_S16:
            get_level_values_s16((const int16_t*)samples, nsamples, nchannels, result->rms, result->max);
            break;
        
        default:
            av_log(NULL, AV_LOG_ERROR, "Only AV_SAMPLE_FMT_S16 for audio_level_calc supported\n");
            return AVERROR_INVALIDDATA;
    }
    
    return 0;
}
