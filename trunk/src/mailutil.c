/* =========================================================================
 * File:        MAILUTIL.C
 * Description: Functions to send mail using the command line mail (1)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

#include "utils.h"



/**
 * Encode a string in UTF8 into Q-quoted printables. Primarily used to encode
 * subject line in mail. See RFC 2047 for details.
 * Note: This version will ignore any '\n' and '\r' characters in the input string
 * @param in INput string to be encoded
 * @param out Outpput string in Q-encoded format
 * @param maxlen Maxbuffer len
 * @return -1 on failuer, 0 on success
 */
int
encodeUTF8toQ(char * const in, char * const out, size_t maxlen) {

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
            // We ignore linefeeds
            pin++;
            continue;
        } else if (*pin == 32) {
            encoded = 1;
            *pout++ = '_';
            pin++;
            n--;
        } else if (*pin == '=' || *pin == '?' || *pin < 32) {
            if (n < 3) {
                return -1;
            }
            *pout++ = '=';
            snprintf(hexbuff, 16, "%02X", *pin++);
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
            snprintf(hexbuff, 16, "%02X", *pin++);
            *pout++ = hexbuff[0];
            *pout++ = hexbuff[1];
            if (*pin == '\0') {
                return -1;
            }
            *pout++ = '=';
            snprintf(hexbuff, 16, "%02X", *pin++);
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
        // Didn't need any encoding so we ust copy the input to the output
        strcpy(out,in);
        return 0;
    }

    return n == 0 && *pin ? -1 : 0;
}


/**
 * Escape quotes in a string as necessary
 * @param tostr
 * @param fromstr
 * @param maxlen
 */
void
escape_quotes(char *tostr, const char *fromstr, size_t maxlen, unsigned remove_n) {
    size_t i=0;
    while( i < maxlen-1 && *fromstr ) {
        if( *fromstr == '"' ) {
            *tostr++ = '\\';
            *tostr++ = '"';
            i += 2;
        } else if( remove_n && (*fromstr == '\n' || *fromstr == '\r') ) {
            *tostr++ = ' ';
            i++;
        } else {
            *tostr++ = *fromstr;
            i++;
        }
	fromstr++;
    }
    *tostr = '\0';
}

/**
 * Send mail using system mail command
 * @param subject
 * @param from
 * @param to
 * @param message
 * @return 0 on success, -1 on failure
 */
int
send_mail(const char * subject, const char * from, const char *to, const char *message) {
    const size_t blen=20*1024;
    char *buffer = calloc(blen,sizeof(char));

    if( strlen(message) >= blen ) {
        syslog(LOG_ERR,"Truncating mail sent from 'tvpvrd'");
    }

    const size_t msglen2 = 2*strlen(message);
    char *qmessage = calloc(msglen2, sizeof(char));
    escape_quotes(qmessage,message,msglen2,0);

    const size_t sublen2 = 2*strlen(subject);
    char *qsubject = calloc(sublen2, sizeof(char));
    escape_quotes(qsubject,subject,sublen2,1);


    if( from == NULL || *from == '\0' ) {
        snprintf(buffer,blen-1,
                 "echo \"%s\" | /usr/bin/mail -s \"%s\" \"%s\"",qmessage, qsubject, to);
    } else {
        snprintf(buffer,blen-1,
                 "echo \"%s\" | /usr/bin/mail -r \"%s\" -s \"%s\" \"%s\"",qmessage, from, qsubject, to);
    }
    free(qmessage);
    free(qsubject);

    int rc=system(buffer);
    free(buffer);

    if( rc ) {
        syslog(LOG_ERR,"Failed to send mail. rc=%d ( %d : %s )",rc,errno,strerror(errno));
    } else {
        logmsg(LOG_DEBUG,"Sent mail to '%s' with subject '%s'",to,subject);
    }

    return rc;

}

/**
 * Insert newline after specified number of characters
 * @param in
 * @param out
 * @param maxlen
 * @param width
 * @return
 */
int
split_in_rows(char * const in, char * const out, size_t maxlen, size_t width) {
    char *pin = in;
    char *pout = out;
    size_t w=0;
    const size_t n = strlen(in);
    if( n+(n/width)+1 >= maxlen ) {
        return -1;
    }
    while( *pin ) {
        if( *pin == '\r' && *(pin+1) == '\n' ) {
            *pout++ = *pin++;
            *pout++ = *pin++;
            w=0;
        } else if( *pin == '\n' ) {
            *pout++ = '\r';
            *pout++ = *pin++;
            w=0;
        } else {
            *pout++ = *pin++;
            w++;
        }
        if( w == width ) {
            *pout++ = '\r' ;
            *pout++ = '\n' ;
            w=0;
        }
    }
    return 0;
}
