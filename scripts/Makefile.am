# ===============================================================================
# $Id$
# Automake build script
#
# ===============================================================================

SUBDIRS = . pm
EXTRA_DIST = init.d.tvpvrd deb.init.d.tvpvrd deb.init.conf.tvpvrd svn2cl.xsl

if have_debian_initd

if have_upstart_init

install-data-local:
	install -D -m 644  $(top_srcdir)/scripts/deb.init.conf.tvpvrd ${DESTDIR}$(sysconfdir)/init/tvpvrd.conf
	[ ! -d ${DESTDIR}$(sysconfdir)/init.d/ ] && mkdir ${DESTDIR}$(sysconfdir)/init.d
	ln -sf /lib/init/upstart-job ${DESTDIR}$(sysconfdir)/init.d/tvpvrd

uninstall-local:
	rm -rf ${DESTDIR}$(sysconfdir)/init.d/tvpvrd
	rm -rf ${DESTDIR}$(sysconfdir)/init/tvpvrd.conf

else

install-data-local:
	install -D -m 755  $(top_srcdir)/scripts/deb.init.d.tvpvrd ${DESTDIR}$(sysconfdir)/init.d/tvpvrd

uninstall-local:
	rm -rf ${DESTDIR}$(sysconfdir)/init.d/tvpvrd

endif

else

install-data-local:
	install -D -m 755  $(top_srcdir)/scripts/init.d.tvpvrd ${DESTDIR}$(sysconfdir)/init.d/tvpvrd

uninstall-local:
	rm -rf ${DESTDIR}$(sysconfdir)/init.d/tvpvrd

endif
