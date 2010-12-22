/* =========================================================================
 * File:        TVSHUTDOWN.H
 * Description: Routines for handling of automatic server shutdown
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2010 Johan Persson
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


#ifndef TVSHUTDOWN_H
#define	TVSHUTDOWN_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Check for possible automatic shutdown. If the conditions are met call the 
 * shutdown external script an initiate shutdown sequence.
 */
void
check_for_shutdown(void);


#ifdef	__cplusplus
}
#endif

#endif	/* TVSHUTDOWN_H */

