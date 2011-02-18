/* =========================================================================
 * File:        DATETIMEUTIL.C
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


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <string.h>

#include "datetimeutil.h"
#include "utils.h"
#include "xstr.h"


/*
 * Utility function.
 * Create a timestamp from date and time
 */
time_t
totimestamp(const int year, const int month, const int day,
            const int hour, const int min, const int sec) {

    time_t timestamp;
    struct tm time_struc;

    time_struc.tm_sec = sec;
    time_struc.tm_min = min;
    time_struc.tm_hour = hour;
    time_struc.tm_mday = day;
    time_struc.tm_mon = month - 1;
    time_struc.tm_year = year - 1900;
    time_struc.tm_isdst = -1;

    timestamp = mktime(&time_struc);

    if (timestamp == -1) {
        logmsg(LOG_ERR, "totimestamp() : Cannot convert tm to timestamp (%d : %s)",
               errno,strerror(errno));
        return -1;
    }

    return timestamp;
}

/*
 * Utility function.
 * Get date and time from a timestamp
 */
int
fromtimestamp(const time_t timestamp, int* year, int* month, int* day,
        int* hour, int* min, int* sec) {

    struct tm time_struc;
    if( NULL == localtime_r(&timestamp,&time_struc) ) {
        logmsg(LOG_ERR,"fromtimestamp() : Cannot convert timestamp (%d : %s)",
               errno,strerror(errno));
        return -1;
    }

    *year = time_struc.tm_year + 1900;
    *month = time_struc.tm_mon + 1;
    *day = time_struc.tm_mday;
    *hour = time_struc.tm_hour;
    *min = time_struc.tm_min;
    *sec = time_struc.tm_sec;

    return 0;
}

/**
 * Get a realtive day specified by a weekday name. This will return a date
 * within the following seven days. If the weekdat name is the same as the
 * current day this will then refer to the next such day within the next
 * seven days.
 * @param wdayname
 * @param y
 * @param m
 * @param d
 * @return
 */
int
getreldatefromdayname(const char *wdayname, int *y, int *m, int *d) {
    const char *wday[] = {
        "sun","mon","tue","wed","thu","fri","sat"
    };
    int yy,mm,dd,hh,min,sec;
    int i=0,step=0;

    fromtimestamp(time(NULL), &yy, &mm, &dd, &hh, &min, &sec);

    if( xstricmp("today",wdayname) == 0 || xstricmp("tod",wdayname) == 0 ) {
        *y = yy;
        *m = mm;
        *d = dd;
        return 0;
    }

    if( xstricmp("tomorrow",wdayname) == 0 || xstricmp("tom",wdayname) == 0 ) {
        dd++;
        time_t tom = totimestamp(yy, mm, dd, hh, min, sec);
        fromtimestamp(tom, &yy, &mm, &dd, &hh, &min, &sec);
        *y = yy;
        *m = mm;
        *d = dd;
        return 0;
    }

    struct tm tm_now;
    tm_now.tm_sec = sec;
    tm_now.tm_min = min;
    tm_now.tm_hour = hh;
    tm_now.tm_mday = dd;
    tm_now.tm_mon = mm - 1;
    tm_now.tm_year = yy - 1900;
    tm_now.tm_isdst = -1;
    mktime(&tm_now);

    for(i=0; i < 7; i++) {
        if( xstricmp(wday[i], wdayname) == 0) {
            if( tm_now.tm_wday > i ) {
                step = (7-tm_now.tm_wday ) + i;
            } else if(tm_now.tm_wday < i)  {
                step = i - tm_now.tm_wday ;
            } else {
                step = 7;
            }
            break;
        }
    }

    if( i >= 7 ) {
        logmsg(LOG_ERR,"getreldatefromdayname() : Unknown dayname '%s'",wdayname);
        return -1;
    }

    tm_now.tm_mday += step;
    mktime(&tm_now);
    *y = tm_now.tm_year+1900;
    *m = tm_now.tm_mon+1;
    *d = tm_now.tm_mday;

    return 0;
}

/*
 * Utility function
 * Increase start and end day as needed to get the next
 * time according to the recurrence type, day, week, monnth
 * and so on
 */
int
increcdays(int rectype,
        time_t *ts_start, time_t *ts_end,
        int *sy, int *sm, int *sd, int *sh, int *smin, int *ssec,
        int *ey, int *em, int *ed, int *eh, int *emin, int *esec) {
    struct tm tm_start;

    // Find out the new date for the next recording in sequence
    switch (rectype) {
        case 0:
            // Single, do nothing
            break;

        case 1:
            // Every day
            *sd += 1;
            *ed += 1;
            break;

        case 2:
            // Every week
            *sd += 7;
            *ed += 7;
            break;

        case 3:
            // Every month
            sm++;
            em++;
            break;

        case 4:
            // Mon-Fri
            do {
                *sd += 1;
                *ed += 1;
                tm_start.tm_sec = *ssec;
                tm_start.tm_min = *smin;
                tm_start.tm_hour = *sh;
                tm_start.tm_mday = *sd;
                tm_start.tm_mon = *sm - 1;
                tm_start.tm_year = *sy - 1900;
                tm_start.tm_isdst = -1;
                mktime(&tm_start);
            } while (tm_start.tm_wday == 6 || tm_start.tm_wday == 0);
            break;

        case 5:
            // Sat-Sun
            do {
                *sd += 1;
                *ed += 1;
                tm_start.tm_sec = *ssec;
                tm_start.tm_min = *smin;
                tm_start.tm_hour = *sh;
                tm_start.tm_mday = *sd;
                tm_start.tm_mon = *sm - 1;
                tm_start.tm_year = *sy - 1900;
                tm_start.tm_isdst = -1;
                mktime(&tm_start);
            } while (tm_start.tm_wday != 6 && tm_start.tm_wday != 0);
            break;

        case 6:
            // Mon-Thu
            do {
                *sd += 1;
                *ed += 1;
                tm_start.tm_sec = *ssec;
                tm_start.tm_min = *smin;
                tm_start.tm_hour = *sh;
                tm_start.tm_mday = *sd;
                tm_start.tm_mon = *sm - 1;
                tm_start.tm_year = *sy - 1900;
                tm_start.tm_isdst = -1;
                mktime(&tm_start);
            } while (tm_start.tm_wday >= 5 || tm_start.tm_wday == 0 );
            break;

        default:
            logmsg(LOG_ERR, "Unknown type of repeat specified for record.");
            return -1;
            break;
    }

    // We need to do a full cycle to adjust the values in case
    // one of the values has wrapped
    *ts_start = totimestamp(*sy, *sm, *sd, *sh, *smin, *ssec);
    if( *ts_start >= 0 ) {
        fromtimestamp(*ts_start, sy, sm, sd, sh, smin, ssec);
    } else {
        logmsg(LOG_ERR,"increcdays() : FATAL Corrupt timeconversion. Cannot continue.");
        (void)exit(EXIT_FAILURE);
        return -1;
    }

    *ts_end = totimestamp(*ey, *em, *ed, *eh, *emin, *esec);
    if( *ts_end >= 0 ) {
        fromtimestamp(*ts_end, ey, em, ed, eh, emin, esec);
    } else {
        logmsg(LOG_ERR,"increcdays() : FATAL Corrupt timeconversion. Cannot continue.");
        (void)exit(EXIT_FAILURE);
        return -1;
    }
    return 0;
}

