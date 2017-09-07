#!/bin/sh

. ${TESTS_COMMON}/etc/tests/common.inc

MKTEMP="mktemp -d -t test_avimux.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"
LOCAL_TEST_PATH="test_data/avi_test/"
AUDIO_CODECS="aac mp3 pcm"

dump_stream_data()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    local stream="${3}"
    ffprobe -print_format json -show_packets -show_data $stream_filename -select_streams $stream 2> /dev/null | \
        jq -c '.packets[] |.codec_type, .stream_index, .size, .data' > $out_filename 2> /dev/null
}

dump_stream_packets()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    local stream="${3}"
    ffprobe -print_format json -show_packets $stream_filename -select_streams $stream 2> /dev/null | \
        jq -c '.packets[] |.codec_type, .stream_index, .dts_time, .duration_time' > $out_filename 2> /dev/null
}

setup()
{

    for audio_codec in $AUDIO_CODECS
    do
        local input_filename="h264_${audio_codec}_source.avpkt"
        local input_cut_filename="h264_${audio_codec}_cut.avpkt"

        local output_filename="h264_${audio_codec}_original.avi"
        local output_restructed_filename="h264_${audio_codec}_restructed.avi"

        ffmpeg -threads 1 -i $LOCAL_TEST_PATH/$input_filename -acodec copy -vcodec copy $SANDBOX_PATH/$output_filename 2> /dev/null
        test_status "Setup. Audio codec: ${audio_codec}. Base muxing to avi"

        ffmpeg -threads 1 -i $LOCAL_TEST_PATH/$input_cut_filename -acodec copy -vcodec copy $SANDBOX_PATH/$output_restructed_filename 2> /dev/null
        test_status "Setup. Audio codec: ${audio_codec}. Muxing to avi with silence reconstruction"

    done
}

next_step()
{
    STEP_NUM=$(($STEP_NUM + 1))
}

teardown()
{
    rm -rf $SANDBOX_PATH
}

test_start "avi remuxing tests [$(realpath $0)]"
setup
for audio_codec in $AUDIO_CODECS
do
    input_filename="h264_${audio_codec}_source.avpkt"
    input_cut_filename="h264_${audio_codec}_cut.avpkt"

    output_filename="h264_${audio_codec}_original.avi"
    output_restructed_filename="h264_${audio_codec}_restructed.avi"

    input_video_info="h264_${audio_codec}_source_video.dat"
    input_video_packets="h264_${audio_codec}_source_video_packets.dat"

    input_audio_info="h264_${audio_codec}_source_audio.dat"
    input_audio_packets="h264_${audio_codec}_source_audio_packets.dat"

    output_video_info="h264_${audio_codec}_original_video.dat"
    output_video_packets="h264_${audio_codec}_restructed_video_packets.dat"

    output_audio_info="h264_${audio_codec}_original_audio.dat"
    output_audio_packets="h264_${audio_codec}_restructed_audio_packets.dat"

    next_step
    dump_stream_data $LOCAL_TEST_PATH/$input_filename $SANDBOX_PATH/$input_video_info "v"
    dump_stream_data $SANDBOX_PATH/$output_filename $SANDBOX_PATH/$output_video_info "v"
    [ -s $SANDBOX_PATH/$input_video_info ] && [ -s $SANDBOX_PATH/$output_video_info ] && diff -q $SANDBOX_PATH/$input_video_info $SANDBOX_PATH/$output_video_info
    test_status "Step ${STEP_NUM}. Audio codec: ${audio_codec}. Checking video data after remuxing"

    next_step
    dump_stream_data $LOCAL_TEST_PATH/$input_filename $SANDBOX_PATH/$input_audio_info "a"
    dump_stream_data $SANDBOX_PATH/$output_filename $SANDBOX_PATH/$output_audio_info "a"
    [ -s $SANDBOX_PATH/$input_audio_info ] && [ -s $SANDBOX_PATH/$output_audio_info ] && diff -q $SANDBOX_PATH/$input_audio_info $SANDBOX_PATH/$output_audio_info
    test_status "Step ${STEP_NUM}. Audio codec: ${audio_codec}. Checking audio data after remuxing."

    next_step
    dump_stream_packets $SANDBOX_PATH/$output_filename $SANDBOX_PATH/$input_video_packets "v"
    dump_stream_packets $SANDBOX_PATH/$output_restructed_filename $SANDBOX_PATH/$output_video_packets "v"
    [ -s $SANDBOX_PATH/$input_video_packets ] && [ -s $SANDBOX_PATH/$output_video_packets ] && diff -q $SANDBOX_PATH/$input_video_packets $SANDBOX_PATH/$output_video_packets
    test_status "Step ${STEP_NUM}. Audio codec: ${audio_codec}. Checking video data after remuxing with silence"

    next_step
    dump_stream_packets $SANDBOX_PATH/$output_filename $SANDBOX_PATH/$input_audio_packets "a"
    dump_stream_packets $SANDBOX_PATH/$output_restructed_filename $SANDBOX_PATH/$output_audio_packets "a"
    [ -s $SANDBOX_PATH/$input_audio_packets ] && [ -s $SANDBOX_PATH/$output_audio_packets ] && diff -q $SANDBOX_PATH/$input_auido_packets $SANDBOX_PATH/$output_audio_packets
    test_status "Step ${STEP_NUM}. Audio codec: ${audio_codec}. Checking audio data after remuxing with silence"
done
teardown
test_done
