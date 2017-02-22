#!/bin/sh

. /etc/tests/common.inc

MKTEMP="mktemp -d -t test_ffdump.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

DUMP_INFO_SAMPLE=$SANDBOX_PATH/info.txt
DUMP_PACKETS_SAMPLE=$SANDBOX_PATH/packets.txt
DUMP_INFO_ORIG=./test_data/expected/ffdump_test_info.txt
DUMP_PACKETS_ORIG=./test_data/expected/ffdump_test_packets.txt


setup()
{
    ffdump test_data/ffdump_in.avi > $DUMP_INFO_SAMPLE
    test_status "Setup. Stream info file created"

    ffdump -c test_data/ffdump_in.avi > $DUMP_PACKETS_SAMPLE
    test_status "Setup. Stream packets dump created"
}

teardown()
{
    rm -rf $SANDBOX_PATH
}

trap teardown EXIT

test_start "ffdump unit tests [$(realpath $0)]"

setup


diff -q $DUMP_INFO_ORIG $DUMP_INFO_SAMPLE

test_status "Step 1. Check stream info"

diff -q $DUMP_PACKETS_ORIG $DUMP_PACKETS_SAMPLE

test_status "Step 2. Check packet dump"

test_done


