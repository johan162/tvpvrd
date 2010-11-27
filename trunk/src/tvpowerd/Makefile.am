# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR="\"$(sysconfdir)\"" -pedantic -Wall -Werror -Wextra -Wpointer-arith -Wstrict-prototypes -D_FORTIFY_SOURCE=2
noinst_PROGRAMS = tvpowerd

if have_iniparser
tvpowerd_SOURCES = tvpowerd.c wakelan.c tvpowerd.h wakelan.h
tvpowerd_LDADD = ../utils.o ../lockfile.o
else
tvpowerd_SOURCES = tvpowerd.c wakelan.c tvpowerd.h wakelan.h
tvpowerd_LDADD = ../utils.o ../lockfile.o ../iniparser.o ../dictionary.o
endif

EXTRA_DIST = tvpowerd.conf pre-shutdown.sh post-startup.sh README install.sh.in uninstall.sh.in init.d.tvpowerd deb.init.d.tvpowerd 

CLEANFILES = *~ tvpowerd *.pid

DISTCLEANFILES=*.Po install.sh uninstall.sh







