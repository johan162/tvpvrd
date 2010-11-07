/* =========================================================================
 * File:        TVPPWRD.H
 * Description: A watcher daemon which is meant to run on a separate machine
 *              from the tvpvrd. This watcher daemon communicates with
 *              the tvpvrd and if the server running tvpvrd has nothing
 *              to do and the next recording is not due to start until a
 *              a specified treshold time in the future it will shut
 *              down the server. Since this watcher knows when the next
 *              recording starts it will then wake up the server at an
 *              appropriate time.
 *              For this daemon to work the server running tvpvrd must
 *              support wake-on-lan
 *
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2010 Johan Persson
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 * =========================================================================
 */


#ifndef TVPOWERD_H
#define	TVPOWERD_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * INIFILE_NAME string
 * Name and path of inifile
 */
#define INIFILE_NAME "tvpowerd.conf"

/*
 * LOCALE_NAME string
 * The default locale to use in case no locale is defined. This is important to get
 * right since otherwise the system() will not interpret the filenames correctly.
 */
#define LOCALE_NAME "en_US.UTF8"


/*
 * VERBOSE_LOG bool
 * Should the log be more verbose
 */
#define VERBOSE_LOG 1

/*
 * LOGFILE_NAME string
 * Optional logfile name (full path).
 */
#define LOGFILE_NAME "syslog"
#define LOGFILE_SYSLOG "syslog"

/*
 * SENDMAIL_ON_ERROR boolean
 * Should we send notification of serious error
 */
#define SENDMAIL_ON_ERROR 0

/*
 * SENDMAIL_ON_SHUTDOWN boolean
 * Should we send mail when remote server is powered off
 */
#define SENDMAIL_ON_SHUTDOWN 0


/*
 * DEFAULT_DAEMONIZE bool
 * If we should run as a daemon, this setting is the default if not specified on the command
 * line or in the ini-filename
 */
#define DEFAULT_DAEMONIZE 1

/*
 * DEFAULT_USERNAME string
 * What user should the server be running as
 */
#define DEFAULT_USERNAME "tvppwrd"

/*
 * SEND_MAILADDRESS string
 * Mail address to use if we have enabled sending notification of serious error
 */
#define SEND_MAILADDRESS "root@localhost"

    
// Names of the ini file and the db file used
extern char inifile[];

// Should we send mail on errors?
extern int send_mail_on_error;

// Mail address to use
extern char send_mailaddress[];

// Name of loclae to use (used to set LC_ALL)
extern char locale_name[];

extern int verbose_log;

extern char logfile_name[];

extern char server_program_name[];



// Clear variable section in memory
#define CLEAR(x) memset (&(x), 0, sizeof(x))

#ifdef	__cplusplus
}
#endif

#endif	/* TVPOWERD_H */

