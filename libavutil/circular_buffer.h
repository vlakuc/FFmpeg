#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <stdint.h>
#include <stddef.h>


typedef struct
{
    /**
     * The data buffer
     */
    int64_t* buffer;

    /**
     * Index of the first element in the buffer
     */
    size_t head;

    /**
     * Index of the last element in the buffer
     */
    size_t tail;

    /**
     * Number of elements sotred in the buffer
     */
    size_t size;

    /**
     * Maximum number of elements that can be sotred in the buffer
     */
    size_t capacity;

} AVCircularBuffer;


/**
 * Allocate circular buffer
 *
 * @param capacity - circular buffer capacity
 */
AVCircularBuffer* av_circular_buffer_alloc(size_t capacity);

/**
 * Free given circular buffer
 *
 * @param circular buffer context (AVCircularBuffer)
 */
void av_circular_buffer_destroy(AVCircularBuffer* context);

/**
 * Return first element of circular buffer
 *
 * @param circular buffer context (AVCircularBuffer)
 */
int64_t av_circular_buffer_head(const AVCircularBuffer* context);

/**
 * Return last element of circular buffer
 *
 * @param circular buffer context (AVCircularBuffer)
 */
int64_t av_circular_buffer_tail(const AVCircularBuffer* context);

/**
 * Reset circular buffer context.
 * 
 * @param circular buffer context (AVCircularBuffer)
 */
void av_circular_buffer_reset(AVCircularBuffer* context);

/**
 * Check if circular buffer is full.
 * Buffer considered as full when capacity is equal to its size.
 * Addition of new element will result to replacement of old element.
 *
 * @param circular buffer context (AVCircularBuffer)
 *
 * @return 1- full, 0 - not full
 */
int av_circular_buffer_is_full(const AVCircularBuffer* context);

/**
 * Check that circular buffer context is allocated properly
 *
 * @param circular buffer context (AVCircularBuffer)
 *
 * @return 1- valid, 0 - not valid
 */
int av_circular_buffer_is_valid(const AVCircularBuffer* context);

/**
 * Return circular buffer capacity
 *
 * @param circular buffer context (AVCircularBuffer)
 */
size_t av_circular_buffer_capacity(const AVCircularBuffer* context);

/**
 * Return circular buffer size
 *
 * @param circular buffer context (AVCircularBuffer)
 */
size_t av_circular_buffer_size(const AVCircularBuffer* context);

/**
 * Add element to circular buffer
 *
 * @param context circular buffer context (AVCircularBuffer)
 * @param data element to be added to the buffer
 */
void av_circular_buffer_enqueue(AVCircularBuffer* context, int64_t data);

/**
 * Get contents of the circular buffer
 *
 * @param context circular buffer context (AVCircularBuffer)
 * @param data    pointer to preallocated buffer of av_circular_buffer_capacity() size
 * @param size    size of data copied to data.
 */
void av_circular_buffer_get_data(const AVCircularBuffer* context, int64_t* data, size_t* size);

/**
 * Get element at position of circular buffer
 *
 * @param context  circular buffer context (AVCircularBuffer)
 * @param position postion of element in buffer
 * @param value    pointer to value storage
 *
 * @return 1 - if position is correct and pointer to value storage is valid, 0 otherwise
 */
int av_circular_buffer_at(const AVCircularBuffer* context, size_t position, int64_t* value);

#endif /* CIRCULAR_BUFFER_H */
