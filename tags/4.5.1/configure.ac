# ===============================================================================
# $Id$
# Autoconf script
# Use autoreconf to rebuild the build environment
# ===============================================================================
AC_PREREQ([2.69])
AC_INIT([tvpvrd],[4.5.1],[johan162@gmail.com])
AC_REVISION([$Id$])
AC_COPYRIGHT([Part of configure script Copyright Johan Persson 2010-2013])
AC_PREFIX_DEFAULT(/usr)
AM_INIT_AUTOMAKE([-Wall -Werror dist-xz silent-rules])
AM_SILENT_RULES
AM_PROG_AR
AC_PROG_LN_S
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
src/etc/tvpvrd.conf.simplified.template
src/etc/tvpvrd.conf.full.template
scripts/Makefile
scripts/pm/Makefile
docs/Makefile
docs/manpages/Makefile
])

# ===============================================================================
# Set default sysconfdir to "/etc" in case prefix is not set by user
# ===============================================================================
AS_IF([ test "$prefix" = NONE],[test "$sysconfdir" = '${prefix}/etc' && sysconfdir=/etc] )


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
AC_CHECK_HEADERS(pcre.h,,AC_MSG_ERROR([pcre.h not available. Please install "libpcre3-dev" package]))
have_readline_h=no
AC_CHECK_HEADERS(readline/readline.h,[have_readline_h=yes],AC_MSG_ERROR([libreadline not available. Please install "libreadline-dev" ]))
AC_CHECK_HEADERS(linux/videodev2.h,,AC_MSG_ERROR([linux/videodev2.h not available]))
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdlib.h string.h strings.h sys/param.h sys/socket.h sys/stat.h sys/ioctl.h syslog.h unistd.h])
have_iniparser_h=no
AC_CHECK_HEADERS(iniparser.h,[have_iniparser_h=yes],AC_MSG_NOTICE([iniparser.h not available. Will use built-in version]))
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

AC_CHECK_PROG(JAVAFOP,fop,yes,no)
AM_CONDITIONAL(have_javafop, test "$JAVAFOP" = "yes")

AC_SUBST(DOCBOOK_XSL)
])

# ===============================================================================
# MACRO AC_CHECK_FFMPEG
# The following macro checks for a working version of the ffmpeg program.
# ===============================================================================
AC_DEFUN([AC_CHECK_FFMPEG],[
  AC_CHECK_PROG(HAVE_FFMPEGBIN,ffmpeg,yes,)
#  AM_CONDITIONAL(have_ffmpegbin, test "x${HAVE_FFMPEGBIN}" = xyes)
])

# ===============================================================================
# MACRO AC_CHECK_AVCONV
# The following macro checks for a working version of the avconv program.
# ===============================================================================
AC_DEFUN([AC_CHECK_AVCONV],[
  AC_CHECK_PROG(HAVE_AVCONVBIN,avconv,yes,)
#  AM_CONDITIONAL(have_avconvbin, test "x{$HAVE_AVCONVBIN}" = xyes)
])

# ===============================================================================
# MACRO AC_SELECT_FFMPEG
# The following macro selects which transcoder should be included in the conf
# file by default.
# ===============================================================================
AC_DEFUN([AC_SELECT_TRANSCBIN],[
  TRANSC_BIN=
  if test "x${HAVE_FFMPEGBIN}" = xyes ; then
    AC_PATH_PROG(TRANSC_BIN,ffmpeg)
  else 
    if test "x${HAVE_AVCONVBIN}" = xyes ; then
      AC_PATH_PROG(TRANSC_BIN,avconv)
    fi
  fi
  AC_SUBST(TRANSC_BIN)
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
# MACRO AC_CHECK_UPSTARTINIT
# Determine if the system uses the newer "Upstart" method to handle init scripts
# We do this by checking if there is a config file for "dmesg" in that case we
# can safely assume an Upstart system
# ===============================================================================
AC_DEFUN([AC_CHECK_UPSTARTINIT],[
AC_MSG_CHECKING([If system uses Upstart init scripts])
if test -f /etc/init/dmesg.conf  -a -f /lib/init/upstart-job; then
    AC_MSG_RESULT([yes])
else
    AC_MSG_RESULT([no])
fi
AM_CONDITIONAL(have_upstart_init, test -f /etc/init/dmesg.conf)
])


# ===============================================================================
# MACRO AC_CHECK_PMTOOLS
# Determine if the system uses pm-tools to control power management. In that
# case the install script will put come scripts to remove the ivtv driver from
# the kernel and shut down tvpvrd before hibernatoin and sleep.
# ===============================================================================
AC_DEFUN([AC_CHECK_PMTOOLS],[
AC_MSG_CHECKING([if system uses pm-tools])
if test -d /etc/pm/sleep.d; then
    AC_MSG_RESULT([yes])
else
    AC_MSG_RESULT([no])
fi
AM_CONDITIONAL(have_pmtools, test -d /etc/pm/sleep.d)
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
if test -f /usr/bin/md5sum; then  FFMPEG_BIN="ffmpeg-test"
  AC_SUBST(FFMPEG_BIN)
    cookie_seed=`(date; uname -a)|xargs echo|md5sum -|cut -c 1-32`
fi
AC_DEFINE_UNQUOTED([LOGIN_COOKIE_SEED],"${cookie_seed}",[Cookie seed for WEB-login cookie])

# ===============================================================================
# Run all previous defined macros
# ===============================================================================
AC_CHECK_DEBINIFILES
AC_CHECK_UPSTARTINIT
AC_CHECK_XSLDOCBOOK
AC_CHECK_FFMPEG
AC_CHECK_AVCONV
AC_SELECT_TRANSCBIN
AC_CHECK_PMTOOLS

# ===============================================================================
# Output all generated files
# ===============================================================================
AC_OUTPUT

# ===============================================================================
# Finally give some information to the user about the configuration setup
# ===============================================================================
AC_MSG_NOTICE([--------------------------------------------------------------------------------])
AC_MSG_NOTICE([INSTALLATION SUMMARY])
AC_MSG_NOTICE([ ])
AC_MSG_NOTICE([   CONGRATULATION! You seem to have a working environment to build the daemon!])
AC_MSG_NOTICE([ ])
AC_MSG_NOTICE([  - Will use ${sysconfdir}/tvpvrd for configuration files. ])
if test "x${have_iniparser_h}" = xno; then
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([  - Will use built-in iniparser library.])
fi
if test -d /etc/pm/sleep.d ; then
   AC_MSG_NOTICE([ ])
   AC_MSG_NOTICE([  - Will install pm-util hooks to unload ivtv driver.])
fi
if test ! "$HAVE_FFMPEGBIN" ; then
   if test ! "$HAVE_AVCONVBIN" ; then
     AC_MSG_NOTICE([ ])
     AC_MSG_NOTICE([  - Transcoding not possible.] )
     AC_MSG_NOTICE([      Neither 'ffmpeg' nor 'avconv' executable was found. ] )
   else
     AC_MSG_NOTICE([ ])
     AC_MSG_NOTICE([  - Will use 'avconv' for transcoding. ])
     AC_MSG_NOTICE([    (using \"${sysconfdir}/tvpvrd/profiles-avconv/\" as profile directory)])
   fi
else
   if test ! "$HAVE_AVCONVBIN" ; then
        AC_MSG_NOTICE([ ])
        AC_MSG_NOTICE([  - Will use 'ffmpeg' for transcoding. ])
   else
        AC_MSG_NOTICE([ ])
        AC_MSG_NOTICE([  - Both 'ffmpeg' or 'avconv' available for transcoding. Default set to 'ffmpeg'. ])
   fi
fi

AC_MSG_NOTICE([ ])

if  test "$XSLTPROC_WORKS" = "yes"; then
    if test "$JAVAFOP" = "no"; then
        AC_MSG_NOTICE([  - Will use limited Docbook5 setup (man- and HTML-pages can be rebuilt).])
        AC_MSG_NOTICE([    To also build PDF versions please install 'fop'])
        AC_MSG_NOTICE([ ])
    else
        AC_MSG_NOTICE([  - Docbook5 available to render documentation. ])
        AC_MSG_NOTICE([    Note: In case of errors rebuilding PDF regarding missing hyphenation])
        AC_MSG_NOTICE([    please refer to: http://offo.sourceforge.net/hyphenation/binary/installation.html ])
        AC_MSG_NOTICE([ ])
    fi
else
    AC_MSG_NOTICE([  - Docbook5 setup NOT detected. Documentation can NOT be rebuilt.])
fi
AC_MSG_NOTICE([--------------------------------------------------------------------------------])

