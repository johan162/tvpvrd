AM_CFLAGS = -pedantic -Wpointer-arith -Wstrict-prototypes -Wall -Werror -Wextra -Wformat=2 -D_FORTIFY_SOURCE=2 
bin_PROGRAMS = tvpowerd

tvpowerd_SOURCES = ../utils.c  ../lockfile.c tvpowerd.c wakelan.c
tvpowerd_LDFLAGS = -liniparser -lpcre -lm
EXTRA_DIST = tvpowerd.conf 
noinst_HEADERS = tvpowerd.h wakelan.h

CLEANFILES = *~






