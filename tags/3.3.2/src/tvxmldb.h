/* =========================================================================
 * File:        TVXMLDB.H
 * Description: Module for reading/writing the database of all recordings
 *              to and from file. The database itself is stored in
 *              XML format in plain text files.  A RNG (CML) grammar for the
 *              database format can be found in the doc/ directory
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010,2011 Johan Persson
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

/*
 * DEFAULT_PREFIX string
 * String for default prefix when mangling names in recurring recordings.
 * This string will used as a separator between the base title/filename and
 * the suffix computed for a recurring recording.
 */
#define DEFAULT_PREFIX "_"
    
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
 * Write the XML DB file to the specified file name but follow
 * the current HTML encodation setting (i.e. possible tanslate
 * '<' to &lt; and so on.
 * @param filename
 * @return -1 on failure, 0 otherwise
 */
int
_writeXMLFileHTML(const int fd);

/**
 * Write the XML DB file to the specified file descriptor
 * @param fd
 * @return -1 on failure, 0 otherwise
 */
int _writeXMLFile(const int fd);

/**
 * Initialize the recording database. This is a plain text file in XML format.
 * The full structure of the DB is defined with an XML RNG (grammar) stored in the
 * document folder in the distribution.
 */
void
init_tvxmldb(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _TVXMLDB_H */

