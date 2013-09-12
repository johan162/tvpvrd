/* =========================================================================
 * File:        LISTHTML.H
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

#ifndef LISTHTML_H
#define	LISTHTML_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Holds the CSS style to be used when generating a list of recordings in HTML
 * format for one row.
 */
#define MAX_TBLCSS_SIZE 255
    
struct css_record_style {
    char tr[MAX_TBLCSS_SIZE];
    char td_l[MAX_TBLCSS_SIZE];
    char td_i[MAX_TBLCSS_SIZE];
    char td_r[MAX_TBLCSS_SIZE];
};

/*
 * Holds the CSS style to be used when generating a list of recordings in HTML
 * format for the entire table.
 */
struct css_table_style {
    struct css_record_style header_row;
    struct css_record_style even_row;
    struct css_record_style odd_row;
    struct css_record_style first_row;
    struct css_record_style last_odd_row;
    struct css_record_style last_even_row;
    char table[MAX_TBLCSS_SIZE];
    char date[MAX_TBLCSS_SIZE];
};

/**
 * Store the CSS style elements in the css_table_style according
 * to the style argument. Currently only one style is supported
 * with style==0
 * @param ts    Structure to be filled with CSS elements
 * @param style Selected style
 * @return 0 on sucess, -1 on failure
 */
int
set_listhtmlcss(struct css_table_style *ts, size_t style);


#ifdef	__cplusplus
}
#endif

#endif	/* LISTHTML_H */

