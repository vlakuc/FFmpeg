#include <stdlib.h>
#include "mem.h"

#include "circular_buffer.h"

AVCircularBuffer* av_circular_buffer_alloc(size_t capacity)
{
    AVCircularBuffer *ptr = av_mallocz(sizeof(AVCircularBuffer));
    ptr->head = 0;
    ptr->tail = 0;
    ptr->size = 0;
    ptr->capacity = capacity;
    ptr->buffer = av_calloc(capacity, sizeof(int64_t));
    return ptr;
}

void av_circular_buffer_destroy(AVCircularBuffer* context)
{
    if (av_circular_buffer_is_valid(context)) {
        av_free(context->buffer);
        av_free(context);
    }
}

void av_circular_buffer_reset(AVCircularBuffer* context)
{
    if (av_circular_buffer_is_valid(context)) {
        memset(context->buffer, 0, context->capacity * sizeof(int64_t));
        context->head = 0;
        context->tail = 0;
        context->size = 0;
    }
}

int64_t av_circular_buffer_head(const AVCircularBuffer* context)
{
    return av_circular_buffer_is_valid(context) ? context->buffer[context->head] : -1;
}

int64_t av_circular_buffer_tail(const AVCircularBuffer* context)
{
    return av_circular_buffer_is_valid(context) ? context->buffer[context->tail] : -1;
}

int av_circular_buffer_is_full(const AVCircularBuffer* context)
{
    return context->size == context->capacity;
}

int av_circular_buffer_is_valid(const AVCircularBuffer* context)
{
    return context != NULL && context->buffer !=NULL;
}

size_t av_circular_buffer_capacity(const AVCircularBuffer* context)
{
    return av_circular_buffer_is_valid(context) ? context->capacity : -1;
}

size_t av_circular_buffer_size(const AVCircularBuffer* context)
{
    return av_circular_buffer_is_valid(context) ? context->size : -1;
}

void av_circular_buffer_enqueue(AVCircularBuffer* context, int64_t data)
{

    if (av_circular_buffer_is_valid(context)) {
        if (context->size !=0)
            context->tail = (context->tail + 1) % context->capacity;
        context->buffer[context->tail] = data;

        //adjust head and tail
        if (av_circular_buffer_is_full(context)) {
            if (context->size != 0) {
                context->head = ( context->tail  + 1) % context->capacity;
            }
        }
        else {
            context->size++;
        }
    }
}

void av_circular_buffer_get_data(const AVCircularBuffer* context, int64_t* data, size_t* size)
{
    if (av_circular_buffer_is_valid(context) && data != NULL) {
        if (av_circular_buffer_is_full(context)) {
            *size = context->capacity;
            if (context->head < context->tail) {
                memcpy(data, context->buffer + context->head, sizeof(int64_t) * (context->tail - context->head + 1));
            }
            else {
                memcpy(data, context->buffer + context->head, sizeof(int64_t) * (context->capacity - context->head));
                memcpy(data + (context->capacity - context->head), context->buffer, sizeof(int64_t) *  (context->tail + 1));
            }
        }
        else {
            *size = context->size;
            memcpy(data, context->buffer, sizeof(int64_t) * context->size);
        }
    }
}


int av_circular_buffer_at(const AVCircularBuffer* context, size_t position, int64_t* value)
{
    size_t pos;
    if (!av_circular_buffer_is_valid(context)
        || value == NULL
        || position >= context->size) {
        return 0;
    }

    pos = (context->head + position) % context->size;
//    printf("pos: %d, val: %lld\n", pos, context->buffer[pos]);
    memcpy(value, &context->buffer[pos], sizeof(int64_t));
    return 1;
}
