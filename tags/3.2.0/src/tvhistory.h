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

#define HISTORYDB_FILENAME "history.xml"
#define HISTORY_LENGTH 20

/**
 * Update history file with name and full path. The actual name
 * of the history DB file is hardcoded to HISTORYDBFILENAME defined
 * above. The history file is stored in the same directory as the
 * DB file for recordings.
 * @return 0 on success, -1 on failure
 */

int
hist_update(char *title, const time_t ts_start, const time_t ts_end, char *fullPathFilename, char *profile);

/**
 * Initialize history from file
 */
void
hist_init(void);

/**
 * Put a formatted version to the specified stream
 * @param fd
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


#ifdef	__cplusplus
}
#endif

#endif	/* TVHISTORY_H */

