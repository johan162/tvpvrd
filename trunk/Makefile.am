AUTOMAKE_OPTIONS = gnu
SUBDIRS = . src scripts docs
EXTRA_DIST = profiles/*.profile

install-data-local: 
	install -d ${DESTDIR}$(sysconfdir)/tvpvrd
	cp -r $(abs_top_srcdir)/profiles/ ${DESTDIR}$(sysconfdir)/tvpvrd
