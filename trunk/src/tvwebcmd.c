/* =========================================================================
 * File:        TVWEBCMD.C
 * Description: Handle the interface via a Web-browser
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

// Standard UNIX includes
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <locale.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h>
#include <time.h>
#include <inttypes.h>

#include "config.h"
#include <pcre.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "tvwebcmd.h"
#include "tvcmd.h"
#include "freqmap.h"
#include "stats.h"
#include "transc.h"
#include "transcprofile.h"
#include "recs.h"
#include "tvhtml.h"
#include "datetimeutil.h"
#include "tvplog.h"
#include "vctrl.h"


// Get the name of the CSS file from the basename of the package, i.e. "tvpvrd"
#define CSSFILE_BASENAME PACKAGE_TARNAME

/*
 * The WEB interface is fairly simplistic. On receiving a GET request from a
 * WEB-browser we immitate the behavior of a HTTP server by responding with a
 * correct web page.
 *
 * At the moment the interface is very simplistic. The WEB adress to be used
 * is specified as "/cmd?c=<command>" where "<command>" should be replaced by the
 * corresponding tvpvrd command. For example, running on the localserver the following
 * command will return a list of all upcoming recordings to the browser
 *
 * http://localhost:9300/cmd?c=l
 *
 * The above URL assumes that the server listens on port 9300 as is the default in
 * the distribution. If you change the port the URL has to be udpated accordingly.
 *
 * The main difference from the terminal connection is that the connection will be
 * automatically closed after each command.
 *
 */

// For some commands (like delete) we want to wait a little bit in order
// for the command to have effect before we report back on the status in
// the web interface. If we didn't do this some commands would not be visible
// until the next refresh of the web page. This could confuse the user.
int cmd_delay=0;

/**
 * Validate submitted user/pwd with stored login credentials
 * @param user
 * @param pwd
 * @return
 */
int
validate_login(char *user, char *pwd) {

    return 0 == strcmp(user, web_user) &&
           0 == strcmp(pwd, web_password);

}

/**
 * Create a unique login cookie for this particular login
 * @param user
 * @param pwd
 * @return
 */
static char *
create_login_cookie(char *user, char *pwd) {

    // Based on the user/password we modify the cookie
    static char _cookie_buff[128];
    strcpy(_cookie_buff, LOGIN_COOKIE_SEED);

    char hostname[128];
    gethostname(hostname, sizeof(hostname));
    hostname[127] = '\0';

    char buff[128];
    strncpy(buff, user, sizeof(buff)-1);
    buff[127] = '\0';
    strncat(buff, pwd, sizeof(buff)-strlen(buff)-1);
    buff[127] = '\0';
    strncat(buff, hostname, sizeof(buff)-strlen(buff)-1);
    buff[127] = '\0';

    int n = MIN(strlen(_cookie_buff), strlen(buff));

    // Now use the "secret" cookie seed to scramble the combination
    // of user,password and server name to create a unique cookie id
    // consisting only of "normal" 49-122 (ASCII) characters.
    for (int i = 0; i < n; ++i) {

        _cookie_buff[i] += buff[i];
        _cookie_buff[i] &= 127;

        int v = _cookie_buff[i];
        
        if ( v < 48)
            v += 48;
        
        if ( v >  57 && v <  65 )
            v += 7;

        else if ( v >  90 && v <  97 )
            v += 6;

        else if ( v >  122 && v != 95)
            v -= 5;

        _cookie_buff[i] = v;

    }

    _cookie_buff[n] = '\0';
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"Created cookie: '%s' from %s",_cookie_buff, buff);
#endif
    return _cookie_buff;
}

/**
 * Validate if the received cookie from the browser is valid
 * @param cookie
 * @return 1 (true) iof the cookie is valid, 0 (false) otherwise
 */
int
validate_cookie(char *cookie) {

    return !strcmp(create_login_cookie(web_user, web_password), cookie);

}

/**
 * Validate that the user is logged in before doing anything else when the
 * WEB login is enabled. This function is called before any other WEB commands
 * are executed.
 * @param buffer
 * @param cookie
 * @param maxlen
 * @return
 */
int
user_loggedin(char *buffer, char *cookie, int maxlen) {
    char **field = (void *) NULL;
    int ret;

    *cookie = '\0';

    if (!require_web_password) {
        // Always succeed
        return 1;
    }

    if ((ret = matchcmd_ml("^Cookie:.*tvpvrd=" _PR_ANP, buffer, &field)) > 1) {

#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG, "Cookie found in HTTP Header: %s",buffer);
#endif

        char *tmpbuff = url_decode(field[1]);
        if( tmpbuff ) {
            logmsg(LOG_DEBUG, "Received cookie: '%s' decoded as: '%s'", field[1], tmpbuff);
            int sucess=0;
            if (validate_cookie(tmpbuff)) {

                logmsg(LOG_DEBUG, "Received cookie validated correctly.");
                strncpy(cookie, tmpbuff, maxlen);
                cookie[maxlen - 1] = '\0';
                sucess = 1;

            } else {

                logmsg(LOG_DEBUG, "Received cookie is not the valid login cookie.");

            }
            free(tmpbuff);
            matchcmd_free(&field);
            return sucess;
        } else {
            matchcmd_free(&field);
            return 0;
        }

    } else {
#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG, "No cookie found to validate in HTTP Header: %s",buffer);
#else
        logmsg(LOG_DEBUG, "No cookie found to validate in HTTP Header.");
#endif
        return 0;

    }
}

/* 
 * Try to determine if the connection is from a mobile phone by examine the headers.
 * If it is a mobile we use a different set of CSS formatting to make it more
 * suitable for a mobiles smaller screen.
 */
int
is_mobile_connection(char *buffer) {

    if( !use_mobile )
        return FALSE;
    
    char **field = (void *) NULL;
    if (matchcmd("X-Wap-Profile:", buffer, &field) > 0) {

        matchcmd_free(&field);
        return TRUE;
    }

    // Extract User-Agent String
    if (matchcmd("User-Agent: (.+)", buffer, &field) > 0) {

#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG, "Found User-Agent: %s", field[1]);
#endif
        char *header = strdup(field[1]);
        matchcmd_free(&field);

        if (matchcmd("(mobile|Nokia|HTC|Android|SonyEricsson|LG|Samsung|blac|moto|doco|java|symb)", header, &field) > 0) {
            matchcmd_free(&field);
            return TRUE;
        }

    }
    
    return FALSE;
}


/**
 * This test function is called when the server receives a new connection and
 * determines if the first command is a GET string. This is then an indication
 * that this is a WEB-browser calling us. The command given is stored in the
 * buffer pointed to by the second argument.
 * @param buffer
 * @param cmd
 * @param maxlen
 * @return 1 if this was a WEB-connection , 0 otherwise
 */
int
webconnection(const char *buffer, char *cmd, int maxlen) {

/*
    static char *allowed_commands[] = {
        "^GET /favicon.ico" _PR_ANY _PR_E,
        "^GET /addrec\\?" _PR_ANY _PR_E,
        "^GET /addqrec\\?" _PR_ANY _PR_E,
        "^GET /delrec\\?" _PR_ANY _PR_E,
        "^GET /login\\?" _PR_ANY _PR_E,
        "^GET /killrec\\?" _PR_ANY _PR_E,
        "^GET /logout" _PR_ANY _PR_E
    };
*/

    *cmd = '\0';
    if (0 == strncmp(buffer, "GET", 3)) {

        // Now extract the command string
        char **field = (void *) NULL;
        int ret=0, found=0;

/*
        static char dumpbuff[2048];
        *dumpbuff='\0';
        if( 0==dump_string_chars(dumpbuff, 2048, buffer) ) {
            logmsg(LOG_DEBUG,"Decoded GET string: %s",dumpbuff);
        } else {
            logmsg(LOG_DEBUG,"Decoded GET string TOO LARGE FOR BUFFER!");
        }
*/

        if ((ret = matchcmd("^GET /cmd\\?c=" _PR_ANPS _PR_HTTP_VER _PR_E, buffer, &field)) > 1) {

            // Found a command string so store it in the buffer
            char *tmpbuff = url_decode(field[1]);
            strncpy(cmd, tmpbuff, maxlen);
            free(tmpbuff);
            if (*cmd != 'h')
                strcat(cmd, " ");
            cmd[maxlen - 1] = '\0';

            found = 1;
            matchcmd_free(&field);

        } else if ((ret = matchcmd("^GET / " _PR_HTTP_VER _PR_E , buffer, &field)) >= 1) {

            // When only the root directory is called we default the command
            // to a "time" command.
            strncpy(cmd, "t", maxlen);
            found = 1;
            matchcmd_free(&field);
            logmsg(LOG_DEBUG,"Found empty GET directory. Assuming command 't'");
            
        } else if ((ret = matchcmd("^GET /" _PR_ANY _PR_HTTP_VER _PR_E, buffer, &field)) > 1) {

            // We get here when the client tries to get the CSS file or issue one
            // of the named commands
            found = 1;
            matchcmd_free(&field);
                                           
        }
#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"FOUND WEBCONNECTION: found=%d, buffer=%s",found,buffer);
#endif

        return found;

    }

    return 0;
}

/**
 * Read a file from our "WEB-root" directory 
 * @param buff Buffer where the file is read into
 * @param maxlen Maximum length of file buffer
 * @return 0 on not modified, 1 if modified , -1 on failure, 
 */
int
read_webroot_file(char *file_buffer, size_t maxlen, size_t *actualfilelen, char *filename, time_t modifiedSince) {
    char full_filename[255];

    snprintf(full_filename, sizeof(full_filename)-1, "%s/tvpvrd/www/%s", CONFDIR, filename);
    logmsg(LOG_DEBUG,"Reading web-root file '%s'",full_filename);

    full_filename[sizeof(full_filename)-1] = '\0';
    struct stat mstatbuf;
    
    if( -1 == stat(full_filename, &mstatbuf) ) {
        logmsg(LOG_ERR,"read_webroot_file: Cannot stat file '%s' ( %d : %s )",full_filename,errno,strerror(errno));
        return -1;

    } else {

        struct tm t_tm1, t_tm2 ;
        char f1time[256],f2time[256];
        
        localtime_r(&mstatbuf.st_mtim.tv_sec, &t_tm1);
        strftime(f1time, 128, "%a, %d %b %Y %T %Z", &t_tm1);

        localtime_r(&modifiedSince, &t_tm2);
        strftime(f2time, 128, "%a, %d %b %Y %T %Z", &t_tm2);

        time_t t1 = mktime(&t_tm1);
        time_t t2 = mktime(&t_tm2);
#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"Comparing file time '%s' with modifiedSince '%s'",f1time,f2time);
#endif

        if( t1 < t2 ) {
            logmsg(LOG_DEBUG,"File '%s' not modified",full_filename);
            return 0; // Not modified
        } else {
            logmsg(LOG_DEBUG,"File '%s' is modified",full_filename);
        }
    }

    *file_buffer = '\0';
    FILE *fp = fopen(full_filename, "r");
    if (fp == NULL) {
        logmsg(LOG_ERR, "Cannot read file '%s'", full_filename);
        return -1;
    } else {
        logmsg(LOG_DEBUG, "Opened file '%s'", full_filename);
    }

    *actualfilelen=fread(file_buffer,sizeof(char),maxlen-1,fp);
    if( ferror(fp) ) {
        logmsg(LOG_ERR, "Error reading file '%s' ( %d : %s )", full_filename,errno,strerror(errno));
        return -1;
        fclose(fp);
    } else {
        if( feof(fp) ) {
            fclose(fp);
            return 1;       
        } else if( *actualfilelen==maxlen-1 ){
            logmsg(LOG_ERR, "Error reading file '%s' . File buffer too small to read entire file", full_filename);
            return -1;
            fclose(fp);            
        } else {
            logmsg(LOG_ERR, "Unknown error reading file '%s' .", full_filename);
            return -1;
            fclose(fp);                        
        }
    }
}

/**
 * Sendback a specified file with a 200 status (indicating file was found and everything
 * is OK)
 * @param sockd
 * @param file_buffer
 * @param buffer_len
 * @param mime_type
 */
void
sendback_http200_file(int sockd, char *file_buffer, size_t buffer_len, char *mime_type) {

    // Initialize a new page
    char server_id[255];
    snprintf(server_id, 254, "tvpvrd %s", server_version);
    // Send back a proper HTTP header

    time_t t = time(NULL);
    struct tm t_tm;
    char ftime[128];

    (void) gmtime_r(&t, &t_tm);
    strftime(ftime, 128, TIME_RFC822_FORMAT, &t_tm);
    _writef(sockd,
            "HTTP/1.1 200 OK\r\n"
            "Date: %s\r\n"
            "Last-Modified: %s\r\n"
            "Server: %s\r\n"
            "Connection: close\r\n"
            "Content-Length: %zu\r\n"
            "Content-Type: %s\r\n\r\n", ftime, ftime, server_id, buffer_len,mime_type);

    size_t num=write(sockd, file_buffer, buffer_len);
    if( num != buffer_len ) {
        logmsg(LOG_ERR, "Could not send file back to browser. Unknown error in write operation.");
    }
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG, "HTTP Header sent back (printed without \\r: "
            "HTTP/1.1 200 OK\n"
            "Date: %s\n"
            "Last-Modified: %s\n"
            "Server: %s\n"
            "Connection: close\n"
            "Content-Length: %zu\n"
            "Content-Type: %s\n\n", ftime, ftime, server_id, buffer_len, mime_type);
#endif    
}

int
get_filemimetype(char *filename, char *mimetype, size_t maxlen) {
    size_t n = strnlen(filename,255)-1;
    *mimetype='\0';
    while( n > 0 && filename[n] != '.') {
        --n;
    }
    if( n <= 0 ) {
        logmsg(LOG_ERR,"Cannot determine mime type based on file extension for '%s'",filename);    
        return -1;
    } else if( strnlen(&filename[n+1],5) > 4 ) {
        logmsg(LOG_ERR,"Unknown file extension '%s'",filename);    
        return -1;
    } else {
        if( 0==strncmp(&filename[n+1],"png",4) ) {
            strncpy(mimetype,"image/png",maxlen);
        } else if( 0==strncmp(&filename[n+1],"jpg",4) || 0==strncmp(&filename[n+1],"jpeg",5)) {
            strncpy(mimetype,"image/jpeg",maxlen);
        } else if( 0==strncmp(&filename[n+1],"gif",4) ) {
            strncpy(mimetype,"image/gif",maxlen);
        } else if( 0==strncmp(&filename[n+1],"css",4) ) {
            strncpy(mimetype,"text/css",maxlen);
        } else if( 0==strncmp(&filename[n+1],"txt",4) ) {
            strncpy(mimetype,"text/plain",maxlen);            
        } else {
            logmsg(LOG_ERR,"Unknown file extension '%s'",filename);    
            return -1;
        }    
        return 0;
    }
}

void
sendback_file(int sockd, char *filename, time_t modifiedSince) {
    char mimetype[24];
    if( -1==get_filemimetype(filename, mimetype, sizeof(mimetype)) ) {
        html_send_404header(sockd);            
    } else {
        size_t const maxfilesize = 50000;
        char *filebuffer = calloc(maxfilesize,sizeof(char));    
        if( NULL==filebuffer ) {
            logmsg(LOG_CRIT,"Out of memory when sending back file!! '%s'",filename);    
            html_send_404header(sockd);        
        } else {
            size_t actualsize=0;
            int ret=read_webroot_file(filebuffer,maxfilesize,&actualsize,filename,modifiedSince);
            if( 1==ret ) {
                sendback_http200_file(sockd, filebuffer, actualsize, mimetype);
                logmsg(LOG_DEBUG,"Sent back file '%s' as mime type '%s'",filename,mimetype);
            } else if( 0==ret) {
                html_send_304header(sockd);
            } else {
                html_send_404header(sockd);
            }
            free(filebuffer);        
        }
    }
}




/**
 * This is the main routine that gets called from the connection handler
 * when a new browser connection has been detected and the command from
 * the browser have been received. This function is totally responsible to
 * execute the command and prepare the WEB-page to be sent back.
 *
 * @param my_socket
 * @param inbuffer
 */
void
web_cmdinterp(const int my_socket, char *inbuffer) {
    char wcmd[1024];
    char *buffer = url_decode(inbuffer);
    char **field = (void *) NULL;
    int ret=0;
    int mobile=FALSE;

    if (webconnection(buffer, wcmd, sizeof(wcmd)-1)) {

        // Reset cmd_delay
        cmd_delay = 0;

        // Try to determine if the connection originated from a
        // mobile phone.
        mobile = is_mobile_connection(buffer);

#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"WEB connection after URL decoding:\n%s\n",buffer);
#endif
         if ((ret = matchcmd("GET /logout HTTP/1.1", buffer, &field)) == 1) {

             matchcmd_free(&field);
             free(buffer);
             web_login_page(my_socket,mobile);
             return;

         }

        // First check if we should handle an add/delete command
        if ((ret = matchcmd("GET /addrec\\?"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_AN
                " " _PR_HTTP_VER ,
                buffer, &field)) > 1) {

            const size_t maxvlen = 256;
            char channel[maxvlen], repeat[maxvlen], repeatcount[maxvlen];
            char sd[maxvlen], sh[maxvlen], smin[maxvlen], eh[maxvlen], emin[maxvlen];
            char profile[maxvlen], title[maxvlen], submit[maxvlen];

            get_assoc_value(repeat, maxvlen, "repeat", &field[1], ret - 1);
            get_assoc_value(repeatcount, maxvlen, "repeatcount", &field[1], ret - 1);
            get_assoc_value(channel, maxvlen, "channel", &field[1], ret - 1);
            get_assoc_value(sd, maxvlen, "start_day", &field[1], ret - 1);
            get_assoc_value(sh, maxvlen, "start_hour", &field[1], ret - 1);
            get_assoc_value(smin, maxvlen, "start_min", &field[1], ret - 1);
            get_assoc_value(eh, maxvlen, "end_hour", &field[1], ret - 1);
            get_assoc_value(emin, maxvlen, "end_min", &field[1], ret - 1);
            get_assoc_value(profile, maxvlen, "profile", &field[1], ret - 1);
            get_assoc_value(title, maxvlen, "title", &field[1], ret - 1);
            get_assoc_value(submit, maxvlen, "submit_addrec", &field[1], ret - 1);

            if (0 == strcmp(submit, "Add")) {
                char tmpcmd[128];
                // Build command

                if (0 == strcmp(repeat, "")) {
                    snprintf(wcmd, sizeof(wcmd)-1, "a %s", channel);
                } else {
                    // Repeat add
                    snprintf(wcmd, sizeof(wcmd)-1, "ar %s %s %s ", repeat, repeatcount, channel);
                }
                if (*sd != '\0') {
                    snprintf(tmpcmd, 128, " %s ", sd);
                    strncat(wcmd, tmpcmd, sizeof(wcmd)-1-strlen(wcmd));
                }
                snprintf(tmpcmd, 128, " %s:%s ", sh, smin);
                strncat(wcmd, tmpcmd, sizeof(wcmd)-1-strlen(wcmd));

                snprintf(tmpcmd, 128, " %s:%s ", eh, emin);
                strncat(wcmd, tmpcmd, sizeof(wcmd)-1-strlen(wcmd));

                snprintf(tmpcmd, 128, " %s @%s ", title, profile);
                strncat(wcmd, tmpcmd, sizeof(wcmd)-1-strlen(wcmd));

            }

            matchcmd_free(&field);

        } else if ((ret = matchcmd("GET /addqrec\\?"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_AN
                " " _PR_HTTP_VER,
                buffer, &field)) > 1) {

            const int maxvlen = 256;
            char channel[maxvlen];
            char length_hour[maxvlen], length_min[maxvlen];
            char profile[maxvlen], title[maxvlen], submit[maxvlen];

            get_assoc_value(channel, maxvlen, "channel", &field[1], ret - 1);
            get_assoc_value(length_hour, maxvlen, "length_hour", &field[1], ret - 1);
            get_assoc_value(length_min, maxvlen, "length_min", &field[1], ret - 1);
            get_assoc_value(profile, maxvlen, "profile", &field[1], ret - 1);
            get_assoc_value(title, maxvlen, "title", &field[1], ret - 1);
            get_assoc_value(submit, maxvlen, "submit_qaddrec", &field[1], ret - 1);

            if (0 == strcmp(submit, "Start")) {
                char tmpcmd[128];
                // Build command
                snprintf(wcmd, sizeof(wcmd)-1, "q %s", channel);
                snprintf(tmpcmd, sizeof(tmpcmd)-1, " %s:%s ", length_hour, length_min);
                strncat(wcmd, tmpcmd, sizeof(wcmd)-1-strlen(wcmd));
                snprintf(tmpcmd, sizeof(tmpcmd)-1, " %s @%s ", title, profile);
                strncat(wcmd, tmpcmd, sizeof(wcmd)-1-strlen(wcmd));
                cmd_delay = 2400000;
            }

            matchcmd_free(&field);


        } else if ((ret = matchcmd("GET /chwt\\?" 
                    _PR_AN "=" _PR_AN " " _PR_HTTP_VER ,
                    buffer, &field)) > 1) {
            const int maxvlen = 128;
            char wtheme[maxvlen];
            get_assoc_value(wtheme, maxvlen, "t", &field[1], ret - 1);
            strncpy(web_theme,wtheme,sizeof(web_theme)-1);
            *wcmd = '\0';
            
        } else if ((ret = matchcmd("GET /killrec\\?" 
                    _PR_AN "=" _PR_N " " _PR_HTTP_VER ,
                    buffer, &field)) > 1) {

            const int maxvlen = 256;
            char recid[maxvlen], submit[maxvlen];
            get_assoc_value(recid, maxvlen, "rid", &field[1], ret - 1);
            get_assoc_value(submit, maxvlen, "submit_killrec", &field[1], ret - 1);
            snprintf(wcmd, sizeof(wcmd)-1, "! %s", recid);

            // Wait half a second to allow the removal to be done and completed so that
            // it will show when the WEB-page is refreshed.
            cmd_delay = 500000;

            matchcmd_free(&field);

        } else if ((ret = matchcmd("^GET /delrec\\?"
                _PR_AN "=" _PR_ANO "&"
                _PR_AN "=" _PR_ANO "&"
                _PR_AN "=" _PR_ANO
                " " _PR_HTTP_VER,
                buffer, &field)) > 1) {

            const int maxvlen = 256;
            char recid[maxvlen], submit[maxvlen], delserie[maxvlen];
            get_assoc_value(recid, maxvlen, "recid", &field[1], ret - 1);
            get_assoc_value(delserie, maxvlen, "delserie", &field[1], ret - 1);
            get_assoc_value(submit, maxvlen, "submit_delrec", &field[1], ret - 1);

            if (0 == strcmp(submit, "Delete")) {
                if (0 == strcmp(delserie, "Yes")) {
                    snprintf(wcmd, sizeof(wcmd)-1, "dr %s", recid);
                } else {
                    snprintf(wcmd, sizeof(wcmd)-1, "d %s", recid);
                }
            }

            matchcmd_free(&field);

        } else if ((ret = matchcmd("^GET /favicon.ico" _PR_ANY _PR_E, buffer, &field)) >= 1) {
            
            matchcmd_free(&field);
            free(buffer);
            logmsg(LOG_DEBUG,"Ignoring 'GET /favicon.ico'");
            html_send_404header(my_socket);
            return;
                        
        } else if ( (ret = matchcmd("^GET /" _PR_FNAME " " _PR_HTTP_VER , buffer, &field)) >= 1) {
                       
            logmsg(LOG_DEBUG,"Found generic GET file command.");
                       
            // Send back the requested file

                // Check if we have received the "If-Modified-Since:" header
                // In that case we send beck the "Not Modified 304" reply
                // HTTP Header date format, i.e. Sat, 29 Oct 1994 19:43:31 GMT
                // RFC 1123 format
                char *filename = strdup(field[1]);
                matchcmd_free(&field);

                time_t mtime = 0;
                struct tm tm_date;
                if( (ret = matchcmd_ml("^If-Modified-Since\\: (.*)", buffer, &field)) > 1 ) {
#ifdef EXTRA_WEB_DEBUG
                    logmsg(LOG_DEBUG,"Found If-Modified-Since: header. Value=%s",field[1]);
#endif
                    // Convert time to a timestamp to compare with file modified time
                    // Match HTTP Header date format, i.e. Sat, 29 Oct 1994 19:43:31 GMT

                    // Try first to use en_US locale since the header is in English.
                    // If this doesn't work then we use the "C" locale but this might not
                    // work to interpret the header.
                    locale_t lc =  newlocale(LC_ALL_MASK,"en_US",NULL);
                    if( lc == NULL ) {
                        lc =  newlocale(LC_ALL_MASK,"C",NULL);
                    }
                    if( lc == NULL ) {
                        logmsg(LOG_ERR,"Cannot create locale to set a date to compare modified since header");
                        matchcmd_free(&field);
                        mtime = mktime(&tm_date);
                        localtime_r(&mtime,&tm_date);
                        mtime += tm_date.tm_gmtoff;
                        localtime_r(&mtime,&tm_date);
                        sendback_file(my_socket,filename,mtime);
                    } else {
                        char *retptr = strptime_l(field[1],"%a, %d %b %Y %T GMT",&tm_date,lc);
                        freelocale(lc);
                        if( retptr == NULL ) {
                            logmsg(LOG_NOTICE,"Failed date parsing in IF-Modified-Since Header (%s)",field[1]);
                            // Set the date a month back to force a resend of the CSS header in case the header
                            // can not be parsed.
                            sendback_file(my_socket,filename,time(NULL)-3600*24*30);
                        } else {

#ifdef EXTRA_WEB_DEBUG
                            logmsg(LOG_DEBUG,"After strptime_l hour=%d, zone=GMT",tm_date.tm_hour);
#endif
                            mtime = mktime(&tm_date);
                            localtime_r(&mtime,&tm_date);

#ifdef EXTRA_WEB_DEBUG
                            logmsg(LOG_DEBUG,"Localtime offset=%d, zone=%s",tm_date.tm_gmtoff,tm_date.tm_zone);
#endif

                            // Since the original time is given in GMT and we want it expressed
                            // in the local time zone we must add the offset from GMTIME for the
                            // current time zone
                            mtime += tm_date.tm_gmtoff;
                            localtime_r(&mtime,&tm_date);

#ifdef EXTRA_WEB_DEBUG
                            logmsg(LOG_DEBUG,"After localtime adjustment hour=%d",tm_date.tm_hour);
#endif


                            sendback_file(my_socket,filename,mtime);
                        }
                        matchcmd_free(&field);
                    }
                    
                } else {
#ifdef EXTRA_WEB_DEBUG
                    logmsg(LOG_DEBUG,"No 'If-Modified-Since:' field. Sending back file.");
#endif
                    sendback_file(my_socket,filename,mtime);
                }
                free(filename);
                free(buffer);
                
                return;


        }

        if ( ((ret = matchcmd("^GET /favicon.ico" _PR_ANY _PR_E, buffer, &field)) < 1) ) {
            // If it's not a "favicon.ico" GET command proceed to execute the
            // tvpvrd command we have received

            static char logincookie[128];

            // If user does not have a valid login then send back the login page
            if (!user_loggedin(buffer, logincookie, 127)) {

                // Check if user just tried to login
                if ((ret = matchcmd("^GET /login\\?"
                        _PR_AN "=" _PR_ANPO "&"
                        _PR_AN "=" _PR_ANPO "&"
                        _PR_AN "=" _PR_ANPO
                        " " _PR_HTTP_VER,
                        buffer, &field)) > 1) {

                    const int maxvlen = 64;
                    char user[maxvlen], pwd[maxvlen], logsubmit[maxvlen];
                    get_assoc_value(user, maxvlen, "user", &field[1], ret - 1);
                    get_assoc_value(pwd, maxvlen, "pwd", &field[1], ret - 1);
                    get_assoc_value(logsubmit, maxvlen, "submit_login", &field[1], ret - 1);

                    matchcmd_free(&field);
                    
                    if (0 == strcmp(logsubmit, "Login")) {

#ifdef EXTRA_WEB_DEBUG
                        logmsg(LOG_DEBUG,"WEB login. users=%s, pwd=%s",user,pwd);
#endif

                        if (!validate_login(user, pwd)) {

                            logmsg(LOG_NOTICE,"WEB login failed. Tried users='%s', pwd='%s'",user,pwd);

                            // Validation of login details failed, send back the login screen
                            web_login_page(my_socket, mobile);

                        } else {
#ifdef EXTRA_WEB_DEBUG
                        logmsg(LOG_DEBUG,"WEB login successful. Sending back main page with cookie",user,pwd);
#endif
                            // Login successful. Show the main page and used the "version" command
                            // as the default command to show in the output area.
                            web_main_page(my_socket, "v", create_login_cookie(user, pwd), mobile);
                        }

                    } else {

                        // Unrecognized login POST fields so go back to login page
                        web_login_page(my_socket, mobile);
                    }
                } else {
                    // If the login cookie is not valid and the user has not given the login command
                    // we just send back the login page.
                    web_login_page(my_socket, mobile);              
                }
            } else {
                // User has a valid login so send back the main page
                web_main_page(my_socket, wcmd, logincookie, mobile);
            }
        } else {
            // Ignore GET favicon.ico
            matchcmd_free(&field);
            html_send_404header(my_socket);
        }
    } else {        
        html_send_404header(my_socket);
        logmsg(LOG_NOTICE, "Unrecognized WEB-command: [len=%d] %s", strlen(buffer), buffer);
    }

    free(buffer);
}

static const char *min_list_start[] = {
    "00", "05", "10", "15", "20", "25", "30",
    "35", "40", "45", "50", "55"
};
static const char *min_list_end[] = {
    "00", "04", "09", "14", "19", "24", "29", "30",
    "39", "44", "49", "54", "59"
};
static const char *hour_list[] = {
    "17", "18", "19", "20", "21", "22", "23",
    "00", "01", "02", "03", "04", "05", "06",
    "07", "08", "09", "10", "11", "12","13", "14", "15", "16"
};
static const char *hourlength_list[] = {
    "0", "1", "2", "3"
};

void
_web_cmd_module_start(int sockd,char *legend) {
    _writef(sockd, "<div class=\"cmdmodule\">\n");
    _writef(sockd, "<fieldset><legend>%s</legend>\n",legend);
}

void
_web_cmd_module_end(int sockd) {
    _writef(sockd, "</fieldset>\n");    
    _writef(sockd, "</div> <!-- cmdmodule -->\n");
}

/**
 * Display the next recording area
 * @param sockd
 */
void
web_cmd_next(int sockd) {
    _web_cmd_module_start(sockd,"Next recording");
    _writef(sockd, "<div class=\"displayasled_on nofloat\" id=\"nextrec\">\n");
    list_recs(1,4,sockd); // Use style==4 , fancy
    _writef(sockd, "</div> <!-- displayasled -->\n");
    _web_cmd_module_end(sockd);
}

/**
 * Display the ongoing transaction area
 * @param sockd
 */
void
web_cmd_ongoingtransc(int sockd) {

    _web_cmd_module_start(sockd,"Ongoing transcoding");
    size_t num=get_num_ongoing_transcodings();

    if( 0==num ) {
        _writef(sockd, "<div class=\"ongoing_transc_entry fullw\">\n");
        _writef(sockd, "<div class=\"displayasled_off\"><pre> - - -</pre></div>\n");
        _writef(sockd, "<div class=\"ongoing_transc_stop_disabled\">Stop</div>\n");
        _writef(sockd, "</div> <!-- ongoing_transc_entry -->\n");
    } else {
        for (size_t i = 0; i < max_ongoing_transcoding; i++) {
            if (0 == i % 2) {
                _writef(sockd, "<div class=\"ongoing_transc_entry%s\">\n", num > 1 ? " halfw" : " fullw");
            } else {
                _writef(sockd, "<div class=\"ongoing_transc_entry%s margleft\">\n", num > 1 ? " halfw" : " fullw");
            }

            if (ongoing_transcodings[i]) {
                time_t now = time(NULL);
                int rtime = now-ongoing_transcodings[i]->start_ts;
                int rh = rtime/3600;
                int rmin = (rtime - rh*3600)/60;

                _writef(sockd, "<div class=\"displayasled_on\"><pre>(%02d:%02d)\n%s</pre></div>\n",rh,rmin,ongoing_transcodings[i]->filename);
                _writef(sockd, "<div class=\"ongoing_transc_stop\"><a href=\"cmd?c=kt%%20%d\">Stop</a></div>\n",i);

            }
            
            _writef(sockd, "</div> <!-- ongoing_transc_entry -->\n");
        }
    }
    
    

    _web_cmd_module_end(sockd);
}

/**
 * Display the ongoing recordings area
 * @param sockd
 */
void
web_cmd_ongoing(int sockd) {

    _web_cmd_module_start(sockd,"Ongoing recordings");
    
    char buffer[255],caption_buffer[255];
    for (size_t i = 0; i < max_video; i++) {
        if (-1 == video_get_cardinfo(i, FALSE, buffer, sizeof (buffer))) {
            snprintf(caption_buffer, sizeof (caption_buffer), "Card %d", i + 1);
        } else {
            snprintf(caption_buffer, sizeof (caption_buffer), "%s.", buffer);
        }
        if (0 == i % 2) {
            _writef(sockd, "<div class=\"ongoing_rec_entry%s\">\n", max_video > 1 ? " halfw" : " fullw");
        } else {
            _writef(sockd, "<div class=\"ongoing_rec_entry%s margleft\">\n", max_video > 1 ? " halfw" : " fullw");
        }
        if (ongoing_recs[i]) {
            int ey, em, ed, eh, emi, es;
            fromtimestamp(ongoing_recs[i]->ts_end, &ey, &em, &ed, &eh, &emi, &es);
            int sy, sm, sd, sh, smi, ss;
            fromtimestamp(ongoing_recs[i]->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
            _writef(sockd, "<div class=\"displayasled_on\"><pre>%s %02d:%02d-%02d:%02d\n%s</pre></div>", ongoing_recs[i]->channel, sh, smi, eh, emi, ongoing_recs[i]->title);
        } else {
            _writef(sockd, "<div class=\"displayasled_off\"><pre> - - -</pre></div>\n");
        }
        _writef(sockd, "<div class=\"caption\">\n");
        _writef(sockd, caption_buffer);
        _writef(sockd, "</div>\n");
        if (ongoing_recs[i]) {
            _writef(sockd, "<div class=\"ongoing_rec_stop\"><a href=\"killrec?rid=%d\">Stop</a></div>\n", i);
        } else {
            _writef(sockd, "<div class=\"ongoing_rec_stop_disabled\">Stop</div>\n");
        }

        _writef(sockd, "</div> <!-- ongoing rec entry -->\n");

    }

    _web_cmd_module_end(sockd);

}

/**
 * Display the quick add area
 * @param sockd
 */
void
web_cmd_qadd(int sockd) {
    const char *station_list[128];
    const size_t n_stations = get_stations(station_list, 128);

    const char *profile_list[64];
    const size_t n_profile = get_profile_names(profile_list, 64);

    const size_t n_hourlength = sizeof (hourlength_list) / sizeof (char *);
    const size_t n_min_end = sizeof (min_list_end) / sizeof (char *);

    _web_cmd_module_start(sockd,"Quick recording");

    /*
     * Add quick recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"addqrec\">\n", "id_qadd_form");

    html_element_select(sockd, "Profile:", "profile", default_transcoding_profile, profile_list, n_profile, "id_qprofile");
    html_element_select(sockd, "Station:", "channel", NULL, station_list, n_stations, "id_qstation");

    html_element_select(sockd, "Len:", "length_hour", "00", hourlength_list, n_hourlength, "id_length_hour");
    html_element_select(sockd, "&nbsp;", "length_min", "45", min_list_end, n_min_end, "id_length_min");

    html_element_input_text(sockd, "Title:", "title", "id_qtitle");
    html_element_submit(sockd, "submit_qaddrec", "Start", "id_qaddrec");

    _writef(sockd, "</form>\n");

    _web_cmd_module_end(sockd);

}

/**
 * Display the add and delete recording area
 * @param sockd
 */
void
web_cmd_add(int sockd) {
    static const struct skeysval_t  day_list[] = {
        {"","Auto"},
        {"Mon","Mon"},
        {"Tue","Tue"},
        {"Wed","Wed"},
        {"Thu","Thu"},
        {"Fri","Fri"},
        {"Sat","Sat"},
        {"Sun","Sun"} 
    };

    const size_t n_day = 8;
    static const struct skeysval_t rpt_list[] = {
        {.key = "", .val = "(none)"},
        {.key = "w", .val = "Weekly"},
        {.key = "d", .val = "Daily"},
        {.key = "f", .val = "Mon-Fri"},
        {.key = "t", .val = "Mon-Thu"},
        {.key = "n", .val = "Tue-Fri"},
        {.key = "s", .val = "Sat-Sun"}
    };
    const size_t n_rpt = 7;
    static const char *rptcount_list[] = {
        " ", "02", "03", "04", "05", "06", "07", "08", "09", "10",
        "11", "12", "13", "14", "15", "16", "17", "18", "19",
        "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
        "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
        "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50"
    };
    const size_t n_rptcount = 39;
    const char *station_list[128];
    const size_t n_stations = get_stations(station_list, 128);

    const char *profile_list[64];
    const size_t n_profile = get_profile_names(profile_list, 64);

    const size_t n_hour = sizeof (hour_list) / sizeof (char *);
    const size_t n_min_start = sizeof (min_list_start) / sizeof (char *);
    const size_t n_min_end = sizeof (min_list_end) / sizeof (char *);

    _web_cmd_module_start(sockd,"New recording");

    /*
     * Add recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"addrec\">\n", "addrecording");

    html_element_select(sockd, "Profile:", "profile", default_transcoding_profile, profile_list, n_profile, "id_profile");
    html_element_select(sockd, "Station:", "channel", NULL, station_list, n_stations, "id_station");
    html_element_select_code(sockd, "Repeat:", "repeat", NULL, rpt_list, n_rpt,"id_rpttype");
    html_element_select(sockd, "Count:", "repeatcount", NULL, rptcount_list, n_rptcount, "id_rptcount");
    
    html_element_select_code(sockd, "Day:", "start_day", NULL, day_list, n_day, "id_start");
    
    html_element_select(sockd, "Start:", "start_hour", "18", hour_list, n_hour, "id_starthour");
    html_element_select(sockd, "&nbsp;", "start_min", NULL, min_list_start, n_min_start, NULL);
    _writef(sockd, "<div class=\"input_container\" id=\"be_hyphen\"><span class=\"be_hyphen\"> &rarr; </span></div>");
    html_element_select(sockd, "End:", "end_hour", "18", hour_list, n_hour, "id_endhour");
    html_element_select(sockd, "&nbsp;", "end_min", "59", min_list_end, n_min_end, NULL);

    html_element_input_text(sockd, "Title:", "title", "id_title");
    html_element_submit(sockd, "submit_addrec", "Add", "id_addrec");

    _writef(sockd, "</form>\n");

    // Close container
    _web_cmd_module_end(sockd);
}


/**
 * Display the add and delete recording area
 * @param sockd
 */
void
web_cmd_del(int sockd) {
    static const char *yn_list[] = {
        "Yes", "No"
    };
    const size_t n_ynlist = 2;
    
    /*
     * Delete recordings
     */
    _web_cmd_module_start(sockd,"Delete recording");
    
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"delrec\"  onsubmit=\"return confirm('Really delete?')\">\n", "deleterecording");

    struct skeysval_t *listrec;
    size_t num = list_recskeyval(&listrec, 10); // style==10, simple format with no idx and no profile
    html_element_select_code(sockd, "Title:", "recid", NULL, listrec, num, "id_delselect");
    for (size_t i = 0; i < num; ++i) {
        free(listrec[i].key);
        free(listrec[i].val);
    }
    free(listrec);

    html_element_select(sockd, "Delete serie:", "delserie", "No", yn_list, n_ynlist, "id_seriesyn");
    html_element_submit(sockd, "submit_delrec", "Delete", "delrec");

    _writef(sockd, "</form>\n");

    // Close container
    _web_cmd_module_end(sockd);
}
/*
void
web_theme_select(int sockd) {
    static const char *theme_list[] = {
        "plain", "hq", "metal", "night"
    };
    const size_t n_themelist = 4;
    
    _writef(sockd,"<form name=\"chwt_form\" method=\"get\" action=\"chwt\" id=\"id_wtform\">\n");
    _writef(sockd,"<div id=\"theme_select\">\n");
    html_element_select(sockd, "Theme:", "t", web_theme, theme_list, n_themelist, "id_wt");
    _writef(sockd,"\n</div> <!-- theme_select -->\n");
    _writef(sockd,"<form>\n");
}
*/

/**
 * The full main page used when we are called from an ordinary browser, This is also
 * the place where we execute the WEb-command as a side effect to get the web output
 * @param sockd
 * @param wcmd
 * @param cookie_val
 * @param mobile
 */
void
web_main_page(int sockd, char *wcmd, char *cookie_val, int mobile) {
    // Initialize a new page

    if (mobile) {
        web_main_page_mobile(sockd, wcmd, cookie_val);
        return;
    }

    html_startpage(sockd, cookie_val, FALSE);
    html_windtitlebar(sockd);

    // Left side : Command table
    _writef(sockd, "<div id=\"windowmenu\">");
    web_commandlist(sockd);
    _writef(sockd, "\n</div> <!-- windowmenu -->\n"); 

    // Right side : Output and recording management
    _writef(sockd, "<div id=\"windowcontent\">\n");
    html_cmd_output(sockd, wcmd);
    usleep(cmd_delay); // Give some time for the command to execute
    web_cmd_next(sockd);
    web_cmd_ongoing(sockd);
    web_cmd_add(sockd);    
    web_cmd_qadd(sockd);
    web_cmd_del(sockd);
    web_cmd_ongoingtransc(sockd);   
    _writef(sockd, "\n</div> <!-- windowcontent -->\n");
 /*   
    if( disp_theme_select ) {
        web_theme_select(sockd);
    }
   */ 
    html_statusbar(sockd);
    html_endpage(sockd);

}

/**
 * The modified (smaller) main page used when we are called from a mobile
 * browser.
 * @param sockd
 * @param wcmd
 * @param cookie_val
 */
void
web_main_page_mobile(int sockd, char *wcmd, char *cookie_val) {
    // Initialize a new page

    html_startpage(sockd, cookie_val, TRUE);
    html_windtitlebar(sockd);

    _writef(sockd, "<div class=\"single_side\">");
    web_commandlist_short(sockd);
    html_cmd_output(sockd, wcmd);
    web_cmd_add(sockd);
    _writef(sockd, "\n</div> <!-- single_side -->\n");

    html_endpage(sockd);

}


/**
 * Display the login page
 * @param sockd
 * @param mobile
 */
void
web_login_page(int sockd, int mobile) {
    // Initialize a new page

    logmsg(LOG_DEBUG,"Sending back login page");
    
    // Give the special cookie value "logout" which will create a
    // header which replace the old cookie and sets it expire time
    // in the past so it is removed from the browser
    html_startpage(sockd, "logout", mobile);
    html_windtitlebar(sockd);

    _writef(sockd, "<div id=\"login_window\">");
    _writef(sockd, "<div id=\"login_title\">Please login</div>");
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"login\">\n", "tvlogin");
    html_element_input_text(sockd, "User:", "user", "id_loginuser");
    html_element_input_password(sockd, "Password:", "pwd", "id_loginpwd");
    html_element_submit(sockd, "submit_login", "Login", "id_submitlogin");

    _writef(sockd, "<form>");
    _writef(sockd, "</div>");

    html_endpage(sockd);
}


/**
 * =================================================================================
 * The following section is used to handle the command menu in the WEB interface
 * this is built up from a list structure to separate the content from the
 * actual layout. This way it is easy to adjust the output depending on whether
 * we are called from a mobile or from a stationary browser.
 *
 * We then have to sets of commands. One set to use for the mobile browser and one
 * full set to be used for the ordinary browser.
 *
 * We also have different commands allowed depending on if the server is running
 * in master or slave mode. (The slave does not support any recording commands)
 * =================================================================================
 */

// An entry for a single command. The display name and the actual command string
// to send back to the server
struct cmd_entry {
    char *cmd_name;
    char *cmd_desc;
};

// Logically we group a set of commnds in groups with logically similair commands
// to make it easier fro the user to navigate.
struct cmd_grp {
    char *grp_name;
    char *grp_desc;
    size_t cmd_num;
    struct cmd_entry *entry;
};



//------------------------------------------------------------------
// MASTER personality
//------------------------------------------------------------------
static struct cmd_entry cmdfunc_master_recs[] = {
    {"lh", "List all"},
    {"lr", "List rep."},
    {"lu", "List single"},
    {"lm", "Mail all"},
    {"lmr","Mail separated"},
    {"rh", "History"},
    {"rhm","Mail history"}
};

static struct cmd_entry cmdfunc_master_transcoding[] = {
    {"wt", "Queue"},
    {"st", "Statistics"}
};

static struct cmd_entry cmdfunc_master_status[] = {
    {"s", "Status"},
    {"t", "Time"},
    {"df","Disk space"}
};

static struct cmd_entry cmdfunc_master_view[] = {
    {"z",  "Settings"},
    {"ls", "Station list"},
    {"vc", "Cards"},
    {"li", "Card inputs"},
    {"lph","Profiles"}    
};

/*
static struct cmd_entry cmdfunc_master_driver[] = {
    {"vc", "Cards"},
    {"li", "List inputs"}
    {"lc%200", "Settings #0"},
    {"lc%201", "Settings #1"}
};
*/

//------------------------------------------------------------------
// SLAVE personality
//------------------------------------------------------------------
static struct cmd_entry cmdfunc_slave_transcoding[] = {
    {"ot", "Ongoing transcodings"},
    {"wt", "Waiting transcodings"},
    {"st", "Statistics"},
    {"lph", "Profiles"}
};

static struct cmd_entry cmdfunc_slave_status[] = {
    {"s", "Status"},
    {"t", "Time"}
};

static struct cmd_entry cmdfunc_slave_view[] = {
    {"z", "Settings"},
    {"rh", "History"}
};

//------------------------------------------------------------------
// Mobile phone master personality
//------------------------------------------------------------------
static struct cmd_entry cmdfunc_master_menu_short[] = {
    {"s", "Status"},
    {"lh", "List"},
    {"o", "Ongoing"}
};


//------------------------------------------------------------------
// Command groups for normal, slave and phone (short version)
//------------------------------------------------------------------

static struct cmd_grp cmd_grp_master[] = {
    {"Server", "Server information", sizeof (cmdfunc_master_status) / sizeof (struct cmd_entry), cmdfunc_master_status},
    {"Recordings", "Stored recordings", sizeof (cmdfunc_master_recs) / sizeof (struct cmd_entry), cmdfunc_master_recs},
    {"View", "View", sizeof (cmdfunc_master_view) / sizeof (struct cmd_entry), cmdfunc_master_view},
    {"Transcoding", "Transcoding info", sizeof (cmdfunc_master_transcoding) / sizeof (struct cmd_entry), cmdfunc_master_transcoding}    
    /*
    {"Capture cards", "Card information", sizeof (cmdfunc_master_driver) / sizeof (struct cmd_entry), cmdfunc_master_driver}
     */
};

static struct cmd_grp cmd_grp_slave[] = {
    {"Transcoding", "Transcoding info", sizeof (cmdfunc_slave_transcoding) / sizeof (struct cmd_entry), cmdfunc_slave_transcoding},
    {"Server", "Show status", sizeof (cmdfunc_slave_status) / sizeof (struct cmd_entry), cmdfunc_slave_status},
    {"View", "View", sizeof (cmdfunc_slave_view) / sizeof (struct cmd_entry), cmdfunc_slave_view}
};

static struct cmd_grp cmd_grp_master_menu_short[] = {
    {"Menu", "Server information", sizeof (cmdfunc_master_menu_short) / sizeof (struct cmd_entry), cmdfunc_master_menu_short}
};

static struct cmd_grp cmd_grp_slave_short[] = {
    {"Server", "Show status", sizeof (cmdfunc_slave_status) / sizeof (struct cmd_entry), cmdfunc_slave_status},
    {"Trans", "Transcoding info", sizeof (cmdfunc_slave_transcoding) / sizeof (struct cmd_entry), cmdfunc_slave_transcoding}
};

/**
 * Display the long command list (used for ordinary browsers)
 * @param sockd
 */
void
web_commandlist(int sockd) {

    static struct cmd_grp *cmdgrp;
    int cmdgrplen;

    if (is_master_server) {
        cmdgrp = cmd_grp_master;
        cmdgrplen = sizeof (cmd_grp_master) / sizeof (struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave;
        cmdgrplen = sizeof (cmd_grp_slave) / sizeof (struct cmd_grp);
    }

    for (int i = 0; i < cmdgrplen; ++i) {

        _writef(sockd, "<div class=\"mnugrp_title_row\">\n<span class=\"mnugrp_title\">%s</span>\n</div>\n",
                cmdgrp[i].grp_name, cmdgrp[i].grp_desc);

        _writef(sockd, "<div class=\"mnugrp_commands\">\n");
        for (size_t j = 0; j < cmdgrp[i].cmd_num; ++j) {
            _writef(sockd, "<a href=\"cmd?c=%s\">&#8718; %s</a><br />\n", cmdgrp[i].entry[j].cmd_name, cmdgrp[i].entry[j].cmd_desc);
        }
        _writef(sockd, "</div>\n");

    }
}

/**
 * Display the short version of the command list. Used for mobile browsers.
 * @param sockd
 */
void
web_commandlist_short(int sockd) {

    static struct cmd_grp *cmdgrp;
    size_t cmdgrplen;

    if (is_master_server) {
        cmdgrp = cmd_grp_master_menu_short;
        cmdgrplen = sizeof (cmd_grp_master_menu_short) / sizeof (struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave_short;
        cmdgrplen = sizeof (cmd_grp_slave_short) / sizeof (struct cmd_grp);
    }

    _writef(sockd, "<div class=\"cmd_menu_short\">\n");
    for (size_t i = 0; i < cmdgrplen; ++i) {

        for (size_t j = 0; j < cmdgrp[i].cmd_num; ++j) {
            _writef(sockd, "<div class=\"cmdgrp_commands_short\">");
            _writef(sockd, "<a href=\"cmd?c=%s\">&#8718; %s</a>", cmdgrp[i].entry[j].cmd_name, cmdgrp[i].entry[j].cmd_desc);
            _writef(sockd, "</div>\n");
        }
    }
    _writef(sockd, "</div> <!-- cmd_menu_short -->\n");

}
