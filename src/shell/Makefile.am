# ===============================================================================
# $Id$
# Automake build script for tvpshell
#
# ===============================================================================
AM_CFLAGS = -DCONFDIR="\"$(sysconfdir)\"" -pedantic -Wall -Werror -Wextra -Wpointer-arith -Wstrict-prototypes -Wshadow -D_FORTIFY_SOURCE=2
bin_PROGRAMS = tvpsh
BUILDNBR_FILE=buildnumber.txt

tvpsh_SOURCES = tvpshell.c

if have_iniparser
tvpsh_LDADD = ../xstr.o ${LIBPCRE} -lreadline
else
tvpsh_LDADD = ../xstr.o ../libiniparser/libiniparser.a ${LIBPCRE} -lreadline
endif

tvpsh_LDFLAGS = -Xlinker --defsym -Xlinker "__BUILD_NUMBER=$$(cat $(BUILDNBR_FILE))"
tvpsh_LDFLAGS += -Xlinker --defsym -Xlinker "__BUILD_DATE=$$(date +'%Y%m%d')"

EXTRA_DIST = README INSTALL

CLEANFILES = *~ tvpsh *.pid 

DISTCLEANFILES=*.Po $(BUILDNBR_FILE)

tvpsh_DEPENDENCIES=$(BUILDNBR_FILE)

$(BUILDNBR_FILE): $(tvpsh_OBJECTS)
	@if ! test -f $(BUILDNBR_FILE); then \
		echo 0 > $(BUILDNBR_FILE); \
	fi
	@echo $$(($$(cat $(BUILDNBR_FILE)) + 1)) > $(BUILDNBR_FILE)


