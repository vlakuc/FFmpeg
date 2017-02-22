#include <stdlib.h>

#include "config.h"

#if HAVE_PTHREADS
#include <pthread.h>
#endif // HAVE_PTHREADS

#include "mem.h"
#include "time.h"
#include "circular_buffer.h"
#include "rate_counter.h"

typedef struct AVRateCounter {
#if HAVE_PTHREADS
    pthread_mutex_t mutex;
#endif // HAVE_PTHREADS

    /**
     * Storage of rate data
     */
    AVCircularBuffer* circular_buffer;
} AVRateCounter;


#if HAVE_PTHREADS

static int mutex_lock(AVRateCounter* ctx)
{
    return pthread_mutex_lock(&ctx->mutex);
}

static int mutex_unlock(AVRateCounter* ctx)
{
    return pthread_mutex_unlock(&ctx->mutex);
}

static int mutex_init(AVRateCounter* ctx)
{
    return pthread_mutex_init(&ctx->mutex, NULL);
}

static int mutex_destroy(AVRateCounter* ctx)
{
    return pthread_mutex_destroy(&ctx->mutex);
}

#else // HAVE_PTHREADS

static int mutex_lock(AVRateCounter* ctx)
{
    return 0;
}

static int mutex_unlock(AVRateCounter* ctx)
{
    return 0;
}

static int mutex_init(AVRateCounter* ctx)
{
    return 0;
}

static int mutex_destroy(AVRateCounter* ctx)
{
    return 0;
}
#endif // HAVE_PTHREADS

AVRateCounter* av_rate_counter_alloc(size_t window_size)
{
    AVRateCounter *ptr = av_mallocz(sizeof(AVRateCounter));

    mutex_init(ptr);
    
    ptr->circular_buffer = av_circular_buffer_alloc(window_size);
    return ptr;
}

void av_rate_counter_destroy(AVRateCounter* ctx)
{
    if (ctx) {
        av_circular_buffer_destroy(ctx->circular_buffer);
        mutex_destroy(ctx);
        av_free(ctx);
    }
}


void av_rate_counter_add_tick(AVRateCounter* ctx, int64_t tick)
{
    mutex_lock(ctx);
    av_circular_buffer_enqueue(ctx->circular_buffer, tick);
    mutex_unlock(ctx);
}

void av_rate_counter_add_tick_now(AVRateCounter* ctx)
{
    if (ctx) {
        av_rate_counter_add_tick(ctx, av_gettime());
    }
}


static double get_rate(AVRateCounter* ctx)
{
    size_t size;
    double delta;

    delta = (av_circular_buffer_tail(ctx->circular_buffer)
                    - av_circular_buffer_head(ctx->circular_buffer)) / 1000000.0;

    size = av_circular_buffer_size(ctx->circular_buffer);
           
    return size == 0 || delta == 0? 0 : (size - 1) / delta;

}


double av_rate_counter_get(AVRateCounter* ctx)
{
    double result;
   
    mutex_lock(ctx);
    result = get_rate(ctx);
    mutex_unlock(ctx);

    return result;
}


double av_rate_counter_get_interval(AVRateCounter* ctx, uint64_t interval)
{
    int64_t tail;
    int64_t diff;
    double result = 0.0;
    
    mutex_lock(ctx);

    tail = av_circular_buffer_tail(ctx->circular_buffer);
    diff = tail - av_circular_buffer_head(ctx->circular_buffer);
    
    if (interval == 0 || diff < 0)
        result = 0.0;
    else if (diff < interval)
        result = get_rate(ctx);
    else {
        double delta;
        size_t size;
        size = av_circular_buffer_size(ctx->circular_buffer);
        for (int pos = size - 1; pos >= 0; --pos) {
            int64_t value;
            if (av_circular_buffer_at(ctx->circular_buffer, pos, &value)) {
                if ((tail - interval) >= value) {
                    delta = tail - value;
                    size = size - 1 - pos;
                    break;
                }
            }
        }
        delta /= 1000000.0;
        result = size == 0 || delta == 0 ? 0.0 : size / delta;
    }
    
    mutex_unlock(ctx);

    return result;
}


void av_rate_counter_reset(AVRateCounter* ctx)
{
    mutex_lock(ctx);
    av_circular_buffer_reset(ctx->circular_buffer);
    mutex_unlock(ctx);
}