# ===============================================================================
# $Id$
# Automake build script
#
# ===============================================================================

SUBDIRS = . pm
EXTRA_DIST = init.d.tvpvrd deb.init.d.tvpvrd svn2cl.xsl

if have_debian_initd

install-data-local:
	install -D -m 755  $(top_srcdir)/scripts/deb.init.d.tvpvrd ${DESTDIR}$(sysconfdir)/init.d/tvpvrd
else

install-data-local:
	install -D -m 755  $(top_srcdir)/scripts/init.d.tvpvrd ${DESTDIR}$(sysconfdir)/init.d/tvpvrd

endif

uninstall-local:
	rm -rf ${DESTDIR}$(sysconfdir)/init.d/tvpvrd