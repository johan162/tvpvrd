AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -pthread -pedantic -Wpointer-arith -Wstrict-prototypes -Wall -Werror -Wextra -Wformat=2 -D_FORTIFY_SOURCE=2 `xml2-config --cflags`
bin_PROGRAMS = tvpvrd
SUBDIRS = . tvpowerd

if have_iniparser
tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c lockfile.c
noinst_HEADERS = freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h lockfile.h
else
tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c lockfile.c iniparser/iniparser.c iniparser/dictionary.c
noinst_HEADERS = freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h lockfile.h iniparser/iniparser.h iniparser/dictionary.h
endif
tvpvrd_LDFLAGS =  `xml2-config --libs`
EXTRA_DIST = tvpvrd.conf tvpvrd.css tvpvrd_mobile.css 


DISTCLEANFILES = config.h .deps

CLEANFILES = *~

install-data-local: 
	install -d ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd.conf ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd.css ${DESTDIR}$(sysconfdir)/tvpvrd
	cp $(abs_srcdir)/tvpvrd_mobile.css ${DESTDIR}$(sysconfdir)/tvpvrd



