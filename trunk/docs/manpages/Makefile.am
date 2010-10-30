CLEANFILES = tvpvrd.1.html tvpvrd.1.pdf tvpvrd.1 tvpvrd.fo *~
man_MANS = tvpvrd.1
EXTRA_DIST = tvpvrd.1 tvpvrd.1.xml tvpvrd.1.html tvpvrd.1.pdf

all: tvpvrd.1 tvpvrd.1.html 

if have_xsltproc
tvpvrd.1.html: tvpvrd.1.xml
	${XSLTPROC} --output tvpvrd.1.html ${DOCBOOK_XSL}html/docbook.xsl $<

tvpvrd.1: tvpvrd.1.xml
	${XSLTPROC} ${DOCBOOK_XSL}manpages/docbook.xsl $<

tvpvrd.1.pdf: tvpvrd.1.xml
	${XSLTPROC} --nonet --output tvpvrd.fo \
		--stringparam paper.type A4 \
		--stringparam body.start.indent 0pt \
		--stringparam title.margin.left 0pt \
		--stringparam variablelist.as.blocks 1 \
		${DOCBOOK_XSL}fo/docbook.xsl $<
	fop tvpvrd.fo $@

install-data-local: 
	install -d ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	install  $(abs_top_srcdir)/docs/manpages/tvpvrd.1.html ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	install  $(abs_top_srcdir)/docs/manpages/tvpvrd.1.pdf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd

else

tvpvrd.1:
	touch $@
tvpvrd.1.pdf:
	touch $@
tvpvrd.1.html:
	touch $@

install-data-local: 
	install -d ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	install  $(abs_top_srcdir)/docs/manpages/tvpvrd.1.html ${DESTDIR}${prefix}/share/doc/packages/tvpvrd
	install  $(abs_top_srcdir)/docs/manpages/tvpvrd.1.pdf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd

endif




