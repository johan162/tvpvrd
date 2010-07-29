/* =========================================================================
 * File:        UTILS.H
 * Description: A collection of small utility functions used by the rest
 *              of the server.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009 Johan Persson
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

#ifndef _UTILS_H
#define	_UTILS_H

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * Always holds a copy of the last log message printed to log
 */
extern char last_logmsg[];

/*
 * Pointer to a suitable pre-format function used with the _writef()
 * function. The primary intention is to handle HTMl formatting of
 * returned command outputs transparanetly.
 */
extern char * (*_write_preformat_func)(char *);

/*
 * Utility function to do formatted write simple to a file descriptor
 */
int
_writef(int fd, const char *buf, ...) ;

/*
 * Utility funciotn. Formatting version of syslogger
 */
void
_vsyslogf(int priority, char *msg, ...);

/*
 * Utility function
 * Log message to either specified log file or if no file is specified use
 * system logger
 */
void
logmsg(int priority, char *msg, ...) ;

/*
 * Utility function.
 * Create a timestamp from date and time
 */
time_t
totimestamp(const int year, const int month, const int day,
            const int hour, const int min, const int sec) ;

/*
 * Utility function.
 * Get date and time from a timestamp
 */
int
fromtimestamp(const time_t timestamp, int* year, int* month, int* day,
              int* hour, int* min, int* sec);
/*
 * Utility function
 * Increase start and end day as needed to get the next
 * time according to the recurrence type, day, week, monnth
 * and so on
 */
int 
increcdays(int rectype, time_t *ts_start, time_t *ts_end,
           int *sy, int *sm, int *sd, int *sh, int *smin, int *ssec,
           int *ey, int *em, int *ed, int *eh, int *emin, int *esec);

/*
 * Utility function that uses Perl Regular Expression library to match
 * a string and return an array of the found subexpressions
 */
int
matchcmd(const char *regex, const char *cmd, char ***field);

/*
 * Return a pointer to a statically allocated string which are filled with
 * 'num' repeats of character 'c'. NOTE: Not thread safe since it uses a
 * statically allocated buffer which will be the same for all threads
 */
char *
rptchr_r(char c, unsigned int num, char *buf);


/*
 * Utility function. Convert string to lower case
 */
void
strtolower(char *s);

/*
 * Case insensitive string comparison
 */
int
stricmp(const char *s1, const char *s2);

/*
 * Remove specified directory and all files and directories under it
 * It behaves similar to "rm -rf dir"
 */
int
removedir(const char *dir);

/*
 * Move and possibly rename the file if the target file already exists
 * The new name will be created by adding a numeric suffix to the base
 * name.
 */
int
mv_and_rename(char *from, char *to, char *newname, int size);

/**
 * Copy the string src to dst, but no more than size - 1 bytes, and
 * null-terminate dst.
 *
 * This function is the same as BSD strlcpy().
 *
 * @param dst destination buffer
 * @param src source string
 * @param size size of destination buffer
 * @return the length of src
 */
size_t
xstrlcpy(char *dst, const char *src, size_t size);

/**
 * Append the string src to the string dst, but to a total length of
 * no more than size - 1 bytes, and null-terminate dst.
 *
 * This function is similar to BSD strlcat(), but differs when
 * size <= strlen(dst).
 *
 * @param dst destination buffer
 * @param src source string
 * @param size size of destination buffer
 * @return the total length of src and dst
 */
size_t
xstrlcat(char *dst, const char *src, size_t size);


/**
 * Validate an integer value read form the inifile. If outside the limits
 * terminate program and print error message to log
 * @param min
 * @param max
 * @param errstr
 * @param val
 * @return val if it passes validation
 */
int
validate(const int min, const int max, const char *name, const int val);

/**
 * Get the current system load
 * @param avg1
 * @param avg5
 * @param avg15
 */
void
getsysload(float *avg1, float *avg5, float *avg15) ;

/**
 * Get the current server uptime
 * @param totaltime
 * @param idletime
 */
void
getuptime(int *totaltime, int *idletime);

/**
 * Set the FD_CLOEXEC flag on the specified file. This will cause the
 * file descriptor to automatically close when we do a process replacement
 * via a exec() family call.
 * @param desc
 * @param value
 * @return -a on fialure, 0 on sucess
 */
int
set_cloexec_flag(int desc, int value);

/**
 * Translate a day name (e.g. mon, tue, wed, ...) to a date relative
 * the current date. The dates will always be within the next 7 days
 * The function also recognizes the two special day names "today" and
 * "tomomrrow" which should be self explanatory.
 * @param wdayname , Name of weekday. Recognized names are
 *                   ["mon","tue","wed","thu","fri","sat","sun","today","tomorrow"]
 * @param y The year
 * @param m The month
 * @param d The day
 * @return
 */
int
getreldatefromdayname(const char *wdayname, int *y, int *m, int *d);

/**
 * Check if a directory exists and if not then create it
 * @param basedir Basedirectory where the new directory will be created
 * @param dir The name of the directory to create
 */
int chkcreatedir(const char *basedir,char *dir);

/**
 * Strip the suffix by replacing the last '.' with a '\0'
 * The found suffix is placed in the space pointed to by
 * the suffix parameter
 * @param filename
 */
int
strip_filesuffix(char *filename,char *suffix, int slen);

/**
 * Return the size of the virtual memory used by the specified
 * process id as well as the number of running threads in the process.
 * @param pid
 * @param size
 * @param unit
 * @param threads
 * @return
 */
int
getwsetsize(int pid, int *size, char *unit, int *threads);


/**
 * Show the last n line of the logfile
 * @param n
 * @param buffer
 * @param maxlen
 * @return
 */
int
tail_logfile(int n, char *buffer, int maxlen);

/**
 * Send a simple text mail to the specified recepients
 * @param subject
 * @param to
 * @param message
 * @return 0 on success -1 otherwise
 */
int
send_mail(const char *subject, const char *to, const char *message);


//-----------------------------------------------------------------------------
// Various defines for Regular expression matching of commands
// This is normally used with the matchcmd() function
//-----------------------------------------------------------------------------

/*
 * First a number of generic unicode regex defines
 */
// Required space(s)
#define _PR_S "[\\p{Z}]+"

// Optional space(s)
#define _PR_SO "[\\p{Z}]*"

// Required alphanumeric sequence
#define _PR_AN "([\\p{L}\\p{N}]+)"

// Required filepath
#define _PR_FILEPATH "([\\p{L}\\p{N}\\/\\.\\_\\-]+)"

// Required alphanumeric and punctuation sequence
//#define _PR_ANP "([\\p{L}\\p{N}\\p{P}]+)"
#define _PR_ANP "([\\p{L}\\p{N}\\p{P}]+)"

// Required alphanumeric, punctuation and space sequence
#define _PR_ANPS "([\\p{L}\\p{N}\\p{P} ]+)"

// Any sequence of symbols
#define _PR_ANY "(\\X+)"

#define _PR_E "$"

/*
 * Symbolic names for entitis in the command strings
 */
// Recording ID
#define _PR_ID "([\\p{N}]{1,3})"

// Optional ID (three digit number)
#define _PR_OPID "([\\p{N}]{1,3})?"

// Required full time (h:m)
#define _PR_TIME "([0-1][0-9]|2[0-3]):([0-5][0-9])"

// Required full time with optional seconds
#define _PR_TIMS "([0-1][0-9]|2[0-3]):([0-5][0-9])(:[0-5][0-9])?"

// Optional time. Only hour required
#define _PR_OPTIME "([0-1][0-9]|2[0-3])(:([0-5][0-9]))?(:([0-5][0-9]))?"

// required full date
#define _PR_FULLDATE "(201[0-9]|2009)-(0[1-9]|1[0-2])-([0-2][0-9]|3[0-2])"

// Relative date from today
#define _PR_RELDATE "(today|tomorrow|mon|monday|tue|tuesday|wed|wednesday|thu|thursday|fri|friday|sat|saturday|sun|sunday)"
#define _PR_DATE "(" _PR_RELDATE "|" _PR_FULLDATE ")"

#define _PR_VIDEO "([0-5])"
#define _PR_DURATION "(0?[0-3]):([0-5][0-9])"
#define _PR_CHANNEL "([\\p{L}][\\p{L}\\p{N}\\p{P}\\+]*)"

#define _PR_TITLE "(\\p{L}[\\p{L}\\p{N} _-]+)"
#define _PR_OPTITLE "(" _PR_S "(\\p{L}[\\p{L}\\p{N} _-]*))?"

#define _PR_PROFN "(@[\\p{L}\\p{N}]+)?"
#define _PR_PROFE "(" _PR_PROFN ")?"
#define _PR_PROFILES "(" _PR_PROFN _PR_SO _PR_PROFN _PR_SO _PR_PROFN ")?"


#ifdef	__cplusplus
}
#endif

#endif	/* _UTILS_H */

