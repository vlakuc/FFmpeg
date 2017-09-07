#!/bin/sh

. /etc/tests/common.inc

MKTEMP="mktemp -d -t test_ffdump_lipsync.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

DUMP_SAMPLE=$SANDBOX_PATH/dump.txt
DUMP_ORIG=./test_data/expected/lipsync_black_white.txt
LIPSYNC_IN=./test_data/black_white.mp4


setup()
{

    ffdump -c -d -r --ls-tracks 1,2 $LIPSYNC_IN 2>/dev/null | cut -f 2- > $DUMP_SAMPLE

    test_status "Setup. Lipsync dump created"
}

teardown()
{
    rm -rf $SANDBOX_PATH
}

trap teardown EXIT

test_start "ffdump lipsync unit test [$(realpath $0)]"

setup
g

diff -q $DUMP_ORIG $DUMP_SAMPLE

test_status "Step 1. Check lipsync dump"

test_done


