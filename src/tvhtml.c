/* =========================================================================
 * File:        TVHTML.C
 * Description: HTML elements to create WEB-interface
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010 Johan Persson
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
#include <syslog.h>
#include <errno.h>
#include <pthread.h>

#include "config.h"
#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "recs.h"
#include "tvcmd.h"
#include "tvhtml.h"
#include "tvwebcmd.h"
#include "build.h"


/**
 * Display the top banner in the WEB interface
 * @param sockd
 */
void
html_topbanner(int sockd) {
    _writef(sockd, "<div class=\"top_banner\">");
    _writef(sockd,
            "%s %s [%s] (build: %lu.%lu )"
#ifdef DEBUG_SIMULATE
            "\n *** DEBUG BUILD *** WILL NOT RECORD REAL VIDEO STREAMS. THIS iS ONLY A DEBUG BUILD.\n"
#endif

            "\n", server_program_name, server_version,
            is_master_server ? "master" : "client",
            (unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER);
    _writef(sockd, "</div> <!-- top_banner -->\n");
}

/**
 * Display the result of a command in the command output area
 * @param sockd
 * @param wcmd
 */
void
web_cmd_output(int sockd, char *wcmd) {

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
            "</html>\r\n";
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
#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"SETTING cookie() in the past");
#endif
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

        // We need to do an extra escape of potential '%' sign since
        // this will otherwise be interpetated as printf() formats in the
        // logmsg printings
        char *tmpbuff2 = esc_percentsign(tmpbuff);

        logmsg(LOG_DEBUG, "Stored cookie: '%s' as URL encoded '%s'", cookie_val, tmpbuff2);

        free(tmpbuff2);

        char header[2048];
        if ( weblogin_timeout > 0 || texp < t ) {
            snprintf(header,2047,
                    "HTTP/1.1 200 OK\r\n"
                    "Date: %s\r\n"
                    "Server: %s\r\n"
                    "Set-Cookie: tvpvrd=%s; path=/; expires=%s\r\n"
                    "Connection: close\r\n"
                    "Content-Type: text/html\r\n\r\n", ftime, server_id, tmpbuff, fexptime);
        } else {
            snprintf(header,2047,
                    "HTTP/1.1 200 OK\r\n"
                    "Date: %s\r\n"
                    "Server: %s\r\n"
                    "Set-Cookie: tvpvrd=%s; path=/\r\n"
                    "Connection: close\r\n"
                    "Content-Type: text/html\r\n\r\n", ftime, server_id, tmpbuff);
        }
        tmpbuff2 = esc_percentsign(header);
#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"Sending back header: %s",tmpbuff2);
#endif
        _writef(sockd,tmpbuff2);
        free(tmpbuff2);
        free(tmpbuff);


    } else {
        char header[2048];
        snprintf(header,2048,
                "HTTP/1.1 200 OK\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n\r\n", ftime, server_id);
        _writef(sockd,header);
#ifdef EXTRA_WEB_DEBUG
        if( cookie_val == (char *)NULL ) {
            logmsg(LOG_DEBUG,"Sending back header with NO COOKIE since cookie_val==NULL. %s",header);
        } else {
            logmsg(LOG_DEBUG,"Sending back header with NO COOKIE since *cookie_val==\"\". %s",header);
        }
#endif

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
 * Return a standard 304 Not Modified Page
 * @param sockd
 */
void
html_notmodified(int sockd) {
    logmsg(LOG_DEBUG,"Sent back not modified header");
    _writef(sockd,
            "HTTP/1.1 304 Not Modified\r\n"
            "Server: tvpvrd\r\n"
            "Connection: close\r\n"
            "Content-Type: text/html\r\n\r\n");
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
    web_commandlist(sockd);

    _writef(sockd,"<div id=\"logout_container\"><div id=\"logout\"><a href=\"logout\">Logout</a></div></div>");

    _writef(sockd, "</div>"); // class="LEFT_side"

    // Right side : Output and recording management
    _writef(sockd, "<div class=\"right_side\">");
    web_cmd_output(sockd, wcmd);
    usleep(cmd_delay); // Give some time for the command to execute
    web_cmd_next(sockd);
    web_cmd_ongoing(sockd);
    web_cmd_ongoingtransc(sockd);
    web_cmd_qadd(sockd);
    web_cmd_add_del(sockd);

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
    web_commandlist_short(sockd);
    web_cmd_output(sockd, wcmd);
    web_cmd_qadd(sockd);
    web_cmd_add_del(sockd);
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

    logmsg(LOG_DEBUG,"Sending back login page");
    
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
