/* =========================================================================
 * File:        TVHTML.H
 * Description: HTML elements to create WEB-interface
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
#ifndef TVHTML_H
#define	TVHTML_H

#ifdef	__cplusplus
extern "C" {
#endif

#define TIME_RFC822_FORMAT "%a, %d %b %Y %T GMT"

#define LOGOUT_BUTTON_IMG "img/logout_button.jpg"

void
html_topbanner(int sockd);

void
html_cmd_output(int sockd, char *wcmd);

void
html_endpage(int sockd);

void
http_header(int sockd, char *cookie_val);

void
html_newpage(int sockd, char *cookie_val, int mobile);

void
html_windtitlebar(int sockd);

void
html_statusbar(int sockd);

void
html_element_select(int sockd, char *legend, char *name, char *selected, const char *list[], int num, char *id);

void
html_element_select_code(int sockd, char *legend, char *name, char *selected, const struct skeysval_t list[], int num, char *id);

void
_html_element_input_text(int sockd, char *legend, char *name, char *id, int passwd);

void
html_element_input_text(int sockd, char *legend, char *name, char *id);

void
html_element_input_password(int sockd, char *legend, char *name, char *id);

void
html_element_submit(int sockd, char *name, char *value, char *id);

void
html_element_submit_disabled(int sockd, char *name, char *value, char *id);

void
html_notfound(int sockd);

void
html_notmodified(int sockd);


#ifdef	__cplusplus
}
#endif

#endif	/* TVHTML_H */

