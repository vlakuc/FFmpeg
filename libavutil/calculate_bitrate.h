#ifndef AVUTIL_CALCULATE_BITRATE_H
#define AVUTIL_CALCULATE_BITRATE_H

#include <stdint.h>

#define RING_BUFFER_SIZE 200

typedef struct {
    uint64_t total_size;
    uint64_t start_time;
    uint64_t prev_time;
    unsigned index;
    unsigned overflowed;
    struct {
        unsigned size;
        unsigned time_diff;
    } entries[RING_BUFFER_SIZE];
} AVBitrateContext;


/**
 * Record size and corresponding timestamp into context
 *
 * @param ctx  - AVBitrateContext context
 * @param size - data size to be used in bit rate calculation (in bytes)
 * @param current_time - timestamp (in mpst cases av_gettime() should be used)
 */
void av_fix_bitrate(AVBitrateContext **ctx, int size, int64_t current_time);

/**
 * Calculate bitrate value (bits/sec) using data recorded by av_fix_bitrate()
 *
 * @param ctx  - AVBitrateContext context
 * @param current_time - timestamp (in most cases av_gettime() should be used)
 */
unsigned av_calculate_bitrate(const AVBitrateContext *ctx, int64_t current_time);

#endif /* AVUTIL_CALCULATE_BITRATE_H */
