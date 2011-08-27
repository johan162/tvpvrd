/* =========================================================================
 * File:        MAILUTIL.C
 * Description: Functions to send mail using the command line mail (1)
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010,2011 Johan Persson
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
#include <errno.h>
#include <libgen.h>

#include "utils.h"
#include "rkey.h"
#include "tvconfig.h"
#include "libsmtpmail/mailclientlib.h"
#include "tvplog.h"

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


/*
static int
get_fext(char *filename,char *fext,size_t maxlen) {
    ssize_t n = strlen(filename) - 1;
    while( n > 0 && filename[n] != '.' ) {
        n--;
    }
    if( n <= 0 || strlen(&filename[n+1]) >= maxlen ) {
        return -1;
    }
    strcpy(fext,&filename[n+1]);
    return 0;
}
*/

/**
 * Send a mail based on a template file. If the template file ends in *.html the mail
 * will be sent as an HTML mail and if a template with the same basename but with
 * extension *.txt exists then that will be used to send a plain text version of the
 * mail. If the template file ends in *.txt then the mail will only be sent as a
 * plain text.
 * @param subject
 * @param from
 * @param to
 * @param templatename
 * @param keys
 * @param nkeys
 * @return
 */

int
send_mail_template(char * subject, char * from, char *to,
                   char *templatename, struct keypairs keys[], size_t nkeys) {

    char *buffer=NULL, *buffer2=NULL;
    char templatefile[256];

    logmsg(LOG_DEBUG,"smtp_use=%d, use_html_mail=%d",smtp_use,use_html_mail);

    if( use_html_mail && smtp_use ) {
        snprintf(templatefile,255,"%s/tvpvrd/%s.html",CONFDIR,templatename);
        logmsg(LOG_DEBUG,"Sending HTML message using template: \"%s\" ",templatefile);
    } else {
        snprintf(templatefile,255,"%s/tvpvrd/%s.txt",CONFDIR,templatename);
        if( use_html_mail ) {
            logmsg(LOG_NOTICE,"Cannot send HTMl mail (no SMTP server configured) using plain text instead with template: \"%s\"",templatefile);
        }
        logmsg(LOG_DEBUG,"Sending TEXT message using template: %s ",templatefile);
    }

    logmsg(LOG_DEBUG,"Replacing keywords in template: \"%s\" ",templatefile);
    int rc = replace_keywords_in_file(templatefile, &buffer, keys, nkeys);
    if( -1 == rc )  {
        logmsg(LOG_ERR,"Failed to do keyword substitution with template: \"%s\". Does it exist?",templatefile);
        return -1;
    } 

    if( !smtp_use || !use_html_mail) {
        logmsg(LOG_DEBUG,"Sending mail via system mail command.");
        rc = send_mail(subject, from, to, buffer) ;
        free(buffer);
        return rc;
    }

    if (use_html_mail) {
        // Also try to get a plain text version
        snprintf(templatefile,255,"%s/tvpvrd/%s.txt",CONFDIR,templatename);
        logmsg(LOG_DEBUG,"Getting a plain text version of the HTML template: %s",templatefile);
        rc = replace_keywords_in_file(templatefile, &buffer2, keys, nkeys);
        if( -1 == rc ) {
            logmsg(LOG_DEBUG,"Could not find a plain text version of the template '%s'",templatefile);
        } 
    }

    struct smtp_handle *handle = smtp_setup(smtp_server,smtp_user,smtp_pwd);
    if( handle == NULL ) {
        logmsg(LOG_ERR,"Could NOT connect to SMTP server (%s) with credentials [%s:%s]",smtp_server,smtp_user,smtp_pwd);
        free(buffer);
        if( buffer2 )
            free(buffer2);
        return -1;
    }

    logmsg(LOG_DEBUG,"Connected to SMTP server (%s) with credentials [%s:%s]",smtp_server,smtp_user,smtp_pwd);
    rc = smtp_add_rcpt(handle,SMTP_RCPT_TO,to);
    if( -1 == rc ) {
        logmsg(LOG_ERR,"Could NOT add To: '%s'",to); 
        free(buffer);
        if( buffer2 )
            free(buffer2);
        smtp_cleanup(&handle);
        return -1;
    } else {
         logmsg(LOG_DEBUG,"Added recepients To: '%s'",to); 
    }

    if( use_html_mail ) {
        rc = smtp_add_html(handle,buffer,buffer2);
    } else {
        rc = smtp_add_plain(handle,buffer);
    }

    if( -1 == rc ) {
        logmsg(LOG_ERR,"Could NOT add body text to mail.");
    }
    
    free(buffer);
    if (buffer2) {
        free(buffer2);
    }

    rc = smtp_sendmail(handle,from,subject);

    if( -1 == rc ) {
        logmsg(LOG_ERR,"Could NOT send mail with subject '%s' using SMTP server !",subject);
    } else {
        logmsg(LOG_DEBUG,"Successfully sent SMTP mail with subject '%s' ",subject);
    }

    smtp_cleanup(&handle);

    return rc;
}


/*
 * Send mail with both HTML and alternative plain text format. The to and from
 * address are taken from the config file
 */
int
sendmail_helper(char *subject,char *buffer_plain,char *buffer_html) {
    int rc;
    if( !smtp_use || !use_html_mail) {

        logmsg(LOG_DEBUG,"Sending list of transcodings via mail system command.");
        rc = send_mail(subject, daemon_email_from, send_mailaddress, buffer_plain);

    } else {

        struct smtp_handle *handle = smtp_setup(smtp_server,smtp_user,smtp_pwd);
        if( handle == NULL ) {
            logmsg(LOG_ERR,"Could NOT connect to SMTP server (%s) with credentials [%s:%s]",smtp_server,smtp_user,smtp_pwd);
            return -1;
        }

        if( -1 == smtp_add_html(handle, buffer_html, buffer_plain) ) {
            logmsg(LOG_ERR,"Could NOT add content in mail");
            smtp_cleanup(&handle);
            return -1;
        }

        if( -1 == smtp_add_rcpt(handle, SMTP_RCPT_TO, send_mailaddress) ) {
            logmsg(LOG_ERR,"Could NOT add recepient to mail");
            smtp_cleanup(&handle);
            return -1 ;
        }

        rc = smtp_sendmail(handle, daemon_email_from, subject);
        if( -1 == rc ) {
            logmsg(LOG_ERR,"could not SEND mail via SMTP.");
        }
        smtp_cleanup(&handle);
    }
    
    return rc;
}
