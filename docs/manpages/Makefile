all: tvpvrd.1 tvpvrd.1.html

pdf: tvpvrd.1.pdf

tvpvrd.1.html: tvpvrd.1.xml
	xsltproc --output tvpvrd.1.html /usr/share/xml/docbook/stylesheet/nwalsh/current/html/docbook.xsl $<

tvpvrd.1: tvpvrd.1.xml
	xsltproc /usr/share/xml/docbook/stylesheet/nwalsh/current/manpages/docbook.xsl $<

tvpvrd.1.pdf: tvpvrd.1.xml
	xsltproc --nonet --output tvpvrd.fo \
		--stringparam paper.type A4 \
		--stringparam body.start.indent 0pt \
		--stringparam title.margin.left 0pt \
		--stringparam variablelist.as.blocks 1 \
		/usr/share/xml/docbook/stylesheet/nwalsh/current/fo/docbook.xsl $<
	fop tvpvrd.fo $@

clean:
	rm -rf tvpvrd.1.html tvpvrd.1.pdf tvpvrd.1 tvpvrd.fo


