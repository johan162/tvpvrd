/* =========================================================================
 * File:        TVXMLDB.H
 * Description: Module for reading/writing the database of all recordings
 *              to and from file. The database itself is stored in
 *              XML format in plain text files.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id: tvxmldb.h 124 2009-11-16 20:07:27Z ljp $
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

#ifndef _TVXMLDB_H
#define	_TVXMLDB_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Read the XML DB from eth sepcified file
 * @param filename
 * @return -1 on failure, 0 otherwise
 */
int
readXMLFile(const char *filename);

/**
 * Write the XML DB file to the specified file name
 * @param filename
 * @return -1 on failure, 0 otherwise
 */
int writeXMLFile(const char *filename);

/**
 * Write the XML DB file to the specified file descriptor
 * @param fd
 * @return -1 on failure, 0 otherwise
 */
int _writeXMLFile(const int fd);


#ifdef	__cplusplus
}
#endif

#endif	/* _TVXMLDB_H */

