# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR='"$(sysconfdir)"' -rdynamic -ggdb -pthread -pedantic -Wall -Werror -Werror=format -Wpointer-arith -Wstrict-prototypes -Wextra -Wshadow -Wformat=2 -fstack-protector `xml2-config --cflags`  -D_FORTIFY_SOURCE=2
bin_PROGRAMS = tvpvrd

BUILDNBR_FILE=buildnumber.txt
SUBDIRS = libsmtpmail libiniparser . tvpowerd shell

tvpvrd_SOURCES = freqmap.c  recs.c  stats.c  transc.c  tvcmd.c  tvpvrsrv.c  tvxmldb.c  utils.c \
vctrl.c tvwebui.c tvhtml.c lockfile.c pcretvmalloc.c tvconfig.c tvshutdown.c mailutil.c \
datetimeutil.c xstr.c rkey.c vcard.c tvplog.c tvhistory.c listhtml.c transcprofile.c \
futils.c httpreq.c tvwebcmd.c \
datetimeutil.h pcretvmalloc.h freqmap.h  recs.h  stats.h  transc.h  tvcmd.h rkey.h \
tvpvrd.h  tvxmldb.h  utils.h  vctrl.h tvwebui.h tvhtml.h lockfile.h build.h tvconfig.h tvshutdown.h \
mailutil.h xstr.h vcard.h tvplog.h tvhistory.h listhtml.h transcprofile.h \
futils.h httpreq.h tvwebcmd.h

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

EXTRA_DIST = etc/README etc/tvpvrd.conf.full.template.in etc/tvpvrd.conf.simplified.template.in \
etc/themes/tvpvrd_mobile.css \
etc/themes/tvpvrd_plain.css etc/themes/tvpvrd_metal.css etc/themes/tvpvrd_hq.css etc/themes/tvpvrd_night.css etc/themes/tvpvrd_deep.css etc/themes/tvpvrd_grey.css \
etc/img/logout_button.jpg \
etc/img/bg_metal.jpg etc/img/bgcmd_metal_off.jpg etc/img/bgcmd_metal_on.jpg \
etc/img/bgtbar_deep.jpg \
etc/img/favicon.ico \
etc/templates/postrec_processing.sh.template \
etc/templates/posttransc_processing.sh.template \
etc/templates/shutdown.sh.template \
etc/templates/startup.sh.template \
etc/templates/switchchannel.sh.template  \
etc/mail_templates/mail_shutdown.html etc/mail_templates/mail_shutdown.txt \
etc/mail_templates/mail_transcend.html etc/mail_templates/mail_transcend.txt \
etc/xsl/history.xsl \
etc/profiles/high.profile etc/profiles/low.profile etc/profiles/normal.profile etc/profiles/thigh.profile etc/profiles/tlow.profile etc/profiles/tnormal.profile etc/profiles-avconv/high.profile etc/profiles-avconv/low.profile etc/profiles-avconv/normal.profile etc/profiles-avconv/thigh.profile etc/profiles-avconv/tlow.profile etc/profiles-avconv/tnormal.profile


DISTCLEANFILES = config.h etc/tvpvrd.conf.full.template etc/tvpvrd.conf.simplified.template $(BUILDNBR_FILE)

CLEANFILES = *~

install-data-local:
	install -m 755 -d ${DESTDIR}${sysconfdir}/tvpvrd/shellscript
	install -m 755 -d ${DESTDIR}${sysconfdir}/tvpvrd/www
	install -m 755 -d ${DESTDIR}${sysconfdir}/tvpvrd/www/img
	install -m 755 -d ${DESTDIR}${sysconfdir}/tvpvrd/mail_templates
	install -m 755 -d ${DESTDIR}${sysconfdir}/tvpvrd/xsl
	install -m 755 -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles
	install -m 755 -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles-avconv	
	install -D -m 755 $(abs_srcdir)/etc/templates/* ${DESTDIR}${sysconfdir}/tvpvrd/shellscript/
	install -D -m 644 $(abs_srcdir)/etc/themes/* ${DESTDIR}${sysconfdir}/tvpvrd/www/
	install -D -m 644 $(abs_srcdir)/etc/img/*.jpg ${DESTDIR}${sysconfdir}/tvpvrd/www/img/
	install -D -m 644 $(abs_srcdir)/etc/img/favicon.ico ${DESTDIR}${sysconfdir}/tvpvrd/www/
	install -D -m 644 $(abs_srcdir)/etc/mail_templates/* ${DESTDIR}${sysconfdir}/tvpvrd/mail_templates/
	install -D -m 644 $(abs_srcdir)/etc/xsl/* ${DESTDIR}${sysconfdir}/tvpvrd/xsl/
	install -D -m 644 $(builddir)/etc/tvpvrd.conf.simplified.template ${DESTDIR}${sysconfdir}/tvpvrd/
	install -D -m 644 $(builddir)/etc/tvpvrd.conf.full.template ${DESTDIR}${sysconfdir}/tvpvrd/
	install -D -m 644 $(abs_srcdir)/etc/profiles-avconv/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles-avconv
	install -D -m 644 $(abs_srcdir)/etc/profiles/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles
	

$(BUILDNBR_FILE): $(tvpvrd_OBJECTS)
	@if ! test -f $(BUILDNBR_FILE); then \
		echo 0 > $(BUILDNBR_FILE); \
	fi
	@echo $$(($$(cat $(BUILDNBR_FILE)) + 1)) > $(BUILDNBR_FILE)
