/* =========================================================================
 * File:        QUOTPRINTED.C
 * Description: Quoted printable encoding
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "quotprinted.h"

#define LINE_WIDTH 76

static int
_chk_softlinebreak(char **pout, size_t *n, size_t *w) {

    if( *w==LINE_WIDTH-1 ) {
        // We need to insert a soft linebreak
        if( **pout == 32 ) {
            if( *n <  6) return -1;

            // Can't have a space or tab at end
            *(*pout)++ = '=';

            *(*pout)++ = '=';
            *(*pout)++ = '\r';
            *(*pout)++ = '\n';

            *(*pout)++ = '2';
            *(*pout)++ = '0';

            *w = 2;
            *n -= 6;
        } else if( **pout == 9 ) {
            if( *n <  6) return -1;
            *(*pout)++ = '=';

            *(*pout)++ = '=';
            *(*pout)++ = '\r';
            *(*pout)++ = '\n';

            *(*pout)++ = '0';
            *(*pout)++ = '9';
            *w = 2;
            *n -= 6;
        } else {
            if( *n < 3 ) return -1;
            (*pout)++;
            *(*pout)++ = '=';
            *(*pout)++ = '\r';
            *(*pout)++ = '\n';
            *w = 0;
            *n -= 3;
        }

    } else {
        (*pout)++;
        (*w)++;
        (*n)--;
    }

    return 0;
}


/**
 * Encode a word in UTF8 into Q-quoted printables. Primarily used to encode
 * subject line in mail. See RFC 2047 for details.
 * Note: This version will ignore any '\n' and '\r' characters in the input string
 * @param in INput string to be encoded
 * @param out Outpput string in Q-encoded format
 * @param maxlen Maxbuffer len
 * @return -1 on failure, 0 on success
 */
int
qprint_encode_word(char * const in, char * const out, size_t maxlen) {

    unsigned char encoded=0;
    unsigned char *pin = (unsigned char *)in;
    char *pout = out;
    char hexbuff[8];
    size_t n = maxlen;
    if (n < 11) {
        return -1;
    }
    snprintf(out, maxlen, "=?utf8?Q?");
    pout += strlen(out);
    while (*pin && n > 0) {
        if (*pin == '\n' || *pin == '\r') {
            // We silently ignore linefeeds in input since the Q format of quoted printable
            // Should just be used for word encoding
            pin++;
            continue;
        } else if (*pin == 32) {
            encoded = 1;
            *pout++ = '_';
            pin++;
            n--;
        } else if (*pin == '_' || *pin == '=' || *pin == '?' || *pin < 32) {
            if (n < 3) {
                return -1;
            }
            *pout++ = '=';
            snprintf(hexbuff, 8, "%02X", *pin++);
            *pout++ = hexbuff[0];
            *pout++ = hexbuff[1];
            n -= 3;
            encoded = 1;
        } else if (*pin > 32 && *pin <= 'z') {
            *pout++ = *pin++;
            n--;
        } else if (*pin > 127) {
            if (n < 6) {
                return -1;
            }
            *pout++ = '=';
            snprintf(hexbuff, 8, "%02X", *pin++);
            *pout++ = hexbuff[0];
            *pout++ = hexbuff[1];
            if (*pin == '\0') {
                return -1;
            }
            *pout++ = '=';
            snprintf(hexbuff, 8, "%02X", *pin++);
            *pout++ = hexbuff[0];
            *pout++ = hexbuff[1];
            n -= 6;
            encoded = 1;
        } else {
            return -1;
        }
    }
    if (n < 3) {
        return -1;
    }
    *pout++ = '?';
    *pout++ = '=';
    *pout = '\0';

    if( !encoded ) {
        // Didn't need any encoding so we just copy the input to the output
        strcpy(out,in);
        return 0;
    }

    return n == 0 && *pin ? -1 : 0;
}


/**
 * Encode a string in UTF8 into Q-quoted printables. Primarily used to encode
 * subject line in mail. See RFC 2047 for details.
 * Note: This version will ignore any '\n' and '\r' characters in the input string
 * @param in INput string to be encoded
 * @param out Outpput string in Q-encoded format
 * @param maxlen Maxbuffer len
 * @return -1 on failure, 0 on success
 */
int
qprint_encode(char * const in, char * const out, size_t maxlen) {
    size_t w = 0;
    unsigned char *pin = (unsigned char *)in;
    char *pout = out;
    char hexbuff[8];
    size_t n = maxlen;
    if (n < strlen(in)+1) {
        return -1;
    }

    while (*pin && n > 0) {
        if (*pin >= 32 && *pin <= 'z' && *pin != '=') {
            *pout = *pin++;
            _chk_softlinebreak(&pout, &n, &w);
        } else {
            if (n < 3) {
                return -1;
            }
            *pout = '=';
            _chk_softlinebreak(&pout, &n, &w) ;
            snprintf(hexbuff, 8, "%02X", *pin++);
            *pout = hexbuff[0];
            _chk_softlinebreak(&pout, &n, &w) ;
            *pout = hexbuff[1];
            _chk_softlinebreak(&pout, &n, &w)  ;
        }
    }
    if (n < 1 && *pin) {
        return -1;
    }
    *pout = '\0';

    return n == 0 && *pin ? -1 : 0;
}



/* EOF */