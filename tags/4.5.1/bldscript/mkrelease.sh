#!/bin/sh
# $Id$
# Utility script to prepare a new release
echo "Creating new ChangeLog ..."
bldscript/mkcl.sh
echo "Running autoreconf ..."
autoreconf
if test "$?" = 0; then
	echo "Running stdconfig ..."
		bldscript/stdconfig.sh > /dev/null
	if test "$?" = 0; then
		make -j 4 -s distcheck 
	else
		echo "Error running config !"
	fi
else
	echo "Error running autoreconf !"
fi

