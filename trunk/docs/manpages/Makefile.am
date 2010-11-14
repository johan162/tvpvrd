CLEANFILES = tvpvrd.fo tvpowerd.fo *~
man_MANS = tvpvrd.1 tvpowerd.1
EXTRA_DIST = tvpvrd.1 tvpvrd.1.xml tvpvrd.1.html tvpvrd.1.pdf tvpowerd.1 tvpowerd.1.xml tvpowerd.1.html tvpowerd.1.pdf

if have_xsltproc

tvpvrd.1.html: tvpvrd.1.xml
	${XSLTPROC} --output tvpvrd.1.html ${DOCBOOK_XSL}/html/docbook.xsl $<

tvpvrd.1: tvpvrd.1.xml
	${XSLTPROC} ${DOCBOOK_XSL}/manpages/docbook.xsl $<

tvpowerd.1.html: tvpowerd.1.xml
	${XSLTPROC} --output tvpowerd.1.html ${DOCBOOK_XSL}/html/docbook.xsl $<

tvpowerd.1: tvpowerd.1.xml
	${XSLTPROC} ${DOCBOOK_XSL}/manpages/docbook.xsl $<


if have_javafop

tvpvrd.1.pdf: tvpvrd.1.xml
	${XSLTPROC} --nonet --output tvpvrd.fo \
		--stringparam paper.type A4 \
		--stringparam body.start.indent 0pt \
		--stringparam title.margin.left 0pt \
		--stringparam variablelist.as.blocks 1 \
		${DOCBOOK_XSL}/fo/docbook.xsl $<
	fop tvpvrd.fo $@

tvpowerd.1.pdf: tvpowerd.1.xml
	${XSLTPROC} --nonet --output tvpowerd.fo \
		--stringparam paper.type A4 \
		--stringparam body.start.indent 0pt \
		--stringparam title.margin.left 0pt \
		--stringparam variablelist.as.blocks 1 \
		${DOCBOOK_XSL}/fo/docbook.xsl $<
	fop tvpowerd.fo $@


all: tvpvrd.1 tvpvrd.1.html tvpvrd.1.pdf tvpowerd.1 tvpowerd.1.html tvpowerd.1.pdf

else

all: tvpvrd.1 tvpvrd.1.html tvpowerd.1 tvpowerd.1.html

endif

install-data-local: 
	install -D -m 644 $(abs_top_srcdir)/docs/manpages/tvpvrd.1.html ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpvrd.1.html
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpvrd.1.pdf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpvrd.1.pdf
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpvrd.1 ${DESTDIR}${prefix}/share/man/man1/tvpvrd.1
	install -D -m 644 $(abs_top_srcdir)/docs/manpages/tvpowerd.1.html ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpowerd.1.html
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpowerd.1.pdf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpowerd.1.pdf
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpowerd.1 ${DESTDIR}${prefix}/share/man/man1/tvpowerd.1

else

all:

tvpvrd.1:
	touch $@
tvpvrd.1.pdf:
	touch $@
tvpvrd.1.html:
	touch $@
tvpowerd.1:
	touch $@
tvpowerd.1.pdf:
	touch $@
tvpowerd.1.html:
	touch $@

install-data-local: 
	install -D -m 644 $(abs_top_srcdir)/docs/manpages/tvpvrd.1.html ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpvrd.1.html
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpvrd.1.pdf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpvrd.1.pdf
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpvrd.1 ${DESTDIR}${prefix}/share/man/man1/tvpvrd.1
	install -D -m 644 $(abs_top_srcdir)/docs/manpages/tvpowerd.1.html ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpowerd.1.html
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpowerd.1.pdf ${DESTDIR}${prefix}/share/doc/packages/tvpvrd/tvpowerd.1.pdf
	install -D -m 644  $(abs_top_srcdir)/docs/manpages/tvpowerd.1 ${DESTDIR}${prefix}/share/man/man1/tvpowerd.1

endif




