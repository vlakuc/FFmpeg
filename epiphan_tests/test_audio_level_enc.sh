#!/bin/sh

. ${TESTS_COMMON}/etc/tests/common.inc

MKTEMP="mktemp -d -t test_audio_level_enc.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

STREAM_SAMPLE_ORIG="./test_data/rawpacket_test_in.mp4"
STREAM_SAMPLE_RAWPKT="${SANDBOX_PATH}/out.avpkt"
STREAM_DATA_FILE_ORIG="./test_data/audio_level_enc_streaminfo_orig.txt"
STREAM_DATA_FILE="${SANDBOX_PATH}/streaminfo.txt"

teardown()
{
    rm -rf $SANDBOX_PATH
}

trap teardown EXIT

next_step()
{
    STEP_NUM=$(($STEP_NUM + 1))
}

encode_stream()
{
    local in_file="${1}"
    local out_file="${2}"
    local fmt="${3}"
    ffmpeg -loglevel panic -i $in_file -c:a audio_level -c:v copy -f $fmt $out_file
}

dump_stream_data()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    local stream="${3}"
    ffprobe -print_format json -show_packets -show_data $stream_filename -select_streams $stream 2> /dev/null | jq -c '.packets[] |.codec_type, .stream_index, .size, .data' > $out_filename
}

## Encoder
test_start "audio_level encoder unit test  [$(realpath $0)]"

next_step
encode_stream $STREAM_SAMPLE_ORIG $STREAM_SAMPLE_RAWPKT "rawpacket"
test_status "Step ${STEP_NUM}. Convert test stream from MP4 format to RAW PACKET with audio_level_enc audio track"

next_step
dump_stream_data $STREAM_SAMPLE_RAWPKT $STREAM_DATA_FILE "a"
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE
test_status "Step ${STEP_NUM}. Check audio track payload."
