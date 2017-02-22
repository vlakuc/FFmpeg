#!/bin/sh


. /etc/tests/common.inc

FAILED=""

FFDUMP=ffdump

test_start "ffdump crop unit tests [$(realpath $0)]"

# Test cropping
while read line
do
    CROP=`echo $line | cut -d' ' -f1`
    NEEDED_RESULT=`echo $line | tr -s ' ' | cut -d' ' -f2`

    if [ -n "$CROP" -a -n "$NEEDED_RESULT" ]
    then
        RESULT=`$FFDUMP -c -r -d --crop $CROP test_data/checker_640x480.jpg | cut -f 3 | cut -d ':' -f 2 | tr -d ' '`

        if [ "$RESULT" -ne "$NEEDED_RESULT" ]
        then
            echo "FAILED cropping $CROP : got $RESULT, needed $NEEDED_RESULT"
            FAILED=1
        else
            echo "PASSED cropping $CROP"
        fi
    fi
done < test_data/test_croppings.txt

[ -z "$FAILED" ]
test_status "ffdump crop test"

test_done

