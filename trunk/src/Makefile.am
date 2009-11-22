AM_CFLAGS = -pthread -pedantic -Wpointer-arith -Wstrict-prototypes -Wall -Werror 
bin_PROGRAMS = tvpvrd

tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c 
tvpvrd_LDFLAGS = -liniparser -lxml2 -lpcre

noinst_HEADERS = freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h

DISTCLEANFILES = confpath.h

CLEANFILES = *~


tvpvrsrv.c : confpath.h

confpath.h: Makefile
	echo '#define CONFDIR "$(sysconfdir)"' >$@

install-data-local: 
	mkdir $(sysconfdir)/tvpvrd
	cp -r $(srcdir)/profiles $(sysconfdir)/tvpvrd
	cp $(srcdir)/src/tvpvrd.conf $(sysconfdir)/tvpvrd

# man page
# man_MANS = tvpvrd.1x
