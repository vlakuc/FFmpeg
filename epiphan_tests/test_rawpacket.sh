#!/bin/sh

. ${TESTS_COMMON}/etc/tests/common.inc

MKTEMP="mktemp -d -t test_rawpacket.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

STREAM_SAMPLE_ORIG="./test_data/rawpacket_test_in.mp4"
STREAM_SAMPLE_RAWPKT_V0="./test_data/rawpacket_v0.avpkt"
STREAM_SAMPLE_RAWPKT_V1="./test_data/rawpacket_v1.avpkt"
STREAM_SAMPLE_RAWPKT="${SANDBOX_PATH}/out.avpkt"
STREAM_SAMPLE_MP4="${SANDBOX_PATH}/out.mp4"
STREAM_DATA_FILE_ORIG="${SANDBOX_PATH}/streaminfo_orig.txt"
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

dump_stream_data()
{
    local stream_filename="${1}"
    local out_filename="${2}"
    local stream="${3}"
    ffprobe -print_format json -show_packets -show_data $stream_filename -select_streams $stream 2> /dev/null | jq -c '.packets[] |.codec_type, .stream_index, .size, .data' > $out_filename
}


check_stream_format()
{
    local stream_file="${1}"
    local fmt="${2}"
    local tmp=`ffprobe -print_format json -show_entries format=format_name $stream_file 2> /dev/null | jq -c -M '.format.format_name'`
    tmp="${tmp%\"}"
    result="${tmp#\"}"
    [ $result == "$fmt" ]
}


encode_stream()
{
    local in_file="${1}"
    local out_file="${2}"
    local fmt="${3}"
    ffmpeg -loglevel panic -i $in_file -c:a copy -c:v copy -f $fmt $out_file
}


check_format_version_mismatch()
{
    local in_file="${1}"
    ffprobe $in_file 2>&1| grep "Unsupported rawpacket format version" > /dev/null
}


## Encoder
test_start "rawpacket encoder unit test [$(realpath $0)]"

next_step
encode_stream $STREAM_SAMPLE_ORIG $STREAM_SAMPLE_RAWPKT "rawpacket"
test_status "Step ${STEP_NUM}. Convert test stream from MP4 format to RAW PACKET"

next_step
check_stream_format $STREAM_SAMPLE_RAWPKT "rawpacket"
test_status "Step ${STEP_NUM}. Check stream format"

next_step
dump_stream_data $STREAM_SAMPLE_ORIG $STREAM_DATA_FILE_ORIG "a"
test_status "Step ${STEP_NUM}. Dump MP4 audio track data."

next_step
dump_stream_data $STREAM_SAMPLE_RAWPKT $STREAM_DATA_FILE "a"
test_status "Step ${STEP_NUM}. Dump RAWPACKET audio track data."

next_step
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE
test_status "Step ${STEP_NUM}. Check audio track payload."

next_step
dump_stream_data $STREAM_SAMPLE_ORIG $STREAM_DATA_FILE_ORIG "v"
test_status "Step ${STEP_NUM}. Dump MP4 video track data."

next_step
dump_stream_data $STREAM_SAMPLE_RAWPKT $STREAM_DATA_FILE "v"
test_status "Step ${STEP_NUM}. Dump RAWPACKET video track data."

next_step
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE
test_status "Step ${STEP_NUM}. Check check video track payload."


## Decoder
STEP_NUM=0
test_start "rawpacket decoder unit test [$(realpath $0)]"

next_step
encode_stream $STREAM_SAMPLE_RAWPKT $STREAM_SAMPLE_MP4 "mp4"
test_status "Step ${STEP_NUM}. Convert test stream from RAW PACKET format to MP4"

next_step
check_stream_format $STREAM_SAMPLE_MP4 "mov,mp4,m4a,3gp,3g2,mj2"
test_status "Step ${STEP_NUM}. Check stream format"

next_step
dump_stream_data $STREAM_SAMPLE_RAWPKT $STREAM_DATA_FILE_ORIG "a"
test_status "Step ${STEP_NUM}. Dump RAWPACKET audio track data."

next_step
dump_stream_data $STREAM_SAMPLE_MP4 $STREAM_DATA_FILE "a"
test_status "Step ${STEP_NUM}. Dump MP4 audio track data."

next_step
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE
test_status "Step ${STEP_NUM}. Check audio track payload."

next_step
dump_stream_data $STREAM_SAMPLE_RAWPKT $STREAM_DATA_FILE_ORIG "v"
test_status "Step ${STEP_NUM}. Dump RAWPACKET video track data."

next_step
dump_stream_data $STREAM_SAMPLE_MP4 $STREAM_DATA_FILE "v"
test_status "Step ${STEP_NUM}. Dump MP4 video track data."

next_step
diff -q $STREAM_DATA_FILE_ORIG $STREAM_DATA_FILE
test_status "Step ${STEP_NUM}. Check check video track payload."


## Version check

STEP_NUM=0
test_start "rawpacket format version check [$(realpath $0)]"

next_step
check_format_version_mismatch $STREAM_SAMPLE_RAWPKT_V0
test_status "Step ${STEP_NUM}. Check rawpacket format version 0"

next_step
check_format_version_mismatch $STREAM_SAMPLE_RAWPKT_V1
test_status "Step ${STEP_NUM}. Check rawpacket format version 1"

test_done
