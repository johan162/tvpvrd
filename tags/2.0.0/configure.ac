# ===============================================================================
# $Id$
# Autoconf script
# To rebuild build environment please execute the bootstrap.sh script that can be found in the repository.
# The bootstrap script will run aclocal, autoheader, autoconf and automake. In addition it will extract
# the ChangeLog file from the repository and create a ChangeLog file in suitable style.
# ===============================================================================
AC_PREREQ([2.65])
AC_INIT([tvpvrd],[2.0.0],[johan162@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror dist-bzip2])
AC_PROG_CC
AC_PROG_CC_C99
AC_CONFIG_HEADER([src/config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
src/tvpowerd/Makefile
src/tvpowerd/install.sh
src/tvpowerd/uninstall.sh
scripts/Makefile
docs/Makefile
docs/manpages/Makefile
])

# ===============================================================================
# Standard checks for headers and C-compiler
# ===============================================================================
AC_LANG_C
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdlib.h string.h strings.h sys/param.h sys/socket.h sys/stat.h sys/ioctl.h syslog.h unistd.h])
AC_CHECK_HEADERS(pcre.h,,AC_MSG_ERROR(pcre.h not available. Please install "libpcre3-dev" package))

AC_CHECK_HEADERS(linux/videodev2.h,,AC_MSG_ERROR(linux/videodev2.h not available))
have_iniparser_h=no
AC_CHECK_HEADERS(iniparser.h,[have_iniparser_h=yes],AC_MSG_NOTICE(iniparser.h not available. Will use builtin))

AC_CHECK_HEADERS(errno.h,,AC_MSG_ERROR(errno.h not available))
AC_CHECK_HEADERS(syslog.h,,AC_MSG_ERROR(syslog.h not available))
AC_CHECK_HEADERS(getopt.h,,AC_MSG_ERROR(getopt.h not available))
AC_CHECK_HEADERS(signal.h,,AC_MSG_ERROR(signal.h not available))
AC_CHECK_HEADERS(fcntl.h,,AC_MSG_ERROR(fcntl.h not available))
AC_CHECK_HEADERS(pwd.h,,AC_MSG_ERROR(pwd.h not available))
AC_CHECK_HEADERS(grp.h,,AC_MSG_ERROR(grp.h not available))
AC_CHECK_HEADERS(libgen.h,,AC_MSG_ERROR(libgen.h not available))
AC_CHECK_HEADERS(dirent.h,,AC_MSG_ERROR(dirent.h not available))

# ===============================================================================
# Checks for needed libraries. Note this will also append the correct linker flags to LDADD variable in the
# makefiles.
# ===============================================================================
AC_CHECK_LIB(pthread,pthread_create,,AC_MSG_ERROR([No pthread library found. Please install libpthread-dev (or similar)]))
if test "x${have_iniparser_h}" = xyes; then
   AC_CHECK_LIB(iniparser, iniparser_getstring,,AC_MSG_NOTICE([iniparser library seems invalid. Will use built-in ininparser]))
fi
AC_CHECK_LIB(pcre, pcre_exec,,AC_MSG_ERROR([pcre library seems invalid. Please reinstall libpcre3-dev (or similar)]))
AC_CHECK_LIB(xml2, xmlParseFile,,AC_MSG_ERROR([No XML2 library found. Please install libxml2-dev (or similar)]))

# ===============================================================================
# Check for working fork and mktime functions just to be on the safe side
# ===============================================================================
AC_FUNC_FORK
AC_FUNC_MKTIME

# ===============================================================================
# Check for standard Unix types that we use. This is almost unnecessary since this shoud be available in
# all sane systems
# ===============================================================================
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# ===============================================================================
# Check for some standard functions thta are used by the daemon
# ===============================================================================
AC_CHECK_FUNCS([atexit bzero floor getcwd inet_ntoa memset mkdir rmdir select socket strchr strnlen strdup strerror])

# ===============================================================================
# AC_CHECK_XSLDOCBOOK
# The following macro will check to see if a working Docbook 5 environment can be found. It will also
# check for the presence of the fop program which is needed to rebuild the PDF versions of the man page.
# An end user of the library does not have to have the Docbook 5 environment installed since the distribution
# includes pre-built versions of the man pages. However a package maintainer that wants to modify or
# rebuild the man pages will need to have a full Docbook 5 setup.
# ===============================================================================
AC_DEFUN([AC_CHECK_XSLDOCBOOK],[ 
DOCBOOK_XSL=
if test ! -f /etc/xml/catalog; then
        for i in /usr/share/sgml/docbook/stylesheet/xsl/nwalsh /usr/share/sgml/docbook/xsl-stylesheets /usr/share/xml/docbook/stylesheet/nwalsh/current
        do
                if test -d "$i"; then
                        DOCBOOK_XSL=$i
                fi
        done
else
        DOCBOOK_XSL="http://docbook.sourceforge.net/release/xsl-ns/current"
fi
AC_CHECK_PROG(XSLTPROC,xsltproc,xsltproc,)
XSLTPROC_WORKS=no
if test -n "$XSLTPROC"; then
	AC_MSG_CHECKING([if Docbook 5 is installed])

        DOCBOOK_XSL_SCHEMA="$DOCBOOK_XSL/html/docbook.xsl"

	$XSLTPROC --nonet $DOCBOOK_XSL_SCHEMA > /dev/null 2>&1 << END
<?xml version="1.0" encoding='ISO-8859-1'?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML 5.0//EN" "http://www.oasis-open.org/docbook/xml/5.0/dtd/docbook.dtd">
<book id="test">
</book>
END
	if test "$?" = 0; then
		XSLTPROC_WORKS=yes
                AC_MSG_RESULT([yes.])
        else
        	AC_MSG_RESULT([no.])
	fi
fi

AM_CONDITIONAL(have_xsltproc, test "$XSLTPROC_WORKS" = "yes")

AC_CHECK_PROG(JAVAFOP,fop,fop,no)
AM_CONDITIONAL(have_javafop, test $JAVAFOP = fop)

AC_SUBST(DOCBOOK_XSL)

])

# ===============================================================================
# AC_CHECK_FFMPEG
# The following macro checks for a working version of the ffmpeg program. It will also determine if the system
# have an old version installed that uses the old style ffmpeg profile files by checking the name of a common
# profile. Not 100% but 99.9% which is good enough.
# ===============================================================================
AC_DEFUN([AC_CHECK_FFMPEG],[
AC_CHECK_PROG(HAVE_FFMPEGBIN,ffmpeg,yes,)
MEDIUM_EXISTS=no
NORMAL_EXISTS=no
AC_MSG_CHECKING([If ffmpeg works])
for i in /usr/share/ffmpeg/libx264-medium.ffpreset /usr/local/share/ffmpeg/libx264-medium.ffpreset
do
        if test -f "$i"; then
                MEDIUM_EXISTS=yes
        fi
done
if test "$MEDIUM_EXISTS" = "yes"; then
        AC_MSG_RESULT([yes])
        AM_CONDITIONAL(have_old_ffmpeg, test 0 = 1)
else
        MEDIUM_EXISTS=no
        NORMAL_EXISTS=no
        for i in /usr/share/ffmpeg/libx264-normal.ffpreset /usr/local/share/ffmpeg/libx264-normal.ffpreset
        do
                if test -f "$i"; then
                        NORMAL_EXISTS=yes
                fi
        done

        if test "$NORMAL_EXISTS" = "yes"; then
                AC_MSG_RESULT([yes])
                AC_MSG_NOTICE([Your version of ffmpeg is rather old. Please consider upgrading it])
                AC_DEFINE(HAVE_OLD_FFMPEG,1,[Whether we are restricted to old style syntax when invoking ffmpeg])
                AM_CONDITIONAL(have_old_ffmpeg, test 1 = 1)
        else
                AC_MSG_RESULT([no.])
                AM_CONDITIONAL(have_old_ffmpeg, test 0 = 1)
        fi
fi

])

# ===============================================================================
# AC_CHECK_DEBINIFILES
# Determine if the system uses Debian style ini files
# ===============================================================================
AC_DEFUN([AC_CHECK_DEBINIFILES],[
AC_MSG_CHECKING([If system uses debian style init.d])
if test ! -f /etc/rc.status; then
                    AC_MSG_RESULT([yes])
else
                    AC_MSG_RESULT([no])
fi
AM_CONDITIONAL(have_debian_initd, test ! -f /etc/rc.status)
])

AM_CONDITIONAL(have_iniparser, test "x${have_iniparser_h}" = xyes)

# ===============================================================================
# Run macros
# ===============================================================================
AC_CHECK_DEBINIFILES
AC_CHECK_XSLDOCBOOK
AC_CHECK_FFMPEG

# ===============================================================================
# Output all generated files
# ===============================================================================
AC_OUTPUT

# ===============================================================================
# Finally give some information to the user about the configuration setup
# ===============================================================================
AC_MSG_NOTICE([--------------------------------------------------------------------------------])
AC_MSG_NOTICE([INSTALLATION SUMMARY])
    AC_MSG_NOTICE([System configuration looks fine.])
if test "x${have_iniparser_h}" = xno; then
   AC_MSG_NOTICE([Will use built-in iniparser library.])
fi
if test ! "$HAVE_FFMPEGBIN" ; then
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([No ffmpeg executable was found. While strictly speaking not] necessary)
    AC_MSG_NOTICE([to build the daemon it will be required to do transcoding. The daemon will])
    AC_MSG_NOTICE([assume that when ffmpeg is used it is reasonable new and supports new style arguments.])
fi
        
if test "$NORMAL_EXISTS" = "yes"; then
    AC_MSG_NOTICE([ ])
    AC_MSG_NOTICE([(BTW. Your version of ffmpeg is rather old. Please consider upgrading it)])
fi

AC_MSG_NOTICE([ ])

if  test "$XSLTPROC_WORKS" = "yes"; then
    if test "$JAVAFOP" = "no"; then
        AC_MSG_NOTICE([In addition the man page can be rebuilt in both html and Unix man format])
        AC_MSG_NOTICE([but the man page can NOT not be rebuild in PDF format. Please install 'fop'])
        AC_MSG_NOTICE([if you want to modify the man page and create a new PDF version])
        AC_MSG_NOTICE([ ])
        AC_MSG_NOTICE([Pre-built versions of the man pages are included in the distribution ])
        AC_MSG_NOTICE([and the install will use these.])
    else
        AC_MSG_NOTICE([In addition the man pages can be rebuilt in both html, Unix man format, and])
        AC_MSG_NOTICE([most likely in PDF format.])
        AC_MSG_NOTICE([ ])
        AC_MSG_NOTICE([However, if you haven't used the fop processor to generate PDF files before])
        AC_MSG_NOTICE([it is highly likely that you will get an error message saying something like])
        AC_MSG_NOTICE([   "SEVERE: Couldn't find hyphenation pattern en" ])
        AC_MSG_NOTICE([Due to licensing issues most distributions does not include the necessary hyphenation])
        AC_MSG_NOTICE([so you would have to do this manually if you want to rebuild the PDF files yourself.])
        AC_MSG_NOTICE([Please see: http://offo.sourceforge.net/hyphenation/binary/installation.html for information.])
        AC_MSG_NOTICE([ ])
        AC_MSG_NOTICE([(Usually it is enough to copy the fop-hyph.jar file into "/usr/share/fop-0.95/lib")])
    fi
else
    AC_MSG_NOTICE([However, man pages can not be rebuilt with current system configuration.])
    AC_MSG_NOTICE([System needs to have DocBook5 and xsltproc installed to do this.])
    AC_MSG_NOTICE([Pre-built versions of the man pages will be installed. So this is not an issue ])
    AC_MSG_NOTICE([unless you want to modify the included man page])
fi
AC_MSG_NOTICE([--------------------------------------------------------------------------------])

