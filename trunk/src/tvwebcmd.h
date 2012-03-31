/* =========================================================================
 * File:        TVWEBCMD.H
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


#ifndef TVWEBCMD_H
#define	TVWEBCMD_H

#ifdef	__cplusplus
extern "C" {
#endif

extern int cmd_delay;

void
web_commandlist(int sockd);

void
web_process_httprequest(const int socket, char *req);

void
web_cmd_add(int sockd);

void
web_cmd_del(int sockd);

void
web_cmd_qadd(int sockd);

int
read_cssfile(char *buff, int maxlen, char *css_filename, time_t modifiedSince);

void
web_cmd_ongoingtransc(int sockd);

void
web_commandlist_short(int sockd);

void
web_cmd_ongoing(int sockd);

void
web_cmd_next(int sockd);

/**
 * This test function is called when the server receives a new conection and
 * determines if the first command is a GET string. This is then an indication
 * that this is a WEB-browser calling us. The command given is stored in the
 * buffer posinted to by the second argument.
 * @param buffer
 * @param cmd
 * @param maxlen
 * @return 1 on succesfull command parsing, 0 otherwise
 */
int
webconnection(const char *buffer, char *cmd, int maxlen);


void
web_main_page(int sockd, char *wcmd, char *cookie_val, int mobile);

void
web_main_page_mobile(int sockd, char *wcmd, char *cookie_val);

void
web_login_page(int sockd, int mobile);


#ifdef	__cplusplus
}
#endif

#endif	/* TVWEBCMD_H */

