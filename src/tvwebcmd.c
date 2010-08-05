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

void
html_element_input_text(int sockd, char *legend, char *name, char *id);

void
html_element_submit(int sockd,char *name, char *value, char *id);

void
html_main_page(int sockd,char *wcmd,char *cookie);

void
html_login_page(int sockd);

void
html_notfound(int sockd);


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
    *cmd = '\0';
    if( 0 == strncmp(buffer,"GET",3) ) {

        // Now extract the command string
        char **field = (void *)NULL;
        int ret ;

        if( (ret=matchcmd("^GET /cmd\\?" _PR_ANPS _PR_S "HTTP" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            // Found a command string so store it in the buffer
            char *tmpbuff = url_decode(field[1]);
            strncpy(cmd,tmpbuff,maxlen);
            free(tmpbuff);
            if( *cmd != 'h')
                strcat(cmd," ");
            cmd[maxlen-1] = '\0';

            return 1;

        } else if ( (ret = matchcmd("^GET /(cmd)? HTTP" _PR_ANY _PR_E, buffer, &field)) > 1 ) {
               
            strncpy(cmd,"v",maxlen);
            return 1;

        } else if( (ret = matchcmd("^GET /addrec\\?" _PR_ANY _PR_E, buffer, &field)) > 1 ) {
                       
            return 1;

        } else if( (ret = matchcmd("^GET /delrec\\?" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            return 1;

        } else if( (ret = matchcmd("^GET /login\\?" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            return 1;

        } else if( (ret = matchcmd("^GET /favicon.ico" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            return 1;

        } else if ( (ret = matchcmd("^GET /" _PR_ANPS "HTTP" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            // Unrecoqnized command
            strncpy(cmd,"xxx",maxlen);
            return 0;

        }

        return 0;

    }

    return 0;
}


/*
 * Parse the full HTTP header sent back from the browser to detect
 * if the corect cookie is set that indicates that the user has logged
 * in in this session
 */
#define LOGIN_COOKIE "d_ye8aj82hApsj02njfuyysad"

int
validate_login(char *user, char *pwd) {

    if( 0 == strcmp(user,web_user) && 0 == strcmp(pwd,web_password) )
        return 1;
    else
        return 0;
}

static char *
create_login_cookie(char *user,char *pwd) {

    static char _cookie_buff[128] ;
    strcpy(_cookie_buff,LOGIN_COOKIE);

    char hostname[128];
    gethostname(hostname,127);
    hostname[127] = '\0';

    char buff[128];
    strncpy(buff,user,127);
    buff[127]='\0';
    strncat(buff,pwd,64);
    buff[127]='\0';
    strncat(buff,hostname,64);
    buff[127]='\0';

    int n=MIN(strlen(_cookie_buff),strlen(buff));


    for(int i=0; i < n; ++i) {

        _cookie_buff[i] += buff[i] ;
        _cookie_buff[i] &= 127;
        if( (int)_cookie_buff[i] < 32 )
            _cookie_buff[i] += 32;

        // Remove the URL special chars, '+' 
        if( _cookie_buff[i] == '+' )
            _cookie_buff[i] = '_';

    }

    _cookie_buff[n] = '\0';

    // logmsg(LOG_DEBUG,"Created cookie: '%s' from %s",_cookie_buff, buff);

    return _cookie_buff;
}


int
validate_cookie(char *cookie) {

    return ! strcmp(create_login_cookie(web_user,web_password),cookie);

}

int
user_loggedin(char *buffer,char *cookie,int maxlen) {
    char **field = (void *)NULL;
    int ret ;

    *cookie = '\0';

    if( ! require_web_password )
        return 1;
    
    if( (ret=matchcmd( _PR_ANY "Cookie: tvpvrd=" _PR_ANP , buffer, &field)) > 1 ) {

        char *tmpbuff = url_decode(field[2]);

        logmsg(LOG_DEBUG,"Received cookie: %s decoded as: %s",field[2],tmpbuff);

        if( validate_cookie(tmpbuff) ) {
            strncpy(cookie,tmpbuff,maxlen);
            cookie[maxlen-1] = '\0';
            free(tmpbuff);
            return 1;
        }
        else {
            free(tmpbuff);
            return 0;
        }

    } else {
        return 0;
    }    
}

/*
 * This is the main routine that gets called from the connection handler
 * when a new browser connection has been detected and the command from
 * thebrowser have been received. Tis function is totally responsible to
 * execute the command and prepare the WEB-page to be sent back.
 */

void
html_cmdinterp(const int my_socket, char *inbuffer) {
    char wcmd[1024];
    char *buffer = url_decode(inbuffer);

    if( webconnection(buffer,wcmd,1023) ) {

        char **field = (void *)NULL;
        int ret ;

        // logmsg(LOG_DEBUG,"*** Checking buffer: %s",buffer);
        // First check if we should handle an add/delete command

        if( (ret = matchcmd("^GET /addrec\\?"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANSO "&"
                            _PR_AN "=" _PR_ANO 
                            " HTTP/1.1",
                            buffer, &field)) > 1 ) {

            const int maxvlen=256;
            char channel[maxvlen], repeat[maxvlen], repeatcount[maxvlen];
            char sd[maxvlen],sh[maxvlen],smin[maxvlen],eh[maxvlen],emin[maxvlen];
            char profile[maxvlen],title[maxvlen],submit[maxvlen];

            get_assoc_value(repeat,maxvlen,"repeat",&field[1],ret-1);
            get_assoc_value(repeatcount,maxvlen,"repeatcount",&field[1],ret-1);
            get_assoc_value(channel,maxvlen,"channel",&field[1],ret-1);
            get_assoc_value(sd,maxvlen,"start_day",&field[1],ret-1);
            get_assoc_value(sh,maxvlen,"start_hour",&field[1],ret-1);
            get_assoc_value(smin,maxvlen,"start_min",&field[1],ret-1);
            get_assoc_value(eh,maxvlen,"end_hour",&field[1],ret-1);
            get_assoc_value(emin,maxvlen,"end_min",&field[1],ret-1);
            get_assoc_value(profile,maxvlen,"profile",&field[1],ret-1);
            get_assoc_value(title,maxvlen,"title",&field[1],ret-1);
            get_assoc_value(submit,maxvlen,"submit_addrec",&field[1],ret-1);

            if( 0==strcmp(submit,"Add") ) {
                char tmpcmd[128];
                // Build command

                if( 0 == strcmp(repeat,"") ) {
                    snprintf(wcmd,1024,"a %s",channel);
                } else {
                    // Repeatet add
                    snprintf(wcmd,1024,"ar %s %s %s ",repeat,repeatcount,channel);
                }
                if( *sd != '\0' ) {
                    snprintf(tmpcmd,128," %s ",sd);
                    strncat(wcmd,tmpcmd,1023);
                }
                snprintf(tmpcmd,128, " %s:%s ", sh,smin);
                strncat(wcmd,tmpcmd,1023);

                if( 0 != strcmp(eh,"00") || 0 != strcmp(emin,"00") ) {
                    snprintf(tmpcmd,128, " %s:%s ", eh,emin);
                    strncat(wcmd,tmpcmd,1023);
                }

                snprintf(tmpcmd,128, " %s @%s ",title,profile);
                strncat(wcmd,tmpcmd,1023);

                logmsg(LOG_DEBUG,"Add cmd=%s",wcmd);

            }

        } else if( (ret = matchcmd("^GET /delrec\\?"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO
                            " HTTP/1.1",
                            buffer, &field)) > 1 ) {

            const int maxvlen=256;
            char recid[maxvlen],submit[maxvlen],delserie[maxvlen];
            get_assoc_value(recid,maxvlen,"recid",&field[1],ret-1);
            get_assoc_value(delserie,maxvlen,"delserie",&field[1],ret-1);
            get_assoc_value(submit,maxvlen,"submit_delrec",&field[1],ret-1);

            if (0 == strcmp(submit, "Delete")) {
                if (0 == strcmp(delserie, "Yes")) {
                    snprintf(wcmd, 1024, "dr %s", recid);
                } else {
                    snprintf(wcmd, 1024, "d %s", recid);
                }
            }
            
        } 
        
        
        if( (ret = matchcmd("^GET /favicon.ico" _PR_ANY _PR_E, buffer, &field)) < 1 ) {
            // If it's not a favicon.ico GET command we proceed

            logmsg(LOG_DEBUG,"**** Browser sent: %s",buffer);
            logmsg(LOG_DEBUG,"**** Translated to: %s",wcmd);
            static char logincookie[128];
            if( ! user_loggedin(buffer,logincookie,127) ) {

                // Check if user just tried to login
                if( (ret = matchcmd("^GET /login\\?"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO
                            " HTTP/1.1",
                            buffer, &field)) > 1 ) {

                    const int maxvlen=64;
                    char user[maxvlen],pwd[maxvlen],logsubmit[maxvlen];
                    get_assoc_value(user,maxvlen,"user",&field[1],ret-1);
                    get_assoc_value(pwd,maxvlen,"pwd",&field[1],ret-1);
                    get_assoc_value(logsubmit,maxvlen,"submit_login",&field[1],ret-1);
                    
                    if( 0==strcmp(logsubmit,"Login") ) {

                        if( ! validate_login(user,pwd) ) {

                            html_login_page(my_socket);

                        } else {

                            html_main_page(my_socket,"v",create_login_cookie(user,pwd));

                        }

                    } else {

                        html_login_page(my_socket);
                    }

                } else {

                    html_login_page(my_socket);

                }
                
            }
            else {

                html_main_page(my_socket,wcmd,logincookie);

            }

        } else {

            // Ignore favicon.ico GET
            logmsg(LOG_DEBUG, "Ignoring WEB GET favicon.ico");

        }
    } else {
        html_notfound(my_socket);
        logmsg(LOG_ERR, "** Unrecognized WEB-command: %s",buffer);
    }

    free(buffer);
}


#define CSSFILE_NAME "tvpvrd.css"

int
read_cssfile(char *buff, int maxlen) {
    char cssfile[255];
    snprintf(cssfile,255,"%s/tvpvrd/%s",CONFDIR,CSSFILE_NAME);
    cssfile[254] = '\0';
    char linebuff[1024];

    *buff = '\0';
    FILE *fp = fopen(cssfile,"r");
    if( fp == NULL ) {
        logmsg(LOG_ERR,"Cannot read CSS file '&s'",cssfile);
        return -1;
    }

    while( maxlen >  0 && fgets(linebuff,1023,fp) ) {
        linebuff[1023] = '\0';
        strncat(buff,linebuff,maxlen);
        maxlen -= strlen(linebuff);
    }

    fclose(fp);
    return 0;
}

void
html_topbanner(int sockd) {
    _writef(sockd,"<div class=\"top_banner\">");
    _writef(sockd,
            "%s %s [%s] (%s)"
#ifdef _LARGEFILE64_SOURCE
                        "\nCompiled with Large File Support (files > 2GB)."
#endif
#ifdef DEBUG_SIMULATE
                        "\n *** DEBUG BUILD *** WILL NOT RECORD REAL VIDEO STREAMS. THIS iS ONLY A DEBUG BUILD.\n"
#endif

            "\n",server_program_name, server_version,
            is_master_server ? "master" : "client", server_build_date);
    _writef(sockd,"</div>");
}

void
html_output(int sockd) {
    _writef(sockd,"<div class=\"cmd_output\"><pre>");
}

void
html_output_end(int sockd) {
    _writef(sockd,"</pre></div>");
}

void
html_endpage(int sockd) {
    const char postamble[] =
    "</div>"
    "</body>"
    "</html>";
    _writef(sockd,postamble);
}

#define TIME_RFC822_FORMAT "%a, %d %b %Y %T %z"
void
http_header(int sockd, char *cookie_val) {
    // Initialize a new page
    char server_id[255];
    snprintf(server_id, 254, "tvpvrd %s", server_version);
    // Send back a proper HTTP header

    time_t t = time(NULL);
    struct tm *t_tm;
    t_tm = localtime(&t);
    char ftime[128];

    if (t_tm == NULL) {
        logmsg(LOG_ERR, "Internal PANIC 'localtime' ( %d : %s )", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    strftime(ftime, 128, TIME_RFC822_FORMAT, t_tm);

    if (cookie_val && *cookie_val) {

        char *tmpbuff = url_encode(cookie_val);
        // logmsg(LOG_DEBUG, "Stored cookie: %s as %s", cookie_val, tmpbuff);
        _writef(sockd,
                "HTTP/1.1 200 OK\r\n"
                "Date: %s\r\n"
                "Server: %s\r\n"
                "Set-Cookie: tvpvrd=%s;Version=1;\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n\r\n", ftime, server_id, tmpbuff);
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

void
html_newpage(int sockd, char *cookie_val) {
    const char preamble[] =
    "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\">\n"
    "<html>"
    "<head>"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n"
    "<title>"
    "%s"
    "</title>\n"
    "<style type=\"text/css\">\n"
    "<!--\n %s -->\n"
    "</style>\n"
    "</head>"
    "<body>\n"
    "<div class=\"top_page\">\n";
    char title[255];
    snprintf(title, 254, "tvpvrd %s", server_version);
    const int maxlen = 8192*2;
    char *cssbuff = calloc(1,maxlen);
    read_cssfile(cssbuff,maxlen);

    http_header(sockd,cookie_val);

    _writef(sockd,preamble,title,cssbuff);
    free(cssbuff);
}

void
html_element_select(int sockd,char *legend,char *name,char *selected, const char *list[], int num, char *id) {
    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_select() !");
        exit(EXIT_FAILURE);
    }

    if( id && *id ) {
        snprintf(buffer,maxlen,
                "<div class=\"input_container\" id=\"%s\">"
                "<div class=\"input_legend\">%s</div>",id,legend);
    } else {
        snprintf(buffer,maxlen,
                "<div class=\"input_container\">"
                "<div class=\"input_legend\">%s</div>",legend);
    }
    _writef(sockd,buffer);

    if( id && *id ) {
        snprintf(buffer,maxlen,"<select name=\"%s\" class=\"%s\" id=\"%s\">\n",name,"input_select",id);
    } else {
        snprintf(buffer,maxlen,"<select name=\"%s\" class=\"%s\">\n",name,"input_select");
    }

    _writef(sockd,buffer);
    for (int i=0; i < num; ++i ) {
        if( selected && 0 == strcmp(selected,list[i]) ) {
            snprintf(buffer,maxlen,"<option selected value=\"%s\">%s</option>\n",list[i],list[i]);
        } else {
            snprintf(buffer,maxlen,"<option value=\"%s\">%s</option>\n",list[i],list[i]);
        }
        _writef(sockd,buffer);
    }
    snprintf(buffer,maxlen,"</select></div>\n");
    _writef(sockd,buffer);
    free(buffer);
}

void
html_element_select_code(int sockd,char *legend,char *name,char *selected, const struct skeysval_t list[], int num, char *id) {
    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_select_code() !");
        exit(EXIT_FAILURE);
    }

    if( id && *id ) {
        snprintf(buffer,maxlen,
                "<div class=\"input_container\" id=\"%s\">"
                "<div class=\"input_legend\">%s</div>",id,legend);
    } else {
        snprintf(buffer,maxlen,
                "<div class=\"input_container\">"
                "<div class=\"input_legend\">%s</div>",legend);
    }
    _writef(sockd,buffer);

    if( id && *id ) {
        snprintf(buffer,maxlen,"<select name=\"%s\" class=\"%s\" id=\"%s\">\n",name,"input_select_code",id);
    } else {
        snprintf(buffer,maxlen,"<select name=\"%s\" class=\"%s\">\n",name,"input_select_code");
    }
    _writef(sockd,buffer);
    int i=0;
    while( i < num ) {
        if( selected && 0 == strcmp(selected,list[i].val) ) {
            snprintf(buffer,maxlen,"<option selected value=\"%s\">%s</option>\n",list[i].key,list[i].val);
        } else {
            snprintf(buffer,maxlen,"<option value=\"%s\">%s</option>\n",list[i].key,list[i].val);
        }
        _writef(sockd,buffer);
       ++i;
    }
    snprintf(buffer,maxlen,"</select></div>\n");
    _writef(sockd,buffer);
    free(buffer);
}

void
html_element_input_text(int sockd, char *legend, char *name, char *id) {
    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_input() !");
        exit(EXIT_FAILURE);
    }
    if( id && *id ) {
        snprintf(buffer,maxlen,
                "<div class=\"input_container\" id=\"%s\">"
                "<div class=\"input_legend\">%s</div>",id,legend);
    } else {
        snprintf(buffer,maxlen,
                "<div class=\"input_container\">"
                "<div class=\"input_legend\">%s</div>",legend);
    }
    _writef(sockd,buffer);

    snprintf(buffer,maxlen,"<input type=\"text\" name=\"%s\" class=\"input_text\"></input></div>\n",name);
    _writef(sockd,buffer);
    free(buffer);
}

void
html_element_submit(int sockd,char *name, char *value, char *id) {

    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_submit() !");
        exit(EXIT_FAILURE);
    }
    snprintf(buffer,maxlen,
            "<div class=\"input_container\">"
            "<div class=\"input_legend\">&nbsp;</div>"
            "<input type=\"submit\" name=\"%s\" value=\"%s\" class=\"input_submit\" id=\"%s\"></div>\n",
            name,value,id);
    _writef(sockd,buffer);

    free(buffer);
}

void
html_notfound(int sockd) {
        _writef(sockd,
                "HTTP/1.1 404 Not Found\r\n"
                "Server: tvpvrd\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n\r\n<html><body><h3>404 - Not found.</h3></body></html>\r\n");
}

void
html_main_page(int sockd,char *wcmd, char *cookie_val) {
    // Initialize a new page

    html_newpage(sockd,cookie_val);
    html_topbanner(sockd);
    _writef(sockd,"<div class=\"left_side\">");
    html_commandlist(sockd);
    _writef(sockd,"</div>"); // class="LEFT_side"

    _writef(sockd,"<div class=\"right_side\">");
    html_output(sockd);

    // We must cwait for the semphore since since commands
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

    html_output_end(sockd);
    /*
     *  Experimental coding
     */
    html_cmd_add_del(sockd);
    _writef(sockd,"</div>"); // class="right_side"

    html_endpage(sockd);

}

void
html_login_page(int sockd) {
    // Initialize a new page
    html_newpage(sockd, NULL);
    html_topbanner(sockd);

    _writef(sockd, "<div class=\"%s\">", "login_container");
    _writef(sockd, "<div class=\"%s\">Please login</div>", "login_title");
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"login\">\n", "tvlogin");
    html_element_input_text(sockd, "User:", "user", "id_loginuser");
    html_element_input_text(sockd, "Password:", "pwd", "id_loginpwd");
    html_element_submit(sockd, "submit_login", "Login", "id_submitlogin");

    _writef(sockd, "<form>");
    _writef(sockd, "</div>");

    html_endpage(sockd);
}

void
html_cmd_add_del(int sockd) {
    static const char *day_list[] = {
        "","Mon","Tue","Wed","Thu","Fri","Sat","Sun"
    };
    const int n_day = 8;
    static const char *min_list[] = {
        "00","05","10","15","20","25","29","30","35","40","45","50","55","59"
    };
    static const char *hour_list[] = {
        "00","01","02","03","04","05","06","07","08","09","10","11","12",
        "13","14","15","16","17","18","19","20","21","22","23"
    };
    static const struct skeysval_t rpt_list[] = {
        {.key = "",.val=""},
        {.key = "w",.val="Weekly"},
        {.key = "d",.val="Daily"},
        {.key = "f",.val="Mon-Fri"},
        {.key = "t",.val="Mon-Thu"},
        {.key = "s",.val="Sat-Sun"},
    };
    const int n_rpt = 6;
    static const char *rptcount_list[] = {
        "","02","03","04","05","06","07","08","09","10",
        "11","12","13","14","15","16","17","18","19",
        "20","21","22","23","24","25","26","27","28","29",
        "30","31","33","33","34","35","36","37","38","39",
    };
    const int n_rptcount = 39 ;
    static const char *yn_list[] = {
        "Yes","No"
    };
    const int n_ynlist = 2;
    static const char *station_list[128];
    const int n_stations = get_stations(station_list,128);

    static const char *profile_list[64];
    const int n_profile = get_profile_names(profile_list,64);

    const int n_hour = sizeof(hour_list)/sizeof(char *);
    const int n_min = sizeof(min_list)/sizeof(char *);

    //_writef(sockd,"<div class=\"cmd_bottom_container\">");

    /*
     * Add recordings
     */
    _writef(sockd,"<form name=\"%s\" method=\"get\" action=\"addrec\">\n","addrecording");

    _writef(sockd,"<fieldset><legend>Add new recording</legend>");
    html_element_select_code(sockd,"Repeat:","repeat",NULL, rpt_list, n_rpt,NULL);
    html_element_select(sockd,"Count:","repeatcount",NULL, rptcount_list, n_rptcount,"id_rptcount");
    html_element_select(sockd,"Profile:","profile",default_transcoding_profile, profile_list, n_profile,"id_profile");
    html_element_select(sockd,"Station:","channel",NULL, station_list, n_stations,"id_station");

    html_element_select(sockd,"Time:","start_day",NULL, day_list, n_day,"id_start");
    html_element_select(sockd,"&nbsp;","start_hour","18", hour_list, n_hour,NULL);
    html_element_select(sockd,"&nbsp;","start_min",NULL, min_list, n_min,NULL);
    _writef(sockd,"<div class=\"input_container\"><div>&nbsp;</div> &nbsp; to &nbsp; </div>");
    html_element_select(sockd,"&nbsp;","end_hour","18", hour_list, n_hour,NULL);
    html_element_select(sockd,"&nbsp;","end_min","59", min_list, n_min,NULL);

    html_element_input_text(sockd,"Title:","title","id_title");
    html_element_submit(sockd,"submit_addrec","Add","");
    _writef(sockd,"</fieldset>");
    _writef(sockd,"</form>\n");


    /*
     * Delete recordings
     */
    _writef(sockd,"<form name=\"%s\" method=\"get\" action=\"delrec\">\n","deleterecording");

    _writef(sockd,"<fieldset><legend>Delete recording</legend>");

    //html_element_input_text(sockd,"Record id:","recid");
    struct skeysval_t *listrec ;
    int num = listrecskeyval(&listrec,3);
    html_element_select_code(sockd,"Title:","recid",NULL,listrec,num,"id_delselect");
    for( int i=0; i < num; ++i ) {
        free(listrec[i].key);
        free(listrec[i].val);
    }
    free(listrec);

    html_element_select(sockd,"Delete serie:","delserie","No",yn_list,n_ynlist,"id_seriesyn");
    html_element_submit(sockd,"submit_delrec","Delete","delrec");
    _writef(sockd,"</fieldset>");
    _writef(sockd,"</form>\n");

    // Close container
    //_writef(sockd,"</div>");
}

struct cmd_entry {
    char *cmd_name;
    char  *cmd_desc;
};
struct cmd_grp {
    char *grp_name;
    char *grp_desc;
    int cmd_num;
    struct cmd_entry *entry;
};

void
html_commandlist(int sockd) {

    /*
  static struct cmd_entry cmdfunc_master[] = {
        {"ar","Add repeated"},
        {"a","Add"},
        {"dp","Display profile"},
        {"dr","Delete repeated"},
        {"d","Delete"},
        {"h","Help"},
        {"i","Information"},
        {"ktf","Toggle kill transcoding flag at shutdown"},
        {"kt", "Kill all transcodings"},
        {"lq","List transocding file list"},
        {"lc","List constrols for capture card"},
        {"ls","List stations"},
        {"lp","List profiles"},
        {"log","Show last log"},
        {"l","Upcoming recordings"},
        {"n","Next immediate recordings"},
        {"otl","Show ongoing transcoding list"},
        {"ot","Show ongoing transcoding"},
        {"o","Show ongoing recording"},
        {"q","Quick recording"},
        {"rst","Reset statistics"},
        {"rp","Refresh transcoding profiles"},
        {"sp","update transcoding profile for recording"},
        {"st","Profile statistics"},
        {"s","Status"},
        {"tf","Transcode named file"},
        {"tl","Transcode all files in named list file"},
        {"t","Server time"},
        {"u","Force DB update"},
        {"vc","Show TV-Card information"},
        {"v","Version"},
        {"wt","List waiting transcodings"},
        {"x","Show DB raw file"},
        {"z","Show ini-file settings"},
        {"!","Abort recording"}
    };

    static struct cmd_entry cmdfunc_slave[] = {
        {"dp","Display profile"},
        {"h","Help"},
        {"ktf","Toggle kill transcoding flag at shutdown"},
        {"kt","Kill ongoing transcodings"},
        {"lp","List profiles"},
        {"lq","List transocding file list"},
        {"log","Show last log"},
        {"otl","Show ongoing transcoding list"},
        {"ot","Show ongoing transcodings"},
        {"rst","Reset statistics"},
        {"st","Profile statistics"},
        {"s","Status"},
        {"tf","Transcode named file"},
        {"tl","Transcode files in list"},
        {"t","Time"},
        {"v","Version"},
        {"wt","List waiting transcodings"},
        {"z","Show ini-file settings"}        
    };

    */

  static struct cmd_entry cmdfunc_master_recs[] = {
        {"l","List all"},
        {"n","Next"},
        {"o","Ongoing"},
        {"ls","Stations"}
    };

  static struct cmd_entry cmdfunc_master_transcoding[] = {
        {"ot","Ongoing"},
        {"wt","Queue"},
        {"st","Statistics"},
        {"lp","Profiles"}
    };

  static struct cmd_entry cmdfunc_master_status[] = {
        {"s","Status"},
        {"t","Time"},
        {"v","Version"}
    };
  
  static struct cmd_entry cmdfunc_master_misc[] = {
        {"xx","View DB file"},
        {"z","View settings"},
        {"log%2050","Last 50 log entries"}
    };

  static struct cmd_entry cmdfunc_master_driver[] = {
        {"vc","Driver information"},
        {"lc 0","Settings for card 0"}
  };

  static struct cmd_entry cmdfunc_slave_transcoding[] = {
        {"ot","Ongoing transcoding"},
        {"wt","Waiting transcodings"}
    };


  static struct cmd_entry cmdfunc_slave_status[] = {
        {"s","Status"},
        {"t","Time"},
        {"v","Version"}
    };

  static struct cmd_entry cmdfunc_slave_misc[] = {
        {"lp","Profiles"},
        {"st","Profile statistics"},
        {"z","Show ini-file settings"},
        {"log%2050","Show last 50 log"}
    };

    static struct cmd_grp cmd_grp_master[] = {
        {"Server","Server information",         sizeof(cmdfunc_master_status)/sizeof(struct cmd_entry),cmdfunc_master_status},
        {"Recordings","Stored recordings",sizeof(cmdfunc_master_recs)/sizeof(struct cmd_entry),cmdfunc_master_recs},
        {"Transcoding","Transcoding info",sizeof(cmdfunc_master_transcoding)/sizeof(struct cmd_entry),cmdfunc_master_transcoding},
        {"Other","Various information",  sizeof(cmdfunc_master_misc)/sizeof(struct cmd_entry),cmdfunc_master_misc},
        {"Capture card","Card information",  sizeof(cmdfunc_master_driver)/sizeof(struct cmd_entry),cmdfunc_master_driver}
    };

    static struct cmd_grp cmd_grp_slave[] = {
        {"Transcoding","Transcoding info",sizeof(cmdfunc_slave_transcoding)/sizeof(struct cmd_entry),cmdfunc_slave_transcoding},
        {"Status","Show status",         sizeof(cmdfunc_slave_status)/sizeof(struct cmd_entry),cmdfunc_slave_status},
        {"Other","Various information",  sizeof(cmdfunc_slave_misc)/sizeof(struct cmd_entry),cmdfunc_slave_misc}
    };

    static struct cmd_grp *cmdgrp;
    int cmdgrplen;

    if( is_master_server ) {
        cmdgrp = cmd_grp_master;
        cmdgrplen = sizeof (cmd_grp_master) / sizeof(struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave;
        cmdgrplen = sizeof (cmd_grp_slave) / sizeof(struct cmd_grp);
    }

    _writef(sockd,"<div class=\"cmd_menu\">");
    for( int i=0; i < cmdgrplen; ++i ) {

/*
        _writef(sockd,"<div class=\"cmdgrp_title_row\"><span class=\"cmdgrp_title\">%s</span> - <span class=\"cmdgrp_desc\">%s</span></div>",
                cmdgrp[i].grp_name,cmdgrp[i].grp_desc);
*/
        _writef(sockd,"<div class=\"cmdgrp_title_row\"><span class=\"cmdgrp_title\">%s</span></div>",
                cmdgrp[i].grp_name,cmdgrp[i].grp_desc);

        _writef(sockd,"<div class=\"cmdgrp_commands\">");
        for( int j=0; j < cmdgrp[i].cmd_num; ++j ) {
            _writef(sockd,"<a href=\"cmd?%s\">%02d. %s</a><br>\n",cmdgrp[i].entry[j].cmd_name,j+1,cmdgrp[i].entry[j].cmd_desc);
        }
        _writef(sockd,"</div>");

    }
    _writef(sockd,"</div>");

}