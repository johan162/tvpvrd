AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -pedantic -Wpointer-arith -Wstrict-prototypes -Wall -Werror -Wextra -Wformat=2 -D_FORTIFY_SOURCE=2 
EXTRA_PROGRAMS = tvpowerd

if have_iniparser
tvpowerd_SOURCES = tvpowerd.c wakelan.c 
tvpowerd_LDADD = ../utils.o ../lockfile.o
else
tvpowerd_SOURCES = tvpowerd.c wakelan.c 
tvpowerd_LDADD = ../utils.o ../lockfile.o ../iniparser.o ../dictionary.o
endif
tvpowerd_LDFLAGS =  -lm

EXTRA_DIST = tvpowerd.conf 
noinst_HEADERS = tvpowerd.h wakelan.h

CLEANFILES = *~ tvpowerd

DISTCLEANFILES=*.Po .deps

all: tvpowerd






