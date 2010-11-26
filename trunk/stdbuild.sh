#!/bin/sh
# $Id$
# Utility script to do a standard build
./stdconfig.sh
make -j 4 -s
