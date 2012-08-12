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
_Bool
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
char *
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
    
    // In case the browser gone mad
    if( '\0' == *headers->Get ) {
        ret = -1;
    }
    
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
    
    if( NULL == str ) {
        logmsg(LOG_CRIT,"NULL pointer passed to web_parse_httpget (numargs=%zu)",numargs);
        if( file ) {
            logmsg(LOG_CRIT,"file=%s",file);
        }
        if( dir ) {
            logmsg(LOG_CRIT,"dir=%s",dir);
        }
        return -1;
    }
    
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
                while( ep < len-9  && s[ep]!='&' ) ++ep;
                
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

#ifdef EXTRA_WEB_DEBUG    
    logmsg(LOG_DEBUG,"Looking for '%s' in cookie header: '%s'",name,c);
#endif
    
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
        
#ifdef EXTRA_WEB_DEBUG    
    logmsg(LOG_DEBUG,"Checking cookie '%s' in header.",cname);
#endif            
        
        if( 0==strncmp(cname,name,sizeof(cname)) ) {
            xsubstr(val,maxlen,c,eq+1, (c[e]=='\0' ? (size_t)-1 : e-1) );

#ifdef EXTRA_WEB_DEBUG    
    logmsg(LOG_DEBUG,"Found cookie '%s' = '%s'",cname,val);
#endif            
            
            return 0;
        }
        s = c[e] ? e+2 : e;     
    }
#ifdef EXTRA_WEB_DEBUG    
    logmsg(LOG_DEBUG,"Cookie '%s' NOT found in header",name);
#endif    
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

