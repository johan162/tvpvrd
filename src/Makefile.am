SUBDIRS = . tvpowerd
AM_CFLAGS = -pthread -pedantic -Wpointer-arith -Wstrict-prototypes -Wall -Werror -Wextra -Wformat=2 -D_FORTIFY_SOURCE=2 `xml2-config --cflags`
bin_PROGRAMS = tvpvrd

tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c lockfile.c
tvpvrd_LDFLAGS = -lxml2 -lpthread -liniparser -lpcre `xml2-config --libs`
EXTRA_DIST = tvpvrd.conf tvpvrd.css tvpvrd_mobile.css
noinst_HEADERS = freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h lockfile.h

DISTCLEANFILES = confpath.h

CLEANFILES = *~

tvpvrsrv.c : confpath.h

transc.c : confpath.h

confpath.h: Makefile.in Makefile
	echo '#define CONFDIR "$(sysconfdir)"' >$@

install-data-local: 
	install -d ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd.conf ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd.css ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd_mobile.css ${DESTDIR}$(sysconfdir)/tvpvrd



