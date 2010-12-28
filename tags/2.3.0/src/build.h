/* =========================================================================
 * File:        BUILD.H
 * Description: Define linker external variable for build number handling
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


#ifndef BUILD_H
#define	BUILD_H

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * These are not real variables. These are defined as linker symbols and we use
 * there address as the value. In this way they will always be updated even if this
 * file itself is not recompiled
 */
extern char   __BUILD_DATE;
extern char   __BUILD_NUMBER;


#ifdef	__cplusplus
}
#endif

#endif	/* BUILD_H */

