/* =========================================================================
 * File:        BASE64ED.H
 * Description: Routines for doing base64 encoding and decoding. See
 *              RFC3548 for details (http://www.ietf.org/rfc/rfc3548.txt)
 *              Note: These routines are not necessarily written to achive
 *              maximum speed but rather maximum clarity to aviod nasty
 *              off by one bugs.
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

#ifndef BASE64ED_H
#define	BASE64ED_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Decode a buffer in base64 encoding
 * 
 * @param inbuff Input buffer
 * @param inbuff_len öength of input buffer
 * @param outbuff Result buffer
 * @param outbuff_maxlen maximum lngth of result buffer
 * @return -1 on error 0 on success
 */
int
base64decode(char *inbuff, const unsigned inbuff_len, char *outbuff, const int outbuff_maxlen) ;

/**
 * Encode a buffer into base64 encoding
 *
 * @param inbuff Input buffer
 * @param inbuff_len Length of input buffer
 * @param outbuff Result buffer
 * @param outbuff_maxlen maximum lngth of result buffer
 * @return -1 on error 0 on success
 */
int
base64encode(char *inbuff, const unsigned inbuff_len, char *outbuff, const int outbuff_maxlen);


#ifdef	__cplusplus
}
#endif

#endif	/* BASE64ED_H */

