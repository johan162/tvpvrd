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
 * Escape quotes in a string as necessary
 * @param tostr
 * @param fromstr
 * @param maxlen
 */
void
escape_quotes(char *tostr, const char *fromstr, const size_t maxlen, unsigned remove_n) {
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


int
send_mail(const char *subject, const char *from, const char *to, const char *message) {
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

