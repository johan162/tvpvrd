AUTOMAKE_OPTIONS = gnu
SUBDIRS = . src scripts docs
EXTRA_DIST = profiles/*.profile profiles.oldffmpeg/*.profile

if have_old_ffmpeg

install-data-local: 
	install -d ${DESTDIR}$(sysconfdir)/tvpvrd/profiles
	cp $(abs_top_srcdir)/profiles.oldffmpeg/* ${DESTDIR}$(sysconfdir)/tvpvrd/profiles

else

install-data-local:
	install -d ${DESTDIR}$(sysconfdir)/tvpvrd
	cp -r $(abs_top_srcdir)/profiles/ ${DESTDIR}$(sysconfdir)/tvpvrd

endif