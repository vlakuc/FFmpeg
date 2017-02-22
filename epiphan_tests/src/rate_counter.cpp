extern "C" {
#include "libavutil/rate_counter.h"
#include "libavutil/avutil.h"
}

#include <memory>
#include <gtest/gtest.h>

namespace
{
using AVRateCounterPtr = std::unique_ptr<AVRateCounter>;

int64_t get_delta_ts(int fps)
{
    AVRational dst_tb;
    dst_tb.num = 1;
    dst_tb.den = 1000000;
    
    AVRational src_tb;
    src_tb.num = 1;
    src_tb.den = fps;
    return av_rescale_q(1, src_tb, dst_tb);
}
    
}

namespace std
{
template<>
struct default_delete<AVRateCounter> {
    void operator()(AVRateCounter* ptr) { av_rate_counter_destroy(ptr); }
};
} // std

TEST(AVRateCounter, Rate)
{
    int windowSizes[] = { 5,10,30,1000 };
    for( int ws: windowSizes )
    {
        int rates[] = { 1,5,10,15,30 };
        for( int fps: rates )
        {

            int64_t ts = 0;
            int64_t ts_delta = get_delta_ts(fps);

            AVRateCounterPtr rc(av_rate_counter_alloc(ws));

            for( int i=0; i<3000; i++ )
            {
                ts = ts + ts_delta;
                av_rate_counter_add_tick(rc.get(), ts);

                if (i > 2)
                    EXPECT_NEAR(av_rate_counter_get(rc.get()), fps, 0.01);
            }
        }
    }
}

TEST(AVRateCounter, GetRateNoData)
{
    const size_t capacity = 100;
    AVRateCounterPtr rc(av_rate_counter_alloc(capacity));
    EXPECT_EQ(av_rate_counter_get(rc.get()), 0);
}


TEST(AVRateCounter, GetRateOnInterval)
{
    const size_t capacity = 400;

    AVRateCounterPtr rc(av_rate_counter_alloc(capacity));
    int64_t ts = 0;
    int rates[] = {1, 5, 25, 50};
    for ( auto fps : rates ) {
        int64_t ts_delta = get_delta_ts(fps);
        
        auto size = 100;
        for (auto i = 0; i < size; ++i) {
            ts = ts + ts_delta;
            av_rate_counter_add_tick(rc.get(), ts);
        }

        auto interval = size / fps * 1000000;
        auto half_interval = interval / 2;
        EXPECT_NEAR(av_rate_counter_get_interval(rc.get(), interval), fps, 0.01);
        EXPECT_NEAR(av_rate_counter_get_interval(rc.get(), half_interval), fps, 0.01);
    }
}

TEST(AVRateCounter, IntervalRateCheckBounds)
{
    const size_t capacity = 100;
    {
        AVRateCounterPtr rc(av_rate_counter_alloc(capacity));
        EXPECT_NEAR(av_rate_counter_get_interval(rc.get(), 0), 0.0, 0.01);
    }

    {
        AVRateCounterPtr rc(av_rate_counter_alloc(capacity));
        EXPECT_NEAR(av_rate_counter_get_interval(rc.get(), 0), 0.0, 0.01);
        av_rate_counter_add_tick(rc.get(), 2000000);
        av_rate_counter_add_tick(rc.get(), 1000000);
        EXPECT_NEAR(av_rate_counter_get_interval(rc.get(), 1000000), 0.0, 0.01);
    }
    
    {
        AVRateCounterPtr rc(av_rate_counter_alloc(capacity));
        auto fps = 50;
        auto ts_delta = get_delta_ts(fps);
        int64_t ts = 0;

        auto size = 100;
        for (auto i = 0; i < size; ++i) {
            ts = ts + ts_delta;
            av_rate_counter_add_tick(rc.get(), ts);
        }
        
        // measurement interval (capacity / fps) is half of requested interval
        auto interval = 2 * size / fps * 1000000;
        EXPECT_NEAR(av_rate_counter_get_interval(rc.get(), interval), fps, 0.01);
    }
}

