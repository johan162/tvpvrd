/* =========================================================================
 * File:        STATS.H
 * Description: Keep track of recording/transcoding statistics
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009 Johan Persson
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

#ifndef _STATS_H
#define	_STATS_H

#ifdef	__cplusplus
extern "C" {
#endif

#define STATS_DIR "stats"

/**
 * Entry for statistics for each profile. The statistics is gathered every time
 * a transcoding is made.
 */
struct profile_stat_entry {
    char profile_name[32];          /* Name of profile */
    unsigned num_samples;           /* Number of statistical samples made */
    unsigned transcoding_speed;     /* Time in s to transcode 1min recording */
    unsigned mp2size_1min;          /* Size in bytes of 1min of MP2 encoding */
    unsigned mp2buffer_time;        /* Time in s to fill the MP2 recording buffer */
    unsigned mp4size_1min;          /* Size in bytes of 1min of MP4 encoding */
    unsigned total_ttime;           /* Total time in s spent transcoding */
    unsigned total_mp2time;         /* Total time in s recorded */
    unsigned total_mp2files;        /* Total MP2 files recorded with this profile */
    unsigned total_mp4files;        /* Total MP4 files recorded with this profile */
    float avg_5load;                /* Average 5 min load on server while transcoding */
};

/**
 * Hold all times gathered during a transcoding, real time, user time, system time
 */
struct timeall {
    struct timeval rtime;
    struct timeval utime;
    struct timeval stime;
};

#define STAT_DIR "stats"

/**
 * Read stored statistics for the specified profile name
 * @param profilename
 * @return 0 on success, -1 on failure
 */
int
read_profile_stats(char *profilename);

/**
 * WRite statistics collected so far to the individual proifle stat files
 * @return 0 on success, -1 on failure
 */
int
write_stats(void);

/**
 * Update the statistics with information from the latest transcoding
 * @param name
 * @param mp2size
 * @param recorded_time
 * @param mp4size
 * @param transcode_time
 * @param avg_5load
 * @return
 */
int
stats_update(char *name, unsigned mp2size, unsigned recorded_time, unsigned mp4size,
             struct timeall *transcode_time, float avg_5load);

/**
 * Read the statistics for the specified profile
 * @param name
 * @param entry
 * @return
 */
int
get_stats(char *name, struct profile_stat_entry **entry);

/**
 * Dump a string representation of all statistics to the specified buffer
 * @param buff Buffer
 * @param size Maximum size of buffer
 * @return
 */
int
dump_profilestats(char *buff, size_t size);

/**
 * Reset all statics (to 0). This will not affect the stored statistics until
 * the files have been updated as well.
 */
void
clear_stats(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _STATS_H */

