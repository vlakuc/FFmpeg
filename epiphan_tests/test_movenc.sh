#!/bin/sh

. ${TESTS_COMMON}/etc/tests/common.inc

MKTEMP="mktemp -d -t test_movenc.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

STREAM_SAMPLE_ORIG="./test_data/rawpacket_test_in.mp4"
STREAM_SAMPLE_FRAG_MP4="${SANDBOX_PATH}/out_frag.mp4"
STREAM_SAMPLE_PROG_MP4="${SANDBOX_PATH}/out_prog.mp4"
STREAM_DATA_FILE_ORIG="./test_data/movenc_streaminfo_orig.txt"
STREAM_DATA_FILE_FRAG="${SANDBOX_PATH}/streaminfo_frag.txt"
STREAM_DATA_FILE_PROG="${SANDBOX_PATH}/streaminfo_prog.txt"

teardown()
{
    rm -rf $SANDBOX_PATH
}

trap teardown EXIT

next_step()
{
    STEP_NUM=$(($STEP_NUM + 1))
}

remux_stream()
{
    local in_file="${1}"
    local out_file="${2}"
    local fmt="${3}"
    local opts="${4}"
    ffmpeg -threads 1 -loglevel panic -i $in_file -codec copy $opts -f $fmt $out_file
}

dump_stream_data()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    ffprobe -print_format json -show_packets -show_data $stream_filename 2> /dev/null | jq -c '.packets[] |.codec_type, .stream_index, .size, .data' > $out_filename
}

test_start "movenc muxer unit test  [$(realpath $0)]"

next_step
remux_stream $STREAM_SAMPLE_ORIG $STREAM_SAMPLE_FRAG_MP4 "mp4" "-movflags empty_moov+duration_update+separate_moof"
test_status "Step ${STEP_NUM}. Convert test stream from MP4 format to fragmented MP4"

next_step
dump_stream_data $STREAM_SAMPLE_FRAG_MP4 $STREAM_DATA_FILE_FRAG
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE_FRAG
test_status "Step ${STEP_NUM}. Check payload fragmented mp4."

next_step
remux_stream $STREAM_SAMPLE_ORIG $STREAM_SAMPLE_PROG_MP4 "mp4" "-movflags empty_moov+duration_update+separate_moof+build_moov"
test_status "Step ${STEP_NUM}. Convert test stream from MP4 format to progressive MP4"

next_step
dump_stream_data $STREAM_SAMPLE_FRAG_MP4 $STREAM_DATA_FILE_PROG
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE_PROG
test_status "Step ${STEP_NUM}. Check payload for progressive mp4."
