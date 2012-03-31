/* =========================================================================
 * File:        HTTPREQ.C
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


// Get the name of the CSS file from the basename of the package, i.e. "tvpvrd"
#define CSSFILE_BASENAME PACKAGE_TARNAME

#define MAX_HTTP_HEADERS 25
struct http_reqheaders {
    char *Get;
    char *Post;
    char *Cookie;
    char *IfModifiedSince;
    char *UserAgent;
    char *Host;
    struct keypair_t headers[MAX_HTTP_HEADERS];
    size_t num;
    _Bool ismobile;
};

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
 * http://localhost:9301/cmd?c=l
 *
 * The above URL assumes that the server listens on port 9301 as is the default in
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
 * Read a file from our "WEB-root" directory 
 * @param buff Buffer where the file is read into
 * @param maxlen Maximum length of file buffer
 * @return 0 on not modified, 1 if modified , -1 on failure, 
 */
int
read_webroot_file(char *file_buffer, size_t maxlen, size_t *actualfilelen, char *filename, time_t modifiedSince) {
    char full_filename[255];

    snprintf(full_filename, sizeof(full_filename)-1, "%s/tvpvrd/www%s", CONFDIR, filename);
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
    strftime(ftime, sizeof(ftime)-1, TIME_RFC822_FORMAT, &t_tm);
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
    logmsg(LOG_DEBUG, "HTTP Header sent back (printed without \\r):\n"
            "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"
            "HTTP/1.1 200 OK\n"
            "Date: %s\n"
            "Last-Modified: %s\n"
            "Server: %s\n"
            "Connection: close\n"
            "Content-Length: %zu\n"
            "Content-Type: %s\n"
            "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"
            , ftime, ftime, server_id, buffer_len, mime_type);
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
        } else if( 0==strncmp(&filename[n+1],"ico",4) ) {
            strncpy(mimetype,"image/x-icon",maxlen);            
        } else if( 0==strncmp(&filename[n+1],"html",5) ) {
            strncpy(mimetype,"text/html",maxlen); 
        } else if( 0==strncmp(&filename[n+1],"pdf",4) ) {
            strncpy(mimetype,"application/pdf",maxlen);
        } else if( 0==strncmp(&filename[n+1],"xml",4) ) {
            strncpy(mimetype,"text/xml",maxlen);             
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
#ifdef EXTRA_WEB_DEBUG            
            logmsg(LOG_CRIT,"Out of memory when sending back file!! '%s'",filename);    
#endif            
            html_send_404header(sockd);        
        } else {
            size_t actualsize=0;
            int ret=read_webroot_file(filebuffer,maxfilesize,&actualsize,filename,modifiedSince);
            if( 1==ret ) {
                sendback_http200_file(sockd, filebuffer, actualsize, mimetype);
                logmsg(LOG_DEBUG,"Sent back file '%s' as mime type '%s'",filename,mimetype);
            } else if( 0==ret) {
                // Sendback not modified header
                html_send_304header(sockd);
            } else {
                // Sendback not found header
                html_send_404header(sockd);
            }
            free(filebuffer);        
        }
    }
}

/* 
 * Try to determine if the connection is from a mobile phone by examine the headers.
 * If it is a mobile we use a different set of CSS formatting to make it more
 * suitable for a mobiles smaller screen.
 */
int
is_mobile_connection(struct http_reqheaders *headers) {

    if( !use_mobile )
        return FALSE;

    char *wap;
    if( 0==get_assoc_value_s(headers->headers,headers->num,"X-Wap-Profile",&wap) ) {
        return TRUE;
    }
    
    char **field = (void *) NULL;
    if (matchcmd("(mobile|Nokia|HTC|Android|SonyEricsson|LG|Samsung|blac|moto|doco|java|symb)", headers->UserAgent, &field) > 0) {
        matchcmd_free(&field);
        return TRUE;
    }
    
    return FALSE;
}

/**
 * Parse the HTTP request from the browser and split it up in the header fields
 * supplied.
 * @param req
 * @param headers
 * @return 
 */
int
web_parse_httpreq(char *req, struct http_reqheaders *headers) {
    // We extract the following possible fields from the HTTP request header
    // GET <path>
    int ret = 0;
    char *row;
    const size_t rowsize=4096;
    row = calloc(rowsize,sizeof(char));
    
    // First read the method line. This will be one of Get, Post, Head, ryc.
    // This u-server only really supports Get and Post methods
    size_t i=0,j=0;
    while( i < rowsize && req[i] && req[i]!='\r' && req[i+1] && req[i+1]!='\n' ) {
        row[j++] = req[i++];        
    }
    if( i>=rowsize || req[i]=='\0' || req[i+1]=='\0' || req[i]!='\r' || req[i+1]!='\n') {
        ret = -1;        
        goto _httpreq_freeret;
    }
    i+=2;    
    row[j]='\0';
    xstrtrim(row);
    
    headers->num=0;
    while( *row ) {
        
        // Extract the name of the field from the current row
        size_t k=0;
        const size_t fieldnamelen = 80;       
        while( k < fieldnamelen && row[k] && row[k]!=' ' && row[k]!=':' ) k++;
        if( k>=fieldnamelen ) {
            ret = -1;
            goto _httpreq_freeret;
        }
        xsubstr(headers->headers[headers->num].key,fieldnamelen,row,0,k-1);
        xsubstr(headers->headers[headers->num].val,1024,row,row[k] == ':' ? k+2 : k+1, -1);
        url_decode_inplace(headers->headers[headers->num].val,sizeof(headers->headers[headers->num].val));
        headers->num++;
        
        j=0;
        while( i < rowsize && req[i] && req[i]!='\r' && req[i+1] && req[i+1]!='\n' ) {
            row[j++] = req[i++];        
        }
        if( i>=rowsize || req[i]=='\0' || req[i+1]=='\0' || req[i]!='\r' || req[i+1]!='\n') {
            goto _httpreq_freeret;
            ret = -1;
        }
        i+=2;    
        row[j]='\0';
        xstrtrim(row);
    }
    
    // Now set some shortcuts to most often used fields
    get_assoc_value_s(headers->headers,headers->num,"GET",&headers->Get);
    get_assoc_value_s(headers->headers,headers->num,"Cookie",&headers->Cookie);
    get_assoc_value_s(headers->headers,headers->num,"If-Modified-Since",&headers->IfModifiedSince);
    get_assoc_value_s(headers->headers,headers->num,"User-Agent",&headers->UserAgent);
    get_assoc_value_s(headers->headers,headers->num,"Post",&headers->Post);
    get_assoc_value_s(headers->headers,headers->num,"Host",&headers->Host);
    
    // Check if this is an access from a mobile phone by checking the user-agent string    
    headers->ismobile = is_mobile_connection(headers);
    
_httpreq_freeret:
    free(row);
    return ret;
    
}

/**
 * Parse a HTTP GET command
 * @param str
 * @param dir
 * @param file
 * @param args
 * @param numargs
 * @return -1 on failure, 0 on success
 */
int
web_parse_httpget(char* str, 
        char *dir, size_t maxdirlen, 
        char *file, size_t maxfilelen, 
        struct keypair_t args[], size_t maxargs, 
        size_t *numargs) {
    
    const char SPACE = ' ';
    *numargs=0;
        
    // The last letters must be a HTTP version, for example " HTTP/1.1"
    const size_t n = strnlen(str,512);
    if( !(isdigit(str[n-1]) && str[n-2]=='.' && isdigit(str[n-3]) &&
          str[n-4] == '/' && 
          str[n-5] == 'P' && str[n-6] == 'T' && str[n-7] == 'T' && str[n-8] == 'H' && str[n-9] == ' ' ) ) {
        return -1;
    }
    
    char *s=strdup(str);
    size_t len = strlen(s);
    // The string after the GET command will be a URL with the DNS name removed and the first
    // character being the '/' which indicates the web root
    // The rest of the string can be a simple filename (e.g. "GET /readme.txt HTTP/1.1")
    // but it could also be a file with parameters, as in "GET /side.php?a=3&b=45&c=19 HTTP/1.1"
    
    // Extract the substring up to ' ' or '?' whichever comes first
    size_t i = 0;
    while( i < n && s[i] != SPACE && s[i] != '?' ) ++i;    
    if( i==n ) return -1;
    int ret=0;
    
    if( s[i] == SPACE || s[i] == '?') {
        char *fbuff = calloc(len+1,sizeof(char));    

        if( -1 == xsubstr(fbuff,len,s,0,i-1) ) {
            free(fbuff);
            ret=-1;
        } else {

            // Find the dir/basename
            const size_t flen=strlen(fbuff);
            size_t j=flen-1;
            while( j>0 && fbuff[j] != '/' ) j--;

            xsubstr(file,maxfilelen,fbuff,j+1,-1);
            
            if( 0==j ) {
                dir[0]='/';
                dir[1]='\0';
            } else {
                xsubstr(dir,maxdirlen,fbuff,0,j-1);
            }

            free(fbuff);            
        }
        
        if( 0==ret && s[i] == '?') {
            // Extract all parameters
            size_t sp=i,ep=i+1;
            char tmpval[255];
            *numargs = 0;
            s[sp] = '&';
            while( 0==ret && s[sp]=='&' ) {
                while( ep < len  && s[ep]!='&' && s[ep]!=SPACE ) ++ep;
                
                size_t j=sp+1;
                while( j < ep && s[j]!='=') ++j;
                if( j >= ep ) {
                    ret=-1;
                } else {
                    // key is sp+1 to j-1
                    // val is j+1 to ep-1
                    if( *numargs < maxargs ) {
                        xsubstr(tmpval,255,s,sp+1,j-1);
                        url_decode_buff(args[*numargs].key,sizeof(args[*numargs].key),tmpval);
                        xsubstr(tmpval,255,s,j+1,ep-1);
                        url_decode_buff(args[*numargs].val,sizeof(args[*numargs].val),tmpval);
                        (*numargs)++;
                    } else {
                        ret=-1;
                    }
                    sp = ep++;                       
                }
            }
        }
        
    } else {
        // Should never happen!
        ret = -1;
    }
    free(s);
    return ret;
}

int
_web_cmd_logout(int socket,struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    logmsg(LOG_DEBUG,"cmd_logout: sock=%d, numargs=%d\n",socket,numargs);
    
    (void)args; // To shut up warning about non used arguments
    (void)login_token;
    
    web_login_page(socket,headers->ismobile);
    return 0;    
}

int
_web_cmd_login(int socket,struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    logmsg(LOG_DEBUG,"cmd_login: sock=%d, numargs=%d\n",socket,numargs);
    
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
    
    // Login successful. Show the main page and use the "version" command
    // as the default command to show in the output area.
    web_main_page(socket, "v", create_login_cookie(user, pwd), headers->ismobile);

    return 0;
}

int
_web_cmd_addrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *repeat, *repeatcount, *channel;
    char *sd,*sh,*smin;
    char *ed,*eh,*emin;
    char *profile,*title,*submit;
    char cmdstr[255];
   

    if( -1 == get_assoc_value_s(args,numargs,"repeat",&repeat) ||
        -1 == get_assoc_value_s(args,numargs,"repeatcount",&repeatcount) ||   
        -1 == get_assoc_value_s(args,numargs,"channel", &channel) ||
        -1 == get_assoc_value_s(args,numargs,"start_day",&sd) ||
        -1 == get_assoc_value_s(args,numargs,"start_hour",&sh) ||
        -1 == get_assoc_value_s(args,numargs,"start_min",&smin) ||
        -1 == get_assoc_value_s(args,numargs,"end_day",&ed) ||
        -1 == get_assoc_value_s(args,numargs,"end_hour",&eh) ||
        -1 == get_assoc_value_s(args,numargs,"end_min",&emin) ||
        -1 == get_assoc_value_s(args,numargs,"profile",&profile) ||
        -1 == get_assoc_value_s(args,numargs,"title",&title) ||
        -1 == get_assoc_value_s(args,numargs,"submit_addrec",&submit) ||
        strcmp(submit, "Add") ) {
        
        return -1;
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

    web_main_page(socket, tmpcmd, login_token, headers->ismobile);
    return 0;
        
}

// (int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token)

int
_web_cmd_delrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *recid, *submit, *delserie;
    
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

    web_main_page(socket, tmpcmd, login_token, headers->ismobile);
    return 0;
}

int
_web_cmd_chwt(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *wtheme;
    
    if( -1 == get_assoc_value_s(args,numargs,"t",&wtheme) ) {
        return -1;
    }
    
    strncpy(web_theme,wtheme,sizeof(web_theme)-1);
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

    char *recid, *submit;
    if( -1 == get_assoc_value_s(args,numargs,"recid",&recid) || 
        -1 == get_assoc_value_s(args,numargs,"submit_killrec",&submit) ) {
        
        return -1;
    }
    
    char tmpcmd[128];
    snprintf(tmpcmd, sizeof(tmpcmd)-1, "! %s", recid);
    web_main_page(socket, tmpcmd, login_token, headers->ismobile);
    return 0;
}


int
_web_cmd_addqrec(int socket, struct keypair_t *args, const size_t numargs, struct http_reqheaders *headers,char *login_token) {
    char *channel;
    char *length_hour, *length_min;
    char *profile, *title, *submit;

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

typedef int (*pwf_t)(int,struct keypair_t *args, const size_t, struct http_reqheaders *,char *);

struct web_cmds_t {
    char path[80];
    char name[80];
    size_t numargs;    
    pwf_t cmdfunc;
};

// The array index for the login command
#define LOGIN_CMDIDX 0

struct web_cmds_t web_cmds[] = {
    {"/","login",0,_web_cmd_login},
    {"/","logout",0,_web_cmd_logout},
    {"/","addrec",12,_web_cmd_addrec},
    {"/","addqreq",6,_web_cmd_addqrec},
    {"/","delreq",3,_web_cmd_delrec},
    {"/","chwt",1,_web_cmd_chwt},
    {"/","killrec",2,_web_cmd_killrec},
    {"/","cmd",1,_web_cmd_command},
    {"/","",0,_web_cmd_default},
    
    /* Sentinel */
    {"","",0,NULL}
};

/**
 * Store the value of the named cookie in the val field
 * @param name
 * @param val
 * @param maxlen
 * @param headers
 * @return -1 if not found, 0 if found
 */
int
web_get_cookie(char *name, char *val, size_t maxlen, struct http_reqheaders *headers) {
    char *c = headers->Cookie;
    
    if( c==NULL ) {
        return -1; // No cookies in header
    }
    
    char cname[255];
    // Cookies are stored as ';' separated key=val pairs in the cookie field
    size_t s=0,e=0,eq=0;
    while( c[s] ) {
        e=s;
        eq=0;
        while( c[e] && c[e] != ';' ) {
            if( '=' == c[e]) eq=e;
            e++;
        }     
        // There must be an '=' sign
        if( 0 == eq) return -1;
        xsubstr(cname,sizeof(cname),c,s,eq-1);
        if( 0==strncmp(cname,name,sizeof(cname)) ) {
            xsubstr(val,maxlen,c,eq+1, (c[e]=='\0' ? (size_t)-1 : e-1) );
            return 0;
        }
        s = c[e] ? e+1 : e;     
    }
    return -1;
}

int
web_validate_login(struct http_reqheaders *headers, char *login_token) {
    
    // 1. First check if the client sent back a valid cookie
    // 2. If not, check if the client just logged in
    
    char cookie[80];
    *login_token = '\0';
    if( 0 == web_get_cookie("tvpvrd",cookie,sizeof(cookie),headers) ) {
        if( validate_cookie(cookie) ) {
            strncpy(login_token,cookie,sizeof(cookie)-1);
            return 0; 
        }
    }
    return -1;
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
        // Set the date a month back to force a resend of the file in case the header can not be parsed.
        sendback_file(socket, fullfilename, time(NULL) - 3600 * 24 * 30);
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
       
    if( *login_token ) {
        // First try if this is any of the predefined commands
        if( -1==web_dispatch_httpget_cmd(socket,dir,file,args,numargs,headers,login_token) ) {

            // If the file is found send it back, otherwise send back not-found-header
            // This means that this function will always succeed
            web_dispatch_httpget_staticfile(socket,dir,file,args,numargs,headers,login_token);

        }
    } else {
        // First check if this is a try to login
        if( 0 == strcmp(file,web_cmds[LOGIN_CMDIDX].name) ) {
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
