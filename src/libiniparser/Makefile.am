# ===============================================================================
# $Id$
# Automake build script. This will build our own copy of the iniparser library
# for systems who doesn't have a system version.
# ===============================================================================
AM_CFLAGS = -DCONFDIR="\"$(sysconfdir)\"" -pedantic -Wall -Werror -Wextra -Wpointer-arith -Wstrict-prototypes -D_FORTIFY_SOURCE=2

noinst_LIBRARIES = libiniparser.a
libiniparser_a_SOURCES = dictionary.c iniparser.c dictionary.h iniparser.h

EXTRA_DIST = README LICENSE INSTALL

CLEANFILES = *~

DISTCLEANFILES=*.Po






