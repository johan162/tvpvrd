/* =========================================================================
 * File:        UTILS.H
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

#ifndef _UTILS_H
#define	_UTILS_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef DEBUG_SIMULATE
#define _dbg_close(fd) _x_dbg_close(fd)
#else
#define _dbg_close(fd) close(fd)
#endif

int
_x_dbg_close(int fd);

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
#define matchcmd_free(field) _matchcmd_free((field),__FUNCTION__,__LINE__)
#define matchcmd(regex,cmd,field) _matchcmd((regex), (cmd),(field),__FUNCTION__,__LINE__)
*/
/*
 * Utility function that uses Perl Regular Expression library to match
 * a string and return an array of the found subexpressions
 */
//int
//_matchcmd(const char *regex, const char *cmd, char ***field,const char *func, int line);

/*
 * This must be called after matchcmd() in order to free the allocated
 * field array
 */
//void
//_matchcmd_free(char **field,const char *func, int line);


/*1016
 * Utility function that uses Perl Regular Expression library to match
 * a string and return an array of the found subexpressions
 */
int
matchcmd(const char *regex, const char *cmd, char ***field);

/*
 * Utility function that uses Perl Regular Expression library to match
 * a string and return an array of the found subexpressions
 * Match Command which is multiline
 */
int
matchcmd_ml(const char *regex, const char *cmd, char ***field);

/*
 * This must be called after matchcmd() in order to free the allocated
 * field array
 */
void
matchcmd_free(char ***field);

/*
 * Return a pointer to a statically allocated string which are filled with
 * 'num' repeats of character 'c'. NOTE: Not thread safe since it uses a
 * statically allocated buffer which will be the same for all threads
 */
char *
rptchr_r(char c, size_t num, char *buf);


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
mv_and_rename(char *from, char *to, char *newname, size_t maxlen);

/**
 * Validate an integer value. If outside the limits
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
 * Show the last n line of the logfile
 * @param n
 * @param buffer
 * @param maxlen
 * @return
 */
int
tail_logfile(unsigned n, char *buffer, size_t maxlen);

char *
url_encode(char *str);

char *
url_decode(char *str);

char *
html_encode(char *str);

char *
esc_percentsign(char *str);

extern int htmlencode_flag;

/**
 * Return the associated value in an aray of key/value pairs
 * @param value
 * @param maxlen
 * @param key
 * @param list
 * @param listlen
 * @return
 */
int
get_assoc_value(char *value, size_t maxlen, char *key, char *list[], size_t listlen);

/**
 * Trim a string by removing beginning and ending psaces
 * @param string
 */
void
xstrtrim(char *string);

/**
 * Escape doublequotes in a string
 * @param tostr
 * @param fromstr
 * @param maxlen
 */
void
escape_quotes(char *tostr, const char *fromstr, const size_t maxlen, unsigned remove_n);


/**
 * Copy the string src to dst, but no more than size - 1 bytes, and
 * null-terminate dst.
 *
 * This function is the same as BSD strlcpy().
 *
 * @param dst destination buffer
 * @param src source string
 * @param maxlen size of destination buffer
 * @return the length of src
 */
size_t
xstrlcpy(char *dst, const char *src, size_t maxlen);

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
 * Safe version of atoi() with extende3d error checking
 * @param str
 * @return value found in string
 */
int
xatoi(char * const str);


//-----------------------------------------------------------------------------
// Various defines for Regular expression matching of commands
// This is normally used with the matchcmd() function
//-----------------------------------------------------------------------------

/*
 * First a number of generic unicode regex defines
 */

// Match HTTP Header date format, i.e. Sat, 29 Oct 1994 19:43:31 GMT
#define _PR_HTTPHEADER_DATE "([\\p{L}]{3}, [\\p{N}]{2} [\\p{L}]{3} [\\p{N}]{4} [\\p{N}:]{8} [\\p{L}]{3})"

// Required space(s)
#define _PR_S "[\\p{Z}]+"

// Optional space(s)
#define _PR_SO "[\\p{Z}]*"

// Required alphanumeric sequence
#define _PR_AN "([\\p{L}\\p{N}\\_]+)"

// Optional alphanumeric sequence
#define _PR_ANO "([\\p{L}\\p{N}\\_]*)"

// Optional alphanumeric sequence
#define _PR_ANSO "([\\p{L}\\p{N}\\_ ]*)"

// Required numeric sequence
#define _PR_N "([\\p{N}]+)"

// Required filepath
#define _PR_FILEPATH "([\\p{L}\\p{N}\\/\\.\\_\\-]+)"

// Required alphanumeric and punctuation sequence
#define _PR_ANP "([\\p{L}\\p{N}\\p{P}\\>\\<\\+\\;\\:\\$\\,\\'\\`\\'\\-\\&\\#]+)"

#define _PR_ANPO "([\\p{L}\\p{N}\\p{P}\\>\\<\\+\\;\\:\\$\\,\\'\\`\\'\\-\\&\\#]*)"

// Required alphanumeric, punctuation and space sequence
#define _PR_ANPS "([\\p{L}\\p{N}\\p{P} \\>\\<\\+\\;\\:\\$\\,\\'\\`\\']+)"

#define _PR_ANPSO "([\\p{L}\\p{N}\\p{P} \\>\\<\\+\\;\\:\\$\\,\\'\\`\\']*)"

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
#define _PR_CHANNEL "([\\p{L}_][\\p{L}\\p{N}\\p{P}\\+]*)"

#define _PR_TITLE "(\\p{L}[\\p{L}\\p{N} _-]+)"
#define _PR_OPTITLE "(" _PR_S "(\\p{L}[\\p{L}\\p{N} _-]*))?"

#define _PR_PROFN "(@[\\p{L}\\p{N}]+)?"
#define _PR_PROFE "(" _PR_PROFN ")?"
#define _PR_PROFILES "(" _PR_PROFN _PR_SO _PR_PROFN _PR_SO _PR_PROFN ")?"

#define TRUE 1
#define FALSE 0

#ifdef	__cplusplus
}
#endif

#endif	/* _UTILS_H */

