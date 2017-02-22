#!/bin/sh

. /etc/tests/common.inc

MKTEMP="mktemp -d -t test_remux_lipsync.XXXXXX"
SANDBOX_PATH="`$MKTEMP`"

DUMP_SAMPLE=$SANDBOX_PATH/dump.txt
DUMP_ORIG=./test_data/expected/lipsync_black_white.txt
LIPSYNC_IN=./test_data/black_white.mp4
AUDIO_CODECS="copy aac mp3 pcm_s16le"
CONTAINERS="avi mov_f mp4_p mp4_f"

check_lipsync()
{
    local fname="${1}"
    local fout=$SANDBOX_PATH/lipsync.txt
    # run ffdump with lipsync option, remove 1110 values
    ffdump -c -r -d --ls-tracks 1,2 --crop 20x85/5x5 $fname 2> /dev/nul | cut -d":" -f4 | grep -v "1110" | sort -u > $fout

    [ -s $fout ]
    test_status "Has audio"

    # remove values that less than abs(0.1)
    local val=`grep -v "0.0" $fout`
    [ -z "$val" -a -s $fout ]
    test_status "Lipsync"
}

remux()
{
    local acodec="${1}"
    local cont="${2}"
    local fname=${cont}_${acodec}
    case $cont in
	avi)
	    flags=""
	    fname=${fname}.avi
	    ;;
	mov_f)
	    flags="-f mov -frag_duration 10000000 -movflags -empty_moov+separate_moof+build_moov"
	    fname=${fname}.mov
	    ;;
	mp4_p)
	    flags="-f mp4 -frag_duration 10000000 -movflags +empty_moov+separate_moof+duration_update+build_moov"
	    fname=${fname}.mp4
	    ;;
	mp4_f)
	    flags="-f mp4 -frag_duration 10000000 -movflags +empty_moov+separate_moof+duration_update"
	    fname=${fname}.mp4
	    ;;

	*)
	   flags=""
	   fname=${fname}.err
	    ;;
    esac

    ffmpeg -i $LIPSYNC_IN $flags -c:a $acodec -c:v copy $SANDBOX_PATH/$fname >& /dev/null

    [ -s $SANDBOX_PATH/$fname ]
    test_status "Remuxing [$acodec] [$cont]"

    check_lipsync $SANDBOX_PATH/$fname
}

run()
{
    for audio_codec in $AUDIO_CODECS
    do
	for container in $CONTAINERS
	do
	    remux $audio_codec $container
	done
    done
}

teardown()
{
    rm -rf $SANDBOX_PATH
}

trap teardown EXIT

test_start "ffdump lipsync unit test [$(realpath $0)]"

run

test_done


