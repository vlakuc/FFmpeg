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
