# ===============================================================================
# $Id$
# Autoconf script
# Use autoreconf to rebuild the build environment
# ===============================================================================
AC_PREREQ([2.65])
AC_INIT([tvpvrd],[4.1.0],[johan162@gmail.com])
AC_REVISION([$Id$])
AC_COPYRIGHT([Part of configure script Copyright Johan Persson 2010-2012])
AM_INIT_AUTOMAKE([-Wall -Werror dist-xz silent-rules])
AM_SILENT_RULES
AC_PROG_CC
AC_PROG_CC_C99
AC_CONFIG_HEADER([src/config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
src/tvpowerd/Makefile
src/tvpowerd/install.sh
src/tvpowerd/uninstall.sh
src/libsmtpmail/Makefile
src/libiniparser/Makefile
src/shell/Makefile
scripts/Makefile
docs/Makefile
docs/manpages/Makefile
])

# ===============================================================================
# Optional ARGS
#
# --enable-simulate   Allow the daemon to run on a server with no real TV cards
#                     relevant low level calls will be simulated. This is useful
#                     to debug and test the logic of the daemon on a system with
#                     no TV-cards installed.
#
# --enable-webdebug   Add some more debug output of HTTP protocol level stuff
#                     enabling this will for example print the exact HTTP headers
#                     received and sent. This only has effect as long as the
#                     log level is also set to 3 (in the application configuration
#                     file)
#
# --enable-stacktrace Force a stack trace to be written to /tmp/tvpvrd_stack.crash
#                     in case of a SIGSEGV or SIGBUS signal to help with debugging
# ===============================================================================
AC_ARG_ENABLE([simulate],
    [  --enable-simulate    Make daemon run on server without TV-Card],
    [enable_simulate=${enableval}],
    [enable_simulate=no])

if test "x${enable_simulate}" = xyes; then
    AC_DEFINE(DEBUG_SIMULATE,1,[Enable simulated TV-card])
fi

AC_ARG_ENABLE([webdebug],
    [  --enable-webdebug    Enable more debug msg of HTTP protocol with verbose=3],
    [enable_webdebug=${enableval}],
    [enable_webdebug=no])

if test "x${enable_webdebug}" = xyes; then
    AC_DEFINE(EXTRA_WEB_DEBUG,1,[Enable more HTTP protocol level debug together with loglevel=3])
fi

AC_ARG_ENABLE([stacktrace],
    [  --enable-stacktrace    Enable a stacktrace dump in case of SIGSEGV or SIGBUS error],
    [enable_stacktrace=${enableval}],
    [enable_stacktrace=no])

if test "x${enable_stacktrace}" = xyes; then
    AC_DEFINE(SIGSEGV_HANDLER,1,[Enable a stacktrace dump in case of SIGSEGV or SIGBUS error])
fi

# ================================================================================
# Add a --with-libpcre-prefix[=DIR] Option to allow user specific directory
# ================================================================================
AC_LIB_HAVE_LINKFLAGS([pcre],[],[],[],[PCRE Library needed. Please install libpcre3-dev (or similar)])

# ===============================================================================
# Standard checks for headers and C-compiler
# ===============================================================================
AC_LANG_C
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdlib.h string.h strings.h sys/param.h sys/socket.h sys/stat.h sys/ioctl.h syslog.h unistd.h])
AC_CHECK_HEADERS(pcre.h,,AC_MSG_ERROR([pcre.h not available. Please install "libpcre3-dev" package]))
AC_CHECK_HEADERS(linux/videodev2.h,,AC_MSG_ERROR([linux/videodev2.h not available]))
have_iniparser_h=no
AC_CHECK_HEADERS(iniparser.h,[have_iniparser_h=yes],AC_MSG_NOTICE([iniparser.h not available. Will use built-in version]))
have_readline_h=no
AC_CHECK_HEADERS(readline/readline.h,[have_readline_h=yes],AC_MSG_NOTICE([readline/readline.h not available. Please install "readline-devel" to get command line editing in tvpsh]))
AC_CHECK_HEADERS(errno.h,,AC_MSG_ERROR([errno.h not available]))
AC_CHECK_HEADERS(syslog.h,,AC_MSG_ERROR([syslog.h not available]))
AC_CHECK_HEADERS(getopt.h,,AC_MSG_ERROR([getopt.h not available]))
AC_CHECK_HEADERS(signal.h,,AC_MSG_ERROR([signal.h not available]))
AC_CHECK_HEADERS(fcntl.h,,AC_MSG_ERROR([fcntl.h not available]))
AC_CHECK_HEADERS(pwd.h,,AC_MSG_ERROR([pwd.h not available]))
AC_CHECK_HEADERS(grp.h,,AC_MSG_ERROR([grp.h not available]))
AC_CHECK_HEADERS(libgen.h,,AC_MSG_ERROR([libgen.h not available]))
AC_CHECK_HEADERS(dirent.h,,AC_MSG_ERROR([dirent.h not available]))

# ===============================================================================
# We need to determine which ranlib to use in order to build our static
# mail utility library
# ===============================================================================
AC_PROG_RANLIB

# ===============================================================================
# Checks for needed libraries. Note this will also append the correct linker flags
# to LDADD variable in the makefiles.
# ===============================================================================
AC_CHECK_LIB(pthread,pthread_create,,AC_MSG_ERROR([No pthread library found. Please install libpthread-dev (or similar)]))
if test "x${have_iniparser_h}" = xyes; then
   AC_CHECK_LIB(iniparser, iniparser_getstring,,AC_MSG_NOTICE([iniparser library seems invalid. Will use built-in ininparser]))
fi

AC_CHECK_LIB(xml2, xmlParseFile,,AC_MSG_ERROR([No XML2 library found. Please install libxml2-dev (or similar)]))
AC_CHECK_LIB(m, lroundf,,AC_MSG_ERROR([No math library found. Please install libm-dev (or similar)]))

if test "x${have_readline_h}" = xyes; then
   AC_CHECK_LIB(readline,readline,,AC_MSG_NOTICE([No GNU readline library found. tvpsh will not be built!]))
fi

# ===============================================================================
# Check for working fork and mktime functions just to be on the safe side
# ===============================================================================
AC_FUNC_FORK
AC_FUNC_MKTIME

# ===============================================================================
# Check for standard Unix types that we use. This is almost unnecessary since this 
# should be available in all sane systems
# ===============================================================================
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# ===============================================================================
# Check for some standard functions that are used by the daemon
# ===============================================================================
AC_CHECK_FUNCS([atexit bzero floor getcwd inet_ntoa memset mkdir rmdir select socket strchr strnlen strdup strerror])

# ===============================================================================
# MACRO AC_CHECK_XSLDOCBOOK
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
# MACRO AC_CHECK_FFMPEG
# The following macro checks for a working version of the ffmpeg program. 
# ===============================================================================
AC_DEFUN([AC_CHECK_FFMPEG],[
  AC_CHECK_PROG(HAVE_FFMPEGBIN,ffmpeg,yes,)
])

# ===============================================================================
# MACRO AC_CHECK_DEBINIFILES
# Determine if the system uses Debian style ini files. This will control which
# ini files will be installed when make install is executed
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

# ===============================================================================
# We need to define an automake conditional to know if we should use the built-in
# iniparser or as a system library.
# ===============================================================================
AM_CONDITIONAL(have_iniparser, test "x${have_iniparser_h}" = xyes)

# ===============================================================================
# We need to define an automake conditional to know if we can build the shell
# ===============================================================================
AM_CONDITIONAL(have_readline, test "x${have_readline_h}" = xyes)

# ===============================================================================
# Create a unique (well almost) login cookie seed for this installation
# ===============================================================================
cookie_seed="424bbb619133439ced118b6ea1cb1a01"
if test -f /usr/bin/md5sum; then
    cookie_seed=`(date; uname -a)|xargs echo|md5sum -|cut -c 1-32`
fi
AC_DEFINE_UNQUOTED([LOGIN_COOKIE_SEED],"${cookie_seed}",[Cookie seed for WEB-login cookie])

# ===============================================================================
# Run all previous defined macros
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
    AC_MSG_NOTICE([Basic system configuration is ok.])
if test "x${have_iniparser_h}" = xno; then
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([  - Will use built-in iniparser library.])
fi
if test "x${have_readline_h}" = xno; then
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([  - WARNING: 'tvpsh' shell will be built with limited command line editing])
   AC_MSG_NOTICE([    since the GNU readline library COULD NOT BE FOUND. Please install.])
   AC_MSG_NOTICE([    readline-devel to get full history and command line editing support])
fi
if test ! "$HAVE_FFMPEGBIN" ; then
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([No ffmpeg executable was found. While strictly speaking not] necessary)
   AC_MSG_NOTICE([to build the daemon it will be required to do transcoding. The daemon will])
   AC_MSG_NOTICE([assume that when ffmpeg is used it is reasonable new and supports new style arguments.])
else
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([Even though you have ffmpeg there might be still problem using it depending if you have])
   AC_MSG_NOTICE([the x264 (mpeg4) encoder properly installed. Please check the full tvpvrd log to see the])
   AC_MSG_NOTICE([exact command line used to invoke ffmpeg. Try that manually and resolve any missing])
   AC_MSG_NOTICE([dependencies until you can manually run the ffmpeg command])
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
        AC_MSG_NOTICE([In case you get any complains when building PDF files about missing hyphenation])
        AC_MSG_NOTICE([please see: http://offo.sourceforge.net/hyphenation/binary/installation.html ]) 
        AC_MSG_NOTICE([ ])
    fi
else
    AC_MSG_NOTICE([However, man pages can not be rebuilt with current system configuration.])
    AC_MSG_NOTICE([System needs to have DocBook5 and xsltproc installed to do this.])
    AC_MSG_NOTICE([Pre-built versions of the man pages will be installed. So this is not an issue ])
    AC_MSG_NOTICE([unless you want to modify the included man page])
fi
AC_MSG_NOTICE([--------------------------------------------------------------------------------])

