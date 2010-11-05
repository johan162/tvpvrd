#!/bin/sh
# $Id$
# Bootstrap sequence to create initial build environment
aclocal
autoheader
autoconf
svn log --xml -v -rHEAD:1 | xsltproc --stringparam strip-prefix "trunk/" scripts/svn2cl.xsl - > ChangeLog
automake -a -c

