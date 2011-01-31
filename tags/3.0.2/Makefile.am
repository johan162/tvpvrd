# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AUTOMAKE_OPTIONS = gnu
SUBDIRS = . src scripts docs
EXTRA_DIST = stdconfig.sh stdbuild.sh profiles/high.profile  profiles/low.profile profiles/normal.profile profiles/thigh.profile profiles/tlow.profile profiles/tnormal.profile profiles.oldffmpeg/high.profile  profiles.oldffmpeg/low.profile profiles.oldffmpeg/normal.profile profiles.oldffmpeg/thigh.profile profiles.oldffmpeg/tlow.profile profiles.oldffmpeg/tnormal.profile

# ===============================================================================
# In autoconf.ac we define a makefile variable have_old_ffmpeg in case the confirguration have found
# an old style ffmpeg executable in the system. The modern version of ffmpeg have deprecated some
# arguments and changed hte plain profiles that are shipped whithe the distribution.
# ===============================================================================
if have_old_ffmpeg

install-data-local: 
	install -m 755 -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles
	install -m 644  $(abs_top_srcdir)/profiles.oldffmpeg/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles

else

install-data-local:
	install -m 755 -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles
	install -m 644 $(abs_top_srcdir)/profiles/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles

endif

uninstall-local:
	rm -rf ${DESTDIR}${sysconfdir}/tvpvrd
	rm -rf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	rm -rf ${DESTDIR}${prefix}/share/man/man1/tvpvrd.1*

DISTCLEANFILES=.dep.inc
