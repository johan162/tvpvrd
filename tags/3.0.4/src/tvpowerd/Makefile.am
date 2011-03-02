# ===============================================================================
# $Id$
# Automake build script
# 
# ===============================================================================
AM_CFLAGS = -DCONFDIR="\"$(sysconfdir)\"" -pedantic -Wall -Werror -Wextra -Wpointer-arith -Wstrict-prototypes -D_FORTIFY_SOURCE=2
noinst_PROGRAMS = tvpowerd 
BUILDNBR_FILE=buildnumber.txt

tvpowerd_SOURCES = tvpowerd.c wakelan.c tvpowerd.h wakelan.h

if have_iniparser
tvpowerd_LDADD = ../utils.o ../lockfile.o  ../datetimeutil.o ../xstr.o \
../libsmtpmail/libsmtpmail.a ${LIBPCRE}
else
tvpowerd_LDADD = ../utils.o ../lockfile.o ../datetimeutil.o ../xstr.o \
../libsmtpmail/libsmtpmail.a ../libiniparser/libiniparser.a ${LIBPCRE}
endif

tvpowerd_LDFLAGS = -Xlinker --defsym -Xlinker "__BUILD_NUMBER=$$(cat $(BUILDNBR_FILE))"
tvpowerd_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_DATE=$$(date +'%Y%m%d')"

EXTRA_DIST = tvpowerd.conf pre-shutdown.sh post-startup.sh README install.sh.in uninstall.sh.in init.d.tvpowerd deb.init.d.tvpowerd 

CLEANFILES = *~ tvpowerd *.pid

DISTCLEANFILES=*.Po install.sh uninstall.sh $(BUILDNBR_FILE)

tvpowerd_DEPENDENCIES=$(BUILDNBR_FILE)

$(BUILDNBR_FILE): $(tvpowerd_OBJECTS)
	@if ! test -f $(BUILDNBR_FILE); then \
		echo 0 > $(BUILDNBR_FILE); \
	fi
	@echo $$(($$(cat $(BUILDNBR_FILE)) + 1)) > $(BUILDNBR_FILE)





