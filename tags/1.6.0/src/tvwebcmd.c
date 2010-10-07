/* =========================================================================
 * File:        TVWEBCMD.C
 * Description: Handle the interface via a Web-browser
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009 Johan Persson
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
// Standard UNIX includes
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "tvpvrd.h"
#include "utils.h"
#include "tvwebcmd.h"
#include "confpath.h"
#include "tvcmd.h"
#include "freqmap.h"
#include "transc.h"
#include "recs.h"
#include "pcre.h"
/*
 * The WEb interface is fairly simplistic. On receiving a GET request from a
 * WEB-browser we immitate the behavior of a HTTP server by responding with a
 * correct web page.
 *
 * At the moment the interface is very simplistic. The WEB adress to be used
 * is specified as "/cmd?<command>" where "<command>" should be replaced by the
 * corresponding tvpvrd command. For example, runnig on the localserver the following
 * command will return a list of all upcoming recordings to the browser
 *
 * http://localhost:9300/cmd?l
 *
 * The above URL assumes that the server listens on port 9300 as is the default in
 * the distribution. If you change the port the URL has to be udpated accordingly.
 *
 * The main difference from the terminal connection is that the connection will be
 * automatically closed after each command.
 *
 */


/* Some local forward declarations.
 * Note: All functions prefixed with "html_" are used to generate HTML output
 * indicated by the name.
 */
void
html_element_input_text(int sockd, char *legend, char *name, char *id);

void
html_element_submit(int sockd, char *name, char *value, char *id);

void
html_main_page(int sockd, char *wcmd, char *cookie, int mobile);

void
html_main_page_mobile(int sockd, char *wcmd, char *cookie);

void
html_login_page(int sockd, int mobile);

void
html_notfound(int sockd);

void
html_cmd_qadd(int sockd);

int
read_cssfile(char *buff, int maxlen, int mobile);

void
html_cmd_ongoingtransc(int sockd);

void
html_commandlist_short(int sockd);

void
html_cmd_ongoing(int sockd);

void
html_cmd_next(int sockd);

#define TIME_RFC822_FORMAT "%a, %d %b %Y %T GMT"

// For some commands (like delete) we want to wait a little bit in order
// for the command to have effect before we report back on the status in
// the web interface. If we didn't do this some commands would not be visible
// until the next refresh of the web page. This could confuse the user.
static int cmd_delay=0;

/* The magic cookie seed used to generate the unique cookie that represents
 * one particular login.
 */
#define LOGIN_COOKIE "d_ye8aj82hApsj02njfuyysad"

/**
 * Validate submitted user/pwd with stored login credentials
 * @param user
 * @param pwd
 * @return
 */
int
validate_login(char *user, char *pwd) {

    if (0 == strcmp(user, web_user) && 0 == strcmp(pwd, web_password))
        return 1;
    else
        return 0;
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
    strcpy(_cookie_buff, LOGIN_COOKIE);

    char hostname[128];
    gethostname(hostname, 127);
    hostname[127] = '\0';

    char buff[128];
    strncpy(buff, user, 127);
    buff[127] = '\0';
    strncat(buff, pwd, 64);
    buff[127] = '\0';
    strncat(buff, hostname, 64);
    buff[127] = '\0';

    int n = MIN(strlen(_cookie_buff), strlen(buff));

    // Now use the "secret" cookie seed to scramble the combination
    // os user,password and server name to create a unique cookie id
    // consisting only of "normal" 33-127 (ASCII) characters.
    for (int i = 0; i < n; ++i) {

        _cookie_buff[i] += buff[i];
        _cookie_buff[i] &= 127;
        if ((int) _cookie_buff[i] < 32)
            _cookie_buff[i] += 32;

        // Remove the URL special chars, '+' 
        if (_cookie_buff[i] == '+')
            _cookie_buff[i] = '_';

    }

    _cookie_buff[n] = '\0';

    logmsg(LOG_DEBUG,"Created cookie: '%s' from %s",_cookie_buff, buff);

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

    if (!require_web_password)
        return 1;

    if ((ret = matchcmd(_PR_ANY "Cookie: tvpvrd=" _PR_ANP, buffer, &field)) > 1) {

        char *tmpbuff = url_decode(field[2]);

        logmsg(LOG_DEBUG, "Received cookie: %s decoded as: %s", field[2], tmpbuff);

        int sucess=0;
        if (validate_cookie(tmpbuff)) {

            logmsg(LOG_DEBUG, "Received cookie valdidated correctly.");

            strncpy(cookie, tmpbuff, maxlen);
            cookie[maxlen - 1] = '\0';
            free(tmpbuff);
            sucess = 1;

        } else {

            logmsg(LOG_DEBUG, "Received cookie was NOT a valdid login cookie.");

            free(tmpbuff);
        }

        matchcmd_free(field);

        return sucess;

    } else {

        logmsg(LOG_DEBUG, "No cookie found to validate in HTTP Header.");

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

    char **field = (void *) NULL;
    if (matchcmd("X-Wap-Profile:", buffer, &field) > 0) {

        matchcmd_free(field);
        return TRUE;
    }

    // Extract User-Agent String
    if (matchcmd("User-Agent: (.+)", buffer, &field) > 0) {
        logmsg(LOG_DEBUG, "Found User-Agent: %s", field[1]);

        char *header = strdup(field[1]);
        matchcmd_free(field);

        if (matchcmd("(mobile|Nokia|HTC|Android|SonyEricsson|LG|Samsung|blac|moto|doco|java|symb)", header, &field) > 0) {
            matchcmd_free(field);
            return TRUE;
        }

    }
    
    return FALSE;
}


/**
 * This test function is called when the server receives a new conection and
 * determines if the first command is a GET string. This is then an indication
 * that this is a WEB-browser calling us. The command given is stored in the
 * buffer posinted to by the second argument.
 * @param buffer
 * @param cmd
 * @param maxlen
 * @return 1 if this was a WEB-connection , 0 otherwise
 */
int
webconnection(const char *buffer, char *cmd, int maxlen) {

    static char *allowed_commands[] = {
        "^GET /favicon.ico" _PR_ANY _PR_E,
        "^GET /addrec\\?" _PR_ANY _PR_E,
        "^GET /addqrec\\?" _PR_ANY _PR_E,
        "^GET /delrec\\?" _PR_ANY _PR_E,
        "^GET /login\\?" _PR_ANY _PR_E,
        "^GET /killrec\\?" _PR_ANY _PR_E,
        "^GET /logout" _PR_ANY _PR_E
    };

    *cmd = '\0';
    if (0 == strncmp(buffer, "GET", 3)) {

        // Now extract the command string
        char **field = (void *) NULL;
        int ret=0, found=0;

        if ((ret = matchcmd("^GET /cmd\\?" _PR_ANPS "HTTP" _PR_ANY _PR_E, buffer, &field)) > 1) {

            // Found a command string so store it in the buffer
            char *tmpbuff = url_decode(field[1]);
            strncpy(cmd, tmpbuff, maxlen);
            free(tmpbuff);
            if (*cmd != 'h')
                strcat(cmd, " ");
            cmd[maxlen - 1] = '\0';

            found = 1;

        } else if ((ret = matchcmd("^GET / HTTP" _PR_ANY _PR_E, buffer, &field)) > 1) {

            // When only the root directory is called we default the command
            // to a "time" command.
            strncpy(cmd, "t", maxlen);
            found = 1;

        } else if ((ret = matchcmd("^GET /" _PR_ANP " HTTP" _PR_ANY _PR_E, buffer, &field)) > 1) {
            
            found = 1;

        } else {
            int i=0;
            int nmax = sizeof(allowed_commands)/sizeof(char *);
            while( i < nmax && ret <= 1 ) {
                ret = matchcmd(allowed_commands[i], buffer, &field);
                ++i;
            }
            found = ret > 1;
        }

        matchcmd_free(field);

        return found;

    }

    return 0;
}

/**
 * Upong receiveng the request to send back the CSS file this function reads
 * the correct CSS file and writes it back to the client using the supplied socket.
 * TODO : Add handling of "304 Not Modified" to save a bit of bandwidth
 * @param sockd
 * @param name
 */
void
sendback_css_file(int sockd, char *name) {
    char *tmpbuff = calloc(1,16000);
    read_cssfile(tmpbuff,16000,strcmp(name,"tvpvrd_mobile")==0);

    // Initialize a new page
    char server_id[255];
    snprintf(server_id, 254, "tvpvrd %s", server_version);
    // Send back a proper HTTP header

    time_t t = time(NULL);
    struct tm t_tm;
    char ftime[128];
    
    (void) gmtime_r(&t, &t_tm);
    strftime(ftime, 128, TIME_RFC822_FORMAT, &t_tm);

    // Todo: Add handling of "304 Not Modified"
    // When receiving the header "If-Modified-Since" from the client

    _writef(sockd,
                "HTTP/1.1 200 OK\r\n"
                "Date: %s\r\n"
                "Last-Modified: %s\r\n"
                "Server: %s\r\n"
                "Connection: close\r\n"
                "Content-Type: text/css\r\n\r\n", ftime, ftime, server_id);

    _writef(sockd,tmpbuff);

    logmsg(LOG_DEBUG,"Sent back CSS sheet %s",name);

    free(tmpbuff);

}

/**
 * This is the main routine that gets called from the connection handler
 * when a new browser connection has been detected and the command from
 * the browser have been received. Tis function is totally responsible to
 * execute the command and prepare the WEB-page to be sent back.
 *
 * @param my_socket
 * @param inbuffer
 */
void
html_cmdinterp(const int my_socket, char *inbuffer) {
    char wcmd[1024];
    char *buffer = url_decode(inbuffer);
    char **field = (void *) NULL;
    int ret=0;

    if (webconnection(buffer, wcmd, 1023)) {

        // Reset cmd_delay
        cmd_delay = 0;

        // Try to determiine if the connection originated from a
        // mobile phone.
        int mobile = is_mobile_connection(buffer);

        logmsg(LOG_DEBUG,"WEB connection after URL decoding:\n%s\n",buffer);

         if ((ret = matchcmd("GET /logout HTTP/1.1", buffer, &field)) == 1) {

             matchcmd_free(field);
             free(buffer);
             html_login_page(my_socket,mobile);
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
                " HTTP/1.1",
                buffer, &field)) > 1) {

            const int maxvlen = 256;
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
                    snprintf(wcmd, 1024, "a %s", channel);
                } else {
                    // Repeatet add
                    snprintf(wcmd, 1024, "ar %s %s %s ", repeat, repeatcount, channel);
                }
                if (*sd != '\0') {
                    snprintf(tmpcmd, 128, " %s ", sd);
                    strncat(wcmd, tmpcmd, 1023);
                }
                snprintf(tmpcmd, 128, " %s:%s ", sh, smin);
                strncat(wcmd, tmpcmd, 1023);

                snprintf(tmpcmd, 128, " %s:%s ", eh, emin);
                strncat(wcmd, tmpcmd, 1023);

                snprintf(tmpcmd, 128, " %s @%s ", title, profile);
                strncat(wcmd, tmpcmd, 1023);

            }

        } else if ((ret = matchcmd("GET /addqrec\\?"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_ANPSO "&"
                _PR_AN "=" _PR_AN
                " HTTP/1.1",
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
                snprintf(wcmd, 1024, "q %s", channel);
                snprintf(tmpcmd, 128, " %s:%s ", length_hour, length_min);
                strncat(wcmd, tmpcmd, 1023);
                snprintf(tmpcmd, 128, " %s @%s ", title, profile);
                strncat(wcmd, tmpcmd, 1023);
                cmd_delay = 2400000;
            }

        } else if ((ret = matchcmd("GET /killrec\\?"
                _PR_AN "=" _PR_AN 
                " HTTP/1.1",
                buffer, &field)) > 1) {

            const int maxvlen = 256;
            char recid[maxvlen], submit[maxvlen];
            get_assoc_value(recid, maxvlen, "rid", &field[1], ret - 1);
            get_assoc_value(submit, maxvlen, "submit_killrec", &field[1], ret - 1);
            snprintf(wcmd, 1024, "! %s", recid);

            // Wait half a second to allow the removal to be done and completed so that
            // it will show when the WEB-page is refreshed.
            cmd_delay = 500000;

        } else if ((ret = matchcmd("^GET /delrec\\?"
                _PR_AN "=" _PR_ANO "&"
                _PR_AN "=" _PR_ANO "&"
                _PR_AN "=" _PR_ANO
                " HTTP/1.1",
                buffer, &field)) > 1) {

            const int maxvlen = 256;
            char recid[maxvlen], submit[maxvlen], delserie[maxvlen];
            get_assoc_value(recid, maxvlen, "recid", &field[1], ret - 1);
            get_assoc_value(delserie, maxvlen, "delserie", &field[1], ret - 1);
            get_assoc_value(submit, maxvlen, "submit_delrec", &field[1], ret - 1);

            if (0 == strcmp(submit, "Delete")) {
                if (0 == strcmp(delserie, "Yes")) {
                    snprintf(wcmd, 1024, "dr %s", recid);
                } else {
                    snprintf(wcmd, 1024, "d %s", recid);
                }
            }

        } else if ( (ret = matchcmd("^GET /" _PR_ANP ".css HTTP/1.1", buffer, &field)) > 1) {
                       
            // Check if this is a call for one of the CSS files that we recognize
            if( strcmp(field[1],"tvpvrd")==0 ||  strcmp(field[1],"tvpvrd_mobile")==0 ) {

                sendback_css_file(my_socket,field[1]);

                matchcmd_free(field);
                free(buffer);
                
                return;

            }

        }

        if( ret > 0 ) {
            matchcmd_free(field);
        }

        if ((ret = matchcmd("^GET /favicon.ico" _PR_ANY _PR_E, buffer, &field)) < 1) {
            // If it's not a favicon.ico GET command we proceed

            logmsg(LOG_DEBUG, "==== Translated to: %s", wcmd);
            static char logincookie[128];

            // If user does not have a valid login then send back the login page
            if (!user_loggedin(buffer, logincookie, 127)) {

                // Check if user just tried to login
                if ((ret = matchcmd("^GET /login\\?"
                        _PR_AN "=" _PR_ANPO "&"
                        _PR_AN "=" _PR_ANPO "&"
                        _PR_AN "=" _PR_ANPO
                        " HTTP/1.1",
                        buffer, &field)) > 1) {

                    const int maxvlen = 64;
                    char user[maxvlen], pwd[maxvlen], logsubmit[maxvlen];
                    get_assoc_value(user, maxvlen, "user", &field[1], ret - 1);
                    get_assoc_value(pwd, maxvlen, "pwd", &field[1], ret - 1);
                    get_assoc_value(logsubmit, maxvlen, "submit_login", &field[1], ret - 1);

                    matchcmd_free(field);
                    
                    if (0 == strcmp(logsubmit, "Login")) {

                        if (!validate_login(user, pwd)) {

                            // Validation of login details failed, send back the login screen
                            html_login_page(my_socket, mobile);

                        } else {

                            // Login successfull. Show the main page and used the "version" command
                            // as the default command to show in the output area.
                            html_main_page(my_socket, "v", create_login_cookie(user, pwd), mobile);
                        }

                    } else {

                        // Unrecognized login command so go back to login page
                        html_login_page(my_socket, mobile);
                    }
                } else {

                    // Unrecognized login command so go back to login page
                    html_login_page(my_socket, mobile);
                }
            } else {

                // User has a valid login so send back the main page
                html_main_page(my_socket, wcmd, logincookie, mobile);
            }
        } else {
            // Ignore GET favicon.ico
            matchcmd_free(field);
            html_notfound(my_socket);
        }
    } else {
        html_notfound(my_socket);
        logmsg(LOG_ERR, "** Unrecognized WEB-command: %s", buffer);
    }

    free(buffer);
}


#define CSSFILE_NAME "tvpvrd"
/**
 * Read a suitable CSS file depending on the client. An identified mobile browser
 * will have a different CSS file compared with a stationary client.
 * @param buff Buffer where the CSS file is stored
 * @param maxlen MAximum length of buffer
 * @param mobile Flag to indicate a mobile browser
 * @return 0 on success, -1 on failure
 */
int
read_cssfile(char *buff, int maxlen, int mobile) {
    char cssfile[255];
    if (mobile) {
        snprintf(cssfile, 255, "%s/tvpvrd/%s_mobile.css", CONFDIR, CSSFILE_NAME);
    } else {
        snprintf(cssfile, 255, "%s/tvpvrd/%s.css", CONFDIR, CSSFILE_NAME);
    }
    cssfile[254] = '\0';
    char linebuff[1024];

    *buff = '\0';
    FILE *fp = fopen(cssfile, "r");
    if (fp == NULL) {
        logmsg(LOG_ERR, "Cannot read CSS file '&s'", cssfile);
        return -1;
    }

    while (maxlen > 0 && fgets(linebuff, 1023, fp)) {
        linebuff[1023] = '\0';
        strncat(buff, linebuff, maxlen);
        maxlen -= strlen(linebuff);
    }

    fclose(fp);
    return 0;
}

/**
 * Display the top banner in the WEB interface
 * @param sockd
 */
void
html_topbanner(int sockd) {
    _writef(sockd, "<div class=\"top_banner\">");
    _writef(sockd,
            "%s %s [%s] (%s)"
#ifdef _LARGEFILE64_SOURCE
            "\nCompiled with Large File Support (files > 2GB)."
#endif
#ifdef DEBUG_SIMULATE
            "\n *** DEBUG BUILD *** WILL NOT RECORD REAL VIDEO STREAMS. THIS iS ONLY A DEBUG BUILD.\n"
#endif

            "\n", server_program_name, server_version,
            is_master_server ? "master" : "client", server_build_date);
    _writef(sockd, "</div> <!-- top_banner -->\n");
}

/**
 * Display the result of a command in the command output area
 * @param sockd
 * @param wcmd
 */
void
html_cmd_output(int sockd, char *wcmd) {

    _writef(sockd, "<div class=\"cmd_output\"><pre>");

    // We must wait for the semphore since since commands
    // might alter data structures and we can only have one
    // thread at a time accessing the data structures
    pthread_mutex_lock(&recs_mutex);

    // Make _writef() do HTML encoding on any output sent
    htmlencode_flag = 1;

    // The execution of the command happens in the command module.
    // Any output from the command are sent to the given socket
    // descriptor and passed back to the browser in this case.
    cmdinterp(wcmd, sockd);
    htmlencode_flag = 0;

    pthread_mutex_unlock(&recs_mutex);

    _writef(sockd, "</pre>\n</div> <!-- cmd_output -->\n");

}
/**
 * Send back the entities that mark the end of a page
 * @param sockd
 */
void
html_endpage(int sockd) {
    const char postamble[] =
            "</div> <!-- top_page -->"
            "</body>"
            "</html>";
    _writef(sockd, postamble);
}

/**
 * Send back the proper HTTP header with cookies and other HTTP protocol
 * specific fields
 * @param sockd
 * @param cookie_val
 */
void
http_header(int sockd, char *cookie_val) {
    // Initialize a new page
    char server_id[255];
    snprintf(server_id, 254, "tvpvrd %s", server_version);
    // Send back a proper HTTP header

    time_t t = time(NULL);
    time_t texp = t;
    if( strcmp(cookie_val,"logout") ) {
        // If not the special logout cookie then put a valid expire time
        texp += weblogin_timeout;
    } else {
        // For logout we set the expire in the past to force the browser
        // to remove the cookie
        logmsg(LOG_DEBUG,"******* SETTING cookie() in the past");
        texp -= 36000;
    }
    struct tm t_tm, t_tmexp;
    (void) gmtime_r(&t, &t_tm);
    (void) gmtime_r(&texp, &t_tmexp);
    char ftime[128], fexptime[128];

    strftime(ftime, 128, TIME_RFC822_FORMAT, &t_tm);
    strftime(fexptime, 128, TIME_RFC822_FORMAT, &t_tmexp);

    if (cookie_val && *cookie_val) {

        char *tmpbuff = url_encode(cookie_val);
        logmsg(LOG_DEBUG, "Stored cookie: %s as %s", cookie_val, tmpbuff);

        if ( weblogin_timeout > 0 || texp < t ) {
            _writef(sockd,
                    "HTTP/1.1 200 OK\r\n"
                    "Date: %s\r\n"
                    "Server: %s\r\n"
                    "Set-Cookie: tvpvrd=%s;Version=1; expires=%s\r\n"
                    "Connection: close\r\n"
                    "Content-Type: text/html\r\n\r\n", ftime, server_id, tmpbuff, fexptime);
        } else {
            _writef(sockd,
                    "HTTP/1.1 200 OK\r\n"
                    "Date: %s\r\n"
                    "Server: %s\r\n"
                    "Set-Cookie: tvpvrd=%s;Version=1;\r\n"
                    "Connection: close\r\n"
                    "Content-Type: text/html\r\n\r\n", ftime, server_id, tmpbuff);
        }

        free(tmpbuff);

    } else {

        _writef(sockd,
                "HTTP/1.1 200 OK\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n\r\n", ftime, server_id);
    }

}

/**
 * Initialize a new HTML page with the proper HTML headers which includes the
 * DOCTYPE and the link to the CSS file.
 * @param sockd
 * @param cookie_val
 * @param mobile
 */
void
html_newpage(int sockd, char *cookie_val, int mobile) {
    const char preamble[] =
            "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\">\n"
            "<html>"
            "<head>"
            "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n"
            "<title>"
            "%s"
            "</title>\n"
            "<link rel=\"stylesheet\" type=\"text/css\" href=\"%s.css\">"
            "</head>"
            "<body>\n"
            "<div class=\"top_page\">\n";
    char title[255];
    snprintf(title, 254, "tvpvrd %s", server_version);

    http_header(sockd, cookie_val);
    _writef(sockd, preamble, title, mobile ? "tvpvrd_mobile" : "tvpvrd" );

}

/**
 * Output a HTML <select> entitie where the key and the display value is the same
 * and populate it with the supplied list.
 * @param sockd
 * @param legend
 * @param name
 * @param selected
 * @param list
 * @param num
 * @param id
 */
void
html_element_select(int sockd, char *legend, char *name, char *selected, const char *list[], int num, char *id) {
    const int maxlen = 8192;
    char *buffer = calloc(1, maxlen);

    if (!buffer) {
        logmsg(LOG_ERR, "Out of memory in html_element_select() !");
        exit(EXIT_FAILURE);
    }

    if (id && *id) {
        snprintf(buffer, maxlen,
                "<div class=\"input_container\" id=\"%s\">"
                "<div class=\"input_legend\">%s</div>", id, legend);
    } else {
        snprintf(buffer, maxlen,
                "<div class=\"input_container\">"
                "<div class=\"input_legend\">%s</div>", legend);
    }
    _writef(sockd, buffer);

    if (id && *id) {
        snprintf(buffer, maxlen, "<select name=\"%s\" class=\"%s\" id=\"%s\">\n", name, "input_select", id);
    } else {
        snprintf(buffer, maxlen, "<select name=\"%s\" class=\"%s\">\n", name, "input_select");
    }

    _writef(sockd, buffer);
    for (int i = 0; i < num; ++i) {
        if (selected && 0 == strcmp(selected, list[i])) {
            snprintf(buffer, maxlen, "<option selected value=\"%s\">%s</option>\n", list[i], list[i]);
        } else {
            snprintf(buffer, maxlen, "<option value=\"%s\">%s</option>\n", list[i], list[i]);
        }
        _writef(sockd, buffer);
    }
    snprintf(buffer, maxlen, "</select></div>\n");
    _writef(sockd, buffer);
    free(buffer);
}

/**
 * Output a HTML <select> entitie where the key and the display value are different
 * and populate it with the supplied list.
 * @param sockd
 * @param legend
 * @param name
 * @param selected
 * @param list
 * @param num
 * @param id
 */
void
html_element_select_code(int sockd, char *legend, char *name, char *selected, const struct skeysval_t list[], int num, char *id) {
    const int maxlen = 8192;
    char *buffer = calloc(1, maxlen);

    if (!buffer) {
        logmsg(LOG_ERR, "Out of memory in html_element_select_code() !");
        exit(EXIT_FAILURE);
    }

    if (id && *id) {
        snprintf(buffer, maxlen,
                "<div class=\"input_container\" id=\"%s\">"
                "<div class=\"input_legend\">%s</div>", id, legend);
    } else {
        snprintf(buffer, maxlen,
                "<div class=\"input_container\">"
                "<div class=\"input_legend\">%s</div>", legend);
    }
    _writef(sockd, buffer);

    if (id && *id) {
        snprintf(buffer, maxlen, "<select name=\"%s\" class=\"%s\" id=\"%s\">\n", name, "input_select_code", id);
    } else {
        snprintf(buffer, maxlen, "<select name=\"%s\" class=\"%s\">\n", name, "input_select_code");
    }
    _writef(sockd, buffer);
    int i = 0;
    while (i < num) {
        if (selected && 0 == strcmp(selected, list[i].val)) {
            snprintf(buffer, maxlen, "<option selected value=\"%s\">%s</option>\n", list[i].key, list[i].val);
        } else {
            snprintf(buffer, maxlen, "<option value=\"%s\">%s</option>\n", list[i].key, list[i].val);
        }
        _writef(sockd, buffer);
        ++i;
    }
    snprintf(buffer, maxlen, "</select></div>\n");
    _writef(sockd, buffer);
    free(buffer);
}

/**
 * Create a text input HTML field
 * @param sockd
 * @param legend
 * @param name
 * @param id
 */

void
_html_element_input_text(int sockd, char *legend, char *name, char *id, int passwd) {
    const int maxlen = 8192;
    char *buffer = calloc(1, maxlen);

    if (!buffer) {
        logmsg(LOG_ERR, "Out of memory in html_element_input() !");
        exit(EXIT_FAILURE);
    }
    if (id && *id) {
        snprintf(buffer, maxlen,
                "<div class=\"input_container\" id=\"%s\">"
                "<div class=\"input_legend\">%s</div>", id, legend);
    } else {
        snprintf(buffer, maxlen,
                "<div class=\"input_container\">"
                "<div class=\"input_legend\">%s</div>", legend);
    }
    _writef(sockd, buffer);

    snprintf(buffer, maxlen, "<input type=\"%s\" name=\"%s\" class=\"input_text\"></input></div>\n", passwd ? "password" : "text" , name);
    _writef(sockd, buffer);
    free(buffer);
}

void
html_element_input_text(int sockd, char *legend, char *name, char *id) {
    _html_element_input_text(sockd, legend, name, id, 0);
}

void
html_element_input_password(int sockd, char *legend, char *name, char *id) {
    _html_element_input_text(sockd, legend, name, id, 1);
}

/**
 * Create a HTML submit button
 * @param sockd
 * @param name
 * @param value
 * @param id
 */
void
html_element_submit(int sockd, char *name, char *value, char *id) {

    const int maxlen = 8192;
    char *buffer = calloc(1, maxlen);

    if (!buffer) {
        logmsg(LOG_ERR, "Out of memory in html_element_submit() !");
        exit(EXIT_FAILURE);
    }
    snprintf(buffer, maxlen,
            "<div class=\"input_container\" id=\"%s\">"
            "<input type=\"submit\" name=\"%s\" value=\"%s\" class=\"input_submit\" id=\"%s\"></div>\n",
            id, name, value, id);
    _writef(sockd, buffer);

    free(buffer);
}

/**
 * Cretae a disabled HTML submit button
 * @param sockd
 * @param name
 * @param value
 * @param id
 */
void
html_element_submit_disabled(int sockd, char *name, char *value, char *id) {

    const int maxlen = 8192;
    char *buffer = calloc(1, maxlen);

    if (!buffer) {
        logmsg(LOG_ERR, "Out of memory in html_element_submit() !");
        exit(EXIT_FAILURE);
    }
    snprintf(buffer, maxlen,
            "<div class=\"input_container\" id=\"%s\">"
            "<input type=\"submit\" disabled name=\"%s\" value=\"%s\" class=\"input_submit\" id=\"%s\"></div>\n",
            id, name, value, id);
    _writef(sockd, buffer);

    free(buffer);
}

/**
 * Return a standard 404 error page (not found)
 * @param sockd
 */
void
html_notfound(int sockd) {
    _writef(sockd,
            "HTTP/1.1 404 Not Found\r\n"
            "Server: tvpvrd\r\n"
            "Connection: close\r\n"
            "Content-Type: text/html\r\n\r\n<html><body><h3>404 - Not found.</h3></body></html>\r\n");
}

/**
 * The full main page used when we are called from an ordinary browser
 * @param sockd
 * @param wcmd
 * @param cookie_val
 * @param mobile
 */
void
html_main_page(int sockd, char *wcmd, char *cookie_val, int mobile) {
    // Initialize a new page

    if (mobile) {
        html_main_page_mobile(sockd, wcmd, cookie_val);
        return;
    }

    html_newpage(sockd, cookie_val, FALSE);
    html_topbanner(sockd);

    // Left side : Command table
    _writef(sockd, "<div class=\"left_side\">");
    html_commandlist(sockd);

    _writef(sockd,"<div id=\"logout_container\"><div id=\"logout\"><a href=\"logout\">Logout</a></div></div>");

    _writef(sockd, "</div>"); // class="LEFT_side"

    // Right side : Output and recording management
    _writef(sockd, "<div class=\"right_side\">");
    html_cmd_output(sockd, wcmd);
    usleep(cmd_delay); // Give some time for the command to execute
    html_cmd_ongoing(sockd);
    html_cmd_ongoingtransc(sockd);
    html_cmd_next(sockd);
    html_cmd_qadd(sockd);
    html_cmd_add_del(sockd);

    _writef(sockd, "</div>");

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
html_main_page_mobile(int sockd, char *wcmd, char *cookie_val) {
    // Initialize a new page

    html_newpage(sockd, cookie_val, TRUE);
    html_topbanner(sockd);

    _writef(sockd, "<div class=\"single_side\">");
    html_commandlist_short(sockd);
    html_cmd_output(sockd, wcmd);
    html_cmd_qadd(sockd);
    html_cmd_add_del(sockd);
    _writef(sockd, "\n</div> <!-- single_side -->");

    html_endpage(sockd);

}

/**
 * Display the login page
 * @param sockd
 * @param mobile
 */
void
html_login_page(int sockd, int mobile) {
    // Initialize a new page

    // Give the special cookie value "logout" which will create a
    // header which replace the old cookie and sets it expire time
    // in the past so it is removed from the browser
    html_newpage(sockd, "logout", mobile);
    html_topbanner(sockd);

    _writef(sockd, "<div class=\"%s\">", "login_container");
    _writef(sockd, "<div class=\"%s\">Welcome to tvpvrd</div>", "login_title");
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"login\">\n", "tvlogin");
    html_element_input_text(sockd, "User:", "user", "id_loginuser");
    html_element_input_password(sockd, "Password:", "pwd", "id_loginpwd");
    html_element_submit(sockd, "submit_login", "Login", "id_submitlogin");

    _writef(sockd, "<form>");
    _writef(sockd, "</div>");

    html_endpage(sockd);
}


static const char *min_list[] = {
    "00", "05", "10", "14", "15", "20", "25", "29", "30",
    "35", "40", "44", "45", "50", "55", "59"
};
static const char *hour_list[] = {
    "17", "18", "19", "20", "21", "22", "23",
    "00", "01", "02", "03", "04", "05", "06",
    "07", "08", "09", "10", "11", "12","13", "14", "15", "16"
};
static const char *hourlength_list[] = {
    "0", "1", "2", "3"
};

/**
 * Display the next recording area
 * @param sockd
 */
void
html_cmd_next(int sockd) {
    _writef(sockd, "<fieldset><legend>Next recording</legend>\n");
    _writef(sockd, "<div class=\"next_rec_container\">\n");
    listrecs(1,4,sockd); // Use style==4 , fancy
    _writef(sockd, "</div>\n");
    _writef(sockd, "</fieldset>\n");
}

/**
 * Display the ongoing transaction area
 * @param sockd
 */
void
html_cmd_ongoingtransc(int sockd) {

    _writef(sockd, "<fieldset><legend>Ongoing transcodings</legend>\n");
    int num=get_num_ongoing_transcodings();

    if( num == 0 ) {
        _writef(sockd, "<div class=\"ongoing_transc_title_disabled\">None.</div>");
    } else {

        for (int i = 0; i < max_ongoing_transcoding; i++) {

            if (ongoing_transcodings[i]) {

                _writef(sockd, "<div class=\"ongoing_transc_entry\">\n");

                time_t now = time(NULL);
                int rtime = now-ongoing_transcodings[i]->start_ts;
                int rh = rtime/3600;
                int rmin = (rtime - rh*3600)/60;

                _writef(sockd, "<div class=\"ongoing_transc_title\">(%02d:%02d) %s</div>",rh,rmin,ongoing_transcodings[i]->filename);
                _writef(sockd, "<div class=\"ongoing_transc_stop\"><a href=\"cmd?kt%%20%d\">Stop</a></div>",i);

                _writef(sockd, "</div>\n");
            }
        }
    }

    _writef(sockd, "</fieldset>\n");
}

/**
 * Display the ongoing recordings area
 * @param sockd
 */
void
html_cmd_ongoing(int sockd) {

    _writef(sockd, "<fieldset><legend>Ongoing recordings</legend>\n");

    int num=0;
    for (int i = 0; i < max_video; i++) {
        num += ongoing_recs[i] ? 1 : 0 ;
    }
    if( num == 0 ) {
        _writef(sockd, "<div class=\"ongoing_transc_title_disabled\">None.</div>");
    } else {

        for (int i = 0; i < max_video; i++) {

            if (ongoing_recs[i]) {

                _writef(sockd, "<div class=\"ongoing_rec_entry\">\n");

                int ey, em, ed, eh, emi, es;
                fromtimestamp(ongoing_recs[i]->ts_end, &ey, &em, &ed, &eh, &emi, &es);
                int sy, sm, sd, sh, smi, ss;
                fromtimestamp(ongoing_recs[i]->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
                _writef(sockd, "<div class=\"ongoing_rec_title\">%s %02d:%02d-%02d:%02d, %s</div>",ongoing_recs[i]->channel,sh,smi,eh,emi,ongoing_recs[i]->title);
                _writef(sockd, "<div class=\"ongoing_rec_stop\"><a href=\"killrec?rid=%d\">Stop</a></div>",i);

                _writef(sockd, "</div>\n");
            }
        }
    }

    _writef(sockd, "</fieldset>\n");

}

/**
 * Display the quick add area
 * @param sockd
 */
void
html_cmd_qadd(int sockd) {
    const char *station_list[128];
    const int n_stations = get_stations(station_list, 128);

    const char *profile_list[64];
    const int n_profile = get_profile_names(profile_list, 64);

    const int n_hourlength = sizeof (hourlength_list) / sizeof (char *);
    const int n_min = sizeof (min_list) / sizeof (char *);

    _writef(sockd, "<div class=\"cmd_qadd_container\">");

    /*
     * Add quick recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"addqrec\">\n", "id_qadd_form");

    _writef(sockd, "<fieldset><legend>Quick recording</legend>");
    html_element_select(sockd, "Profile:", "profile", default_transcoding_profile, profile_list, n_profile, "id_qprofile");
    html_element_select(sockd, "Station:", "channel", NULL, station_list, n_stations, "id_qstation");

    html_element_select(sockd, "Length:", "length_hour", "00", hourlength_list, n_hourlength, "id_length_hour");
    html_element_select(sockd, "&nbsp;", "length_min", "45", min_list, n_min, "id_length_min");

    html_element_input_text(sockd, "Title:", "title", "id_qtitle");
    html_element_submit(sockd, "submit_qaddrec", "Start", "id_qaddrec");
    _writef(sockd, "</fieldset>\n");

    _writef(sockd, "</form>\n");

    _writef(sockd, "</div> <!-- qadd_container -->");

}

/**
 * Display the delete recording area
 * @param sockd
 */
void
html_cmd_add_del(int sockd) {
    static const char *day_list[] = {
        " ", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
    };
    const int n_day = 8;
    static const struct skeysval_t rpt_list[] = {
        {.key = "", .val = "(none)"},
        {.key = "w", .val = "Weekly"},
        {.key = "d", .val = "Daily"},
        {.key = "f", .val = "Mon-Fri"},
        {.key = "t", .val = "Mon-Thu"},
        {.key = "s", .val = "Sat-Sun"},
    };
    const int n_rpt = 6;
    static const char *rptcount_list[] = {
        " ", "02", "03", "04", "05", "06", "07", "08", "09", "10",
        "11", "12", "13", "14", "15", "16", "17", "18", "19",
        "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
        "30", "31", "33", "33", "34", "35", "36", "37", "38", "39",
    };
    const int n_rptcount = 39;
    static const char *yn_list[] = {
        "Yes", "No"
    };
    const int n_ynlist = 2;
    const char *station_list[128];
    const int n_stations = get_stations(station_list, 128);

    const char *profile_list[64];
    const int n_profile = get_profile_names(profile_list, 64);

    const int n_hour = sizeof (hour_list) / sizeof (char *);
    const int n_min = sizeof (min_list) / sizeof (char *);

    _writef(sockd, "<div class=\"cmd_add_del_container\">");

    /*
     * Add recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"addrec\">\n", "addrecording");

    _writef(sockd, "<fieldset><legend>New recording</legend>");
    html_element_select(sockd, "Profile:", "profile", default_transcoding_profile, profile_list, n_profile, "id_profile");
    html_element_select(sockd, "Station:", "channel", NULL, station_list, n_stations, "id_station");
    html_element_select_code(sockd, "Repeat:", "repeat", NULL, rpt_list, n_rpt,"id_rpttype");
    html_element_select(sockd, "Count:", "repeatcount", NULL, rptcount_list, n_rptcount, "id_rptcount");
    html_element_select(sockd, "Day:", "start_day", NULL, day_list, n_day, "id_start");
    html_element_select(sockd, "Start:", "start_hour", "18", hour_list, n_hour, "id_starthour");
    html_element_select(sockd, "&nbsp;", "start_min", NULL, min_list, n_min, NULL);
    _writef(sockd, "<div class=\"input_container\" id=\"be_hyphen\"><span class=\"be_hyphen\"> &rarr; </span></div>");
    html_element_select(sockd, "End:", "end_hour", "18", hour_list, n_hour, "id_endhour");
    html_element_select(sockd, "&nbsp;", "end_min", "59", min_list, n_min, NULL);

    html_element_input_text(sockd, "Title:", "title", "id_title");
    html_element_submit(sockd, "submit_addrec", "Add", "id_addrec");
    _writef(sockd, "</fieldset>\n");

    _writef(sockd, "</form>\n");


    /*
     * Delete recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"delrec\"  onsubmit=\"return confirm('Really delete?')\">\n", "deleterecording");

    _writef(sockd, "<fieldset>\n<legend>Delete recording</legend>\n");

    struct skeysval_t *listrec;
    int num = listrecskeyval(&listrec, 3);
    html_element_select_code(sockd, "Title:", "recid", NULL, listrec, num, "id_delselect");
    for (int i = 0; i < num; ++i) {
        free(listrec[i].key);
        free(listrec[i].val);
    }
    free(listrec);

    html_element_select(sockd, "Delete serie:", "delserie", "No", yn_list, n_ynlist, "id_seriesyn");
    html_element_submit(sockd, "submit_delrec", "Delete", "delrec");
    _writef(sockd, "</fieldset>\n");

    _writef(sockd, "</form>\n");

    // Close container
    _writef(sockd, "</div> <!-- add_del_container -->");
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
    int cmd_num;
    struct cmd_entry *entry;
};


// Each structure below defines one command group
static struct cmd_entry cmdfunc_master_recs[] = {
    {"lh", "List"}
};

static struct cmd_entry cmdfunc_master_transcoding[] = {
    {"ot", "Ongoing"},
    {"wt", "Queue"},
    {"st", "Statistics"},
    {"lp", "Profiles"}
};

static struct cmd_entry cmdfunc_master_status[] = {
    {"s", "Status"},
    {"t", "Time"}
};

static struct cmd_entry cmdfunc_master_view[] = {
    {"xx", "DB file"},
    {"z", "Settings"},
    {"ls", "Station list"},
    {"log%20100", "Recent log"}
};

static struct cmd_entry cmdfunc_master_driver[] = {
    {"vc", "Driver"},
    {"lc%200", "Settings #0"}
};

static struct cmd_entry cmdfunc_slave_transcoding[] = {
    {"ot", "Ongoing transcoding"},
    {"wt", "Waiting transcodings"},
    {"st", "Statistics"},
    {"lp", "Profiles"}
};

static struct cmd_entry cmdfunc_slave_status[] = {
    {"s", "Status"},
    {"t", "Time"}
};

static struct cmd_entry cmdfunc_slave_view[] = {
    {"z", "Settings"},
    {"log%2050", "Recent log"}
};

static struct cmd_entry cmdfunc_master_menu_short[] = {
    {"s", "Status"},
    {"lh", "Recs"},
    {"n", "Next"},
    {"o", "Ongoing"}
};

static struct cmd_grp cmd_grp_master[] = {
    {"Server", "Server information", sizeof (cmdfunc_master_status) / sizeof (struct cmd_entry), cmdfunc_master_status},
    {"Recordings", "Stored recordings", sizeof (cmdfunc_master_recs) / sizeof (struct cmd_entry), cmdfunc_master_recs},
    {"Transcoding", "Transcoding info", sizeof (cmdfunc_master_transcoding) / sizeof (struct cmd_entry), cmdfunc_master_transcoding},
    {"View", "View", sizeof (cmdfunc_master_view) / sizeof (struct cmd_entry), cmdfunc_master_view},
    {"Capture card", "Card information", sizeof (cmdfunc_master_driver) / sizeof (struct cmd_entry), cmdfunc_master_driver}
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
html_commandlist(int sockd) {

    static struct cmd_grp *cmdgrp;
    int cmdgrplen;

    if (is_master_server) {
        cmdgrp = cmd_grp_master;
        cmdgrplen = sizeof (cmd_grp_master) / sizeof (struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave;
        cmdgrplen = sizeof (cmd_grp_slave) / sizeof (struct cmd_grp);
    }

    _writef(sockd, "<div class=\"cmd_menu\">");
    for (int i = 0; i < cmdgrplen; ++i) {

        _writef(sockd, "<div class=\"cmdgrp_title_row\"><span class=\"cmdgrp_title\">%s</span></div>",
                cmdgrp[i].grp_name, cmdgrp[i].grp_desc);

        _writef(sockd, "<div class=\"cmdgrp_commands\">");
        for (int j = 0; j < cmdgrp[i].cmd_num; ++j) {
            _writef(sockd, "<a href=\"cmd?%s\">&#8718; %s</a><br>\n", cmdgrp[i].entry[j].cmd_name, cmdgrp[i].entry[j].cmd_desc);
        }
        _writef(sockd, "</div>");

    }
    _writef(sockd, "</div>");

}

/**
 * Display the short version of the command list. Used for mobile browsers.
 * @param sockd
 */
void
html_commandlist_short(int sockd) {

    static struct cmd_grp *cmdgrp;
    int cmdgrplen;

    if (is_master_server) {
        cmdgrp = cmd_grp_master_menu_short;
        cmdgrplen = sizeof (cmd_grp_master_menu_short) / sizeof (struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave_short;
        cmdgrplen = sizeof (cmd_grp_slave_short) / sizeof (struct cmd_grp);
    }

    _writef(sockd, "<div class=\"cmd_menu_short\">\n");
    for (int i = 0; i < cmdgrplen; ++i) {

        for (int j = 0; j < cmdgrp[i].cmd_num; ++j) {
            _writef(sockd, "<div class=\"cmdgrp_commands_short\">");
            _writef(sockd, "<a href=\"cmd?%s\">&#8718; %s</a>", cmdgrp[i].entry[j].cmd_name, cmdgrp[i].entry[j].cmd_desc);
            _writef(sockd, "</div>\n");
        }
    }
    _writef(sockd, "</div> <!-- cmd_menu_short -->\n");

}
