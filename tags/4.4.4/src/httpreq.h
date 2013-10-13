/* =========================================================================
 * File:        HTTPREQ.H
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


#ifndef HTTPREQ_H
#define	HTTPREQ_H

#ifdef	__cplusplus
extern "C" {
#endif
    
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

char *
create_login_cookie(char *user, char *pwd);

_Bool
validate_login(char *user, char *pwd);

int
web_validate_login(struct http_reqheaders *headers, char *login_token);

void
sendback_file(int sockd, char *filename, time_t modifiedSince);

/**
 * Parse the HTTP request from the browser and split it up in the header fields
 * supplied.
 * @param req
 * @param headers
 * @return 
 */
int
web_parse_httpreq(char *req, struct http_reqheaders *headers);

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
        size_t *numargs);


#ifdef	__cplusplus
}
#endif

#endif	/* HTTPREQ_H */

