extern "C" {
#include "libavutil/audio_level.h"
#include "libavutil/frame.h"
#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
}

#include <gtest/gtest.h>

namespace {

void av_audio_level_calc_test(const char* samples, const char* level, int channels, int frame_num)
{
    AVAudioLevel audio_level;
    AVFrame frame;
    FILE *test_input = fopen(samples, "rb");
    FILE *test_output = fopen(level, "rb");
    uint8_t buf[2*2048];
    float rms[2];
    float max[2];
    int i, j;

    //Check fixtures file existance
    ASSERT_TRUE(test_input != NULL);
    ASSERT_TRUE(test_output != NULL);
    for (i = 0; i < frame_num; i++) {
        //Load input fixture
        ASSERT_TRUE(fread(buf, 2048, channels, test_input) != static_cast<size_t>(2048*channels));
        //Load output fixture
        ASSERT_TRUE(fread(rms, sizeof(float), channels, test_output) != (channels*sizeof(float)));
        ASSERT_TRUE(fread(max, sizeof(float), channels, test_output) != (channels*sizeof(float)));
        //Prepare AVFrame
        frame.data[0] = buf;
        frame.format = AV_SAMPLE_FMT_S16;
        if (channels == 2)
            frame.channel_layout = AV_CH_LAYOUT_STEREO;
        else frame.channel_layout = AV_CH_LAYOUT_MONO;
        frame.channels = channels;
        frame.nb_samples = 1024;
        //testing function
        av_frame_audio_level_calc(&frame, &audio_level);
        //Compare 
        for (j = 0; j < channels; j++) {
            EXPECT_FLOAT_EQ(rms[j], audio_level.rms[j]);
            EXPECT_FLOAT_EQ(max[j], audio_level.max[j]);    
        }
    }
    //Free resources
    fclose(test_input);
    fclose(test_output);
}
    
}  // anonymous namespace



TEST(AVAudioLevel, av_frame_audio_level_silence)
{
    uint8_t buf[2*2048];
    AVAudioLevel audio_level;
    AVFrame frame;
    int i;
    //Fill AVFrame with silence
    memset (buf, 0, 2*2048);
    frame.data[0] = buf;
    frame.format = AV_SAMPLE_FMT_S16;
    frame.channel_layout = AV_CH_LAYOUT_STEREO;
    frame.channels = 2;
    frame.nb_samples = 1024;
    //Calc Audio level
    av_frame_audio_level_calc(&frame, &audio_level);
    //Compare
    for (i = 0; i < 2; i++) {
        EXPECT_FLOAT_EQ(-100.0, audio_level.rms[i]);
        EXPECT_FLOAT_EQ(-100.0, audio_level.max[i]);
    }

}

TEST(AVAudioLevel, av_frame_audio_level_no_data)
{
    uint8_t buf[2*2048];
    AVAudioLevel audio_level;
    AVFrame frame;
    int i;
    //Fill AVFrame with staff
    memset (buf, 0xff, 2*2048);
    frame.data[0] = buf;
    frame.format = AV_SAMPLE_FMT_S16;
    frame.channel_layout = AV_CH_LAYOUT_STEREO;
    frame.channels = 0; //  no channels
    frame.nb_samples = 1024;
    //Calc Audio level
    av_frame_audio_level_calc(&frame, &audio_level);
    //Compare
    for (i = 0; i < 2; i++) {
        EXPECT_FLOAT_EQ(-100.0, audio_level.rms[i]);
        EXPECT_FLOAT_EQ(-100.0, audio_level.max[i]);
    }
    frame.nb_samples = 0;
    frame.channels = 2;
    //Calc Audio level
    av_frame_audio_level_calc(&frame, &audio_level);
    //Compare
    for (i = 0; i < 2; i++) {
        EXPECT_FLOAT_EQ(-100.0, audio_level.rms[i]);
        EXPECT_FLOAT_EQ(-100.0, audio_level.max[i]);
    }
}

TEST(AVAudioLevel, av_frame_audio_level_calc_mono)
{
    av_audio_level_calc_test("test_data/audio_level_calc_mono_in.dbg", "test_data/expected/AVLevel_mono.dbg", 1, 1);
}

TEST(AVAudioLevel, av_frame_audio_level_calc_stereo)
{
    av_audio_level_calc_test("test_data/audio_level_calc_stereo_in.dbg", "test_data/expected/AVLevel_stereo.dbg", 2, 2);
}
