/* =========================================================================
 * File:        WAKELAN.H
 * Description: Routines to wake up a server which have network support
 *              for LAN wakeup
 *
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Note:        Portions of this code taken from newsgroups.
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

/**
 * Parse a MAC address
 * @param mac The parsed MAC address
 * @param str The string to be parsed
 * @return 1 if the string was succesfully parsed
 */
int
parse_mac(unsigned char *mac, char *str);

int
wakelan(char *mac, char *target, int str_bport);

