/* =========================================================================
 * File:        UTILS.C
 * Description: A collection of small utility functions used by the rest
 *              of the daemon.
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
#include <libgen.h>
#include <math.h>
#include <syslog.h>

#include <errno.h>
#include <sys/param.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <locale.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"
#include <pcre.h>

// Local headers
#include "libsmtpmail/mailclientlib.h"
#include "mailutil.h"

#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "rkey.h"
#include "xstr.h"
#include "tvplog.h"

int htmlencode_flag;

/**
 * Debug version of close()
 * @param fd
 * @return
 */
int
_x_dbg_close(int fd) {

    logmsg(LOG_NOTICE,"dbg_close() : fd=%d",fd);
    return close(fd);

}

void * 
_chk_calloc(size_t num, size_t size) {
    void *ptr = calloc(num, size);
    if( ptr == NULL ) {
        logmsg(LOG_ERR,"FATAL: Cannot allocate memory. Terminating.");
        exit(EXIT_FAILURE);
    } else {
        return ptr;
    }
}

/*
 * _writef
 * Utility function
 * Simplify a formatted write to a file descriptor
 */
int
_writef(int fd, const char *buf, ...) {
    if( fd >= 0 ) {
        const int blen = MAX(8192*2,strlen(buf)+1);

        char *tmpbuff = _chk_calloc(blen,sizeof(char));
        va_list ap;
        va_start(ap, buf);
        vsnprintf(tmpbuff, blen, buf, ap);
        tmpbuff[blen-1] = 0;
        int ret;
        if( htmlencode_flag ) {
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
    cregex = pcre_compile(regex,PCRE_CASELESS|PCRE_MULTILINE|PCRE_NEWLINE_CRLF|PCRE_UTF8,
            &errptr,&erroff,NULL);

    //cregex = pcre_compile(regex,PCRE_CASELESS|PCRE_UTF8,&errptr,&erroff,NULL);
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
matchcmd_free(char ***field) { //,const char *func, int line) {
    //logmsg(LOG_DEBUG, "matchcmd_free() called from '%s()' at line #%05d",func,line);
    if( field != (void *)NULL ) {
        pcre_free_substring_list((const char **)*field);
        *field = NULL;
    }
}

/*
 * Fill the supplied buffer with 'num' repeats of character 'c'
 */
char *
rptchr_r(char c, size_t num, char *buf) {
    num = MIN(255, num);
    for (unsigned i = 0; i < num; i++)
        buf[i] = c;
    buf[num] = '\0';
    return buf;
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
    char lbuff[30];
    int ld = open("/proc/loadavg", O_RDONLY);
    if( -1 == read(ld, lbuff, 30) ) {
        logmsg(LOG_ERR,"FATAL: Cannot read '/proc/loadavg' ( %d : %s )",errno,strerror(errno));
        *avg1=-1; *avg5=-1; *avg15=-1;
    }
    close(ld);
    lbuff[14] = '\0';
    char oldlocale[64];
    strncpy(oldlocale,setlocale(LC_ALL,NULL),sizeof(oldlocale)-1);
    oldlocale[sizeof(oldlocale)-1] = '\0';
    setlocale(LC_ALL,"C");
    sscanf(lbuff, "%f%f%f", avg1, avg5, avg15);
    logmsg(LOG_DEBUG,"*** Load average: lbuff=%s (%f %f %f)",lbuff,*avg1, *avg5, *avg15);
    setlocale(LC_ALL,oldlocale);
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
 * Set FD_CLOEXEC file flag. This will close a stream unconditionally when
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
 * Make a call to 'df' system function in rder to find out remaining disk
 * space
 * @param dir
 * @param fs
 * @param size
 * @param used
 * @param avail
 * @param use
 * @return 0 success, -1 failure
 */
int
get_diskspace(char *dir, char *fs, char *size, char *used, char *avail, int *use) {
    int const maxbuff = 512;
    char cmd[maxbuff];
    snprintf(cmd, maxbuff, "df -hP %s 2>&1", dir);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char buffer[maxbuff];
        char * ret1 = fgets(buffer, maxbuff - 1, fp); // Read and discard header
        char * ret2 = fgets(buffer, maxbuff - 1, fp);
        if( NULL == ret1 || NULL == ret2 ) {
            (void)pclose(fp);
            return -1;
        }
        buffer[maxbuff - 1] = '\0';
        buffer[strlen(buffer) - 1] = '\0'; // Get rid of newline

        if (0 == pclose(fp)) {
            // A typical returned string looks like
            // //192.168.0.199/media  4.1T  612G  3.5T  15% /mnt/omega/mm

            int ret = sscanf(buffer, "%s %s %s %s %d", fs, size, used, avail, use);
            if (5 != ret) {
                return -1;
            } else {
                return 0;
            }
        } else {
            return -1;
        }
    } else {
        return -1;
    }
}


char *
esc_percentsign(char *str) {
    char *buff=_chk_calloc(1,strlen(str)*3+1);
    char *pbuff=buff;
    if( str==NULL ) {
        return NULL;
    }
    while( *str ) {
        if( *str == '%' )  {
            *pbuff++ = '%';
            *pbuff++ = '%';
        } else {
            *pbuff++ = *str;
        }
        str++;
    }
    *pbuff = '\0';
    return buff;
}


/* Converts a hex character to its integer value */
char from_hex(const char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(const char code) {
  static char hex[] = "0123456789ABCDEF";
  return hex[code & 0x0f];
}

/*
 * URL encode a buffer.
 * Note: Calling function is responsible to free returned string
 */
char *url_encode(char *str) {
  char *pstr = str;
  char *buf = _chk_calloc(1,strlen(str) * 3 + 1);
  char *pbuf = buf;
  
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') {
      *pbuf++ = *pstr;
    }
    else if (*pstr == ' ') {
      *pbuf++ = '+';
    } else {
      *pbuf++ = '%';
      *pbuf++ = to_hex(*pstr >> 4);
      *pbuf++ = to_hex(*pstr);
    }
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
  if( str == NULL ) {
      return NULL;
  }
  char *pstr = str, *buf = _chk_calloc(1,strlen(str) + 1), *pbuf = buf;
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


/**
 * URL decode a into a buffer.
 * @param decode
 * @param maxlen
 * @param str
 * @return 
 */
int 
url_decode_buff(char *decode, size_t maxlen, char *str) {
  if( str == (char *)NULL ) {
      return -1;
  }
  char *pstr = str;
  while (maxlen > 1 && *pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *decode++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    } else if (*pstr == '+') {
      *decode++ = ' ';
    } else {
      *decode++ = *pstr;
    }
    pstr++;
    maxlen--;
  }
  *decode = '\0';
  if( 1 == maxlen && *pstr ) {
      return -1;
  } else {   
        return 0;
  }
}

/*
 * HTML encode a buffer.
 * Note: Calling function is responsible to free returned string
 */
char *html_encode(char *str) {
    if( str == NULL ) {
        return NULL;
    }
    char *pstr=str, *buf = _chk_calloc(strlen(str) * 6 + 1,sizeof(char)), *pbuf = buf;
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
get_assoc_value(char *value, size_t maxlen, char *key, char *list[], size_t listlen) {
    size_t i = 0;
    if( list == NULL || key == NULL || value == NULL ) {
        return -1;
    }
    while( i < listlen ) {
        if( 0 == strcmp(key,list[i]) ) {
            strncpy(value,list[i+1], maxlen);
            xstrtrim(value);
            return 0;
        }
        i += 2;
    }
    return -1;
}

int
get_assoc_value_s(struct keypair_t kv[],size_t maxlen,char *key,char **v) {
    size_t i=0;
    while( i < maxlen && strcmp(kv[i].key,key) ) 
        i++;
    if( i==maxlen )
        return -1;
    else {
        *v = kv[i].val;
        return 0;
    }
}


int
get_assoc_value_i(struct keypair_t kv[],size_t maxlen,char *key,int *v) {
    size_t i=0;
    while( i < maxlen && strcmp(kv[i].key,key) ) 
        i++;
    if( i==maxlen )
        return -1;
    else {
        *v = xatoi(kv[i].val);
        return 0;
    }
}



/**
 * Dump ascii values in string together with the string
 * @param buffer
 * @param maxlen
 * @param str
 * @return 
 */
int
dump_string_chars(char *buffer, size_t maxlen, char const *str) {
    char tmpbuff[16];
    size_t len = strlen(str);
    if( buffer == NULL ) {
        return -1;
    }
    snprintf(buffer,maxlen,"%s \n(",str);
    maxlen -= len+3;
    for(size_t i=0; i < len; ++i ) {
        snprintf(tmpbuff,15,"%02X,",(unsigned)str[i]);
        if( maxlen > 2 )
            strcat(buffer,tmpbuff);
        else
            return -1;
        maxlen -= 3;
    }
    snprintf(tmpbuff,15,")\n");
    
    if( maxlen > 2 )
        strcat(buffer,tmpbuff);
    else
        return -1;
    
    return 0;
}



/*
 * Read a reply from a socket with 2s timeout.
 * We only read the first chunk of data available.
 * To read all data on the socket see waitreadn()
 */
int
waitread(int sock, char *buffer, int maxbufflen) {
    fd_set read_fdset;
    struct timeval timeout;

    FD_ZERO(&read_fdset);
    FD_SET(sock, &read_fdset);

    timerclear(&timeout);
    timeout.tv_sec = 2;

    int ret = select(sock + 1, &read_fdset, NULL, NULL, &timeout);
    if( 0 == ret ) {
        // Timeout
        return -1;
    } else {
        int nread = read(sock, buffer, maxbufflen);
        buffer[nread] = '\0';
    }
    return 0;
}

/*
 * Used to read an unknown amount of data from a socket
 * We keep filling the buffer until we get a timeout and there
 * is nothing more to read.
 */
int
waitreadn(int sock, char *buffer, int maxbufflen) {
    const int buffsize = 128*1024;
    char *pbuff = _chk_calloc(buffsize,sizeof(char));
    *buffer = '\0';
    int totlen=0;
    while( totlen < maxbufflen && waitread(sock,pbuff,buffsize) > -1 ) {
        strcat(buffer,pbuff);
        int len=strlen(pbuff);
        totlen += len;
        buffer[totlen] = '\0';
    }
    buffer[maxbufflen-1] = '\0';
    free(pbuff);
    if( *buffer == '\0' )
        return -1;
    else
        return 0;
}


/* utils.c */
