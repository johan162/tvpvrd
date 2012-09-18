# ===============================================================================
# $Id$
# Automake build script
#
# ===============================================================================
EXTRA_DIST = 10tvpvrd README ivtv.config

install-data-local:
	install -D -m 755  $(top_srcdir)/scripts/pm/10tvpvrd ${DESTDIR}/usr/lib/pm-utils/sleep.d/10tvpvrd
	install -D -m 644  $(top_srcdir)/scripts/pm/ivtv.config ${DESTDIR}/etc/pm/config.d/ivtv.config

uninstall-local:
	rm -rf ${DESTDIR}/usr/lib/pm-utils/sleep.d/10tvpvrd
	rm -rf ${DESTDIR}/etc/pm/config.d/ivtv.config
