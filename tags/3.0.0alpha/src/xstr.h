/* =========================================================================
 * File:        XSTR.H
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
 * Cases insensitive string comparison
 * @param dir
 * @return
 */
int
xstricmp(const char *s1, const char *s2);

/*
 * Utility function. Convert string of maximum 4095 characters to lower case
 */
void
xstrtolower(char *s);


#ifdef	__cplusplus
}
#endif

#endif	/* XSTR_H */

