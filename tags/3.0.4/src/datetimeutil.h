/* =========================================================================
 * File:        DATETIMEUTIL.H
 * Description: Functions to manipulate timestamps and do date/time calculations
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010,2011 Johan Persson
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


#ifndef DATETIMEUTIL_H
#define	DATETIMEUTIL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Utility function.
 * Create a timestamp from date and time
 */
time_t
totimestamp(const int year, const int month, const int day,
            const int hour, const int min, const int sec) ;

/*
 * Utility function.
 * Get date and time from a timestamp
 */
int
fromtimestamp(const time_t timestamp, int* year, int* month, int* day,
              int* hour, int* min, int* sec);
/*
 * Utility function
 * Increase start and end day as needed to get the next
 * time according to the recurrence type, day, week, monnth
 * and so on
 */
int
increcdays(int rectype, time_t *ts_start, time_t *ts_end,
           int *sy, int *sm, int *sd, int *sh, int *smin, int *ssec,
           int *ey, int *em, int *ed, int *eh, int *emin, int *esec);
   
/**
 * Translate a day name (e.g. mon, tue, wed, ...) to a date relative
 * the current date. The dates will always be within the next 7 days
 * The function also recognizes the two special day names "today" and
 * "tomomrrow" which should be self explanatory.
 * @param wdayname , Name of weekday. Recognized names are
 *                   ["mon","tue","wed","thu","fri","sat","sun","today","tomorrow"]
 * @param y The year
 * @param m The month
 * @param d The day
 * @return
 */
int
getreldatefromdayname(const char *wdayname, int *y, int *m, int *d);




#ifdef	__cplusplus
}
#endif

#endif	/* DATETIMEUTIL_H */

