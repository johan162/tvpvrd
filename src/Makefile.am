# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -pthread -pedantic -Wall -Werror -Wpointer-arith -Wstrict-prototypes -Wextra -Wformat=2 -fstack-protector `xml2-config --cflags`
bin_PROGRAMS = tvpvrd

BUILDNBR_FILE=buildnumber.txt
SUBDIRS = libsmtpmail libiniparser . tvpowerd

tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c  vctrl.c \
tvwebcmd.c tvhtml.c lockfile.c pcretvmalloc.c tvconfig.c tvshutdown.c mailutil.c datetimeutil.c xstr.c rkey.c \
datetimeutil.h pcretvmalloc.h freqmap.h  recs.h  stats.h  transc.h  tvcmd.h rkey.h \
tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h tvhtml.h lockfile.h build.h tvconfig.h tvshutdown.h \
mailutil.h xstr.h 

tvpvrd_LDFLAGS =  `xml2-config --libs`
tvpvrd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_NUMBER=$$(cat $(BUILDNBR_FILE))"
tvpvrd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_DATE=$$(date +'%Y%m%d')"

if have_iniparser
tvpvrd_LDADD = libsmtpmail/libsmtpmail.a
tvpvrd_DEPENDENCIES=$(BUILDNBR_FILE) libsmtpmail/libsmtpmail.a
else
tvpvrd_LDADD = libsmtpmail/libsmtpmail.a libiniparser/libiniparser.a
tvpvrd_DEPENDENCIES=$(BUILDNBR_FILE) libsmtpmail/libsmtpmail.a libiniparser/libiniparser.a
endif

EXTRA_DIST = etc/tvpvrd.conf etc/tvpvrd.css etc/tvpvrd_mobile.css etc/switchchannel.sh.template \
etc/postrec_processing.sh.template etc/posttransc_processing.sh.template etc/shutdown.sh.template

DISTCLEANFILES = config.h $(BUILDNBR_FILE)

CLEANFILES = *~

install-data-local:
	@if test -f ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf; then \
		echo "Will not overwrite existing tvpvrd.conf file !"; \
	else \
		install -D -m 644 $(abs_srcdir)/etc/tvpvrd.conf ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf; \
	fi
	install -D -m 755 $(abs_srcdir)/etc/switchchannel.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/switchchannel.sh.template; \
	install -D -m 755 $(abs_srcdir)/etc/postrec_processing.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/postrec_processing.sh.template; \
	install -D -m 755 $(abs_srcdir)/etc/shutdown.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/shutdown.sh.template; \
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.css
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd_mobile.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd_mobile.css


$(BUILDNBR_FILE): $(tvpvrd_OBJECTS)
	@if ! test -f $(BUILDNBR_FILE); then \
		echo 0 > $(BUILDNBR_FILE); \
	fi
	@echo $$(($$(cat $(BUILDNBR_FILE)) + 1)) > $(BUILDNBR_FILE)
