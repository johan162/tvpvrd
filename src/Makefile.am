# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -pthread -pedantic -Wall -Werror -Wpointer-arith -Wstrict-prototypes -Wextra -Wformat=2 -fstack-protector -D_FORTIFY_SOURCE=2 `xml2-config --cflags`
bin_PROGRAMS = tvpvrd
SUBDIRS = . tvpowerd
BUILDNBR_FILE=buildnumber.txt

if have_iniparser
tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c tvhtml.c lockfile.c freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h tvhtml.h lockfile.h build.h
else
tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c tvwebcmd.c tvhtml.c lockfile.c freqmap.h  recs.h  stats.h  transc.h  tvcmd.h  tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h tvhtml.h lockfile.h build.h iniparser/iniparser.c iniparser/dictionary.c iniparser/iniparser.h iniparser/dictionary.h
endif

tvpvrd_LDFLAGS =  `xml2-config --libs`
tvpvrd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_NUMBER=$$(cat $(BUILDNBR_FILE))"
tvpvrd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_DATE=$$(date +'%Y%m%d')"

EXTRA_DIST = etc/tvpvrd.conf etc/tvpvrd.css etc/tvpvrd_mobile.css etc/switchchannel.sh.template

DISTCLEANFILES = config.h $(BUILDNBR_FILE)

CLEANFILES = *~

install-data-local:
	@if test -f ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf; then \
		echo "Will not overwrite existing tvpvrd.conf file !"; \
	else \
		install -D -m 644 $(abs_srcdir)/etc/tvpvrd.conf ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf; \
	fi
	install -D -m 755 $(abs_srcdir)/etc/switchchannel.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/switchchannel.sh.template; \
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.css
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd_mobile.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd_mobile.css

all: $(BUILDNBR_FILE)

$(BUILDNBR_FILE): $(tvpvrd_OBJECTS)
	@if ! test -f $(BUILDNBR_FILE); then \
		echo 0 > $(BUILDNBR_FILE); \
	fi
	@echo $$(($$(cat $(BUILDNBR_FILE)) + 1)) > $(BUILDNBR_FILE)
