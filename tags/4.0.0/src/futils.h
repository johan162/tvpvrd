/* =========================================================================
 * File:        FUTILS.H
 * Description: A collection of file and directory utility functions.
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

#ifndef FUTILS_H
#define	FUTILS_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef int (*pfi_cu_callback)(char *,unsigned);

/**
 * Process a set of files in a given directory calling the 'callback' function
 * with the full path of each file and its sequence number starting from 0. 
 * @param dirbuff
 * @param suffix
 * @param maxfiles
 * @param numfiles
 * @param callback
 * @return 0 on success, -1 on failure
 */
int
process_files(const char *dirbuff, char *suffix, size_t maxfiles, 
              size_t *numfiles, pfi_cu_callback callback);

/*
 * Remove specified directory and all files and directories under it
 * It behaves similar to "rm -rf dir"
 */
int
removedir(const char *dir);

/*
 * Move and possibly rename the file if the target file already exists
 * The new name will be created by adding a numeric suffix to the base
 * name.
 */
int
mv_and_rename(char *from, char *to, char *newname, size_t maxlen);


/**
 * Check if a directory exists and if not then create it
 * @param basedir Basedirectory where the new directory will be created
 * @param dir The name of the directory to create
 */
int
chkcreatedir(const char *basedir,char *dir);

/**
 * Strip the suffix by replacing the last '.' with a '\0'
 * The found suffix is placed in the space pointed to by
 * the suffix parameter
 * @param filename
 */
int
strip_filesuffix(char *filename,char *suffix, int slen);

/**
 * Show the last n line of the logfile
 * @param n
 * @param buffer
 * @param maxlen
 * @return
 */
int
tail_logfile(unsigned n, char *buffer, size_t maxlen);



#ifdef	__cplusplus
}
#endif

#endif	/* FUTILS_H */

