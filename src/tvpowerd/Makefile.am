# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -pedantic -Wextra -Wpointer-arith -Wstrict-prototypes -D_FORTIFY_SOURCE=2
noinst_PROGRAMS = tvpowerd

if have_iniparser
tvpowerd_SOURCES = tvpowerd.c wakelan.c tvpowerd.h wakelan.h
tvpowerd_LDADD = ../utils.o ../lockfile.o
else
tvpowerd_SOURCES = tvpowerd.c wakelan.c tvpowerd.h wakelan.h
tvpowerd_LDADD = ../utils.o ../lockfile.o ../iniparser.o ../dictionary.o
endif
tvpowerd_LDFLAGS =  -lm

EXTRA_DIST = tvpowerd.conf pre-shutdown.sh post-startup.sh README install.sh.in uninstall.sh.in init.d.tvpowerd deb.init.d.tvpowerd 

CLEANFILES = *~ tvpowerd

DISTCLEANFILES=*.Po .deps install.sh uninstall.sh







