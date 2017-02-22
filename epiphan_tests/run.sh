#!/bin/sh

. ${TESTS_COMMON}/etc/tests/common.inc


test_start "ffmpeg unit tests"
./ffmpeg_tests

./test_ffcopy.sh

./test_rawpacket.sh

./test_ffdump.sh

./test_ffdump_crop.sh

./test_ffdump_lipsync.sh

./test_audio_level_enc.sh

./test_avi.sh

./test_avsync_filter.sh
