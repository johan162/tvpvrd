/* =========================================================================
 * File:        XSTR.C
 * Description: Some extra string utility functions
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

// We want the full POSIX and C99 standard
#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

#include "utils.h"

/**
 * String copying with extra safety
 * @param dst
 * @param src
 * @param size for dst string including terminating 0
 * @return Final lenght of destination string
 */
size_t
xstrlcpy(char *dst, const char *src, size_t size) {
    *dst = '\0';
    if( size == 0 )
        return 0;
    strncpy(dst,src,size-1);
    if( size > 0 )
        dst[size-1] = '\0';
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

    if( size == 0 )
        return 0;

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
 * Trim a string inplace by removing beginning and ending spaces
 * @param str
 */
void
xstrtrim(char *str) {
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

    free(tmp);
}


/**
 * A safer version of atoi using strtol with error checking
 * @param str
 * @return
 */
int
xatoi(char * const str) {
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) || (errno != 0 && val == 0)) {
       val = 0 ;
    }

    if (endptr == str) {
       val = 0 ;
    }
    
    return (int)val;
}


/*
 * Utility function. Convert string of maximum 4095 characters to lower case
 */
int
xstrtolower(char *s) {
    int safetylimit = 4096;
    while ( --safetylimit > 0 && (*s = tolower(*s)) ) {
        s++;
    }
    if( safetylimit <= 0 ) {
        //logmsg(LOG_ERR,"FATAL : strtolower() : Failed safetylimit !");
        return -1;
    }
    return 0;
}

/**
 * Cases insensitive string comparison
 * @param dir
 * @return
 */
int
xstricmp(const char *s1, const char *s2) {
    const int safetylimit = 4096;
    int len1 = strnlen(s1,safetylimit);
    int len2 = strnlen(s2,safetylimit);
    if( len1 >= safetylimit || len2 >= safetylimit ) {
        //logmsg(LOG_ERR,"FATAL : stricmp() safetylimit exceedded !");
        return -1;
    }
    char *b1=strdup(s1);
    char *b2=strdup(s2);
    if( b1 == NULL || b2 == NULL ) {
        //logmsg(LOG_ERR,"FATAL: stricmp() Out of memory ! ( %d : %s )",errno,strerror(errno));
        return -1;
    }
    xstrtolower(b1);
    xstrtolower(b2);
    int ret=strncmp(b1,b2,safetylimit);
    free(b1);
    free(b2);
    return ret;
}
