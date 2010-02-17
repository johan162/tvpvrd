/* =========================================================================
 * File:        RECS.C
 * Description: The recs.c module hold all functions to manipulate the
 *              list of current defined recordings. This is mainatained
 *              as a vector for each video capture card which holds
 *              details on specific recordings.
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


// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <strings.h>
#include <libgen.h>
#include <unistd.h>
#include <assert.h>
#include <wchar.h>

#include "tvpvrd.h"
#include "utils.h"
#include "recs.h"
#include "transc.h"


/*
 * recs
 * A two dimensional array with a list of recordings per video stream
 * The REC_IDX macro is a shorthand to get hold of elements in the array
 * since this should be treated as a two-dimensional array
 */

struct recording_entry **recs; // [MAX_VIDEO][MAX_ENTRIES];

/*
 * num_entries
 * Number of pending recording entries per video stream
 */
int *num_entries; //[MAX_VIDEO];

/*
 * ongoing_recs
 * The ongoing recordings (per video stream)
 */
struct recording_entry **ongoing_recs; //[MAX_VIDEO];

/*
 * seqnbr
 * Running sequence number for all recordings. This is used to give a unique
 * id for each recording as long as the server is running.
 */
static int seqnbr = 1;

/*
 * recurrence_id
 * Unique id for each recurrence sequence
 */
static int recurrence_id = 1;

/**
 * Check if the submitted entry is colliding/overlapping with an existing
 * entry in the pending recordings for the specified video stream or
 * any ongoing recordings.
 *
 * Return 1 if there is a collision 0 otherwise
 */
int
isentryoverlapping(int video, struct recording_entry* entry) {
    // Check all existing entries if the start or stop
    // time is in between the existing start and stop time

    if (entry->recurrence == 0) {
        // No recurrence
        for (int i = 0; i < num_entries[video]; ++i) {
            if (entry->ts_start >= recs[REC_IDX(video, i)]->ts_start &&
                entry->ts_start <= recs[REC_IDX(video, i)]->ts_end) {

                logmsg(LOG_NOTICE,"New entry collides with: '%s'",recs[REC_IDX(video, i)]->title);
                return 1;
            }

            if (entry->ts_end >= recs[REC_IDX(video, i)]->ts_start &&
                entry->ts_end <= recs[REC_IDX(video, i)]->ts_end) {

                logmsg(LOG_NOTICE,"New entry collides with: '%s'",recs[REC_IDX(video, i)]->title);
                return 1;
            }
            if (entry->ts_start < recs[REC_IDX(video, i)]->ts_start &&
                entry->ts_end > recs[REC_IDX(video, i)]->ts_end) {

                logmsg(LOG_NOTICE,"New entry collides with: '%s'",recs[REC_IDX(video, i)]->title);
                return 1;
            }
        }

        if( ongoing_recs[video] ) {
            if (entry->ts_start >= ongoing_recs[video]->ts_start &&
                entry->ts_start <= ongoing_recs[video]->ts_end) {

                logmsg(LOG_NOTICE,"New recurring entry collides with ongoing recording at video=%d",video);
                return 1;
            }
            if (entry->ts_end >= ongoing_recs[video]->ts_start &&
                entry->ts_end <= ongoing_recs[video]->ts_end) {

                logmsg(LOG_NOTICE,"New recurring entry collides with ongoing recording at video=%d",video);
                return 1;
            }
            if (entry->ts_start < ongoing_recs[video]->ts_start &&
                entry->ts_end > ongoing_recs[video]->ts_end) {

                logmsg(LOG_NOTICE,"New recurring entry collides with ongoing recording at video=%d",video);
                return 1;
            }
        }

    } else {
        int sy, sm, sd, sh, smin, ssec;
        int ey, em, ed, eh, emin, esec;

        // Recurrence. This means we need to check all future
        // recurrences for collisins as well.
        fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smin, &ssec);
        fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emin, &esec);
        time_t ts_start = entry->ts_start;
        time_t ts_end = entry->ts_end;

        for (int j = 0; j < entry->recurrence_num; j++) {

            for (int i = 0; i < num_entries[video]; ++i) {
                if (ts_start >= recs[REC_IDX(video, i)]->ts_start &&
                    ts_start <= recs[REC_IDX(video, i)]->ts_end) {

                    logmsg(LOG_NOTICE,"New recurring entry collides at occurence %d with: '%s'",j,recs[REC_IDX(video, i)]->title);
                    return 1;
                }
                if (ts_end >= recs[REC_IDX(video, i)]->ts_start &&
                    ts_end <= recs[REC_IDX(video, i)]->ts_end) {

                    logmsg(LOG_NOTICE,"New recurring entry collides at occurence %d with: '%s'",j,recs[REC_IDX(video, i)]->title);
                    return 1;
                }
                if (ts_start < recs[REC_IDX(video, i)]->ts_start &&
                    ts_end > recs[REC_IDX(video, i)]->ts_end) {

                    logmsg(LOG_NOTICE,"New recurring entry collides at occurence %d with: '%s'",j,recs[REC_IDX(video, i)]->title);
                    return 1;
                }
            }

            if( ongoing_recs[video] ) {
                if (ts_start >= ongoing_recs[video]->ts_start &&
                    ts_start <= ongoing_recs[video]->ts_end) {

                    logmsg(LOG_NOTICE,"New entry collides at occurrence %d with ongoing recording at video=%d",j,video);
                    return 1;
                }
                if (ts_end >= ongoing_recs[video]->ts_start &&
                    ts_end <= ongoing_recs[video]->ts_end) {

                    logmsg(LOG_NOTICE,"New entry collides at occurrence %d with ongoing recording at video=%d",j,video);
                    return 1;
                }

                if (ts_start < ongoing_recs[video]->ts_start &&
                    ts_end > ongoing_recs[video]->ts_end) {

                    logmsg(LOG_NOTICE,"New entry collides at occurrence %d with ongoing recording at video=%d",j,video);
                    return 1;
                }
            }

            // Prepare to check next recurrency
            increcdays(entry->recurrence_type,
                    &ts_start, &ts_end,
                    &sy, &sm, &sd, &sh, &smin, &ssec,
                    &ey, &em, &ed, &eh, &emin, &esec);
        }

    }

    return 0;
}

/* ----------------------------------------------------------------------------------
 * Routines to manage the list of pending recordings for each
 * video stream.
 * ----------------------------------------------------------------------------------
 */

/**
 * Initialize all the dynamic structures
 */
void
initrecs(void) {
    recs            = (struct recording_entry **) calloc(max_video, max_entries * sizeof (struct recording_entry *));
    ongoing_recs    = (struct recording_entry **) calloc(max_video, sizeof (struct recording_entry *));
    num_entries     = (int *) calloc(max_video, sizeof (int));

    if( recs == NULL || ongoing_recs == NULL || num_entries == NULL ) {
        fprintf(stderr,"FATAL: Out of memory. Aborting program.\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Free all dynamic structures
 */
void
freerecs(void) {
    for (int i = 0; i < max_video; ++i) {
        for (int j = 0; j < num_entries[i]; ++j) {
            if( recs[REC_IDX(i,j)] ) {
                freerec(recs[REC_IDX(i,j)] ,"freerecs()" );
                recs[REC_IDX(i,j)] = NULL ;
            }
        }
    }
    free(recs);

    for (int i = 0; i < max_video; ++i) {
        if( ongoing_recs[i] )
            freerec(ongoing_recs[i],"freerecs(),II");
    }
    free(ongoing_recs);
    free(num_entries);    
}

void freerec(struct recording_entry *entry ,char *caller) {
    //logmsg(LOG_DEBUG,"freerec() called from '%s'",caller);
    for(int i=0; i < REC_MAX_TPROFILES ; i++) {
        free(entry->transcoding_profiles[i]);
    }
    free(entry);
}

/**
 * Create a new record from the given fields
 */
struct recording_entry *
newrec(const char *title, const char *filename, const time_t start,
        const time_t end, const char *channel, const int recurrence,
        const int recurrence_type, const int recurrence_num,
        const int recurrence_mangling,
        char *profiles[]) {

    struct recording_entry* ptr =
            (struct recording_entry *) calloc(1, sizeof (struct recording_entry));
    if (ptr == NULL) {
        logmsg(LOG_ERR, "Failed to allocate new entry due to no memory left.");
        return NULL;
    }

    int i;
    for(i=0; i < REC_MAX_TPROFILES ; i++) {
        ptr->transcoding_profiles[i] = calloc(1,REC_MAX_TPROFILE_LEN);
    }

    for(i=0; i < REC_MAX_TPROFILES && profiles[i] && strnlen(profiles[i],REC_MAX_TPROFILE_LEN) > 0; i++) {
        strncpy(ptr->transcoding_profiles[i],profiles[i],REC_MAX_TPROFILE_LEN-1);
        ptr->transcoding_profiles[i][REC_MAX_TPROFILE_LEN-1] = '\0';
    }
    
    if( i == 0 ) {
        strncpy(ptr->transcoding_profiles[0],default_transcoding_profile,REC_MAX_TPROFILE_LEN-1);
        ptr->transcoding_profiles[0][REC_MAX_TPROFILE_LEN-1] = '\0';
    }


    // This will be updated after a sucessfull insertrec()
    ptr->seqnbr = -1;

    // Secure that the string is 0 terminated in case it has been
    // truncated by the max length. Remember that strncpy() will not
    // set a 0 in case the string is truncated.
    strncpy(ptr->title, title, REC_MAX_NTITLE - 1);
    ptr->title[REC_MAX_NTITLE - 1] = 0;

    strncpy(ptr->filename, filename, REC_MAX_NFILENAME - 1);
    ptr->filename[REC_MAX_NFILENAME - 1] = 0;

    strncpy(ptr->channel, channel, REC_MAX_NCHANNEL - 1);
    ptr->channel[REC_MAX_NCHANNEL - 1] = 0;

    strncpy(ptr->recurrence_mangling_prefix, "_", REC_MAX_NPREFIX - 1);
    ptr->recurrence_mangling_prefix[REC_MAX_NPREFIX - 1] = 0;

    // Start and end timestamp for recording
    ptr->ts_start = start;
    ptr->ts_end = end;

    // Recurrence 0=No, 1=Yes
    ptr->recurrence = recurrence;

    // Type: 0=Single, 1=day, 2=week, 3=month, 4=mon-fri, 5=sat-sun, 6=mon-thu
    ptr->recurrence_type = recurrence_type;

    // Number: num = Number of recurrences
    // Note: If this is a negative number then instead it is the timestamp
    // for the last date when this recurrance should stop
    ptr->recurrence_num = recurrence_num;

    // Mangling type: 0=Recurrence seq, 1=Date
    ptr->recurrence_mangling = recurrence_mangling;

    // The initial number to use in the title mangling of a repeated squence
    ptr->recurrence_start_number = 1;

    return ptr;
}

/*
 * Comparison function for qsort in order to sort the list of recordings
 * (per video stream) in order of start timestamp
 */
int
_cmprec(const void *r1, const void *r2) {
    struct recording_entry * e1 = *(struct recording_entry **) r1;
    struct recording_entry * e2 = *(struct recording_entry **) r2;
    if (e1->ts_start < e2->ts_start)
        return -1;
    else if (e1->ts_start > e2->ts_start)
        return 1;
    else
        return 0;
}

/*
 * Sort list of pending recordings according to start timestamp
 */
void
sortrecs(const int video) {
    qsort(&recs[video * max_entries], num_entries[video], sizeof (struct recording_entry *), _cmprec);
}

/*
 * Helper function to insert a new recording in the list. Should never
 * be called directly.
 */
int
_insertrec(int video, struct recording_entry* entry) {

    if (num_entries[video] >= max_entries) {
        logmsg(LOG_ERR, "Can not store more recordings on video %d. Maximum %d allowed.",video, max_entries);
        return 0;
    }
    entry->video = video;
    recs[REC_IDX(video, num_entries[video])] = entry;
    num_entries[video]++;
    sortrecs(video);
    return 1;
}

int
adjust_initital_repeat_date(time_t *start, time_t *end, int recurrence_type) {
    int sy, sm, sd, sh, smin, ssec;
    int ey, em, ed, eh, emin, esec;

    if( recurrence_type < 0 || recurrence_type > 6 ) {
        logmsg(LOG_ERR, "FATAL: Internal error. Unknown recurrence type %d in adjust_initital_repeat_date()",recurrence_type);
        return -1;
    }

    fromtimestamp(*start, &sy, &sm, &sd, &sh, &smin, &ssec);
    fromtimestamp(*end, &ey, &em, &ed, &eh, &emin, &esec);

    // If we have a recurrence on Mod-Fri (or possible Sat-Sun)
    // The we need to make sure that the start date also
    // obeys this. If this is not the case we move the start
    // day forward until the condition is met.
    if( recurrence_type == 4 || recurrence_type == 6 ) {
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
 * Insert a new recording in the list after checking that it doesn't
 * collide with an existing recording
 */
int
insertrec(int video, struct recording_entry * entry) {
    int len;
    char *filetype;
    char *bname, *dname;
    char bnamecore[256];
    char tmpbuff[512], tmpbuff2[512], bname_buffer[256], dname_buffer[256];
    time_t ts_start, ts_end;
    int sy, sm, sd, sh, smin, ssec;
    int ey, em, ed, eh, emin, esec;
    struct recording_entry *newentry = NULL;

    if (isentryoverlapping(video, entry)) {
        return 0;
    }

    if (entry->recurrence) {

        // Make sure there is enough room on this video to store
        // all repeated recordings
        if( num_entries[video]+entry->recurrence_num > max_entries ) {
            return 0;
        }

        strncpy(bname_buffer, entry->filename, 255);
        bname = basename(bname_buffer);

        strncpy(dname_buffer, entry->filename, 255);
        dname = dirname(dname_buffer);

        filetype = strchr(bname, '.');
        len = MIN((filetype - bname), 255);
        strncpy(bnamecore, bname, len);
        bnamecore[len] = 0;

        (void)adjust_initital_repeat_date(&entry->ts_start, &entry->ts_end, entry->recurrence_type);

        fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smin, &ssec);
        fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emin, &esec);
        ts_start = entry->ts_start;
        ts_end = entry->ts_end;

        assert(entry->recurrence_num > 0);

        for (int i = 0; i < entry->recurrence_num; i++) {

            // Name mangling for title
            if (entry->recurrence_mangling == 0) {
                snprintf(tmpbuff, 512, "%s %d-%02d-%02d %02d.%02d", entry->title, sy, sm, sd, sh, smin);
            } else {
                snprintf(tmpbuff, 512, "%s (%02d/%02d)", entry->title, i + entry->recurrence_start_number, entry->recurrence_num);
            }

            // Name mangling of filename
            snprintf(tmpbuff2, 512, "%s/%s%s%d-%02d-%02d%s%02d.%02d%s",
                    dname, bnamecore, entry->recurrence_mangling_prefix, sy, sm, sd,
                    entry->recurrence_mangling_prefix, sh, smin, filetype);

            newentry = newrec(tmpbuff, tmpbuff2,
                    ts_start, ts_end,
                    entry->channel, 1,
                    entry->recurrence_type,
                    entry->recurrence_num - i,
                    entry->recurrence_mangling,
                    entry->transcoding_profiles);

            newentry->seqnbr =  seqnbr++;
            newentry->recurrence_id = recurrence_id;
            newentry->recurrence_start_number = i + entry->recurrence_start_number;

            // Store an unmangled filename with the recording
            strncpy(newentry->recurrence_filename, bname, REC_MAX_NFILENAME - 1);
            newentry->recurrence_filename[REC_MAX_NFILENAME - 1] = '\0';

            // Store an unmangled title with the recording
            strncpy(newentry->recurrence_title, entry->title, REC_MAX_NTITLE - 1);
            newentry->recurrence_title[REC_MAX_NTITLE - 1] = '\0';

            (void)_insertrec(video, newentry);

            // Find out the new date for the next recording in sequence
            if( -1 == increcdays(entry->recurrence_type,
                    &ts_start, &ts_end,
                    &sy, &sm, &sd, &sh, &smin, &ssec,
                    &ey, &em, &ed, &eh, &emin, &esec) )  {
                return 0;
            }

        }
        recurrence_id++;
        freerec(entry,"insertrec()");
    } else {
        entry->seqnbr = seqnbr++;
        _insertrec(video, entry);
    }
    // Return the last used sequence number
    return seqnbr-1;
}

/**
 * Give a textual representation to the recurrece type
 * It is the callers responsibility that the buff is larger
 * enout to hold at least 12 bytes.
 * @return 1 on success, 0 on failure
 */
int
getrectypestr(const int rectype, char *buff, int size) {
    static char *sn[] = {"None.", "Daily", "Weekly", "Monthly", "Mon-Fri", "Sat-Sun"};

    if (rectype >= 0 && rectype <= 5) {
        strncpy(buff, sn[rectype],size);
        return 0;
    } else {
        strncpy(buff, "(Unknown)",size);
        return -1;
    }
}


/*
 * Fill the supplied buffer with a textual representation of a
 * header line suitable for future pritings of records from
 * 'dumprecord()'
 */
void
dumprecord_header(int style, char *buffer, int bufflen) {
    char tmpbuff[512];
    char r[256];
    const int header_length=65;

    snprintf(tmpbuff, 511, "%s\n", rptchr_r('-', header_length,r));
    strncpy(buffer, tmpbuff, bufflen);
    buffer[bufflen-1] = '\0';

    if (style == 0) {
        snprintf(tmpbuff, 511, "%-3s| %-17s| %-6s| %-31s\n",
                 "#", "Start", "End", "Title");
        strncat(buffer, tmpbuff, bufflen);
    } else {
        snprintf(tmpbuff, 511, "List of recordings\n");
        strncat(buffer, tmpbuff,bufflen);
    }

    snprintf(tmpbuff, 511, "%s\n", rptchr_r('-', header_length,r));
    strncat(buffer, tmpbuff, bufflen);
    buffer[bufflen-1] = '\0';
}

/*
 * Fill the supplied buffer with a textual representation of the recording
 * entry pointed to by 'entry'. The styöe affect how the string is fomratted
 *
 * style	Format
 * ----------------------------------------------
 * 0		One line, short format
 * 1		Record, several lines, short format
 * 2		Record, several lines, long format
 */
void
dumprecord(struct recording_entry* entry, int style, char *buffer, int bufflen) {
    int sy, sm, sd, sh, smi, ss;
    int ey, em, ed, eh, emi, es;
    char rectype[16];
    struct tm result;
    static char wday_name[7][3] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
    fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emi, &es);
    (void)getrectypestr(entry->recurrence_type, rectype,16);

    // We need localtime to find the day of week for the start
    (void)localtime_r(&entry->ts_start, &result);

    if (style == 0) {
        char profbuff[256], profile[REC_MAX_TPROFILE_LEN+1];
        profbuff[0] = '\0';
        int left=255;
        for(int k=0; k < REC_MAX_TPROFILES && strlen(entry->transcoding_profiles[k]) > 0; k++) {
            snprintf(profile,REC_MAX_TPROFILE_LEN,"@%s",entry->transcoding_profiles[k]);
            if( k > 0 ) {
                strcat(profbuff,", ");
                left -= 2;
            }
            strncat(profbuff,profile,left);
            left -= strlen(profile);
        }
        profbuff[255] = '\0';

        snprintf(buffer, bufflen, "[%03d|%-8.8s|%04d-%02d-%02d|%.3s|%02d:%02d|%02d:%02d|%-30s|%s]\n",
                entry->seqnbr,
                entry->channel,
                sy, sm, sd, 
                wday_name[result.tm_wday],
                sh, smi, eh, emi, entry->title,profbuff);

    } else {
        if (entry->recurrence) {
            if (style == 2) {
                snprintf(buffer,bufflen,
                        "%10s: %d\n"
                        "%10s: %s\n"
                        "%10s: %s\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %d\n"
                        "%10s: %s\n"
                        "%10s: %s %d recordings remain after this (RID:%d)\n"
                        "%10s: %s %s\n"
                        "%10s: %s %s\n"
                        "\n",
                        "#", entry->seqnbr,
                        "Title", entry->title,
                        "Channel", entry->channel,
                        "Start", sy, sm, sd, sh, smi, ss,
                        "End", ey, em, ed, eh, emi, es,
                        "Video", entry->video,
                        "Filename", entry->filename,
                        "Repeats", rectype, entry->recurrence_num - 1, entry->recurrence_id,
                        "", "Base-title   :", entry->recurrence_title,
                        "", "Base-filename:", entry->recurrence_filename);
            } else if (style == 1) {
                snprintf(buffer,bufflen,
                        "%10s: %d\n"
                        "%10s: %s\n"
                        "%10s: %s\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %s %d recordings remain after this\n"
                        "\n",
                        "#", entry->seqnbr,
                        "Title", entry->title,
                        "Channel", entry->channel,
                        "Start", sy, sm, sd, sh, smi, ss,
                        "End", ey, em, ed, eh, emi, es,
                        "Repeats", rectype, entry->recurrence_num - 1);

            }
        } else {
            if (style == 2) {
                snprintf(buffer, bufflen, "\n"
                        "%10s: %d\n"
                        "%10s: %s\n"
                        "%10s: %s\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %d\n"
                        "%10s: %s\n"
                        "%10s: %s\n"
                        "\n",
                        "#", entry->seqnbr,
                        "Title", entry->title,
                        "Channel", entry->channel,
                        "Start", sy, sm, sd, sh, smi, ss,
                        "End", ey, em, ed, eh, emi, es,
                        "Video", entry->video,
                        "Filename", entry->filename,
                        "Repeats", "None.");
            } else if (style == 1) {
                snprintf(buffer, bufflen,
                        "%10s: %d\n"
                        "%10s: %s\n"
                        "%10s: %s\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %d-%02d-%02d %02d:%02d:%02d\n"
                        "%10s: %s\n"
                        "\n",
                        "#", entry->seqnbr,
                        "Title", entry->title,
                        "Channel", entry->channel,
                        "Start", sy, sm, sd, sh, smi, ss,
                        "End", ey, em, ed, eh, emi, es,
                        "Repeats", "None.");
            }
        }
    }
    buffer[bufflen-1] = '\0';
}

/*
 * Dump record with specified seqnbr. If the recording is part of a
 * repeated recording and 'repeats' is != 0 then all repeated records will
 * all be dumped to the specified buffer
 */
int
dumprecordid(int seqnbr, int repeats, int style, char *buffer, int bufflen) {
    int i, video, found, rid;
    struct recording_entry *entry=NULL;
    char tmpbuff[512];

    *buffer = '\0';
    
    found = 0;
    for (video = 0; video < max_video && !found; video++) {
        for (i = 0; i < num_entries[video] && !found; i++) {
            if (recs[REC_IDX(video, i)]->seqnbr == seqnbr) {
                entry = recs[REC_IDX(video, i)];
                found = 1;
            }
        }
    }

    --video;
    *buffer = 0;
    if (found) {
        int left = bufflen ;
        if (entry->recurrence && repeats) {
            rid = entry->recurrence_id;
            for (i = 0; i < num_entries[video]; i++) {
                if (recs[REC_IDX(video, i)]->recurrence_id == rid) {
                    dumprecord(recs[REC_IDX(video, i)], style, tmpbuff, 512);
                    if( left > strnlen(tmpbuff,511)) {
                        strncat(buffer, tmpbuff, left);
                        left -= strnlen(tmpbuff,511);
                    }
                    else {

                        if( left > 3 )
                            strncat( buffer, "...\n", 511);
                        return found;

                    }
                }
            }

        } else {
            dumprecord(entry, style, buffer, bufflen);
        }
    }
    return found;
}

/*
 * Print a list of all recordings to specified file for the specified video stream
 */
void
listrecs(int style, int fd) {
    struct recording_entry **entries;
    char buffer[2048];

    entries = calloc(2*max_entries,sizeof (struct recording_entry *));
    if( entries == NULL ) {
        logmsg(LOG_ERR,"listrecs() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }
    bzero(buffer, 2048);

    //dumprecord_header(style,buffer,2048);
    //write(fd, buffer, strlen(buffer));

    // We combine all recroding in all video in order to
    // give a combined sorted list of pending recordings
    int k=0;
    for (int video = 0; video < max_video; video++) {
        for (int i = 0; i < num_entries[video]; ++i) {
            entries[k++] = recs[REC_IDX(video, i)];
        }
    }

    qsort(entries, k, sizeof (struct recording_entry *), _cmprec);

    for(int i=0; i < k; i++ ) {
        dumprecord(entries[i], style, buffer, 2048);
        _writef(fd, buffer);
    }

}

/*
 * Delete the top recording in the list for the specified video stream
 * NOTE: This will also free the memory occupied by that record
 */
void
deletetoprec(const int video) {
    if (num_entries[video] < 1) {
        logmsg(LOG_ERR, "Cannot delete records since there are no recordings for video %d\n", video);
    } else {
        if( recs[REC_IDX(video, 0)] ) {
            freerec(recs[REC_IDX(video, 0)],"deletetoprec()");
            recs[REC_IDX(video, 0)] = recs[REC_IDX(video, num_entries[video] - 1)];
            recs[REC_IDX(video, num_entries[video] - 1)] = NULL;
            num_entries[video]--;
            sortrecs(video);
        }
        else {
            logmsg(LOG_ERR,"Internal error. Trying to delete non existent recording.");
        }
    }
}

/*
 * Remove the top recording from the list but do NOT deallocate the memory
 * occupied by the record.
 */
void
removetoprec(const int video) {
    if (num_entries[video] < 1) {
        logmsg(LOG_ERR, "Cannot delete records since there are no recordings for video %d.",video);
    } else {
        recs[REC_IDX(video, 0)] = recs[REC_IDX(video, num_entries[video] - 1)];
        recs[REC_IDX(video, num_entries[video] - 1)] = NULL;
        num_entries[video]--;
        sortrecs(video);
    }
}

int 
updateprofile(int seqnbr, char *profile) {

    if( ! transcoding_profile_exist(profile) ) {
        return 0;
    }

    // First find the record with this sequence number
    int foundidx=-1, foundvideo=-1,found = 0;
    for (int video = 0; video < max_video && !found; video++) {
        for (int i = 0; i < num_entries[video] && !found; i++) {
            if (recs[REC_IDX(video, i)]->seqnbr == seqnbr) {
                found = 1;
                foundidx = i;
                foundvideo = video;
            }
        }
    }
    if (!found) {
        return 0;
    } else {
        // FIXME: Proile
        strncpy(recs[REC_IDX(foundvideo, foundidx)]->transcoding_profiles[0],profile,REC_MAX_TPROFILE_LEN-1);
        recs[REC_IDX(foundvideo, foundidx)]->transcoding_profiles[0][REC_MAX_TPROFILE_LEN-1] = '\0';
        return seqnbr;
    }
}

/*
 * Delete a recording with specified sequence number.
 * If "allrecurrences" is true then all recurrent recording
 * will be deleted if the record is part of a sequnce of recordings
 */
int
deleterecid(int seqnbr, int allrecurrences) {
    int nbr, found=0, foundidx=-1, foundvideo=-1, rid;

    // First find the record with this sequence number
    found = 0;
    for (int video = 0; video < max_video && !found; video++) {
        for (int i = 0; i < num_entries[video] && !found; i++) {
            if (recs[REC_IDX(video, i)]->seqnbr == seqnbr) {
                found = 1;
                foundidx = i;
                foundvideo = video;
            }
        }
    }

    if (!found) {

        return 0;
        
    } else {

        // Check if the is part of a recurrence sequence
        if (recs[REC_IDX(foundvideo, foundidx)]->recurrence && allrecurrences) {

            // Delete all recordings part of this repeated recording
            rid = recs[REC_IDX(foundvideo, foundidx)]->recurrence_id;
            for (int i = 0; i < num_entries[foundvideo]; i++) {
                if (recs[REC_IDX(foundvideo, i)]->recurrence_id == rid) {
                    freerec(recs[REC_IDX(foundvideo, i)],"deleterecid()");
                    recs[REC_IDX(foundvideo, i)] = NULL;
                }
            }

            // Now we need to compact the array of recordings
            nbr = 0;
            struct recording_entry **tmprecs = calloc(max_entries, sizeof (struct recording_entry *));
            for (int i = 0; i < num_entries[foundvideo]; i++) {
                if (recs[REC_IDX(foundvideo, i)]) {
                    tmprecs[nbr] = recs[REC_IDX(foundvideo, i)];
                    nbr++;
                }
            }
            for (int i = 0; i < nbr; i++) {
                recs[REC_IDX(foundvideo, i)] = tmprecs[i];
            }
            num_entries[foundvideo] = nbr;
            free(tmprecs);

        } else {
            // Just delete this record
            freerec(recs[REC_IDX(foundvideo, foundidx)],"deleterecid(), II");
            recs[REC_IDX(foundvideo, foundidx)] = recs[REC_IDX(foundvideo, num_entries[foundvideo] - 1)];
            recs[REC_IDX(foundvideo, num_entries[foundvideo] - 1)] = NULL;
            num_entries[foundvideo]--;
        }
        sortrecs(foundvideo);
        return 1;
    }
}
