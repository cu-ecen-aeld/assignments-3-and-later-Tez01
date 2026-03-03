#!/bin/sh
filesdir=$1
searchstr=$2

if [ -z "$1" ] || [ -z "$2" ]
then
	echo "missing argument(s)"
    	exit 1
fi

if ! [ -d "$1" ]
then
	echo "$1 not a directory"
	exit 1
fi

numfiles=$(grep -r "$searchstr" "$filesdir" 2>/dev/null | wc -l)	# Sending stdout to null to prevent printing anything
numlines=$(grep -rl "$searchstr" "$filesdir" 2>/dev/null | wc -l)

echo "The number of files are $numfiles and the number of matching lines are $numlines"
