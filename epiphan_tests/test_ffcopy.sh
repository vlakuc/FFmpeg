#!/bin/sh

. ${TESTS_COMMON}/etc/tests/common.inc

MKTEMP="mktemp -d -t test_ffcopy.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

TRACK_LIST=$SANDBOX_PATH/tracks.txt
STREAM_SAMPLE=$SANDBOX_PATH/sample.mp4
STREAM_SAMPLE_ORIG=./test_data/expected/ffcopy_out.mp4

STREAM_INFO_ORIG=$SANDBOX_PATH/info_orig.txt
STREAM_INFO_NEW=$SANDBOX_PATH/info_new.txt

STREAM_AUDIO_DATA_ORIG=$SANDBOX_PATH/orig_audio.dat
STREAM_AUDIO_DATA=$SANDBOX_PATH/sample_audio.dat

STREAM_VIDEO_DATA_ORIG=$SANDBOX_PATH/orig_video.dat
STREAM_VIDEO_DATA=$SANDBOX_PATH/sample_video.dat

setup()
{
    ffcopy -show_tracks test_data/ffcopy_in.mp4 > $TRACK_LIST
    test_status "Setup. Track file created"

    ffcopy -no_progress -tracks 2,3 test_data/ffcopy_in.mp4 $STREAM_SAMPLE
    test_status "Setup. Stream sample created"

    dump_stream_info $STREAM_SAMPLE_ORIG $STREAM_INFO_ORIG
    test_status "Setup. Expected stream info  created"

    dump_stream_info $STREAM_SAMPLE $STREAM_INFO_NEW
    test_status "Setup. Stream info  created"

    dump_stream_data $STREAM_SAMPLE $STREAM_AUDIO_DATA "a" && \
        dump_stream_data $STREAM_SAMPLE_ORIG $STREAM_AUDIO_DATA_ORIG "a" && \
        dump_stream_data $STREAM_SAMPLE $STREAM_VIDEO_DATA "v" && \
        dump_stream_data $STREAM_SAMPLE_ORIG $STREAM_VIDEO_DATA_ORIG "v"
    test_status "Setup. Stream data files created"

}

teardown()
{
    rm -rf $SANDBOX_PATH
}

trap teardown EXIT

dump_stream_info()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    local entries=format=nb_streams,format_name,format_long_name,starting_time,duration:stream
    local ignore_tags=".streams[].tags.creation_time, .streams[].coded_width, .streams[].coded_height,"
    ignore_tags="${ignore_tags} .streams[].chroma_location, .streams[].refs, .streams[].is_avc,"
    ignore_tags="${ignore_tags} .streams[].nal_length_size, .streams[].profile, .streams[].max_bit_rate"
    ffprobe -print_format json -show_entries $entries $stream_filename 2> /dev/null | \
        jq -M "del(${ignore_tags})" > $out_filename
}


dump_stream_data()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    local stream="${3}"
    ffprobe -print_format json -show_packets -show_data $stream_filename -select_streams $stream 2> /dev/null | \
        jq -c '.packets[] |.codec_type, .stream_index, .size, .data' > $out_filename
}


test_start "ffcopy unit tests [$(realpath $0)]"

setup

diff -q ./test_data/expected/ffcopy_tracks.txt $TRACK_LIST
test_status "Step 1. Check track list"


diff -q $STREAM_INFO_ORIG $STREAM_INFO_NEW
test_status "Step 2. Check sample info"


diff -q $STREAM_AUDIO_DATA $STREAM_AUDIO_DATA_ORIG && \
    diff -q $STREAM_VIDEO_DATA $STREAM_VIDEO_DATA_ORIG
test_status "Step 3. Check bit exact"
    
test_done
