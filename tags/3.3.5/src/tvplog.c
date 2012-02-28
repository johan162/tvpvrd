/* =========================================================================
 * File:        TVPLOG.C
 * Description: Routines to do logging
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010,2011,2012 Johan Persson
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


// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

// Standard defines
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <syslog.h>
#include <time.h>

#include <sys/types.h>
#include <sys/param.h>

#include "tvconfig.h"
#include "tvplog.h"
#include "utils.h"
#include "mailutil.h"

// Last logmessage
#define MAX_LASTLOGMSG 1024
char last_logmsg[MAX_LASTLOGMSG] = {'\0'};

// Needed to flag we are in the logmsg function to avoid
// _writef doing possible html output encoding
int inlogfunction=0;

// We will define this config variable here insead of the propre place in
// yvconfig since otherwise tvpowerd will note get this variable defined
// since it doesn't include the tvconfig module.
char daemon_email_from[64] = {'\0'};

/* Global information about the server */
extern char server_version[] ;
extern char server_program_name[] ;


/*
 * _writef
 * Utility function
 * Simplify a formatted write to a file descriptor
 */
int
_writef_log(int fd, const char *buf, ...) {
    if( fd >= 0 ) {
        const int blen = MAX(8192*2,strlen(buf)+1);
        char *tmpbuff = calloc(blen,sizeof(char));
        if( tmpbuff == NULL ) {
            syslog(LOG_ERR,"FATAL: Cannot allocate buffer in _writef_log()");
            return -1;
        }
        va_list ap;
        va_start(ap, buf);
        vsnprintf(tmpbuff, blen, buf, ap);
        tmpbuff[blen-1] = 0;
        int ret = write(fd, tmpbuff, strnlen(tmpbuff,blen));
        free(tmpbuff);
        return ret;
    }
    return -1;
}


/**
 * _vsyslogf
 * Write a message to the system logger with printf() formatting capabilities
 * @param priority Priority
 * @param msg Format string
 * @param ... Arguments for format string
 */
void
_vsyslogf(int priority, char *msg, ...) {
    static const int blen = 4096;
    char tmpbuff[blen];

    va_list ap;
    va_start(ap,msg);

    int erroffset = 0 ;
    if( priority == LOG_ERR ) {
        tmpbuff[0] = '*';
        tmpbuff[1] = '*';
        tmpbuff[2] = '*';
        tmpbuff[3] = ' ';
        erroffset = 4;
    }
    vsnprintf(tmpbuff+erroffset,blen-1-erroffset,msg,ap);
    tmpbuff[blen-1] = 0 ;
    syslog(priority,"%s", tmpbuff);
}

/*
 * Utility function
 * Log message to either specified log file or if no file is specified use
 * system logger. The name of the output device to use is set in the main
 * program and communicated here with a global variable
 */
#define MAXLOGFILEBUFF 10*1024
static char _lastlogmsg[4096] = {'\0'};
static int _lastlogcnt=0;

void logmsg(int priority, char *msg, ...) {
    static const int blen = 20*1024;
    static int _loginit = 0 ;
    char *msgbuff,*tmpbuff,*logfilebuff;


    msgbuff = calloc(1,blen);
    tmpbuff = calloc(1,blen);
    logfilebuff = calloc(1,blen);
    if( msgbuff == NULL || tmpbuff == NULL || logfilebuff == NULL ) {
        free(msgbuff);
        free(tmpbuff);
        free(logfilebuff);
        syslog(priority,"FATAL. Can not allocate message buffers in logmsg().");
        return;
    }

    // We only print errors by default and info if the verbose flag is set
    if ( (priority == LOG_ERR) || (priority == LOG_CRIT) ||
         ((priority == LOG_INFO)   && verbose_log > 0) ||
         ((priority == LOG_NOTICE) && verbose_log > 1) ||
         ((priority == LOG_DEBUG)  && verbose_log > 2)      ) {
        va_list ap;
        va_start(ap,msg);

        int erroffset = 0 ;
        if( priority == LOG_ERR ) {
            tmpbuff[0] = '*';
            tmpbuff[1] = '*';
            tmpbuff[2] = ' ';
            erroffset = 3;
        } else if(priority == LOG_CRIT) {
            tmpbuff[0] = '*';
            tmpbuff[1] = '*';
            tmpbuff[2] = '*';
            tmpbuff[3] = '*';
            tmpbuff[4] = ' ';
            erroffset = 5;
        }
        vsnprintf(tmpbuff+erroffset,blen-1-erroffset,msg,ap);

        if( 0==strcmp(tmpbuff,_lastlogmsg) ) {
            free(msgbuff);
            free(tmpbuff);
            free(logfilebuff);
            _lastlogcnt++;
            return;
        } else {
            if( _lastlogcnt > 0 ) {
                char *tmpbuff2 = calloc(1,blen);
                snprintf(tmpbuff2,blen,"[Repeated %d times] : %s",_lastlogcnt,_lastlogmsg);
                _lastlogcnt = -1;
                logmsg(priority,tmpbuff2);
                free(tmpbuff2);
                _lastlogcnt=0;
                *_lastlogmsg = '\0';
            } else if( 0 == _lastlogcnt ) {
                strncpy(_lastlogmsg,tmpbuff,4095);
            }
        }
        tmpbuff[blen-1] = 0 ;

        if ( *logfile_name == '\0' ||  strcmp(logfile_name, LOGFILE_SYSLOG) == 0 ) {
            if( !_loginit ) {
                openlog(server_program_name, LOG_PID | LOG_CONS, LOG_DAEMON);
                _loginit = 1;
            }
            syslog(priority, "%s", tmpbuff);
            // Syslogger
        } else {
            mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            int fd;

            // Use file
            if( strcmp(logfile_name,"stdout") == 0) {
                fd=STDOUT_FILENO;
            }
            else {
                fd = open(logfile_name, O_APPEND | O_CREAT | O_WRONLY , mode);
            }
            if (fd < 0) {
                // Use syslogger as last resort
                if( !_loginit ) {
                    openlog(server_program_name, LOG_PID | LOG_CONS, LOG_USER);
                    _loginit = 1;
                }
                syslog(LOG_ERR, "Couldn't open specified log file. Falling back to syslog.");
                syslog(priority, "%s", tmpbuff);
            } else {
                static const int tblen = 32;
                char timebuff[tblen] ;
                //char *envloc = getenv("LC_ALL");

                time_t now = time(NULL);
                ctime_r(&now,timebuff);
                timebuff[strnlen(timebuff,tblen)-1] = 0;
                //snprintf(msgbuff, blen-1, "(%ld) (%s) %s: %s\n", now, envloc, timebuff, tmpbuff);
                snprintf(msgbuff, blen-1, "%s: %s\n", timebuff, tmpbuff);
                msgbuff[blen-1] = 0 ;
                _writef_log(fd, msgbuff);
                if( fd != STDOUT_FILENO ) {
                    close(fd);
                }
                strncpy(last_logmsg,msgbuff,MAX_LASTLOGMSG-1);
                last_logmsg[MAX_LASTLOGMSG-1]='\0';
            }
        }

        if( priority == LOG_ERR && send_mail_on_error ) {
            // The sibject in the mail will be the name of the daemon together with
            // the canonical hostname
            const char mailSubjectFormat[] = {"tvpvrd@%s - FAILURE"};
            const int tblen = 32;
            const int shortblen = 80;
            char timebuff[tblen] ;
            char subjbuff[shortblen], hostname[shortblen];
            time_t now = time(NULL);
            ctime_r(&now,timebuff);
            timebuff[strnlen(timebuff,tblen)-1] = 0;
            snprintf(msgbuff, blen-1, "%s: %s\n", timebuff, tmpbuff);
            msgbuff[blen-1] = '\0' ;

            // Create subject line with host name
            gethostname(hostname,shortblen);
            hostname[shortblen-1] = '\0';
            snprintf(subjbuff, shortblen, mailSubjectFormat, hostname);
            subjbuff[shortblen-1] = '\0';

            // Extract the last lines from the log file and include in the mail.
            if( strcmp(logfile_name,"stdout") && 0 == tail_logfile(20,logfilebuff,blen) ) {
                strncat(msgbuff,"\n\n---- LAST 20 LINES FROM LOG FILE ----\n",blen-1-strlen(msgbuff));
                strncat(msgbuff,logfilebuff,blen-1-strlen(msgbuff));
                msgbuff[blen-1] = '\0' ;
            }

            if( send_mail(subjbuff,daemon_email_from, send_mailaddress,msgbuff) ) {
                syslog(priority, "'tvpvrd' Failed sending error notification mail. ");
                syslog(priority, "%s", tmpbuff);
            } else {
                logmsg(LOG_DEBUG,"Mail notification on error sent to '%s'",send_mailaddress);
            }
        }

        va_end(ap);
    }

    free(msgbuff);
    free(tmpbuff);
    free(logfilebuff);

}

/* EOF */