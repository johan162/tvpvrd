#!/bin/sh
# Count the number of source lines in the project
# $Id$
wc -l src/*.c src/*.h *.ac *.am src/*.am src/tvpowerd/*.c src/tvpowerd/*.h src/tvpowerd/*.c src/tvpowerd/*.am src/libsmtpmail/*.h src/libsmtpmail/*.c src/libsmtpmail/*.am src/etc/*.template src/etc/templates/* src/etc/themes/* src/etc/mail_templates/* src/etc/xsl/* docs/manpages/*.xml src/libiniparser/*.c src/libiniparser/*.h src/shell/*.c src/shell/*.am profiles/* profiles-avconv/*

