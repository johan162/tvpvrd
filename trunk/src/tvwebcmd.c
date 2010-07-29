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

        } else if ( (ret = matchcmd("^GET /" _PR_ANPS "HTTP" _PR_ANY _PR_E, buffer, &field)) > 1 ) {

            // Unrecoqnized command
            strncpy(cmd,"xxx",maxlen);
            return 1;

        }


        return 0;

    }

    return 0;
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