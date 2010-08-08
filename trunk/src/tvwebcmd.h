/* =========================================================================
 * File:        TVWEBCMD.H
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


#ifndef TVWEBCMD_H
#define	TVWEBCMD_H

#ifdef	__cplusplus
extern "C" {
#endif


void
html_output(int sockd);

void
html_output_end(int sockd);

void
html_endpage(int sockd);

void
html_newpage(int sockd, char *cookie_val, int mobile);

void
html_topbanner(int sockd);

void
html_commandlist(int sockd);

void
html_cmdinterp(const int my_socket, char *buffer);

void
html_cmd_add_del(int sockd);

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


#ifdef	__cplusplus
}
#endif

#endif	/* TVWEBCMD_H */

