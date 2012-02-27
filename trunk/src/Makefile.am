# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -rdynamic -ggdb -pthread -pedantic -Wall -Werror -Wpointer-arith -Wstrict-prototypes -Wextra -Wshadow -Wformat=2 -fstack-protector `xml2-config --cflags`  -D_FORTIFY_SOURCE=2
bin_PROGRAMS = tvpvrd

BUILDNBR_FILE=buildnumber.txt
SUBDIRS = libsmtpmail libiniparser . tvpowerd 

#if have_readline
SUBDIRS += shell
#endif

tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c \
vctrl.c tvwebcmd.c tvhtml.c lockfile.c pcretvmalloc.c tvconfig.c tvshutdown.c mailutil.c \
datetimeutil.c xstr.c rkey.c vcard.c tvplog.c tvhistory.c listhtml.c  \
datetimeutil.h pcretvmalloc.h freqmap.h  recs.h  stats.h  transc.h  tvcmd.h rkey.h \
tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebcmd.h tvhtml.h lockfile.h build.h tvconfig.h tvshutdown.h \
mailutil.h xstr.h vcard.h tvplog.h tvhistory.h listhtml.h

tvpvrd_LDFLAGS =  `xml2-config --libs`
tvpvrd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_NUMBER=$$(cat $(BUILDNBR_FILE))"
tvpvrd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_DATE=$$(date +'%Y%m%d')"

if have_iniparser
tvpvrd_LDADD = libsmtpmail/libsmtpmail.a ${LIBPCRE}
tvpvrd_DEPENDENCIES=$(BUILDNBR_FILE) libsmtpmail/libsmtpmail.a
else
tvpvrd_LDADD = libsmtpmail/libsmtpmail.a libiniparser/libiniparser.a ${LIBPCRE}
tvpvrd_DEPENDENCIES=$(BUILDNBR_FILE) libsmtpmail/libsmtpmail.a libiniparser/libiniparser.a
endif

EXTRA_DIST = etc/tvpvrd.conf.full.template etc/tvpvrd.conf.simplified.template \
etc/tvpvrd.css etc/tvpvrd_mobile.css etc/switchchannel.sh.template \
etc/postrec_processing.sh.template etc/posttransc_processing.sh.template etc/shutdown.sh.template \
etc/mail_transcend.txt etc/mail_transcend.html etc/mail_shutdown.html etc/mail_shutdown.txt \
etc/startup.sh.template

DISTCLEANFILES = config.h $(BUILDNBR_FILE)

CLEANFILES = *~

install-data-local:
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd.conf.full.template ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf.full.template;
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd.conf.simplified.template ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.conf.simplified.template;
	install -D -m 755 $(abs_srcdir)/etc/switchchannel.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/switchchannel.sh.template; 
	install -D -m 755 $(abs_srcdir)/etc/postrec_processing.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/postrec_processing.sh.template; 
	install -D -m 755 $(abs_srcdir)/etc/shutdown.sh.template ${DESTDIR}${sysconfdir}/tvpvrd/shutdown.sh.template; 
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd.css 
	install -D -m 644 $(abs_srcdir)/etc/tvpvrd_mobile.css ${DESTDIR}${sysconfdir}/tvpvrd/tvpvrd_mobile.css 
	install -D -m 644 $(abs_srcdir)/etc/mail_transcend.txt ${DESTDIR}${sysconfdir}/tvpvrd/mail_transcend.txt
	install -D -m 644 $(abs_srcdir)/etc/mail_transcend.html ${DESTDIR}${sysconfdir}/tvpvrd/mail_transcend.html
	install -D -m 644 $(abs_srcdir)/etc/mail_shutdown.html ${DESTDIR}${sysconfdir}/tvpvrd/mail_shutdown.html
	install -D -m 644 $(abs_srcdir)/etc/mail_shutdown.txt ${DESTDIR}${sysconfdir}/tvpvrd/mail_shutdown.txt
	install -D -m 644 $(abs_srcdir)/etc/history.xsl ${DESTDIR}${sysconfdir}/tvpvrd/history.xsl


$(BUILDNBR_FILE): $(tvpvrd_OBJECTS)
	@if ! test -f $(BUILDNBR_FILE); then \
		echo 0 > $(BUILDNBR_FILE); \
	fi
	@echo $$(($$(cat $(BUILDNBR_FILE)) + 1)) > $(BUILDNBR_FILE)
