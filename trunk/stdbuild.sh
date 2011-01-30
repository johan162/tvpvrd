#!/bin/sh
# $Id$
# Utility script to do a standard build
./stdconfig.sh
if test "$?" = 0; then
    make -j4 -s
else
    echo "Configuration failed. Cannot build package."
fi

