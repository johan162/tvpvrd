/* =========================================================================
 * File:        TVWEBCMD.C
 * Description: Handle the HTTP protocol
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
#include <ctype.h>

#include "config.h"
#include <pcre.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "tvwebui.h"
#include "tvcmd.h"
#include "stats.h"
#include "transc.h"
#include "transcprofile.h"
#include "recs.h"
#include "tvhtml.h"
#include "datetimeutil.h"
#include "tvplog.h"
#include "xstr.h"
#include "httpreq.h"


typedef int (*pwf_t)(int,struct keypair_t *args, const size_t, struct http_reqheaders *,char *);

struct web_cmds_t {
    char path[80];
    char name[80];
    size_t numargs;    
    pwf_t cmdfunc;
};

// The array index for the login command
#define LOGIN_CMDIDX 0

// Root directory, webname, number of arguments, function
struct web_cmds_t *web_cmds;

int
_web_cmd_logout(int socket,struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_logout: sock=%d, numargs=%d",socket,numargs);
#endif        
    
    (void)args; // To shut up warning about non used arguments
    (void)login_token;
    (void)numargs;
    
    web_login_page(socket,headers->ismobile);
    return 0;    
}

int
_web_cmd_login(int socket,struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_login: sock=%d, numargs=%d",socket,numargs);
#endif 
    if ( require_web_password ) {
        char *user, *pwd, *submit;
        (void)login_token;
        // Now extract the parameters and if they are found check that this
        // is the correct form submission and check that the credentials match.
        // Otherwise just send back the login page.
        if( -1 == get_assoc_value_s(args,numargs,"user",&user) ||
            -1 == get_assoc_value_s(args,numargs,"pwd",&pwd) ||
            -1 == get_assoc_value_s(args,numargs,"submit_login",&submit) || 
            strcmp(submit, "Login") ||
            !validate_login(user, pwd) ) {

    #ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"WEB login unsuccessful. users=%s, pwd=%s",user!=NULL ? user : "NULL",pwd!=NULL  ? pwd : "NULL");
    #endif    

            // No valid login parameters. Send back login page
            web_login_page(socket, headers->ismobile);
            return 0;
        }
        web_main_page(socket, "v", create_login_cookie(user, pwd), headers->ismobile);        
    } else {
        // Login successful. Show the main page and use the "version" command
        // as the default command to show in the output area.
        web_main_page(socket, "v", "", headers->ismobile); // Empty login cookie
    }
    return 0;
}

int
_web_cmd_addrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *repeat, *repeatcount, *channel;
    char *sd,*sh,*smin;
    char *eh,*emin;
    char *profile,*title,*submit;
    char *startnum, *name_mangling;
    char cmdstr[255];
   
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_addrec: sock=%d, numargs=%d",socket,numargs);
#endif 
    
    if( -1 == get_assoc_value_s(args,numargs,"repeat",&repeat) ||
        -1 == get_assoc_value_s(args,numargs,"repeatcount",&repeatcount) ||   
        -1 == get_assoc_value_s(args,numargs,"channel", &channel) ||
        -1 == get_assoc_value_s(args,numargs,"start_day",&sd) ||
        -1 == get_assoc_value_s(args,numargs,"start_hour",&sh) ||
        -1 == get_assoc_value_s(args,numargs,"start_min",&smin) ||
        -1 == get_assoc_value_s(args,numargs,"end_hour",&eh) ||
        -1 == get_assoc_value_s(args,numargs,"end_min",&emin) ||
        -1 == get_assoc_value_s(args,numargs,"profile",&profile) ||
        -1 == get_assoc_value_s(args,numargs,"title",&title) ||
        -1 == get_assoc_value_s(args,numargs,"submit_addrec",&submit) ||             
        strcmp(submit, "Add") ) {
        
        return -1;
    }
    
    if( display_advanced_rec_control ) {
          if( -1 == get_assoc_value_s(args,numargs,"startnum",&startnum) ||
              -1 == get_assoc_value_s(args,numargs,"name_mangling",&name_mangling) ) {
              return -1;
          }
    } else {
        startnum = NULL;
        name_mangling = NULL;
    }

    char tmpcmd[128];
    // Build command

    if (0 == strcmp(repeat, "")) {
        snprintf(cmdstr, sizeof(cmdstr)-1, "a %s", channel);
    } else {
        // Repeat add
        snprintf(cmdstr, sizeof(cmdstr)-1, "ar %s %s %s ", repeat, repeatcount, channel);
    }
    if (*sd != '\0') {
        snprintf(tmpcmd, sizeof(tmpcmd)-1, " %s ", sd);
        strncat(cmdstr, tmpcmd, sizeof(cmdstr)-1-strlen(cmdstr));
    }
    snprintf(tmpcmd, sizeof(tmpcmd)-1, " %s:%s ", sh, smin);
    strncat(cmdstr, tmpcmd, sizeof(cmdstr)-1-strlen(cmdstr));

    snprintf(tmpcmd, sizeof(tmpcmd)-1, " %s:%s ", eh, emin);
    strncat(cmdstr, tmpcmd, sizeof(cmdstr)-1-strlen(cmdstr));

    snprintf(tmpcmd, sizeof(tmpcmd)-1, " %s @%s ", title, profile);
    strncat(cmdstr, tmpcmd, sizeof(cmdstr)-1-strlen(cmdstr));

#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmdstring=\"%s\"",cmdstr);
#endif 
    
    if( display_advanced_rec_control ) {
        int snum = xatoi(startnum);
        if( snum > 0 && snum <= 30 ) {
            set_initial_recurrence_start_number(snum);
        } else {
            return -1;
        }
        int nmt = xatoi(name_mangling);
        if( nmt >= 0 && nmt <= 2 ) {
            default_repeat_name_mangle_type = nmt;
        } else {
            return -1;
        }
    }
    web_main_page(socket, cmdstr, login_token, headers->ismobile);
    return 0;
        
}

// (int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token)

int
_web_cmd_delrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *recid, *submit, *delserie;

#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_delrec: sock=%d, numargs=%d",socket,numargs);
#endif 
    
    if( -1 == get_assoc_value_s(args,numargs,"recid",&recid) ||    
        -1 == get_assoc_value_s(args,numargs,"delserie",&delserie) ||
        -1 == get_assoc_value_s(args,numargs,"submit_delrec",&submit) ||
        strcmp(submit, "Delete") ) {
        
        return -1;
    }
        
    char tmpcmd[128];
    
    if (0 == strcmp(delserie, "Yes")) {
       snprintf(tmpcmd, sizeof(tmpcmd)-1, "dr %s", recid);
    } else {
       snprintf(tmpcmd, sizeof(tmpcmd)-1, "d %s", recid);
    }
    

#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmdstring=\"%s\"",tmpcmd);
#endif   

    web_main_page(socket, tmpcmd, login_token, headers->ismobile);
    return 0;
}

int
_web_cmd_chwt(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *wtheme;
    
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_chwt: sock=%d, numargs=%d",socket,numargs);
#endif 
    
    if( -1 == get_assoc_value_s(args,numargs,"t",&wtheme) ) {
        return -1;
    }
    
    strncpy(web_theme,wtheme,sizeof(web_theme)-1);

#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmdstring=\"t\"");
#endif   
    
    web_main_page(socket, "t", login_token, headers->ismobile);
    return 0;

}

/*

_web_cmd_XXXXX(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *a1,*a2,*a3;
    
    if( -1 == get_assoc_value_s(args,numargs,"a1",&a1) || 
        -1 == get_assoc_value_s(args,numargs,"a2",&a2) || 
        -1 == get_assoc_value_s(args,numargs,"a3",&a3) ) {
        
        return -1;
        
    }
 
    web_main_page(socket, COMMAND, login_token, headers->ismobile);
    return 0;  
 
}
*/

int
_web_cmd_killrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_killrec: sock=%d, numargs=%d",socket,numargs);
#endif 
    char *recid;
    if( -1 == get_assoc_value_s(args,numargs,"rid",&recid) ) {
        
        return -1;
    }
    
    char tmpcmd[128];
    snprintf(tmpcmd, sizeof(tmpcmd)-1, "! %s", recid);
    
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmdstring=\"%s\"",tmpcmd);
#endif   
    
    web_main_page(socket, tmpcmd, login_token, headers->ismobile);
    return 0;
}


int
_web_cmd_addqrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *channel;
    char *length_hour, *length_min;
    char *profile, *title, *submit;
    
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmd_addqrec: sock=%d, numargs=%d",socket,numargs);
#endif 
    
    if( -1 == get_assoc_value_s(args,numargs,"channel",&channel) || 
        -1 == get_assoc_value_s(args,numargs,"length_hour",&length_hour) || 
        -1 == get_assoc_value_s(args,numargs,"length_min",&length_min) || 
        -1 == get_assoc_value_s(args,numargs,"profile",&profile) || 
        -1 == get_assoc_value_s(args,numargs,"title",&title) ||             
        -1 == get_assoc_value_s(args,numargs,"submit_qaddrec",&submit) ||
        strcmp(submit, "Start") ) {    
        
        return -1;
        
    }
    
    char tmpcmd[128];
    snprintf(tmpcmd, sizeof(tmpcmd)-1, "q %s %s:%s %s @%s ", channel,length_hour, length_min, title, profile);
    
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG,"cmdstring=\"%s\"",tmpcmd);
#endif   
    
    web_main_page(socket, tmpcmd, login_token, headers->ismobile);
    return 0;     
    
}

int
_web_cmd_default(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    (void) args;
    (void) numargs;
    web_main_page(socket, "t", login_token, headers->ismobile);
    return 0;
}

int
_web_cmd_command(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    (void) args;
    (void) numargs;
    
    char *c;
    if( -1 == get_assoc_value_s(args,numargs,"c",&c) ) {
        
        return -1;
    }
        
    web_main_page(socket, c, login_token, headers->ismobile);
    return 0;
}


void
init_web_cmds(void) {
    
    static struct web_cmds_t web_cmds_plain[] = {
        {"/","login",3,_web_cmd_login},
        {"/","logout",0,_web_cmd_logout},
        {"/","addrec",11,_web_cmd_addrec},
        {"/","addqrec",6,_web_cmd_addqrec},
        {"/","delrec",3,_web_cmd_delrec},
        {"/","chwt",1,_web_cmd_chwt},
        {"/","killrec",1,_web_cmd_killrec},
        {"/","cmd",1,_web_cmd_command},
        {"/","",0,_web_cmd_default},

        /* Sentinel */
        {"","",0,NULL}    
    };
    
    static struct web_cmds_t web_cmds_advanced[] = {
        {"/","login",3,_web_cmd_login},
        {"/","logout",0,_web_cmd_logout},
        {"/","addrec",13,_web_cmd_addrec},
        {"/","addqrec",6,_web_cmd_addqrec},
        {"/","delrec",3,_web_cmd_delrec},
        {"/","chwt",1,_web_cmd_chwt},
        {"/","killrec",1,_web_cmd_killrec},
        {"/","cmd",1,_web_cmd_command},
        {"/","",0,_web_cmd_default},

        /* Sentinel */
        {"","",0,NULL}    
    };
    
    if( display_advanced_rec_control ) {
        web_cmds = web_cmds_advanced;
    } else {
        web_cmds = web_cmds_plain;
    }
}

_Bool web_cmd_islogin(char *file) {
    return 0 == strcmp(file,web_cmds[LOGIN_CMDIDX].name);
}

int
web_dispatch_httpget_cmd(const int socket,char *path,char *name,
                     struct keypair_t *args,size_t numargs,
                     struct http_reqheaders *headers,
                     char *login_token) {
    size_t i=0;
    
    // If either a path or name doesn't match keep searching
    while( *web_cmds[i].path && 
           (strncmp(path,web_cmds[i].path,255) || strncmp(name,web_cmds[i].name,255)) ) {
        ++i;
    }    
    if( *web_cmds[i].path == '\0' || web_cmds[i].numargs != numargs ) {
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG, "Command not recognized. c[%d].path=%s, c[%d].numargs=%d, numargs=%d",i,web_cmds[i].path,i,web_cmds[i].numargs,numargs);
#endif        
        return -1;
    }
    return web_cmds[i].cmdfunc(socket,args,numargs,headers,login_token);

}

void
web_dispatch_httpget_staticfile(const int socket, char *path, char *filename, struct keypair_t *args, size_t numargs, struct http_reqheaders *headers, char *login_token) {

#ifdef EXTRA_WEB_DEBUG
    if( headers->IfModifiedSince ) {
        logmsg(LOG_DEBUG, "Found If-Modified-Since: header. Value=%s", headers->IfModifiedSince);
    }
#endif
    (void) path;
    (void) args;
    (void) numargs;
    (void) login_token;
    
    char fullfilename[255];
    if( path[0]=='/' && path[1]=='\0' ) {
        snprintf(fullfilename,sizeof(fullfilename),"/%s",filename);
    } else {
        snprintf(fullfilename,sizeof(fullfilename),"%s/%s",path,filename);
    }
#ifdef EXTRA_WEB_DEBUG
    logmsg(LOG_DEBUG, "path=%s, filename=%s, fullfilename=%s",path,filename,fullfilename);
#endif
    
    // Convert time to a timestamp to compare with file modified time
    // Match HTTP Header date format, i.e. Sat, 29 Oct 1994 19:43:31 GMT

    // Try first to use en_US locale since the header is in English.
    // If this doesn't work then we use the "C" locale but this might not
    // work to interpret the header.
    time_t mtime = 0;
    struct tm tm_date;
    CLEAR(tm_date);

    if ( NULL == headers->IfModifiedSince || NULL == strptime(headers->IfModifiedSince, "%a, %d %b %Y %T %Z", &tm_date)) {
#ifdef EXTRA_WEB_DEBUG            
        logmsg(LOG_DEBUG, "No IF-Modified-Since headers or failed to parse it");
#endif
        // Set the file date ten years back to force a resend of the file in case the header can not be parsed.
        sendback_file(socket, fullfilename, time(NULL) - 3600 * 24 * 30 * 12 * 10);
    } else {

#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG, "After strptime hour=%d, zone=%s, isdst=%d", tm_date.tm_hour,
                tm_date.tm_zone,tm_date.tm_isdst);
#endif
        tm_date.tm_zone = "GMT";
        mtime = mktime(&tm_date);
        localtime_r(&mtime, &tm_date);

#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG, "Localtime offset=%d, zone=%s, hour=%d, isdst=%d", 
                tm_date.tm_gmtoff, tm_date.tm_zone,tm_date.tm_hour,tm_date.tm_isdst);
#endif

        // Since the original time is given in GMT and we want it expressed
        // in the local time zone we must add the offset from GMTIME for the
        // current time zone            
        mtime -= tm_date.tm_isdst * 3600;
        mtime += tm_date.tm_gmtoff;
        localtime_r(&mtime, &tm_date);

#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG, "After localtime adjustment hour=%d", tm_date.tm_hour);
#endif
        sendback_file(socket, fullfilename, mtime);

    }
}


/**
 * Parse the HTTP request from the browser. We start by splitting the header 
 * @param socket
 * @param headers
 * @param login_token
 * @return 
 */
int
web_exec_httpget(const int socket,struct http_reqheaders *headers,char *login_token) {
    struct keypair_t *args;
    char dir[255];
    char file[255];
    size_t numargs;
    int ret=0;
    
    const size_t maxargs=20;
    args = calloc(maxargs,sizeof(struct keypair_t));
    
    if( -1==web_parse_httpget(headers->Get,dir,sizeof(dir),file,sizeof(file),args,maxargs,&numargs) ) {
        ret = -1;
        goto _freeret;
    }
    
#ifdef EXTRA_WEB_DEBUG    
    logmsg(LOG_DEBUG,"File=%s, GET=%s, numargs=%zu, login_token=%s",file,headers->Get,numargs,login_token);
#endif
       
    if( *login_token || ! require_web_password) {
        // First try if this is any of the predefined commands
        if( -1==web_dispatch_httpget_cmd(socket,dir,file,args,numargs,headers,login_token) ) {

            // If the file is found send it back, otherwise send back not-found-header
            // This means that this function will always succeed
            web_dispatch_httpget_staticfile(socket,dir,file,args,numargs,headers,login_token);

        }
    } else {
        // First check if this is a try to login
        if( web_cmd_islogin(file) ) {
            _web_cmd_login(socket,args,numargs,headers,login_token);
        } else {        
            // We only allow *.css and image files files to get sent back without a login
            size_t n=strlen(file);
            if( (file[n-4]=='.' && file[n-3]=='c' && file[n-2]=='s' && file[n-1]=='s') || 
                (file[n-4]=='.' && file[n-3]=='j' && file[n-2]=='p' && file[n-1]=='g') ||
                (file[n-4]=='.' && file[n-3]=='p' && file[n-2]=='n' && file[n-1]=='g') || 
                (file[n-4]=='.' && file[n-3]=='i' && file[n-2]=='c' && file[n-1]=='o') ) {

                // If the file is found send it back, otherwise send back not-found-header
                // This means that this function will always succeed
                web_dispatch_httpget_staticfile(socket,dir,file,args,numargs,headers,login_token);

            } else {

                web_login_page(socket, headers->ismobile);

            }
        }
    }
    
_freeret:    
    free(args);
    return ret;
     
}

/**
 * This is the top function that receive the HTTP request header from the client
 * @param socket
 * @param req
 * @return 
 */

void
web_process_httprequest(const int socket, char *req) {
    struct http_reqheaders *headers = calloc(1,sizeof(struct http_reqheaders));    

    if( -1 == web_parse_httpreq(req, headers) ) {
        html_send_404header(socket);
    }
#ifdef EXTRA_WEB_DEBUG
        logmsg(LOG_DEBUG,"HTTP REQUEST:\n------------------------------------------------------------\n%s\n------------------------------------------------------------\n",req);
#endif
    
    // The first thing we need to do is to handle the login 
    // If the login is validated the login_token is set
    char login_token[80];
    web_validate_login(headers,login_token);
        
    if( -1 == web_exec_httpget(socket,headers,login_token) ) {
        web_main_page(socket, "", login_token, headers->ismobile);
    }

}
