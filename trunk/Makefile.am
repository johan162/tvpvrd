# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AUTOMAKE_OPTIONS = gnu
SUBDIRS = . src scripts docs

uninstall-local:
	rm -rf ${DESTDIR}${sysconfdir}/tvpvrd
	rm -rf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	rm -rf ${DESTDIR}${prefix}/share/man/man1/tvpvrd.1*
	rm -rf ${DESTDIR}${prefix}/share/man/man1/tvpsh.1*

DISTCLEANFILES=.dep.inc
