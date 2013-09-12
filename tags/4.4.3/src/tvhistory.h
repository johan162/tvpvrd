/* =========================================================================
 * File:        TVHISTORY.H
 * Description: Module to handle history recording of made recordings
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2011 Johan Persson
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


#ifndef TVHISTORY_H
#define	TVHISTORY_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * The history file itself is stored as a plain XML file in file HISTORYDB_FILENAME
 * The XSL file is a simple transformation file written in XSL to which is used to format
 * the XML file when viewed in a browser. The XSL file is stored in the config file directory
 * and referenced from the XML file. One other possibility would be to use an XSL processor
 * e.g. "xsltproc" to produce a HTML version of the XML file using the XSL. For example as
 * $> xsltproc /etc/tvpvrd/xsl/history.xsl history.xml > history.html
 * 
*/
#define HISTORYDB_FILENAME "history.xml"
#define HISTORY_XSL_FILENAME "history.xsl"
#define HISTORY_LENGTH 99

/**
 * Update history file with a new recording. The actual name
 * of the history DB file is hard coded to HISTORYDB_FILENAME defined
 * above. The history file is stored in the same directory as the
 * DB file for recordings.
 * 
 * @param title Title of recording
 * @param ts_start Start timestamp
 * @param ts_end End timestamp
 * @param fullPathFilename Full path to encoded file
 * @param profile Name of profile used for encoding
 * @return 0 on success, -1 on failure
 */   
int
hist_addrec(char *title, const time_t ts_start, const time_t ts_end, char *fullPathFilename, char *profile);

/**
 * Initialize history from file
 */
void
hist_init(void);

/**
 * Put a formatted version to the specified stream
 * @param fd stream descriptor
 * @return 0 on success, -1 on failure
 */
int
hist_list(int fd);

/**
 * Put a formatted version of the history list in the supplied buffer
 * @param buff
 * @param maxlen
 * @return 0 on success, -1 on failure
 */
int
hist_listbuff(char *buff, size_t maxlen);

/**
 * Mail the history to the predefined address given in the tvpvrd config file
 * by the user.
 */
int
hist_mail(void);

#ifdef	__cplusplus
}
#endif

#endif	/* TVHISTORY_H */

