EXTRA_DIST = init.d.tvpvrd deb.init.d.tvpvrd svn2cl.xsl

install-data-local: 
	install -D -m 755  $(top_srcdir)/scripts/init.d.tvpvrd ${DESTDIR}$(sysconfdir)/init.d/tvpvrd
    
uninstall-local:
	rm -rf ${DESTDIR}$(sysconfdir)/init.d/tvpvrd