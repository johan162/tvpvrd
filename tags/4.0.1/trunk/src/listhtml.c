/* =========================================================================
 * File:        LISTHTML.C
 * Description: Data structures and helper functions used to generate a
 *              CSS styled table. USed to generate pretty versions of the
 *              recording and history tables.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009-2012 Johan Persson
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <assert.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h> // Needed to get MIN()/MAX()
#include <time.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "listhtml.h"
#include "tvplog.h"

/**
 * Store a set of CSS styles in the style structure. The 'style' argument will control
 * which style is used.
 * @param ts
 * @param style
 * @return 0 (always)
 */
int
set_listhtmlcss(struct css_table_style *ts, size_t style) {
    
    if( style > 0 ) {
        logmsg(LOG_DEBUG,"Unknown style (%u) specified in set_listhtmlcss()",style);
        style=0;
    }

    bzero(ts,sizeof(struct css_table_style));

    snprintf(ts->even_row.td_i,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_even_row_background,tblcss_vborder_interior, tblcss_hborder_interior);
    snprintf(ts->even_row.td_l,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_even_row_background,tblcss_vborder_outer, tblcss_hborder_interior);
    snprintf(ts->even_row.td_r,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-right:%s;border-bottom:%s;",tblcss_even_row_background, tblcss_vborder_interior, tblcss_vborder_outer, tblcss_hborder_interior);
    snprintf(ts->even_row.tr,MAX_TBLCSS_SIZE,"font-family:%s;",tblcss_fontfamily);

    snprintf(ts->odd_row.td_i,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_odd_row_background,tblcss_vborder_interior, tblcss_hborder_interior);
    snprintf(ts->odd_row.td_l,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_odd_row_background,tblcss_vborder_outer, tblcss_hborder_interior);
    snprintf(ts->odd_row.td_r,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-right:%s;border-bottom:%s;",tblcss_odd_row_background, tblcss_vborder_interior, tblcss_vborder_outer, tblcss_hborder_interior);
    snprintf(ts->odd_row.tr,MAX_TBLCSS_SIZE,"font-family:%s;",tblcss_fontfamily);

    snprintf(ts->header_row.td_i,MAX_TBLCSS_SIZE,"color:%s;background:%s;border-left:%s;border-bottom:%s;border-top:%s;",tblcss_header_textcolor, tblcss_header_background, tblcss_vborder_interior, tblcss_hborder_interior, tblcss_hborder_outer);
    snprintf(ts->header_row.td_l,MAX_TBLCSS_SIZE,"color:%s;background:%s;border-left:%s;border-bottom:%s;border-top:%s;",tblcss_header_textcolor, tblcss_header_background, tblcss_vborder_outer, tblcss_hborder_interior, tblcss_hborder_outer);
    snprintf(ts->header_row.td_r,MAX_TBLCSS_SIZE,"color:%s;background:%s;border-left:%s;border-right:%s;border-bottom:%s;border-top:%s;",tblcss_header_textcolor, tblcss_header_background , tblcss_vborder_interior, tblcss_vborder_outer, tblcss_hborder_interior, tblcss_hborder_outer);
    snprintf(ts->header_row.tr,MAX_TBLCSS_SIZE,"font-family:%s;",tblcss_header_fontfamily);

    snprintf(ts->last_even_row.td_i,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_even_row_background,tblcss_vborder_interior, tblcss_hborder_outer);
    snprintf(ts->last_even_row.td_l,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_even_row_background,tblcss_vborder_outer, tblcss_hborder_outer);
    snprintf(ts->last_even_row.td_r,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-right:%s;border-bottom:%s;",tblcss_even_row_background, tblcss_vborder_interior, tblcss_vborder_outer, tblcss_hborder_outer);
    snprintf(ts->last_even_row.tr,MAX_TBLCSS_SIZE,"font-family:%s;",tblcss_fontfamily);

    snprintf(ts->last_odd_row.td_i,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_odd_row_background,tblcss_vborder_interior, tblcss_hborder_outer);
    snprintf(ts->last_odd_row.td_l,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-bottom:%s;",tblcss_odd_row_background,tblcss_vborder_outer, tblcss_hborder_outer);
    snprintf(ts->last_odd_row.td_r,MAX_TBLCSS_SIZE,"background:%s;border-left:%s;border-right:%s;border-bottom:%s;",tblcss_odd_row_background, tblcss_vborder_interior, tblcss_vborder_outer, tblcss_hborder_outer);
    snprintf(ts->last_odd_row.tr,MAX_TBLCSS_SIZE,"font-family:%s;",tblcss_fontfamily);

    snprintf(ts->table,MAX_TBLCSS_SIZE,"%s",tblcss_table);
    snprintf(ts->date,MAX_TBLCSS_SIZE,"%s",tblcss_date);

    return 0;
}


/*
 EOF
 */