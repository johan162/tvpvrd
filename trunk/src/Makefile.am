# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -pthread -pedantic -Wpointer-arith -Wstrict-prototypes -Wextra -Wformat=2 -D_FORTIFY_SOURCE=2 `xml2-config --cflags`
bin_PROGRAMS = tvpvrd
SUBDIRS = . tvpowerd

if have_iniparser
tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c lockfile.c freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h lockfile.h
else
tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c lockfile.c iniparser/iniparser.c iniparser/dictionary.c freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h lockfile.h iniparser/iniparser.h iniparser/dictionary.h
endif
tvpvrd_LDFLAGS =  `xml2-config --libs`

EXTRA_DIST = tvpvrd.conf tvpvrd.css tvpvrd_mobile.css switchchannel.sh.template

DISTCLEANFILES = config.h .deps

CLEANFILES = *~

install-data-local: 
	if test -f ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf; then \
		echo "Will not overwrite existing tvpvrd.conf file !"; \
	else \
		install -D -m 644 $(abs_srcdir)/tvpvrd.conf ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf; \
	fi
	if test -f ${DESTDIR}${sysconfdir}/tvpvrd/switchchannel.sh.template; then \
		echo "Will not overwrite existing switchchannel.sh.template file !"; \
	else \
		install -D -m 755 $(abs_srcdir)/switchchannel.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/switchchannel.sh.template; \
	fi

	install -b -m 644 $(abs_srcdir)/tvpvrd.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.css
	install -b -m 644 $(abs_srcdir)/tvpvrd_mobile.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd_mobile.css



