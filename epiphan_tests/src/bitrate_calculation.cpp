extern "C" {
    #include "libavutil/calculate_bitrate.h"
    #include "libavutil/calculate_bitrate.c"
}

#include <unistd.h>

#include <gtest/gtest.h>

#define TIMEOUT 100000 //100 ms

namespace {

TEST(TestAVBitrateContext, CheckCalculateBitrateNull)
{
    AVBitrateContext *ctx = NULL;
    ASSERT_EQ(av_calculate_bitrate(ctx, av_gettime()), 0);
}


TEST(TestAVBitrateContext, CheckCalculateBitrate) 
{
    AVBitrateContext *ctx = NULL;
    auto current_time = av_gettime();
    av_fix_bitrate(&ctx, 0, current_time);

    for(auto i = 0; i<10; ++i) {
        current_time += TIMEOUT;
        av_fix_bitrate(&ctx, 1000, current_time);
    }

    ASSERT_EQ(av_calculate_bitrate(ctx, current_time), 80000);
}


TEST(TestAVBitrateContext, CheckFixBitrateNull)
{
	AVBitrateContext *ctx = NULL;
	av_fix_bitrate(&ctx, 0, av_gettime());
	ASSERT_TRUE(ctx != NULL);
	ASSERT_EQ(ctx->start_time, 0);
	ASSERT_EQ(ctx->total_size, 0);
	ASSERT_EQ(ctx->index, 0);
	ASSERT_EQ(ctx->overflowed, 0);
	ASSERT_TRUE(ctx->prev_time != 0);
}


TEST(TestAVBitrateContext, CheckFixBitrate)
{
    AVBitrateContext *ctx = NULL;
    av_fix_bitrate(&ctx, 0, 0);

//    usleep(TIMEOUT); //100 ms pause

    av_fix_bitrate(&ctx, 1000, TIMEOUT);
    ASSERT_EQ(ctx->start_time, 100); //100 ms
    ASSERT_EQ(ctx->total_size, 1000);
}


TEST(TestAVBitrateContext, CheckFixBitrateOverflowed)
{
    AVBitrateContext *ctx = NULL;
    auto current_time = av_gettime();
    av_fix_bitrate(&ctx, 0, current_time);
    
    for(int i=0; i<RING_BUFFER_SIZE+50; ++i) {
        current_time += TIMEOUT/100;
        av_fix_bitrate(&ctx, 1000, current_time);
    }

    ASSERT_EQ(ctx->overflowed, 1);
    ASSERT_LE(ctx->total_size, RING_BUFFER_SIZE*1000);
    ASSERT_LE(ctx->start_time, RING_BUFFER_SIZE);
}


TEST(TestAVBitrateContext, CheckCalculateBitrateOverflowed)
{
    AVBitrateContext *ctx = NULL;
    auto current_time = av_gettime();
    av_fix_bitrate(&ctx, 0, current_time);

    for(auto i = 0; i<RING_BUFFER_SIZE+50; ++i) {
        current_time += TIMEOUT/100;
        av_fix_bitrate(&ctx, 100, current_time);
    }

    ASSERT_EQ(av_calculate_bitrate(ctx, current_time), 800000);
}


}
