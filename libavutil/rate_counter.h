#ifndef RATE_COUNTER_H
#define RATE_COUNTER_H

#include <stddef.h>
#include <inttypes.h>

typedef struct AVRateCounter AVRateCounter;

/**
 * Allocate rate counter context
 *
 * @param window_size - number of measurements stored in context
 *
 * @return pinter to allocated AVRateCounter context or NULL
 */
AVRateCounter *av_rate_counter_alloc(size_t window_size);

/**
 * Free rate counter context
 *
 * @param ctx - AVRateCounter context
 *
 */
void av_rate_counter_destroy(AVRateCounter* ctx);

/**
 * Add timestamp to context
 *
 * @param ctx  - AVRateCounter context
 * @param tick - timestamp to be used in following measurements
 */
void av_rate_counter_add_tick(AVRateCounter* ctx, int64_t tick);

/**
 * Add current timestamp returned by av_gettime() to context
 *
 * @param ctx - AVRateCounter context
 *
 */
void av_rate_counter_add_tick_now(AVRateCounter* ctx);

/**
 * Return rate calculated on whole window_size;
 *
 * @param ctx - AVRateCounter context
 *
 */
double av_rate_counter_get(AVRateCounter* ctx);

/**
 * Return rate calculated for last msec interval
 *
 * @param ctx - AVRateCounter context
 * @param interval in msec
 */
double av_rate_counter_get_interval(AVRateCounter* ctx, uint64_t interval);

/**
 * Reset rate counter context
 *
 * @param ctx - AVRateCounter context
 */
void av_rate_counter_reset(AVRateCounter* ctx);

#endif /* RATE_COUNTER_H */
