#!/bin/sh
#
# Clean all files that autotools have generated
# They can be re-created by running bootstrap.sh
#
rm -f configure aclocal.m4 ar-lib config.guess config.rpath config.sub depcomp install-sh missing Makefile.in
rm -rf autom4te.cache
