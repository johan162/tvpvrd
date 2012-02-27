/* =========================================================================
 * File:        VCARD.H
 * Description: Some utility functions to setup the video card in preparation
 *              for capture
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

#ifndef VCARD_H
#define	VCARD_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Set the video encoding HW parameters on the video card from the values specified in the
 * supplied profile
 * @param fd
 * @param profile
 * @return 0 on success, -1 on failure
 */
int
setup_hw_parameters(int fd, struct transcoding_profile_entry *profile);

int
setup_video(unsigned video,struct transcoding_profile_entry *profile);

/**
 * Set the initial parameters for the TV-card so we know that they exist
 * and have a known state in case the profiles are not allowed to change
 * HW parameters.
 */
void
setup_capture_cards(void);


#ifdef	__cplusplus
}
#endif

#endif	/* VCARD_H */

