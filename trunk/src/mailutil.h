/* =========================================================================
 * File:        MAILUTIL.C
 * Description: Functions to send mail using the command line mail (1)
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

#ifndef MAILUTIL_H
#define	MAILUTIL_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Send mail to address
 * @param subject
 * @param from
 * @param to
 * @param message
 * @return -1 on failure 0 on success
 */
int
send_mail(const char *subject, const char *from, const char *to, const char *message);

#ifdef	__cplusplus
}
#endif

#endif	/* MAILUTIL_H */

