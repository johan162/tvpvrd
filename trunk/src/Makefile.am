AM_CFLAGS = -pthread -pedantic -Wpointer-arith -Wstrict-prototypes -Wall -Werror -D_FORTIFY_SOURCE=2
bin_PROGRAMS = tvpvrd

tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c
tvpvrd_LDFLAGS = -liniparser -lxml2 -lpcre
EXTRA_DIST = tvpvrd.conf
noinst_HEADERS = freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h

DISTCLEANFILES = confpath.h

CLEANFILES = *~


tvpvrsrv.c : confpath.h

transc.c : confpath.h

confpath.h: Makefile.in Makefile
	echo '#define CONFDIR "$(sysconfdir)"' >$@

install-data-local: 
	install -d ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd.conf ${DESTDIR}$(sysconfdir)/tvpvrd



