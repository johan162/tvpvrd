/* =========================================================================
 * File:        DATETIMEUTIL.C
 * Description: Functions to manipulate timestamps and do date/time calculations
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


#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <syslog.h>
#include <string.h>

#include "datetimeutil.h"
#include "utils.h"
#include "xstr.h"
#include "tvplog.h"

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
 * Get a relative day specified by a weekday name. This will return a date
 * within the following seven days. If the weekdat name is the same as the
 * current day this will then refer to the next such day within the next
 * seven days. If the special value "today" is given the relative day will
 * be the (obvious) today.
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



/**
 * Adjust the given start and end date for a recording so that it actually starts
 * on a day that is allowed according to the repeat type. For example if the repeat
 * is onlye said to be on weekebds and the start date is on a wed we need to adjust
 * it by three days.
 * @param start
 * @param end
 * @param recurrence_type
 * @return 0 on success, -1 on failure
 */
int
adjust_initital_repeat_date(time_t *start, time_t *end, int recurrence_type) {
    int sy, sm, sd, sh, smin, ssec;
    int ey, em, ed, eh, emin, esec;

    if( recurrence_type < 0 || recurrence_type > 9 ) {
        logmsg(LOG_ERR, "FATAL: Internal error. Unknown recurrence type %d in adjust_initital_repeat_date()",recurrence_type);
        return -1;
    }
    
    fromtimestamp(*start, &sy, &sm, &sd, &sh, &smin, &ssec);
    fromtimestamp(*end, &ey, &em, &ed, &eh, &emin, &esec);   
    
    time_t now = time(NULL);
    if (*start < now) {
        // Time is earlier than current time. Assume first possible day is tomorrow
        *start = totimestamp(sy, sm, sd + 1, sh, smin, ssec);
        *end = totimestamp(ey, em, ed + 1, eh, emin, esec);
        fromtimestamp(*start, &sy, &sm, &sd, &sh, &smin, &ssec);
        fromtimestamp(*end, &ey, &em, &ed, &eh, &emin, &esec);   
    }    
  
    // If we have a recurrence on Mon-Fri (or possible Sat-Sun)
    // The we need to make sure that the start date also
    // obeys this. If this is not the case we move the start
    // day forward until the condition is met.
    if( recurrence_type == 4 || recurrence_type == 6 || recurrence_type == 7 || 
        recurrence_type == 8 || recurrence_type == 9 ) {
        // Mon Fri
        struct tm tm_start;
        tm_start.tm_sec = ssec;
        tm_start.tm_min = smin;
        tm_start.tm_hour = sh;
        tm_start.tm_mday = sd;
        tm_start.tm_mon = sm - 1;
        tm_start.tm_year = sy - 1900;
        tm_start.tm_isdst = -1;
        mktime(&tm_start);
        
        if( recurrence_type == 4  ) {
            // Type is 7 (Mon-Fri)
            if( tm_start.tm_wday == 6 ) {
                sd += 2;
                ed += 2;
            } else if( tm_start.tm_wday == 0 ) {
                sd++;
                ed++;
            }
        } else if( recurrence_type == 6  ) {
            // Type must be 6 (Mon-Thu)
            if (tm_start.tm_wday == 6) {
                // Mon - Thu so skip a sat+sun
                sd += 2;
                ed += 2;
            } else if (tm_start.tm_wday == 0) {
                // Mon - Thu so skip a sun
                sd++;
                ed++;
            } else if (tm_start.tm_wday == 5) {
                // Mon - Thu so skip a friday+weekend
                sd += 3;
                ed += 3;
            }
        } else if( recurrence_type == 7  ) {
            // Type is 7 (Tue-Fri)
            if (tm_start.tm_wday == 6) {
                // skip a sat+sun+mon
                sd += 3;
                ed += 3;
            } else if (tm_start.tm_wday == 0) {
                // skip a sunday and monday
                sd += 2;
                ed += 2;
            } else if (tm_start.tm_wday == 1) {
                // skip monday
                sd += 1;
                ed += 1;
            }
        } else if( recurrence_type == 8  ) {
            // Type is 8 (Wed-Fri)
            if (tm_start.tm_wday == 6) {
                // skip a sat+sun+mon+tue
                sd += 4;
                ed += 4;
            } else if (tm_start.tm_wday == 0) {
                // skip a sunday and monday
                sd += 3;
                ed += 3;
            } else if (tm_start.tm_wday == 1) {
                // skip monday
                sd += 2;
                ed += 2;
            } else if (tm_start.tm_wday == 2) {
                // skip tue
                sd += 1;
                ed += 1;           
            }
        } else if( recurrence_type == 9  ) {
            // Type is 9 (Tue-Thu)
            if (tm_start.tm_wday == 6) {
                // skip a sat+sun+mon
                sd += 3;
                ed += 3;
            } else if (tm_start.tm_wday == 0) {
                // skip a sunday and monday
                sd += 2;
                ed += 2;
            } else if (tm_start.tm_wday == 1) {
                // skip monday
                sd += 1;
                ed += 1;
            } else if (tm_start.tm_wday == 5) {
                // skip fri+sat+sun+mon
                sd += 4;
                ed += 4;                
            }            
        }

    } else if( recurrence_type == 5 ) {
        // Sat-Sun
        struct tm tm_start;
        tm_start.tm_sec = ssec;
        tm_start.tm_min = smin;
        tm_start.tm_hour = sh;
        tm_start.tm_mday = sd;
        tm_start.tm_mon = sm - 1;
        tm_start.tm_year = sy - 1900;
        tm_start.tm_isdst = -1;
        mktime(&tm_start);
        if( tm_start.tm_wday < 6 && tm_start.tm_wday > 0) {
            sd += 6-tm_start.tm_wday;
            ed += 6-tm_start.tm_wday;
        }
    }

    *start = totimestamp(sy, sm, sd, sh, smin, ssec);
    *end = totimestamp(ey, em, ed, eh, emin, esec);

    return 0;
}

/*
 * Utility function
 * Increase start and end day as needed to get the next
 * time according to the recurrence type, day, week, monnth
 * and so on
 */
int
increcdays(int recurrence_type,
        time_t *ts_start, time_t *ts_end,
        int *sy, int *sm, int *sd, int *sh, int *smin, int *ssec,
        int *ey, int *em, int *ed, int *eh, int *emin, int *esec) {
    struct tm tm_start;

    if( recurrence_type < 0 || recurrence_type > 9 ) {
        logmsg(LOG_ERR, "FATAL: Internal error. Unknown recurrence type %d in increcdays()",recurrence_type);
        return -1;
    }

    // Find out the new date for the next recording in sequence
    switch (recurrence_type) {
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

        case 7:
            // Tue-Fri
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
            } while (tm_start.tm_wday == 6 || tm_start.tm_wday <= 1 );
            break;
            
        case 8:
            // Wed-Fri
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
            } while (tm_start.tm_wday == 6 || tm_start.tm_wday <= 2 );
            break;
            
        case 9:
            // Tue-Thu
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
            } while (tm_start.tm_wday >= 5 || tm_start.tm_wday <= 1 );
            break;


        default:
            logmsg(LOG_ERR, "Unknown recurrence_type (%d) specified for record.",recurrence_type);
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

