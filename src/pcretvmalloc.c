/* =========================================================================
 * File:        PCRETVMALLOC.C
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
// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <syslog.h>
#include <errno.h>

#include "utils.h"
#include "pcretvmalloc.h"
#include "tvplog.h"

/*
 * The following memory routines are just used to double check that
 * all the calls to PCRE regep routines are matched with respect to
 * memory allocation.
 */

int pcre_mem_count=0;


struct tvp_mem_entry *pcre_mem_list = (void *)NULL;
int tvp_call_count=0;

void *
tvp_malloc(size_t size) {
    struct tvp_mem_entry *ent = calloc(1,sizeof(struct tvp_mem_entry));
    ent->size=size;
    ent->ptr=malloc(size);

    struct tvp_mem_entry *walk = pcre_mem_list;

    if( walk == NULL ) {
        pcre_mem_list = ent;
    } else {
        while( walk->next ) {
            walk = walk->next;
        }
        walk->next = ent;
    }
    tvp_call_count++;
    //logmsg(LOG_DEBUG,"PCRE MALLOC: %06d bytes",size);
    return ent->ptr;
}

void
tvp_free(void *ptr) {
    struct tvp_mem_entry *walk = pcre_mem_list;
    struct tvp_mem_entry *prev = NULL;

    while( walk && walk->ptr != ptr ) {
        prev = walk;
        walk = walk->next;
    }
    if( walk == NULL ) {
        logmsg(LOG_CRIT,"FATAL: Trying to deallocat PCRE without previous allocation !");
    } else {
        if( prev == NULL ) {
            // First entry in list
            pcre_mem_list = walk->next;
            //logmsg(LOG_DEBUG,"PCRE FREE: %06d bytes",walk->size);
            free(walk->ptr);
            free(walk);
        } else {
            prev->next = walk->next;
            //logmsg(LOG_DEBUG,"PCRE FREE: %06d bytes",walk->size);
            free(walk->ptr);
            free(walk);
        }
    }
    tvp_call_count--;
}

void
tvp_mem_list(int sockd) {
    struct tvp_mem_entry *walk = pcre_mem_list;
    int n=0;
    _writef(sockd,"PCRE MALLOC List: %02d\n",tvp_call_count);
    while( walk ) {
        ++n;
        _writef(sockd,"  #%0002d: size = %06d bytes\n",n,walk->size);
        walk = walk->next;
    }
}

