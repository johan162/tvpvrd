/* =========================================================================
 * File:        UTILS.C
 * Description: A collection of small utility functions used by the rest
 *              of the server.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010 Johan Persson
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
#include <unistd.h>  
#include <pcre.h>    
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <locale.h>


// Local headers
#include "tvpvrd.h"
#include "utils.h"

// Last logmessage
#define MAX_LASTLOGMSG 1024
char last_logmsg[MAX_LASTLOGMSG] = {'\0'};

// Needed to flag we are in the logmsg function to avoid
// _writef doing possible html output encoding
static int inlogfunction=0;
int htmlencode_flag=0;

/*
 * _writef
 * Utility function
 * Simplify a formatted write to a file descriptor
 */
int
_writef(int fd, const char *buf, ...) {
    if( fd >= 0 ) {
        const int blen = MAX(8192*2,strlen(buf)+1);
        char *tmpbuff = calloc(blen,sizeof(char));
        if( tmpbuff == NULL ) {
            logmsg(LOG_ERR,"FATAL: Cannot allocate buffer in _writef()");
            return -1;
        }
        va_list ap;
        va_start(ap, buf);
        vsnprintf(tmpbuff, blen, buf, ap);
        tmpbuff[blen-1] = 0;
        int ret;
        if( !inlogfunction && htmlencode_flag ) {
            char *htmlbuff = html_encode(tmpbuff);
            ret = write(fd, htmlbuff, strlen(htmlbuff));
            free( htmlbuff);
        } else {
            ret = write(fd, tmpbuff, strnlen(tmpbuff,blen));
        }
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
    syslog(priority,tmpbuff);
}

/*
 * Utility function
 * Log message to either specified log file or if no file is specified use
 * system logger. The name of the output device to use is set in the main
 * program and communicated here with a global variable
 */
#define MAXLOGFILEBUFF 10*1024

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
    if ( (priority == LOG_ERR) ||
         ((priority == LOG_INFO)   && verbose_log > 0) ||
         ((priority == LOG_NOTICE) && verbose_log > 1) ||
         ((priority == LOG_DEBUG)  && verbose_log > 2)      ) {
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

        if ( *logfile_name == '\0' ||  strcmp(logfile_name, LOGFILE_SYSLOG) == 0 ) {
            if( !_loginit ) {
                openlog(server_program_name, LOG_PID | LOG_CONS, LOG_DAEMON);
                _loginit = 1;
            }
            syslog(priority, tmpbuff);
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
                syslog(priority, tmpbuff);
            } else {
                static const int tblen = 32;
                char timebuff[tblen] ;
                time_t now = time(NULL);
                ctime_r(&now,timebuff);
                timebuff[strnlen(timebuff,tblen)-1] = 0;
                snprintf(msgbuff, blen-1, "%s: %s\n", timebuff, tmpbuff);
                msgbuff[blen-1] = 0 ;
                inlogfunction = 1;
                _writef(fd, msgbuff);
                inlogfunction = 0;
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
            if( 0 == tail_logfile(20,logfilebuff,blen) ) {
                strncat(msgbuff,"\n\n---- LAST 20 LINES FROM LOG FILE ----\n",blen);
                strncat(msgbuff,logfilebuff,blen-1);
                msgbuff[blen-1] = '\0' ;
            }

            if( send_mail(subjbuff,send_mailaddress,msgbuff) ) {
                syslog(priority, "'tvpvrd' Failed sending error notification mail. ");
                syslog(priority, tmpbuff);
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

/*
 * Utility function.
 * Create a timestamp from date and time
 */
time_t
totimestamp(const int year, const int month, const int day,
            const int hour, const int min, const int sec) {

    time_t timestamp;
    struct tm time_struc;

    time_struc.tm_sec = sec;
    time_struc.tm_min = min;
    time_struc.tm_hour = hour;
    time_struc.tm_mday = day;
    time_struc.tm_mon = month - 1;
    time_struc.tm_year = year - 1900;
    time_struc.tm_isdst = -1;

    timestamp = mktime(&time_struc);

    if (timestamp == -1) {
        logmsg(LOG_ERR, "totimestamp() : Cannot convert tm to timestamp (%d : %s)",
               errno,strerror(errno));
        return -1;
    }

    return timestamp;
}

/*
 * Utility function.
 * Get date and time from a timestamp
 */
int
fromtimestamp(const time_t timestamp, int* year, int* month, int* day,
        int* hour, int* min, int* sec) {

    struct tm time_struc;
    if( NULL == localtime_r(&timestamp,&time_struc) ) {
        logmsg(LOG_ERR,"fromtimestamp() : Cannot convert timestamp (%d : %s)",
               errno,strerror(errno));
        return -1;
    }

    *year = time_struc.tm_year + 1900;
    *month = time_struc.tm_mon + 1;
    *day = time_struc.tm_mday;
    *hour = time_struc.tm_hour;
    *min = time_struc.tm_min;
    *sec = time_struc.tm_sec;

    return 0;
}

/*
 * Utility function
 * Increase start and end day as needed to get the next
 * time according to the recurrence type, day, week, monnth
 * and so on
 */
int
increcdays(int rectype,
        time_t *ts_start, time_t *ts_end,
        int *sy, int *sm, int *sd, int *sh, int *smin, int *ssec,
        int *ey, int *em, int *ed, int *eh, int *emin, int *esec) {
    struct tm tm_start;

    // Find out the new date for the next recording in sequence
    switch (rectype) {
        case 0:
            // Single, do nothing
            break;

        case 1:
            // Every day
            *sd += 1;
            *ed += 1;
            break;

        case 2:
            // Every week
            *sd += 7;
            *ed += 7;
            break;

        case 3:
            // Every month
            sm++;
            em++;
            break;

        case 4:
            // Mon-Fri
            do {
                *sd += 1;
                *ed += 1;
                tm_start.tm_sec = *ssec;
                tm_start.tm_min = *smin;
                tm_start.tm_hour = *sh;
                tm_start.tm_mday = *sd;
                tm_start.tm_mon = *sm - 1;
                tm_start.tm_year = *sy - 1900;
                tm_start.tm_isdst = -1;
                mktime(&tm_start);
            } while (tm_start.tm_wday == 6 || tm_start.tm_wday == 0);
            break;

        case 5:
            // Sat-Sun
            do {
                *sd += 1;
                *ed += 1;
                tm_start.tm_sec = *ssec;
                tm_start.tm_min = *smin;
                tm_start.tm_hour = *sh;
                tm_start.tm_mday = *sd;
                tm_start.tm_mon = *sm - 1;
                tm_start.tm_year = *sy - 1900;
                tm_start.tm_isdst = -1;
                mktime(&tm_start);
            } while (tm_start.tm_wday != 6 && tm_start.tm_wday != 0);
            break;

        case 6:
            // Mon-Thu
            do {
                *sd += 1;
                *ed += 1;
                tm_start.tm_sec = *ssec;
                tm_start.tm_min = *smin;
                tm_start.tm_hour = *sh;
                tm_start.tm_mday = *sd;
                tm_start.tm_mon = *sm - 1;
                tm_start.tm_year = *sy - 1900;
                tm_start.tm_isdst = -1;
                mktime(&tm_start);
            } while (tm_start.tm_wday >= 5 || tm_start.tm_wday == 0 );
            break;

        default:
            logmsg(LOG_ERR, "Unknown type of repeat specified for record.");
            return -1;
            break;
    }

    // We need to do a full cycle to adjust the values in case
    // one of the values has wrapped
    *ts_start = totimestamp(*sy, *sm, *sd, *sh, *smin, *ssec);
    if( *ts_start >= 0 ) {
        fromtimestamp(*ts_start, sy, sm, sd, sh, smin, ssec);
    } else {
        logmsg(LOG_ERR,"increcdays() : FATAL Corrupt timeconversion. Cannot continue.");
        (void)exit(EXIT_FAILURE);
        return -1;
    }

    *ts_end = totimestamp(*ey, *em, *ed, *eh, *emin, *esec);
    if( *ts_end >= 0 ) {
        fromtimestamp(*ts_end, ey, em, ed, eh, emin, esec);
    } else {
        logmsg(LOG_ERR,"increcdays() : FATAL Corrupt timeconversion. Cannot continue.");
        (void)exit(EXIT_FAILURE);
        return -1;
    }
    return 0;
}

/*
 * Utility function that uses Perl Regular Expression library to match
 * a string and return an array of the found subexpressions
 * NOTE: It is the calling routines obligation to free the returned
 * field with a call to
 * pcre_free_substring_list((const char **)field);
 */
int
matchcmd(const char *regex, const char *cmd, char ***field) { //, const char *func, int line) {
    pcre *cregex;
    int ovector[100];
    const char *errptr;
    int erroff,ret;

    //logmsg(LOG_DEBUG, "matchcmd() called from '%s()' at line #%05d",func,line);
    cregex = pcre_compile(regex,PCRE_CASELESS|PCRE_UTF8,&errptr,&erroff,NULL);
    if( cregex ) {
        ret = pcre_exec(cregex,NULL,cmd,strlen(cmd),0,0,ovector,90);
        pcre_free(cregex);
        if( ret > 0 ) {
            (void)pcre_get_substring_list(cmd,ovector,ret,(const char ***)field);
            return ret;
        }
    }
    return -1;
}

/*
 * Utility function that uses Perl Regular Expression library to match
 * a string and return an array of the found subexpressions
 * NOTE: It is the calling routines obligation to free the returned
 * field with a call to
 * pcre_free_substring_list((const char **)field);
 */
int
matchcmd_ml(const char *regex, const char *cmd, char ***field) { //, const char *func, int line) {
    pcre *cregex;
    int ovector[100];
    const char *errptr;
    int erroff,ret;

    //logmsg(LOG_DEBUG, "matchcmd() called from '%s()' at line #%05d",func,line);
    cregex = pcre_compile(regex,PCRE_CASELESS|PCRE_UTF8|PCRE_NEWLINE_CRLF|PCRE_MULTILINE,&errptr,&erroff,NULL);
    if( cregex ) {
        ret = pcre_exec(cregex,NULL,cmd,strlen(cmd),0,0,ovector,90);
        pcre_free(cregex);
        if( ret > 0 ) {
            (void)pcre_get_substring_list(cmd,ovector,ret,(const char ***)field);
            return ret;
        }
    }
    return -1;
}

void
matchcmd_free(char **field) { //,const char *func, int line) {
    //logmsg(LOG_DEBUG, "matchcmd_free() called from '%s()' at line #%05d",func,line);
    if( field != (void *)NULL ) {
        pcre_free_substring_list((const char **)field);
    }
}



/*
 * Fill the supplied buffer with 'num' repeats of character 'c'
 */
char *
rptchr_r(char c, unsigned int num, char *buf) {
    num = MIN(255, num);
    for (int i = 0; i < num; i++)
        buf[i] = c;
    buf[num] = '\0';
    return buf;
}


/*
 * Utility function. Convert string of maximum 4095 characters to lower case
 */
void
strtolower(char *s) {
    int safetylimit = 4096;
    while ( --safetylimit > 0 && (*s = tolower(*s)) ) {
        s++;
    }
    if( safetylimit <= 0 ) {
        logmsg(LOG_ERR,"FATAL : strtolower() : Failed safetylimit !");
        (void)exit(EXIT_FAILURE);
    }
}

/**
 * Cases insensitive string comparison
 * @param dir
 * @return
 */
int
stricmp(const char *s1, const char *s2) {
    const int safetylimit = 4096;
    int len1 = strnlen(s1,safetylimit);
    int len2 = strnlen(s2,safetylimit);
    if( len1 >= safetylimit || len2 >= safetylimit ) {
        logmsg(LOG_ERR,"FATAL : stricmp() safetylimit exceedded !");
        (void)exit(EXIT_FAILURE);
    }
    char *b1=strdup(s1);
    char *b2=strdup(s2);
    if( b1 == NULL || b2 == NULL ) {
        logmsg(LOG_ERR,"FATAL: stricmp() Out of memory ! ( %d : %s )",errno,strerror(errno));
        (void)exit(EXIT_FAILURE);
    }
    strtolower(b1);
    strtolower(b2);
    int ret=strncmp(b1,b2,safetylimit);
    free(b1);
    free(b2);
    return ret;
}

/*
 * Remove specified directory and all files and directories under it
 * It behaves similar to "rm -rf dir"
 */
int
removedir(const char *dir) {
    struct dirent *dirp;
    struct stat statbuf;
    char tmpbuff[512];

    DIR *dp = opendir(dir);
    if (dp == NULL) {
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL) {
        if ((strcmp(dirp->d_name, ".") != 0) && (strcmp(dirp->d_name, "..") != 0)) {
            snprintf(tmpbuff, 512, "%s/%s", dir, dirp->d_name);
            lstat(tmpbuff, &statbuf);

            if (S_ISDIR(statbuf.st_mode)) {
                if (removedir(tmpbuff) < 0) {
                    (void) closedir(dp);
                    return -1;
                }
            } else if (S_ISREG(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) {
                if (remove(tmpbuff)) {
                    (void) closedir(dp);
                    return -1;
                }
            } else {
                (void) closedir(dp);
                return -1;
            }
        }
    }
    int ret = rmdir(dir);
    (void) closedir(dp);
    return ret;
}

/**
 * Move file from "from" to "to" if the target file already
 * exists then try to rename the file by adding a "_nn" suffix
 * where nn is a two digit number. The file will keep the same
 * file extension.
 * The "newname" buffer is set to the name of the file copied
 * to which might have changed.
 * @param from
 * @param to
 * @return 0 on sucess, -1 on fail
 */
int
mv_and_rename(char *from, char *to, char *newname, int size) {
    struct stat fstat;
    char buff1[256],short_filename[128],to_directory[256], suffix[8];

    *newname = '\0';
    if( -1 == stat(from,&fstat) ) {
        logmsg(LOG_ERR,"FATAL: Cannot move and rename file '%s'. (%d : %s)",from,errno,strerror(errno));
        return -1;
    }

    if( 0 == stat(to,&fstat) ) {
        // File exists. Try renaming it and see if that works
        strncpy(buff1,to,255);
        strncpy(short_filename, basename(buff1),127);
        short_filename[127] = '\0';
        strncpy(to_directory, dirname(buff1),255);
        to_directory[255] = '\0';
        int k = strlen(short_filename)-1;
        while( k >  0 && short_filename[k] != '.' ) {
            k--;
        }
        if( short_filename[k] == '.' ) {
            if( strlen(&short_filename[k]) > 7 ) {
                // A suffix > 7 characters, don't think so!
                logmsg(LOG_ERR,"FATAL: Cannot move and rename file '%s' to '%s'. Invalid file suffix '%s'",
                       from,to,&short_filename[k]);
                return -1;
            }
            strncpy(suffix,&short_filename[k],7);
            suffix[7] = '\0';
            short_filename[k] = '\0';
        } else {
            logmsg(LOG_ERR,"FATAL: Cannot move and rename file '%s' to '%s' destination file must have a valid suffix.",
                   from,to);
            return -1;
        }

        int i=0, ret;
        do {
            i++;
            snprintf(buff1,255,"%s/%s_%03d%s",to_directory,short_filename,i,suffix);
            ret = stat(buff1,&fstat);
        } while( i < 999 && ret == 0 );
        if( i >= 999 ) {
            logmsg(LOG_ERR,"FATAL: Cannot move and rename file '%s' to '%s'. Too many duplicates.",
                   from,to);
            return -1;
        }
    } else if( errno == ENOENT ) {
        // File doesn't exist
        strncpy(buff1,to,255);
        buff1[255] = '\0';
    } else {
        // Some other problems
        logmsg(LOG_ERR,"FATAL: Cannot move and rename file '%s' to '%s' (%d : %s)",
               from,to,errno,strerror(errno));
        return -1;
    }
    int ret = rename(from, buff1);
    if( -1 == ret ) {
        logmsg(LOG_ERR,"FATAL: Cannot move and rename file '%s' to '%s' (%d : %s)",
              from,buff1,errno,strerror(errno));
    }
    strncpy(newname,buff1,size);
    newname[size-1] = '\0';
    return ret;
}

/**
 * String copying with extra safety
 * @param dst
 * @param src
 * @param size
 * @return Final lenght of destintaion string
 */
size_t
xstrlcpy(char *dst, const char *src, size_t size) {
    *dst = '\0';
    strncpy(dst,src,size-1);
    if( size > 0 )
        dst[size-1] = '\0';
    else
        dst[0] = '\0';
    return strnlen(dst,size);
}

/**
 * String concatenation with extra safety
 * @param dst
 * @param src
 * @param size
 * @return final length of ddt string
 */
size_t
xstrlcat(char *dst, const char *src, size_t size) {
    if( strnlen(dst,size-1) == size-1 )
        return size-1;
    if( strlen(src) + strlen(dst) < size ) {
        strncat(dst,src,size-1);
        if( size > 0 )
            dst[size-1] = '\0';
        else
            dst[0] = '\0';
    }
    return strnlen(dst,size);
}

/**
 * Validate a given parameter against a min/max value
 * @param min
 * @param max
 * @param name
 * @param val
 * @return val if in bounds
 */
int
validate(const int min, const int max, const char *name, const int val) {
    if( val >= min && val <= max )
        return val;
    logmsg(LOG_ERR,"Value for '%s' is out of allowed range [%d,%d]. Aborting. \n",name,min,max);
    (void)exit(EXIT_FAILURE);
    return -1;
}

/**
 * Get system load
 * @param avg1
 * @param avg5
 * @param avg15
 */
void
getsysload(float *avg1, float *avg5, float *avg15) {
    char lbuff[24];
    int ld = open("/proc/loadavg", O_RDONLY);
    if( -1 == read(ld, lbuff, 24) ) {
        logmsg(LOG_ERR,"FATAL: Cannot read '/proc/loadavg' ( %d : %s )",errno,strerror(errno));
        *avg1=-1; *avg5=-1; *avg15=-1;
    }
    close(ld);
    sscanf(lbuff, "%f%f%f", avg1, avg5, avg15);
}

/**
 * Get total system uptime
 * @param totaltime
 * @param idletime
 */
void
getuptime(int *totaltime, int *idletime) {
    int ld = open("/proc/uptime", O_RDONLY);

    char lbuff[24];
    if( -1 == read(ld, lbuff, 24) ) {
        logmsg(LOG_ERR,"FATAL: Cannot read '/proc/uptime' ( %d : %s )",errno,strerror(errno));
        *totaltime = 0;
        *idletime = 0;
    }
    close(ld);

    float tmp1,tmp2;
    sscanf(lbuff,"%f%f",&tmp1,&tmp2);
    *totaltime = round(tmp1);
    *idletime = round(tmp2);
}

/**
 * Set FD_CLOEXEC file flag. This will close a stream unconditoinally when
 * a process is terminated
 * @param desc
 * @param value
 * @return
 */
int
set_cloexec_flag(int desc, int value) {
    int oldflags = fcntl(desc, F_GETFD, 0);

    if (oldflags < 0)
        return oldflags;

    if (value != 0) {
        oldflags |= FD_CLOEXEC;
    } else {
        oldflags &= ~FD_CLOEXEC;
    }
    
    return fcntl(desc, F_SETFD, oldflags);
}

/**
 * Get a realtive day specifeid by a weekday name. This will return a date
 * within the following seven days. If the weekdat name is the same as the
 * current day this will then refer to the next such day within the next
 * seven days.
 * @param wdayname
 * @param y
 * @param m
 * @param d
 * @return
 */
int
getreldatefromdayname(const char *wdayname, int *y, int *m, int *d) {
    const char *wday[] = {
        "sun","mon","tue","wed","thu","fri","sat"
    };
    int yy,mm,dd,hh,min,sec;
    int i=0,step=0;

    fromtimestamp(time(NULL), &yy, &mm, &dd, &hh, &min, &sec);

    if( stricmp("today",wdayname) == 0 || stricmp("tod",wdayname) == 0 ) {
        *y = yy;
        *m = mm;
        *d = dd;
        return 0;
    }

    if( stricmp("tomorrow",wdayname) == 0 || stricmp("tom",wdayname) == 0 ) {
        dd++;
        time_t tom = totimestamp(yy, mm, dd, hh, min, sec);
        fromtimestamp(tom, &yy, &mm, &dd, &hh, &min, &sec);
        *y = yy;
        *m = mm;
        *d = dd;
        return 0;
    }

    struct tm tm_now;
    tm_now.tm_sec = sec;
    tm_now.tm_min = min;
    tm_now.tm_hour = hh;
    tm_now.tm_mday = dd;
    tm_now.tm_mon = mm - 1;
    tm_now.tm_year = yy - 1900;
    tm_now.tm_isdst = -1;
    mktime(&tm_now);

    for(i=0; i < 7; i++) {
        if( stricmp(wday[i], wdayname) == 0) {
            if( tm_now.tm_wday > i ) {
                step = (7-tm_now.tm_wday ) + i;
            } else if(tm_now.tm_wday < i)  {
                step = i - tm_now.tm_wday ;
            } else {
                step = 7;
            }
            break;
        }
    }

    if( i >= 7 ) {
        logmsg(LOG_ERR,"getreldatefromdayname() : Unknown dayname '%s'",wdayname);
        return -1;
    }

    tm_now.tm_mday += step;
    mktime(&tm_now);
    *y = tm_now.tm_year+1900;
    *m = tm_now.tm_mon+1;
    *d = tm_now.tm_mday;

    return 0;
}

/*
 * Check if directory exists and if not create it
 */
int
chkcreatedir(const char *basedir,char *dir) {
    const mode_t mode =  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    char bdirbuff[512];
    struct stat fstat ;

    snprintf(bdirbuff,511,"%s/%s",basedir,dir);
    bdirbuff[511] = '\0';
    logmsg(LOG_NOTICE,"Checking directory '%s'",bdirbuff);
    if( -1 == stat(bdirbuff,&fstat) ) {
        if( -1 == mkdir(bdirbuff,mode) ) {
            logmsg(LOG_ERR,"FATAL: Cannot create directory %s (%d : %s).",
                   bdirbuff,errno,strerror(errno));
            return -1;
        } else {
	    logmsg(LOG_NOTICE,"Created directory '%s'",bdirbuff);
	}
    }
    return 0;
}

/**
 * Strip the suffix by replacing the last '.' with a '\0'
 * The found suffix is placed in the space pointed to by
 * the suffix parameter
 * @param filename
 */
int
strip_filesuffix(char *filename,char *suffix, int slen) {
    int len = strnlen(filename,256);
    if( len >= 256 ) {
        logmsg(LOG_ERR,"FATAL: String too long to strip suffix");
        return -1;
    }
    int k=len-1;
    while(k>0 && filename[k] != '.' ) {
        k--;
    }
    if( k > 0 ) {
        strncpy(suffix,&filename[k+1],slen);
        suffix[slen-1] = '\0';
        filename[k] = '\0';
    }
    return 0;
}

/**
 * Find out the size of the working set for the specified process id
 * and the current number of running threads.
 * This corresponds to the allocated virtual memory for this process.
 * @param pid process id
 * @param unit string with the unit for the size (normally kB)
 * @param threads Number of running threads
 * @return 0 on success -1 on failure
 */
int
getwsetsize(int pid, int *size, char *unit, int *threads) {
    char buffer[256];
    char linebuffer[1024];

    *size = -1;
    *unit = '\0';
    *threads = -1;

    sprintf(buffer,"/proc/%d/status",pid);

    FILE *fp = fopen(buffer,"r");
    if( fp == NULL ) {
        logmsg(LOG_ERR,"Cannot open '%s' (%d : %s)\n",buffer,errno,strerror(errno));
        return -1;
    }

    char tmpbuff[64];
    while( NULL != fgets(linebuffer,1023,fp) ) {
        strncpy(tmpbuff,linebuffer,6);
        tmpbuff[6] = '\0';
        if( strcmp(tmpbuff,"VmSize") == 0  ) {
            sscanf(linebuffer,"%s %d %s",tmpbuff,size,unit);
        }
        if( strcmp(tmpbuff,"Thread") == 0  ) {
            sscanf(linebuffer,"%s %d",tmpbuff,threads);
            break;
        }
    }

    fclose(fp);

    if( -1 == *size || -1 == *threads || '\0' == *unit ) {
        logmsg(LOG_ERR,"getwsetsize() : Failed to read process information.");
        return -1;
    }

    return 0;

}

/**
 * Return the last n line from the logfile
 * @param n Number of lines to return
 * @param buffer Buffer
 * @param maxlen Maximum length of the buffer
 * @return 0 success, -1 failure
 */
int
tail_logfile(int n, char *buffer, int maxlen) {
    if( n < 1 || n > 999 )
        return -1;

    // We can only show the logfile if a proper file has been specified
    // and not stdout or the system logger
    if( strcmp(logfile_name,"stdout") == 0 || strcmp(logfile_name,LOGFILE_SYSLOG) == 0) {
        logmsg(LOG_ERR,"Trying to view logfile when logfile is not a file.");
        return -1;
    }

    char cmd[512];
    snprintf(cmd,512,"tail -n %d %s",n,logfile_name);

    FILE *fp = popen(cmd, "r");
    if( fp == NULL ) {
        logmsg(LOG_ERR,"Failed popen() in tail_logfile(). (%d : %s)",errno,strerror(errno));
        return -1;
    }

    const int maxbuff=512;
    char linebuffer[maxbuff];
    *buffer = '\0';
    while( maxlen > 1024 && NULL != fgets(linebuffer,maxbuff,fp) ) {
        strncat(buffer,linebuffer,maxlen);
        maxlen -= strlen(linebuffer);
    }

    if( maxlen <= maxbuff ) {
        *buffer = '\0';
        pclose(fp);
        return -1;
    }
    pclose(fp);

    return 0;
}

/**
 * Send a simple text mail to the specified recepients
 * @param subject
 * @param to
 * @param message
 * @return 0 on success -1 otherwise
 */
int
send_mail(const char *subject, const char *to, const char *message) {
    const int blen=20*1024;
    char buffer[blen];

    /*
    char *loc = getenv("LC_ALL");
    if( loc ) {
        logmsg(LOG_DEBUG,"Current system LC_ALL=%s",loc);
    } else {
        logmsg(LOG_DEBUG,"Current system UNKNOWN",loc);
    }
    */

    if( strlen(message) >= blen ) {
        syslog(LOG_ERR,"Truncating mail sent from 'tvpvrd'");
    }

    snprintf(buffer,blen-1,
	"echo '%s' | /usr/bin/mail -s '%s' '%s'",message, subject, to);

    logmsg(LOG_DEBUG,"Mail sent to: '%s' with subject: '%s'",to,subject);

    return system(buffer);

}

/* Converts a hex character to its integer value */
char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(char code) {
  static char hex[] = "0123456789ABCDEF";
  return hex[code & 15];
}

/*
 * URL encode a buffer.
 * Note: Calling function is responsible to free returned string
 */
char *url_encode(char *str) {
  char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
      *pbuf++ = *pstr;
    else if (*pstr == ' ')
      *pbuf++ = '+';
    else
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
  }
  *pbuf = '\0';
  return buf;
}

/*
 * URL decode a buffer.
 * Note: Calling function is responsible to free returned string
 */
char *url_decode(char *str) {
  char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;
  while (*pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    } else if (*pstr == '+') {
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
  }
  *pbuf = '\0';
  return buf;
}

/*
 * HTML encode a buffer.
 * Note: Calling function is responsible to free returned string
 */
char *html_encode(char *str) {
    char *pstr=str, *buf = malloc(strlen(str) * 6 + 1), *pbuf = buf;
    while (*pstr) {
        switch( *pstr ) {
            case '<':
                *pbuf++ = '&';
                *pbuf++ = 'l';
                *pbuf++ = 't';
                *pbuf++ = ';';
                break;
            case '>':
                *pbuf++ = '&';
                *pbuf++ = 'g';
                *pbuf++ = 't';
                *pbuf++ = ';';
                break;
            case '&':
                *pbuf++ = '&';
                *pbuf++ = 'a';
                *pbuf++ = 'm';
                *pbuf++ = 'p';
                *pbuf++ = ';';
                break;
            case '"':
                *pbuf++ = '&';
                *pbuf++ = 'q';
                *pbuf++ = 'u';
                *pbuf++ = 'o';
                *pbuf++ = 't';
                *pbuf++ = ';';
                break;
            default:
                *pbuf++ = *pstr;
                break;
        }
        ++pstr;
    }
    *pbuf = '\0';
    return buf;
}


/**
 * Trim a string by removing beginning and ending spaces
 * @param str
 */
void strtrim(char *str) {
    char *tmp = strdup(str),*startptr=tmp;
    int n=strlen(str);
    char *endptr = tmp+n-1;

    while( *startptr == ' ') {
        startptr++;
    }

    while( n>0 && *endptr == ' ') {
        --n;
        --endptr;
    }

    while( startptr <= endptr ) {
        *str++ = *startptr++;
    }

    *str = '\0';
}

/**
 * Get associated value from a list of keys and values. The returned values are
 * stripped from beginning and ending spaces
 * @param value
 * @param maxlen
 * @param key
 * @param list
 * @param n
 * @return
 */
int
get_assoc_value(char *value,int maxlen,char *key,char *list[],int n) {
    int i = 0;
    while( i < n ) {
        if( 0 == strcmp(key,list[i]) ) {
            strncpy(value,list[i+1],maxlen);
            strtrim(value);
            return 0;
        }
        i += 2;
    }
    return -1;
}
/* utils.c */