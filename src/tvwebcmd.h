/* =========================================================================
 * File:        TVWEBCMD.H
 * Description: Handle the interface via a Web-browser
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id: tvwebui.h 655 2012-03-31 20:56:59Z johan162 $
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

void
init_web_cmds(void);

void
web_process_httprequest(const int socket, char *req);


#ifdef	__cplusplus
}
#endif

#endif	/* TVWEBCMD_H */

