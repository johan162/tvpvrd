/* =========================================================================
 * File:        PCRETVMALLOC.H
 * Description: Traced memory allocation/deallocation for PCRE library
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


#ifndef PCREMALLOC_H
#define	PCREMALLOC_H

#ifdef	__cplusplus
extern "C" {
#endif

struct tvp_mem_entry {
    void *ptr;
    size_t size;
    struct tvp_mem_entry *next;
};

void *
tvp_malloc(size_t size);

void
tvp_free(void *ptr);

void
tvp_mem_list(int sockd);


#ifdef	__cplusplus
}
#endif

#endif	/* PCREMALLOC_H */

