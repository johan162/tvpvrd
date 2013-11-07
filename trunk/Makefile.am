# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AUTOMAKE_OPTIONS = gnu
SUBDIRS = . src scripts docs
EXTRA_DIST = stdconfig.sh stdbuild.sh profiles/high.profile  profiles/low.profile profiles/normal.profile profiles/thigh.profile profiles/tlow.profile profiles/tnormal.profile profiles-avconv/high.profile  profiles-avconv/low.profile profiles-avconv/normal.profile profiles-avconv/thigh.profile profiles-avconv/tlow.profile profiles-avconv/tnormal.profile

install-data-local:
	install -m 755 -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles
	install -m 755 -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles-avconv
	install -m 644 $(abs_top_srcdir)/profiles-avconv/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles-avconv
	install -m 644 $(abs_top_srcdir)/profiles/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles


uninstall-local:
	rm -rf ${DESTDIR}${sysconfdir}/tvpvrd
	rm -rf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	rm -rf ${DESTDIR}${prefix}/share/man/man1/tvpvrd.1*
	rm -rf ${DESTDIR}${prefix}/share/man/man1/tvpsh.1*

DISTCLEANFILES=.dep.inc
