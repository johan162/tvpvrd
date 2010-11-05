AC_INIT([tvpvrd],[1.9.0],[johan162@gmail.com])
AM_INIT_AUTOMAKE([-Wall -Werror])
AC_PROG_CC
AC_PROG_CC_C99
AC_CONFIG_HEADER([src/config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
src/tvpowerd/Makefile
scripts/Makefile
docs/Makefile
docs/manpages/Makefile
])

AC_LANG_C
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_HEADER_STDC
AC_CHECK_HEADERS([arpa/inet.h fcntl.h stdlib.h string.h strings.h sys/param.h sys/socket.h sys/stat.h sys/ioctl.h syslog.h unistd.h])
AC_CHECK_HEADER(pcre.h,,AC_MSG_ERROR(pcre.h not available. Please install "pcre-devel" package))
AC_CHECK_HEADER(linux/videodev2.h,,AC_MSG_ERROR(linux/videodev2.h not available))
AC_CHECK_HEADER(iniparser.h,,AC_MSG_ERROR(iniparser.h not available. Please install "iniparser-devel" package. Unfortunately there doesnt seem like there is a pre-built package available for Ubuntu which means that you have to build and download it yourself if you are using Ubuntu))
AC_CHECK_HEADER(errno.h,,AC_MSG_ERROR(errno.h not available))
AC_CHECK_HEADER(syslog.h,,AC_MSG_ERROR(syslog.h not available))
AC_CHECK_HEADER(getopt.h,,AC_MSG_ERROR(getopt.h not available))
AC_CHECK_HEADER(signal.h,,AC_MSG_ERROR(signal.h not available))
AC_CHECK_HEADER(fcntl.h,,AC_MSG_ERROR(fcntl.h not available))
AC_CHECK_HEADER(pwd.h,,AC_MSG_ERROR(pwd.h not available))
AC_CHECK_HEADER(grp.h,,AC_MSG_ERROR(grp.h not available))
AC_CHECK_HEADER(libgen.h,,AC_MSG_ERROR(libgen.h not available))
AC_CHECK_HEADER(dirent.h,,AC_MSG_ERROR(dirent.h not available))

#AC_CHECK_LIB(pthread,pthread_create)
#AC_CHECK_LIB([iniparser], [iniparser_getstring])
#AC_CHECK_LIB([pcre], [pcre_exec])
#AC_CHECK_LIB([xml2], [xmlParseFile])

AC_FUNC_FORK
AC_FUNC_MKTIME

AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

AC_CHECK_FUNCS([atexit bzero floor getcwd inet_ntoa memset mkdir rmdir select socket strchr strnlen strdup strerror])

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
	AC_MSG_CHECKING([if Docbook environment is installed ... ])

        DOCBOOK_XSL_SCHEMA="$DOCBOOK_XSL/html/docbook.xsl"

	$XSLTPROC --nonet $DOCBOOK_XSL_SCHEMA > /dev/null 2>&1 << END
<?xml version="1.0" encoding='ISO-8859-1'?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook XML 5.0//EN" "http://www.oasis-open.org/docbook/xml/5.0/dtd/docbook.dtd">
<book id="test">
</book>
END
	if test "$?" = 0; then
		XSLTPROC_WORKS=yes
                AC_MSG_RESULT(YES. Working docbook 5.0 environment found. Documentation can be rebuilt.)
        else
        	AC_MSG_RESULT(NO. No working docbook 5.0 environment. Documentation can not be reubilt. )
	fi
fi

AM_CONDITIONAL(have_xsltproc, test "$XSLTPROC_WORKS" = "yes")

AC_SUBST(DOCBOOK_XSL)

])

AC_CHECK_XSLDOCBOOK

AC_OUTPUT
