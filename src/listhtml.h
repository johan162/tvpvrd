/* 
 * File:   listhtml.h
 * Author: ljp
 *
 * Created on February 27, 2012, 8:41 PM
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

