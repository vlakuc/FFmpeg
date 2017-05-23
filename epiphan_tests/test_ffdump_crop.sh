#!/bin/sh


. /etc/tests/common.inc


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
        [ "$RESULT" -eq "$NEEDED_RESULT" ]; test_status "$CROP (${NEEDED_RESULT} => ${RESULT})"
    fi
done < test_data/test_croppings.txt

test_done

