#!/bin/sh


# Test stream contains 3 video and 2 audio tracks
# All audio tracks are generated with audacity utility.
# Video track 0:0 contains 2 seconds black picture followed by 2 sec white picture.
# Audio track 0:1 contains 2 sec tone with square wave followed by 2 sec silence.
# Video track 0:2 contains 2 seconds blue picture followed by 2 sec yellow picture.
# Audio track 0:3 contains 2 sec tone with wave of sawtooth form folloed by 2 sec noise signal with low aplitude.
# Video track 0:4 contains dynamic picture generated fith "life" filter with two rectangular areas in top left corner and bottom right corner where 5/5sec blue/white pattern in displayed.

# Command to generate black_white pattern:
# ffmpeg -threads 1 -f lavfi -i color=c=white:size=vga:duration=5:rate=60 -f lavfi -i color=c=black:size=vga:duration=5:rate=60 -i /extra/video/tone.wav -i /extra/video/silence.wav -t 10         -filter_complex "[1:v][3:a][0:v][2:a]concat=2:v=1:a=1[v][a]" -map "[v]" -map "[a]" -c:v libx264 -c:a aac black_white.mp4

# Command to generate blue_yellow pattern:
# ffmpeg -threads 1 -f lavfi -i color=c=yellow:size=qvga:duration=5:rate=60 -f lavfi -i color=c=blue:size=qvga:duration=5:rate=60 -i /extra/video/sawtooth.wav -i /extra/video/quiet_noise.wav -t 10  -filter_complex "[1:v][3:a][0:v][2:a]concat=2:v=1:a=1[v][a]" -map "[v]" -map "[a]" -c:v libx264 -c:a aac yellow_blue.mp4


# Command to generate video ROI track:
# ffmpeg -threads 1 -f lavfi -i life=s=vga:mold=10:r=60:ratio=0.1:death_color=#C83232:life_color=#00ff00,scale=1200:800:flags=16 -f lavfi -i color=c=blue:size=200x200:duration=5:rate=60 -f lavfi -i color=c=white:size=200x200:duration=5:rate=60  -t 10 -filter_complex "[1:0][2:0]concat[out], [out]split[v1][v2], [0:0][v1]overlay=x=10:y=10[out2], [out2][v2]overlay=x=main_w-overlay_w-10:y=main_h-overlay_h-10"  -c:v libx264 video_roi.mp4


# Command to combine all tracks together:
# ffmpeg -i black_white.mp4 -i yellow_blue.mp4 -i video_roi.mp4 -c copy -map 0 -map 1 -map 2 avsync_test.mp4


. ${TESTS_COMMON}/etc/tests/common.inc

setup()
{
    MKTEMP="mktemp -d -t test_avsync.XXXXXX"
    SANDBOX_PATH="`$MKTEMP`"
    AVSYNC_SAMPLE="./test_data/avsync_test.mp4"
    FILTER_OUTPUT="${SANDBOX_PATH}/avsync.log"

    AVSYNC_MULTITRACK_EXPECTED="./test_data/expected/avsync_multitrack.txt"
    AVSYNC_MASTER_2_EXPECTED="./test_data/expected/avsync_multitrack_m2.txt"
    AVSYNC_AUDIO_AUDIO_EXPECTED="./test_data/expected/avsync_audio_audio.txt"
    AVSYNC_VIDEO_VIDEO_EXPECTED="./test_data/expected/avsync_video_video.txt"
    AVSYNC_VIDEO_ROI_TOP_LEFT_EXPECTED="./test_data/expected/avsync_video_roi_tl.txt"
    AVSYNC_VIDEO_ROI_BOTTOM_RIGHT_EXPECTED="./test_data/expected/avsync_video_roi_br.txt"
}

teardown()
{
    rm -rf $SANDBOX_PATH
}


next_step()
{
    STEP_NUM=$(($STEP_NUM + 1))
}


measure_avsync()
{
    local filter_opts=$1
    ffmpeg -y -threads 1 -i $AVSYNC_SAMPLE -filter_complex "${filter_opts}" -f mp4 /dev/null > /dev/null 2>&1
}


check_metrics()
{
    local expected=$1
    [ $GENERATE_TEST_DATA -eq 1 ] && cat $FILTER_OUTPUT | cut -f 2- > "${SANDBOX_PATH}/`basename $expected`"
    cat $FILTER_OUTPUT | cut -f 2- | diff -q $expected -
}

if ! ffmpeg -hide_banner -filters | grep -q -w avsync; then
    test_start "AVSync filter"
    test_ignored "avsync filter is not available"
    test_done
    exit 0
fi


setup


GENERATE_TEST_DATA=0

for i in "$@"
do
    case $i in
        --generate)
            GENERATE_TEST_DATA=1
            echo "Generated test expected results are placed in ${SANDBOX_PATH}"
        ;;
        *)
        ;;
    esac
done
        
[ $GENERATE_TEST_DATA -eq 0 ] && trap teardown EXIT

test_start "AVSync filter unittest [$(realpath $0)]"
next_step
measure_avsync "avsync=v=2:a=2:o=${FILTER_OUTPUT}:t=20"
test_status "Step ${STEP_NUM}. Compute avsync metrics for 4 tracks (2 audio / 2 video). Master track is 0 (video)."

next_step
check_metrics $AVSYNC_MULTITRACK_EXPECTED
test_status "Step ${STEP_NUM}. Check avsync metrics."

next_step
measure_avsync "avsync=v=2:a=2:o=${FILTER_OUTPUT}:t=20:m=2"
test_status "Step ${STEP_NUM}. Compute avsync metrics for 4 tracks (2 audio / 2 video). Master track is 2 (audio)."

next_step
check_metrics $AVSYNC_MASTER_2_EXPECTED
test_status "Step ${STEP_NUM}. Check avsync metrics."

next_step
measure_avsync "[0:1][0:3]avsync=v=0:a=2:o=${FILTER_OUTPUT}"
test_status "Step ${STEP_NUM}. Compute avsync metrics for 2 audio tracks."

next_step
check_metrics $AVSYNC_AUDIO_AUDIO_EXPECTED
test_status "Step ${STEP_NUM}. Check avsync metrics."

next_step
measure_avsync "[0:0][0:2]avsync=v=2:t=20:o=${FILTER_OUTPUT}"
test_status "Step ${STEP_NUM}. Compute avsync metrics for 2 video tracks."
next_step
check_metrics $AVSYNC_VIDEO_VIDEO_EXPECTED
test_status "Step ${STEP_NUM}. Check avsync metrics."

next_step
measure_avsync "[0:0][0:4]avsync=v=2:t=20:o=${FILTER_OUTPUT}:r='1@(10,10,100,100)'"
test_status "Step ${STEP_NUM}. Compute avsync metrics for 2 video tracks. Second track contains pattern in top left corner"
next_step
check_metrics $AVSYNC_VIDEO_ROI_TOP_LEFT_EXPECTED
test_status "Step ${STEP_NUM}. Check avsync metrics."

next_step
measure_avsync "[0:0][0:4]avsync=v=2:t=20:o=${FILTER_OUTPUT}:r='1@(540,380,100,100)'"
test_status "Step ${STEP_NUM}. Compute avsync metrics for 2 video tracks. Second track contains pattern in bottom right corner"
next_step
check_metrics $AVSYNC_VIDEO_ROI_BOTTOM_RIGHT_EXPECTED
test_status "Step ${STEP_NUM}. Check avsync metrics."



# Test input parameters validity

next_step
measure_avsync "avsync=v=2:t=-1:r='1@(540,380,100,100)'"
test_status_negative "Step ${STEP_NUM}. Threshold (-1) is out of range"

next_step
measure_avsync "avsync=v=2:t=101:r='1@(540,380,100,100)'"
test_status_negative "Step ${STEP_NUM}. Threshold (101) is out of range"


next_step
measure_avsync "avsync=v=4"
test_status_negative "Step ${STEP_NUM}. Number of video streams is out of range"

next_step
measure_avsync "avsync=a=3"
test_status_negative "Step ${STEP_NUM}. Number of audio streams is out of range"

next_step
measure_avsync "avsync=m=5"
test_status_negative "Step ${STEP_NUM}. Master stream index is out of range"

next_step
measure_avsync "avsync=r='1@(540,380,100,100)'"
test_status_negative "Step ${STEP_NUM}. Video ROI should not be set for audio stream."

next_step
measure_avsync "avsync=r='5@(540,380,100,100)'"
test_status_negative "Step ${STEP_NUM}. Video ROI stream index is out of range."

next_step
measure_avsync "avsync=r='4@(540,380,200,100)'"
test_status_negative "Step ${STEP_NUM}. Video ROI width should not exeed frame width."

next_step
measure_avsync "avsync=r='4@(540,380,100,200)'"
test_status_negative "Step ${STEP_NUM}. Video ROI height should not exeed frame height."

next_step
measure_avsync "avsync=r='4@(840,380,100,100)'"
test_status_negative "Step ${STEP_NUM}. Video ROI x-coord should not exeed frame width."

next_step
measure_avsync "avsync=r='4@(540,580,100,100)'"
test_status_negative "Step ${STEP_NUM}. Video ROI y-coord should not exeed frame width."



test_done
