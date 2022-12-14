/* =========================================================================
 * File:        LOCKFILE.H
 * Description: Functions to manage the lockfile used to avoid
 *              running to instances of the program at once.
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

#ifndef LOCKFILE_H
#define	LOCKFILE_H


#ifdef	__cplusplus
extern "C" {
#endif

#define LOCKFILE_DIR "/var/run"
/*
 * TVPVRD_LOCKFILE string
 * Full name for lockfile to use
 */
extern char lockfilename[] ;

/**
 * Delete exising lockfile (if we have the permission to do so)
 */
void
deleteockfile(void);

/**
 * Update the PID (Process ID) in the created file
 * @return
 */
int
updatelockfilepid(void);

/**
 * Create the PID file to remember that this
 * @return
 */
int
createlockfile(void);

/**
 * 
 */
void
setup_lockfile(void);

#ifdef	__cplusplus
}
#endif

#endif	/* LOCKFILE_H */

