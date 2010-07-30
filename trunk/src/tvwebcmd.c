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

#include "tvpvrd.h"
#include "utils.h"
#include "tvwebcmd.h"
#include "confpath.h"
#include "tvcmd.h"
#include "freqmap.h"
#include "transc.h"
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

            logmsg(LOG_DEBUG,"Decoded command: %s",cmd);

            return 1;

        } else if ( (ret = matchcmd("^GET /(cmd)? HTTP" _PR_ANY _PR_E, buffer, &field)) > 1 ) {
               
            // Empty call. Interpret as a 'v' status call
            strncpy(cmd,"v",maxlen);
            return 1;

        } else if( (ret = matchcmd("^GET /addrec?" _PR_ANY _PR_E, buffer, &field)) > 1 ) {
            
            logmsg(LOG_DEBUG,"Received addrec: %s",field[1]);
            
            return 1;

        } else if( (ret = matchcmd("^GET /delrec?" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            logmsg(LOG_DEBUG,"Received delrec: %s",field[1]);

            return 1;

        } else if ( (ret = matchcmd("^GET /" _PR_ANPS "HTTP" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            // Unrecoqnized command
            strncpy(cmd,"xxx",maxlen);
            return 1;

        }

        return 0;

    }

    return 0;
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
                            _PR_AN "=" _PR_ANO 
                            " HTTP/1.1",
                            buffer, &field)) > 1 ) {

            const int maxvlen=256;
            char channel[maxvlen];
            char sd[maxvlen],sh[maxvlen],smin[maxvlen],eh[maxvlen],emin[maxvlen];
            char profile[maxvlen],title[maxvlen],submit[maxvlen];

            get_assoc_value(channel,maxvlen,"channel",&field[1],ret-1);
            get_assoc_value(sd,maxvlen,"start_day",&field[1],ret-1);
            if( 0==strcmp("Today",sd) ) {
                sd[0]='\0';
            }
            get_assoc_value(sh,maxvlen,"start_hour",&field[1],ret-1);
            get_assoc_value(smin,maxvlen,"start_min",&field[1],ret-1);
            get_assoc_value(eh,maxvlen,"end_hour",&field[1],ret-1);
            get_assoc_value(emin,maxvlen,"end_min",&field[1],ret-1);
            get_assoc_value(profile,maxvlen,"profile",&field[1],ret-1);
            get_assoc_value(title,maxvlen,"title",&field[1],ret-1);
            get_assoc_value(submit,maxvlen,"submit_addrec",&field[1],ret-1);

            if( 0==strcmp(submit,"Add") ) {
                snprintf(wcmd,1024,
                    "a %s %s %s:%s %s:%s %s @%s",
                    channel,sd,sh,smin,eh,emin, title,profile);
            }

        } else if( (ret = matchcmd("^GET /delrec\\?"
                            _PR_AN "=" _PR_ANO "&"
                            _PR_AN "=" _PR_ANO
                            " HTTP/1.1",
                            buffer, &field)) > 1 ) {

            const int maxvlen=256;
            char recid[maxvlen],submit[maxvlen];
            get_assoc_value(recid,maxvlen,"recid",&field[1],ret-1);
            get_assoc_value(submit,maxvlen,"submit_delrec",&field[1],ret-1);

           if( 0==strcmp(submit,"Delete") ) {
                snprintf(wcmd, 1024, "d %s", recid);
            }
            
        }

        logmsg(LOG_DEBUG,"Web command translated to: %s",wcmd);
        
        // Initialize a new page
        char title[255];
        snprintf(title,254,"tvpvrd %s",server_version);
        html_newpage(my_socket,title);
        html_topbanner(my_socket);
        _writef(my_socket,"<div class=\"left_side\">");
        html_commandlist(my_socket);
        _writef(my_socket,"</div>"); // class="LEFT_side"

        _writef(my_socket,"<div class=\"right_side\">");
        html_output(my_socket);

        // We must cwait for the semphore since since commands
        // might alter data structures and we can only have one
        // thread at a time accessing the data structures
        pthread_mutex_lock(&recs_mutex);

        // Make _writef() do HTML encoding on any output sent
        htmlencode_flag = 1;

        // The execution of the command happens in the command module.
        // Any output from the command are sent to the given socket
        // descriptor and passed back to the browser in this case.
        cmdinterp(wcmd, my_socket);
        htmlencode_flag = 0;

        pthread_mutex_unlock(&recs_mutex);

        html_output_end(my_socket);
        /*
         *  Experimental coding
         */
        html_cmd_add_del(my_socket);
        _writef(my_socket,"</div>"); // class="right_side"


        html_endpage(my_socket);

    } else {
        
        logmsg(LOG_DEBUG, "Browser sent unrecognized command: %s",buffer);

    }

    free(buffer);
}


#define CSSFILE_NAME "tvpvrd.css"

int
read_cssfile(char *buff, int maxlen) {
    char cssfile[255];
    snprintf(cssfile,255,"%s/tvpvrd/%s",CONFDIR,CSSFILE_NAME);
    cssfile[254] = '\0';
    char linebuff[512];

    *buff = '\0';
    FILE *fp = fopen(cssfile,"r");
    if( fp == NULL ) {
        logmsg(LOG_ERR,"Cannot read CSS file '&s'",cssfile);
        return -1;
    }

    while( maxlen >  0 && fgets(linebuff,511,fp) ) {
        linebuff[511] = '\0';
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

void
html_newpage(int sockd, char *title) {
    const char preamble[] =
    "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\">"
    "<html>"
    "<head>"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />"
    "<title>"
    "%s"
    "</title>"
    "<style type=\"text/css\">"
    "%s"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"top_page\">";

    char cssbuff[4096];
    read_cssfile(cssbuff,4096);

    _writef(sockd,preamble,title,cssbuff);
}

void
html_element_select(int sockd,char *legend,char *name,char *selected, const char *list[], int num) {
    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_select() !");
        exit(EXIT_FAILURE);
    }

    snprintf(buffer,maxlen,
            "<div class=\"input_container\">"
            "<div class=\"input_legend\">%s</div>",legend);
    _writef(sockd,buffer);

    snprintf(buffer,maxlen,"<select name=\"%s\" class=\"%s\">\n",name,"input_legend");
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
html_element_input_text(int sockd, char *legend, char *name) {
    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_input() !");
        exit(EXIT_FAILURE);
    }
    snprintf(buffer,maxlen,
            "<div class=\"input_container\">"
            "<div class=\"input_legend\">%s</div>",legend);
    _writef(sockd,buffer);

    snprintf(buffer,maxlen,"<input type=\"text\" name=\"%s\" class=\"input_text\"></input></div>\n",name);
    _writef(sockd,buffer);
    free(buffer);
}

void
html_element_submit(int sockd,char *name, char *value) {

    const int maxlen=8192;
    char *buffer = calloc(1,maxlen);

    if( ! buffer ) {
        logmsg(LOG_ERR,"Out of memory in html_element_submit() !");
        exit(EXIT_FAILURE);
    }
    snprintf(buffer,maxlen,
            "<div class=\"input_container\">"
            "<div class=\"input_legend\">&nbsp;</div>"
            "<input type=\"submit\" name=\"%s\" value=\"%s\" class=\"input_submit\"></div>\n",
            name,value);
    _writef(sockd,buffer);

    free(buffer);
}

void
html_cmd_add_del(int sockd) {
    static const char *day_list[] = {
        "Today","Mon","Tue","Wed","Thu","Fri","Sat","Sun"
    };
    int n_day = 7;
    static const char *min_list[] = {
        "00","05","10","15","20","25","30","35","40","45","50","55"
    };
    static const char *hour_list[] = {
        "00","01","02","03","04","05","06","07","08","09","10","11","12",
        "13","14","15","16","17","18","19","20","21","22","23"
    };
    static const char *station_list[128];
    int n_stations = get_stations(station_list,128);

    static const char *profile_list[64];
    int n_profile = get_profile_names(profile_list,64);

    int n_hour = sizeof(hour_list)/sizeof(char *);
    int n_min = sizeof(min_list)/sizeof(char *);

    _writef(sockd,"<div class=\"cmd_bottom_container\">");

    _writef(sockd,"<form name=\"%s\" method=\"get\" action=\"addrec\">\n","addrecording");

    _writef(sockd,"<fieldset><legend>Add new recording</legend>");
    
    html_element_select(sockd,"Ch:","channel",NULL, station_list, n_stations);
    html_element_select(sockd,"Start:","start_day",NULL, day_list, n_day);
    html_element_select(sockd,"&nbsp;","start_hour",NULL, hour_list, n_hour);
    html_element_select(sockd,"&nbsp;","start_min",NULL, min_list, n_min);

    html_element_select(sockd,"End:","end_hour",NULL, hour_list, n_hour);
    html_element_select(sockd,"&nbsp;","end_min",NULL, min_list, n_min);

    html_element_select(sockd,"Profile:","profile",default_transcoding_profile, profile_list, n_profile);
    html_element_input_text(sockd,"Title:","title");
    html_element_submit(sockd,"submit_addrec","Add");
    _writef(sockd,"</fieldset>");
    _writef(sockd,"</form>\n");

    _writef(sockd,"<form name=\"%s\" method=\"get\" action=\"delrec\">\n","deleterecording");

    _writef(sockd,"<fieldset><legend>Delete recording</legend>");

    html_element_input_text(sockd,"Record id:","recid");
    html_element_submit(sockd,"submit_delrec","Delete");
    _writef(sockd,"</fieldset>");
    _writef(sockd,"</form>\n");

    _writef(sockd,"</div>");
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
        {"n","Nextr immediate recordings"},
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

  static struct cmd_entry cmdfunc_master_query[] = {
        {"h","Help"},
        {"lc 0","List controls for capture card 0"},
        {"ls","List stations"},
        {"lp","List profiles"},
        {"log%2050","Show last 50 log"},
        {"l","Upcoming recordings"},
        {"n","Next immediate recordings"},
        {"otl","Show ongoing transcoding list"},
        {"ot","Show ongoing transcoding"},
        {"o","Show ongoing recording"},
        {"st","Profile statistics"},
        {"s","Status"},
        {"t","Server time"},
        {"vc","Show TV-Card information"},
        {"v","Version"},
        {"wt","List waiting transcodings"},
        {"x","Show DB raw file"},
        {"z","Show ini-file settings"},
    };

    static struct cmd_entry cmdfunc_slave_query[] = {
        {"h","Help"},
        {"lp","List profiles"},
        {"log","Show last log"},
        {"otl","Show ongoing transcoding list"},
        {"ot","Show ongoing transcodings"},
        {"st","Profile statistics"},
        {"s","Status"},
        {"t","Server time"},
        {"v","Version"},
        {"wt","List waiting transcodings"},
        {"z","Show ini-file settings"}
    };

    static struct cmd_grp cmd_grp_master[] = {
        {"Query","Non modifying commands",sizeof(cmdfunc_master_query)/sizeof(struct cmd_entry),cmdfunc_master_query}
    };

    static struct cmd_grp cmd_grp_slave[] = {
        {"Query","Non modifying commands",sizeof(cmdfunc_slave_query)/sizeof(struct cmd_entry),cmdfunc_slave_query}
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

        _writef(sockd,"<div class=\"cmdgrp_title_row\"><span class=\"cmdgrp_title\">%s</span> - <span class=\"cmdgrp_desc\">%s</span></div>",
                cmdgrp[i].grp_name,cmdgrp[i].grp_desc);

        _writef(sockd,"<div class=\"cmdgrp_commands\">");
        for( int j=0; j < cmdgrp[i].cmd_num; ++j ) {
            _writef(sockd,"<a href=\"cmd?%s\">%02d. %s</a><br>\n",cmdgrp[i].entry[j].cmd_name,j+1,cmdgrp[i].entry[j].cmd_desc);
        }
        _writef(sockd,"</div>");

    }
    _writef(sockd,"</div>");

}