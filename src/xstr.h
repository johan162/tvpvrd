/* =========================================================================
 * File:        XSTR.H
 * Description: Some extra string utility functions
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


#ifndef XSTR_H
#define	XSTR_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Trim a string by removing beginning and ending psaces
 * @param string
 */
void
xstrtrim(char *string);


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

/**
 * A safer version of atol using strtol with error checking
 * @param str
 * @return
 */
long
xatol(char * const str);

/**
 * Cases insensitive string comparison
 * @param dir
 * @return
 */
int
xstricmp(const char *s1, const char *s2);

/*
 * Utility function. Convert string of maximum 4095 characters to lower case
 * @param s 
 */
void
xstrtolower(char *s);

/**
 * Replace all non filename safe character in a string with the specified
 * character. The replacement is done in place.
 * @param s
 * @param replace
 */
void
xstrfilify(char *s, char replace);

/**
 * Extract substring to buffer
 * @param to     Buffer to write the extracted string to
 * @param maxlen Maximum length of buffer
 * @param from   String to extract from
 * @param s      Start position
 * @param e      End position
 * @return 0 on success, -1 on failure
 */
int
xsubstr(char *to, size_t maxlen, char *from,size_t s,size_t e);

/**
 * MB String length
 * @param s
 * @return 
 */
size_t
xmblen(const char *s);

/**
 * MB String right padding to length
 * @param s
 * @param pad
 * @param maxlen
 * @param padc
 * @return 
 */
int
xmbrpad(char *s, size_t pad, const size_t maxlen, const char padc);

/**
 * MB String left padding to length
 * @param s
 * @param pad
 * @param maxlen
 * @param padc
 * @return 
 */
int
xmblpad(char *s, size_t pad, const size_t maxlen, const char padc);


#ifdef	__cplusplus
}
#endif

#endif	/* XSTR_H */

