#!/bin/bash

writefile=$1
writestr=$2


if [ -z "$writefile" ] || [ -z "$writestr" ]
then
	echo "missing argument(s)"
    	exit 1
fi

# Arguments present
#Creates a new file with name and path writefile with content writestr, overwriting any existing file and creating the path if it doesn’t exist. Exits with value 1 and error print statement if the file could not be created.i


if ! { mkdir -p "$(dirname -- "$writefile")" && echo "$writestr"  > "$writefile" ; }
then
	echo "File could not be created"
	exit 1
fi	
