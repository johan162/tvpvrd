/* =========================================================================
 * File:        BASE64ED.C
 * Description: Routines for doing base64 encoding and decoding. See
 *              RFC3548 for details (http://www.ietf.org/rfc/rfc3548.txt)
 *              Note: These routines are not necessarily written to achive
 *              maximum speed but rather maximum clarity to avoid nasty
 *              off by one bugs.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64ed.h"

static char b64table[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};


inline static char
base64_decodechar(char c) {
    char *p = strchr(b64table, c);
    return p != NULL ? (char) (p - b64table) : 0;
}

/**
 * Do bease64 encoding of input data stream
 * @param inbuff
 * @param inbuff_len
 * @param outbuff
 * @param outbuff_maxlen
 * @return
 */
int
base64decode(char * const inbuff, const unsigned inbuff_len, char * const outbuff, const size_t outbuff_maxlen) {

    if ((outbuff_maxlen < (inbuff_len / 4) * 3 + 1) ||
            (inbuff_len % 4)) {
        return -1;
    }

    *outbuff = '\0';
    size_t n = inbuff_len;
    char *pinbuff = inbuff;
    char *poutbuff = outbuff;

    while (n > 4) {
        const unsigned char a1 = base64_decodechar(*pinbuff++);
        const unsigned char a2 = base64_decodechar(*pinbuff++);
        const unsigned char a3 = base64_decodechar(*pinbuff++);
        const unsigned char a4 = base64_decodechar(*pinbuff++);

        *poutbuff++ = (a1 << 2) | (a2 >> 4);
        *poutbuff++ = (a2 << 4) | (a3 >> 2);
        *poutbuff++ = (a3 << 6) | a4;

        n -= 4;
    }

    // Now handle the last quartet which may or may not have padding
    // character ('=')
    const unsigned char a1 = base64_decodechar(*pinbuff++);
    const unsigned char a2 = base64_decodechar(*pinbuff++);
    *poutbuff++ = (a1 << 2) | (a2 >> 4);
    if (*pinbuff != '=') {
        const unsigned char a3 = base64_decodechar(*pinbuff++);
        const unsigned char a4 = base64_decodechar(*pinbuff);
        *poutbuff++ = (a2 << 4) | (a3 >> 2);
        if (*pinbuff != '=') {
            *poutbuff++ = (a3 << 6) | a4;
        }
    }
    *poutbuff++ = '\0';
    return 0;
}

/**
 * Do base64 decode of input data stream
 * @param inbuff
 * @param inbuff_len
 * @param outbuff
 * @param outbuff_maxlen
 * @return
 */
int
base64encode(char * const inbuff, const size_t inbuff_len, char * const outbuff, const size_t outbuff_maxlen) {

    int n = inbuff_len;
    char *pinbuff = inbuff;
    char *poutbuff = outbuff;

    if (outbuff_maxlen < (inbuff_len / 3 + 1) * 4) {
        return -1;
    }

    while (n >= 3) {

        const unsigned char a1 = *pinbuff++;
        const unsigned char a2 = *pinbuff++;
        const unsigned char a3 = *pinbuff++;

        *poutbuff++ = b64table[a1 >> 2];
        *poutbuff++ = b64table[((a1 & 0x03) << 4) | (a2 >> 4)];
        *poutbuff++ = b64table[((a2 & 0x0f) << 2) | (a3 >> 6)];
        *poutbuff++ = b64table[a3 & 0x3f];

        n -= 3;
    }

    if (n == 2) {

        const unsigned char a1 = *pinbuff++;
        const unsigned char a2 = *pinbuff;

        *poutbuff++ = b64table[a1 >> 2];
        *poutbuff++ = b64table[((a1 & 0x03) << 4) | (a2 >> 4)];
        *poutbuff++ = b64table[(a2 & 0x0f) << 2 ];
        *poutbuff++ = '=';

    } else if (n == 1) {

        const unsigned char a1 = *pinbuff;

        *poutbuff++ = b64table[a1 >> 2];
        *poutbuff++ = b64table[(a1 & 0x03) << 4];
        *poutbuff++ = '=';
        *poutbuff++ = '=';
    }

    *poutbuff = '\0';

    return 0;
}

