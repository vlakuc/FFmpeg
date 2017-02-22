
extern "C" {
#include "libavutil/circular_buffer.h"
#include "libavutil/avutil.h"
}


#include <array>
#include <memory>
#include <gtest/gtest.h>


namespace
{
using AVCircularBufferPtr = std::unique_ptr<AVCircularBuffer>;
}

// A program may add a template specialization for any standard library template
// to namespace std only if the declaration depends on a user-defined type and
// the specialization meets the standard library requirements for the original
// template and is not explicitly prohibited.” [n3690; 17.6.4.2.1]
namespace std
{

template<>
struct default_delete<AVCircularBuffer> {
    void operator()(AVCircularBuffer* ptr) { av_circular_buffer_destroy(ptr); }
};
} // std


TEST(CircularBuffer, CheckCapacity)
{
    const size_t capacity = 100;
    
    AVCircularBufferPtr cb(av_circular_buffer_alloc(capacity));
    ASSERT_FALSE( NULL == cb.get());
    ASSERT_FALSE( NULL == cb->buffer);
    EXPECT_EQ(capacity, av_circular_buffer_capacity(cb.get()));
}


TEST(CircularBuffer, ContextValidation)
{
    AVCircularBuffer *cb = NULL;
    
    EXPECT_FALSE(av_circular_buffer_is_valid(cb));

    cb = static_cast<AVCircularBuffer*>(av_mallocz(sizeof(AVCircularBuffer)));
    EXPECT_FALSE(av_circular_buffer_is_valid(cb));
    av_free(cb);
}


TEST(CircularBuffer, EnqueueNoMoreThanCapacity)
{
    const size_t capacity = 5;
    AVCircularBufferPtr cb(av_circular_buffer_alloc(capacity));

    EXPECT_EQ(av_circular_buffer_head(cb.get()), 0);
    EXPECT_EQ(av_circular_buffer_tail(cb.get()), 0);

    int initial_value = 1;

    for (size_t value = initial_value; value <= capacity; ++value) {
        av_circular_buffer_enqueue(cb.get(), value);
        EXPECT_EQ(av_circular_buffer_head(cb.get()), initial_value);
        EXPECT_EQ(av_circular_buffer_tail(cb.get()), value);
    }
}


TEST(CircularBuffer, Overflow)
{
    const size_t capacity = 5;
    AVCircularBufferPtr cb(av_circular_buffer_alloc(capacity));

    EXPECT_EQ(av_circular_buffer_head(cb.get()), 0);
    EXPECT_EQ(av_circular_buffer_tail(cb.get()), 0);

    int initial_value = 1;
    size_t value = initial_value;
    for (; value <= capacity + 2; ++value) {
        av_circular_buffer_enqueue(cb.get(), value);
    }

    // [ 6 7 3 4 5 ] head = 3, tail = 7
    EXPECT_EQ(av_circular_buffer_head(cb.get()), initial_value + 2);
    EXPECT_EQ(av_circular_buffer_tail(cb.get()), capacity + 2);
}

TEST(CircularBuffer, GetData)
{
    const size_t capacity = 5;
    AVCircularBufferPtr cb(av_circular_buffer_alloc(capacity));

    size_t value = 1;
    av_circular_buffer_enqueue(cb.get(), value++);
    av_circular_buffer_enqueue(cb.get(), value);
    
    size_t size = 0;

    std::array<int64_t, capacity> buf = {{0}};
    av_circular_buffer_get_data(cb.get(), buf.data(), &size);
    std::array<int64_t, capacity> expected = {{1, 2, 0, 0, 0}};
    
    EXPECT_EQ(size, value);
    EXPECT_EQ(buf, expected);

    value++;
    for (; value <= capacity; ++value) {
        av_circular_buffer_enqueue(cb.get(), value);
    }

    size = 0;
    buf.fill(0);
    av_circular_buffer_get_data(cb.get(), buf.data(), &size);

    expected = {{1, 2, 3, 4, 5}};
    EXPECT_EQ(size, capacity);
    EXPECT_EQ(buf, expected);


    av_circular_buffer_enqueue(cb.get(), value++);
    av_circular_buffer_enqueue(cb.get(), value);

    size = 0;
    buf.fill(0);
    av_circular_buffer_get_data(cb.get(), buf.data(), &size);

    expected = {{3, 4, 5, 6, 7}};
    EXPECT_EQ(size, capacity);
    EXPECT_EQ(buf, expected);


    av_circular_buffer_enqueue(cb.get(), ++value);
    size = 0;
    buf.fill(0);
    
    av_circular_buffer_get_data(cb.get(), buf.data(), &size);

    expected = {{4, 5, 6, 7, 8}};
    EXPECT_EQ(size, capacity);
    EXPECT_EQ(buf, expected);
}


TEST(CircularBuffer, GetElementAtPosition)
{
    const size_t capacity = 5;
    
    AVCircularBufferPtr cb(av_circular_buffer_alloc(capacity));

    int64_t val;
    EXPECT_FALSE(av_circular_buffer_at(NULL, 0, &val) );
    EXPECT_FALSE(av_circular_buffer_at(cb.get(), 0, &val) );
    EXPECT_FALSE(av_circular_buffer_at(cb.get(), 0, NULL) );
    EXPECT_FALSE(av_circular_buffer_at(cb.get(), 6, &val));
    EXPECT_FALSE(av_circular_buffer_at(cb.get(), -1, &val));


    std::vector<int> range = {{0,1,2,3}};
    for (auto i : range) {
        av_circular_buffer_enqueue(cb.get(), i);
    }

    for (auto i : range) {
        int64_t value;
        auto result = av_circular_buffer_at(cb.get(), i, &value);
        EXPECT_TRUE(result);
        EXPECT_EQ(i, value);
    }

    for (auto i : {4,5,6}) {
        av_circular_buffer_enqueue(cb.get(), i);
    }

    std::vector<int> values = {{2,3,4,5,6}};
    for (auto i = 0u; i < capacity; ++i) {
        int64_t value;
        auto result = av_circular_buffer_at(cb.get(), i, &value);
        EXPECT_TRUE(result);
        EXPECT_EQ(values[i], value);
    }
}


