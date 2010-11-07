#!/bin/sh
# $Id$
# Bootstrap sequence to create initial build environment
aclocal
autoheader
autoconf
if test -d .svn; then
    svn log --xml -v -rHEAD:1 | xsltproc --stringparam strip-prefix "trunk/" scripts/svn2cl.xsl - > ChangeLog
fi
automake -a -c

