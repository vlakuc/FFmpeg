#!/bin/sh -x

[ -z "$FFDUMP" ] && FFDUMP=build/Debug/ffdump

if [ ! -x "$FFDUMP" ]
then
	echo "FAILED: can't execute '$FFDUMP'"
	exit 1
fi

FAILED=""

# Test cropping
while read line
do
	CROP=`echo $line | cut -d' ' -f1`
	NEEDED_RESULT=`echo $line | cut -d' ' -f2`

	if [ -n "$CROP" -a -n "$NEEDED_RESULT" ]
	then
		RESULT=`$FFDUMP -crd --crop $CROP test_data/checker_640x480.jpg | cut -f 3 | cut -d ':' -f 2 | tr -d ' '`

		if [ "$RESULT" -ne "$NEEDED_RESULT" ]
		then
			echo "FAILED cropping $CROP : got $RESULT, needed $NEEDED_RESULT"
			FAILED=1
		else
			echo "PASSED cropping $CROP"
		fi
	fi
done < test_data/test_croppings.txt

echo "FAILED: $FAILED"

[ ! -z "$FAILED" ] && exit 1

exit 0
