/* =========================================================================
 * File:        STATS.C
 * Description: Keep track of recording/transcoding statistics
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010 Johan Persson
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

// Standard UNIX includes
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <iniparser.h>
#include <fcntl.h>
#include <sys/stat.h>

// Application specific includes
#include "tvpvrd.h"
#include "transc.h"
#include "stats.h"
#include "utils.h"

/**
 * Array of statistics for each profile
 */
static struct profile_stat_entry *profile_stats[MAX_TRANS_PROFILES] ;
static unsigned num_stats = 0 ;

int
get_stats(char *name, struct profile_stat_entry **entry) {
    int i;
    for(i=0; i < num_stats && strncmp(name,profile_stats[i]->profile_name,32); i++)
        ;
    if( i >= num_stats ) {
        logmsg(LOG_ERR,"Internal error : get_stats() : Can't find profile %s\n",name);
        return -1;
    }
    *entry = profile_stats[i];
    return 0;
}

int
stats_update(char *name,unsigned mp2size,unsigned recorded_time,unsigned mp4size,
             struct timeall *transcode_time, unsigned transcode_and_copy_time) {
             
    struct profile_stat_entry *entry;
    
    if( -1 == get_stats(name,&entry) ) {
        return -1;
    }

    entry->num_samples++;

    // We ignore usec

    // The total time running in the system
    //unsigned t1_min = (transcode_time->utime.tv_sec + transcode_time->stime.tv_sec) / 60;
    //unsigned t1_sec = (transcode_time->utime.tv_sec + transcode_time->stime.tv_sec) % 60;

    // The user experienced running time (including when the process has been waiting)
    unsigned t2_min = transcode_time->rtime.tv_sec / 60 ;
    unsigned t2_sec = transcode_time->rtime.tv_sec % 60;

    logmsg(LOG_NOTICE,"Adding stats. ['%s', mp2size=%6d kB, mp4size=%6d kB, rec.time=%3d min,"
            " trans.rtime=%03d:%02d min, trans.utime=%3d:%02d min, trans.stime=%3d:%02d min]",
           name,mp2size/1024,mp4size/1024,recorded_time/60,
           t2_min,t2_sec,transcode_time->utime.tv_sec/60, transcode_time->utime.tv_sec%60,
           transcode_time->stime.tv_sec/60,transcode_time->stime.tv_sec%60);
    
    // MP2 size per recorded minute
    if( entry->mp2size_1min == 0 ) {
        entry->mp2size_1min = mp2size / (recorded_time/60);
    } else {
        entry->mp2size_1min += mp2size / (recorded_time/60);
        entry->mp2size_1min /= 2;
    }

    if( mp4size > 0 && t2_min > 0) {
        // MP4 size per recorded minute
        if( entry->mp4size_1min == 0 ) {
            entry->mp4size_1min = mp4size / (recorded_time/60);
        } else {
            entry->mp4size_1min += mp4size / (recorded_time/60);
            entry->mp4size_1min /= 2;
        }

        // Number of seconds transcoded per running minute
        if(entry->transcoding_speed==0) {
            entry->transcoding_speed = recorded_time / t2_min ;
        } else {
            entry->transcoding_speed += recorded_time / t2_min;
            entry->transcoding_speed /= 2;
        }
    }

    entry->total_ttime += t2_min;
    entry->total_mp2files += mp2size > 0 ? 1 : 0 ;
    entry->total_mp4files += mp4size > 0 ? 1 : 0 ;
    entry->total_mp2time += mp2size > 0 ? recorded_time : 0;

    return 0;
}

int
read_profile_stats(char *profilename) {
    char filename[256];

    // First check tht this stats doesn't already exist. This could happen if we try to re-read
    // the stats and we don't want that since it would erase all updated stats in the memory.
    int i;
    for (i = 0; i < num_stats && strncmp(profilename, profile_stats[i]->profile_name, 32); i++)
        ;
    if (i < num_stats) {
        logmsg(LOG_NOTICE, "  -- statistics for profile '%s' already exists, skipping.", profilename);
        return -1;
    }

    snprintf(filename, 255, "%s/%s/%s.stats", datadir, STATS_DIR, profilename);

    struct profile_stat_entry *entry = calloc(1, sizeof (struct profile_stat_entry));
    if (entry == NULL) {
        logmsg(LOG_ERR, "read_profile_stats() : Out of memory (%d : %s)", errno, strerror(errno));
        return -1;
    }
    strncpy(entry->profile_name, profilename, 31);
    entry->profile_name[31] = '\0';

    dictionary *stats = iniparser_load(filename);
    if (stats) {

        entry->num_samples = iniparser_getint(stats, "stats:num_samples", 0);
        entry->transcoding_speed = iniparser_getint(stats, "stats:transcoding_speed", 0);
        entry->mp2size_1min = iniparser_getint(stats, "stats:mp2size_1min", 0);
        entry->mp4size_1min = iniparser_getint(stats, "stats:mp4size_1min", 0);
        entry->total_ttime = iniparser_getint(stats, "stats:total_ttime", 0);
        entry->total_mp2time = iniparser_getint(stats, "stats:total_mp2time", 0);
        entry->total_mp2files = iniparser_getint(stats, "stats:total_mp2files", 0);
        entry->total_mp4files = iniparser_getint(stats, "stats:total_mp4files", 0);
        iniparser_freedict(stats);
        profile_stats[num_stats++] = entry;

        logmsg(LOG_NOTICE, "  -- read statistics for profile %s", profilename);

    } else {

        profile_stats[num_stats++] = entry;

        logmsg(LOG_NOTICE, "  -- innitializing new statistics for profile %s.", profilename);

    }

    return 0;
}

int
write_stats(void) {
    char filename[256];
    
    for(int i=0; i < num_stats; i++) {
        snprintf(filename,255,"%s/%s/%s.stats",datadir,STATS_DIR,profile_stats[i]->profile_name);
        filename[255] = '\0';
        
        int fd = open(filename, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if( -1 == fd ) {
            logmsg(LOG_ERR,"Can't open statistics file '%s' for writing. ( %d : %s)",filename,errno,strerror(errno));
            return -1;
        }
        _writef(fd,
                "[stats]\n"
                "transcoding_speed=%d\n"
                "mp2size_1min=%d\n"
                "mp4size_1min=%d\n"
                "total_ttime=%d\n"
                "total_mp2time=%d\n"
                "total_mp2files=%d\n"
                "total_mp4files=%d\n",
                profile_stats[i]->transcoding_speed,
                profile_stats[i]->mp2size_1min,
                profile_stats[i]->mp4size_1min,
                profile_stats[i]->total_ttime,
                profile_stats[i]->total_mp2time,
                profile_stats[i]->total_mp2files,
                profile_stats[i]->total_mp4files
               );
        close(fd);
    }
    return 0;
}

void
clear_stats(void) {
    // First clear memory
    for(int i=0; i < num_stats; i++) {
        struct profile_stat_entry *entry = profile_stats[i];
        entry->num_samples          = 0;
        entry->transcoding_speed    = 0;
        entry->mp2size_1min         = 0;
        entry->mp4size_1min         = 0;
        entry->total_ttime          = 0;
        entry->total_mp2time        = 0;
        entry->total_mp2files       = 0;
        entry->total_mp4files       = 0;
    }

    // And then the stored stats on th disk
    write_stats();
}

int
dump_profilestats(char *buff, int size) {
    char tmpbuff[512];
    *buff = '\0';

    int left = size-1;
    for (int i = 0; i < num_stats; i++) {
        if (profile_stats[i]) {
            snprintf(tmpbuff, 511,
                    "%-24s: '%s'\n"
                    "%-24s: %d s transcoded / min\n"
                    "%-24s: %.1f Mb/min\n"
                    "%-24s: %.1f Mb/min\n"
                    "%-24s: %.1f\n"
                    "%-24s: %d min\n"
                    "%-24s: %d min\n"
                    "%-24s: %d\n"
                    "%-24s: %d\n\n",
                    "profile_name", profile_stats[i]->profile_name,
                    "transcoding_speed", profile_stats[i]->transcoding_speed,
                    "mp2size_1min", (float)profile_stats[i]->mp2size_1min/1024.0/1024.0,
                    "mp4size_1min", (float)profile_stats[i]->mp4size_1min/1024.0/1024.0,
                    "comp_ratio", (float)profile_stats[i]->mp2size_1min / (float)profile_stats[i]->mp4size_1min,
                    "total_ttime", profile_stats[i]->total_ttime,
                    "total_mp2time", profile_stats[i]->total_mp2time/60,
                    "total_mp2files", profile_stats[i]->total_mp2files,
                    "total_mp4files", profile_stats[i]->total_mp4files
                    );
            tmpbuff[511] = '\0';
            if( left > strlen(tmpbuff) ) {
                strncat(buff, tmpbuff, size - 1);
                left -= strlen(tmpbuff);
            }
        } else {
            logmsg(LOG_ERR, "Internal error: NULL pointer profile_stats[%d]", i);
        }
    }
    buff[size - 1] = '\0';
    return 0;
}
