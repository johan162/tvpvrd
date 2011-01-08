# ===============================================================================
# $Id$
# Automake build script. This will create the utility library used to handle
# SMTP mail sending.
# ===============================================================================
AM_CFLAGS = -DCONFDIR="\"$(sysconfdir)\"" -pedantic -Wall -Werror -Wextra -Wpointer-arith -Wstrict-prototypes -D_FORTIFY_SOURCE=2

noinst_LIBRARIES = libsmtpmail.a
libsmtpmail_a_SOURCES = base64ed.c mailclientlib.c quotprinted.c base64ed.h mailclientlib.h quotprinted.h

EXTRA_DIST = README

CLEANFILES = *~

DISTCLEANFILES=*.Po






