/* =========================================================================
 * File:        RECS.C
 * Description: The recs.c module hold all functions to manipulate the
 *              list of current defined recordings. This is mainatained
 *              as a vector for each video capture card which holds
 *              details on specific recordings.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009-2011 Johan Persson
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
#include "utils.h"
#include "recs.h"
#include "stats.h"
#include "transc.h"
#include "datetimeutil.h"
#include "tvplog.h"
#include "tvplog.h"

/*
 * recs
 * A two dimensional array with a list of recordings per video stream
 * The REC_IDX macro is a shorthand to get hold of elements in the array
 * since this should be treated as a two-dimensional array
 * The reason this is not implemented as a two dimensional array is that
 * this is dynamically allocated as a single array.
 */
struct recording_entry **recs; // [MAX_VIDEO][MAX_ENTRIES];

/*
 * num_entries
 * Number of pending recording entries per video stream
 */
unsigned *num_entries; //[MAX_VIDEO];

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
static int global_seqnbr = 1;

/*
 * recurrence_id
 * Unique id for each recurrence sequence. This is used to know if a recording is
 * part of a recurrent sequence.
 */
static unsigned recurrence_id = 1;

/**
 * Check if the submitted entry is colliding/overlapping with an existing
 * entry in the pending recordings for the specified video stream or
 * any ongoing recordings.
 *
 * Return 1 if there is a collision 0 otherwise
 */
int
isentryoverlapping(unsigned video, struct recording_entry* entry) {
    // Check all existing entries if the start or stop
    // time is in between the existing start and stop time

    if (entry->recurrence == 0) {
        // No recurrence
        for (size_t i = 0; i < num_entries[video]; ++i) {
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

                logmsg(LOG_DEBUG,"New recurring entry collides with ongoing recording at video=%d",video);
                return 1;
            }
            if (entry->ts_end >= ongoing_recs[video]->ts_start &&
                entry->ts_end <= ongoing_recs[video]->ts_end) {

                logmsg(LOG_DEBUG,"New recurring entry collides with ongoing recording at video=%d",video);
                return 1;
            }
            if (entry->ts_start < ongoing_recs[video]->ts_start &&
                entry->ts_end > ongoing_recs[video]->ts_end) {

                logmsg(LOG_DEBUG,"New recurring entry collides with ongoing recording at video=%d",video);
                return 1;
            }
        }

    } else {
        int sy, sm, sd, sh, smin, ssec;
        int ey, em, ed, eh, emin, esec;

        int esy, esm, esd, esh, esmin, essec;
        int eey, eem, eed, eeh, eemin, eesec;


        // Recurrence. This means we need to check all future
        // recurrences for collisins as well.
        fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smin, &ssec);
        fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emin, &esec);
        time_t ts_start = entry->ts_start;
        time_t ts_end = entry->ts_end;

        for (size_t j = 0; j < entry->recurrence_num; j++) {

            for (size_t i = 0; i < num_entries[video]; ++i) {
                struct recording_entry *e = recs[REC_IDX(video, i)];
                if (ts_start >= e->ts_start && ts_start <= e->ts_end) {

                    logmsg(LOG_DEBUG,"New recurring entry collides at occurence %d with: '%s' on video %d in position %d",
                            j,e->title,video,i);
                    fromtimestamp(e->ts_start, &esy, &esm, &esd, &esh, &esmin, &essec);
                    fromtimestamp(e->ts_end, &eey, &eem, &eed, &eeh, &eemin, &eesec);
                    logmsg(LOG_DEBUG,"[e->ts_start=%u, e->ts_end=%u]=(%02d:%02d-%02d:%02d %02d/%02d-%02d/%02d)",
                                      e->ts_start,e->ts_end,esh,esmin,eeh,eemin,esd,esm,eed,eem);
                    logmsg(LOG_DEBUG,"[entry->ts_start=%u, entry->ts_end=%u]=(%02d:%02d-%02d:%02d %02d/%02d)",
                                      ts_start,ts_end,sh,smin,eh,emin,sd,sm);
                    return 1;
                }
                if (ts_end >= e->ts_start && ts_end <= e->ts_end) {

                    logmsg(LOG_DEBUG,"New recurring entry collides at occurence %d with: '%s' on video %d in position %d",
                            j,e->title,video,i);
                    fromtimestamp(e->ts_start, &esy, &esm, &esd, &esh, &esmin, &essec);
                    fromtimestamp(e->ts_end, &eey, &eem, &eed, &eeh, &eemin, &eesec);
                    logmsg(LOG_DEBUG,"[e->ts_start=%u, e->ts_end=%u]=(%02d:%02d-%02d:%02d %02d/%02d-%02d/%02d)",
                                      e->ts_start,e->ts_end,esh,esmin,eeh,eemin,esd,esm,eed,eem);
                    logmsg(LOG_DEBUG,"[entry->ts_start=%u, entry->ts_end=%u]=(%02d:%02d-%02d:%02d %02d/%02d)",
                                      ts_start,ts_end,sh,smin,eh,emin,sd,sm);
                    return 1;
                }
                if (ts_start < e->ts_start && ts_end > e->ts_end) {

                    logmsg(LOG_DEBUG,"New recurring entry collides at occurence %d with: '%s' on video %d in position %d",
                            j,e->title,video,i);
                    fromtimestamp(e->ts_start, &esy, &esm, &esd, &esh, &esmin, &essec);
                    fromtimestamp(e->ts_end, &eey, &eem, &eed, &eeh, &eemin, &eesec);
                    logmsg(LOG_DEBUG,"[e->ts_start=%u, e->ts_end=%u]=(%02d:%02d-%02d:%02d %02d/%02d-%02d/%02d)",
                                      e->ts_start,e->ts_end,esh,esmin,eeh,eemin,esd,esm,eed,eem);
                    logmsg(LOG_DEBUG,"[entry->ts_start=%u, entry->ts_end=%u]=(%02d:%02d-%02d:%02d %02d/%02d)",
                                      ts_start,ts_end,sh,smin,eh,emin,sd,sm);
                    return 1;
                }
            }

            if( ongoing_recs[video] ) {
                if (ts_start >= ongoing_recs[video]->ts_start && ts_start <= ongoing_recs[video]->ts_end) {

                    logmsg(LOG_DEBUG,"New entry collides at occurrence %d with ongoing recording at video=%d",j,video);
                    return 1;
                }
                if (ts_end >= ongoing_recs[video]->ts_start && ts_end <= ongoing_recs[video]->ts_end) {

                    logmsg(LOG_DEBUG,"New entry collides at occurrence %d with ongoing recording at video=%d",j,video);
                    return 1;
                }

                if (ts_start < ongoing_recs[video]->ts_start && ts_end > ongoing_recs[video]->ts_end) {

                    logmsg(LOG_DEBUG,"New entry collides at occurrence %d with ongoing recording at video=%d",j,video);
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
    num_entries     = (unsigned *) calloc(max_video, sizeof (int));

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

    for (unsigned i = 0; i < max_video; ++i) {
        for (unsigned j = 0; j < num_entries[i]; ++j) {
            if( recs[REC_IDX(i,j)] ) {
                freerec(recs[REC_IDX(i,j)]);
                recs[REC_IDX(i,j)] = NULL ;
            }
        }
    }
    free(recs);

    for (unsigned i = 0; i < max_video; ++i) {
        if( ongoing_recs[i] ) {
            freerec(ongoing_recs[i]);
        }
    }

    free(ongoing_recs);
    free(num_entries);
}

/**
 * Free a single recording entry
 * @param entry Pointer to record entry to erase
 * @param caller String for name of caller (used for enhanced memory debugging)
 */
void freerec(struct recording_entry *entry ) { // ,char *caller) {
    //logmsg(LOG_DEBUG,"freerec() called from '%s'",caller);
    for(int i=0; i < REC_MAX_TPROFILES ; i++) {
        free(entry->transcoding_profiles[i]);
    }
    free(entry);
}

/**
 * Create a new record from the given fields. This will in essence create a new
 * entry for a TV-program recording
 */
struct recording_entry *
newrec(const char *title, const char *filename, const time_t start,
        const time_t end, const char *channel, const int recurrence,
        const int recurrence_type, const unsigned recurrence_num,
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

    // Type: 0=Single, 1=day, 2=week, 3=month, 4=mon-fri, 5=sat-sun, 6=mon-thu, 7=tue-fri
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
sortrecs(const unsigned video) {
    qsort(&recs[video * max_entries], (size_t)num_entries[video], sizeof (struct recording_entry *), _cmprec);
}

/*
 * Helper function to insert a new recording in the list. Should never
 * be called directly.
 */
int
_insertrec(unsigned video, struct recording_entry* entry) {

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

/*
 * Insert a new recording in the list after checking that it doesn't
 * collide with an existing recording.
 * Return last usde sequence number > 0 on sucess and -1 on failure
 */
int
insertrec(unsigned video, struct recording_entry * entry) {
    size_t len;
    char *filetype;
    char *bname, *dname;
    char bnamecore[256];
    char tmpbuff[512], tmpbuff2[512], bname_buffer[256], dname_buffer[256];
    time_t ts_start, ts_end;
    int sy, sm, sd, sh, smin, ssec;
    int ey, em, ed, eh, emin, esec;
    struct recording_entry *newentry = NULL;

    if (isentryoverlapping(video, entry)) {
        return -1;
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
        len = (size_t)MIN((filetype - bname), 255);
        strncpy(bnamecore, bname, len);
        bnamecore[len] = 0;

        (void)adjust_initital_repeat_date(&entry->ts_start, &entry->ts_end, entry->recurrence_type);

        fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smin, &ssec);
        fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emin, &esec);
        ts_start = entry->ts_start;
        ts_end = entry->ts_end;

        assert(entry->recurrence_num > 0);

        for (size_t i=0; i < entry->recurrence_num; i++) {

            // Name mangling for title
            if (entry->recurrence_mangling == 0) {
                snprintf(tmpbuff, 512, "%s %d-%02d-%02d %02d.%02d", entry->title, sy, sm, sd, sh, smin);
            } else {
                snprintf(tmpbuff, 512, "%s (%02d/%02d)", 
                         entry->title,
                         (int)i + entry->recurrence_start_number,
                         entry->recurrence_num+entry->recurrence_start_number-1);
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

            newentry->seqnbr =  global_seqnbr++;
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
        freerec(entry);
    } else {
        entry->seqnbr = global_seqnbr++;
        _insertrec(video, entry);
    }
    // Return the last used sequence number
    return global_seqnbr-1;
}

/**
 * Give a textual representation to the recurrence type
 * It is the callers responsibility that the buff parameter is large
 * enough to hold at least 12 bytes.
 * @return 1 on success, 0 on failure
 */
int
getrectypestr(const int type, const int longformat, char *buffer, size_t maxlen) {
    // d=daily, w=weekly, m=monthly, f=Mon-Fri, s=Sat-Sun, t=Mon-Thu
    static char *names[] = {"-","d","w","m","f","s","t","n"};
    static char *longnames[] = {"-","daily","weekly","monthly","Mon-Fri","Sat-Sun","Mon-Thu","Tue-Fri"};
    if( type >= 0 && type < 8 ) {
        if( longformat == 0 )
            strncpy(buffer,names[type],maxlen);
        else
            strncpy(buffer,longnames[type],maxlen);
        buffer[maxlen-1] = '\0';
        return 0;
    }
    *buffer = '\0';
    return -1;
}


/*
 * Fill the supplied buffer with a textual representation of a
 * header line suitable for future printing of records from
 * 'dumprecord()'
 */
void
dumprecord_header(int style, char *buffer, size_t bufflen) {
    char hdbuffer[128];
    char tmpbuffer[512];
    const unsigned header_length=85;

    rptchr_r('=', header_length,hdbuffer);  
    time_t ts_tmp = time(NULL);
        
    if (style == 3) {
        
         snprintf(tmpbuffer, sizeof(tmpbuffer),
                "Generated by: %s %s, %s" 
                "%s\n"
                "%-4s"
                "%-8s"
                "%-11s"
                "%-6s"
                "%-6s"
                "%-40s"
                "%-10s\n"
                "%s\n",
                 server_program_name,server_version,ctime(&ts_tmp),
                 hdbuffer,
                "# ",
                "Ch ",
                "Date ",
                "Start ",
                "End ",
                "Title ",
                "Profile",                 
                 hdbuffer);        
        
        strncat(buffer, tmpbuffer, bufflen-1-strlen(buffer));
    } else {
        // TODO: Add proper formatting for the rest of the styles    
        *buffer = '\0';
    }

    buffer[bufflen-1] = '\0';
}

/**
 * Fill the supplied buffer with a representation of the record in the format
 * of a row in a HTML table
 * @param entry
 * @param buffer
 * @param bufflen
 */


void
dumprecord_row(struct recording_entry* entry, char *buffer, size_t bufflen, size_t idx,
                   struct css_record_style *rs, int format_repeat, int use_csshtml) {
    int sy, sm, sd, sh, smi, ss;
    int ey, em, ed, eh, emi, es;
    char rectypename[16];
    char rectypelongname[16];
    char profbuff[256], profile[REC_MAX_TPROFILE_LEN+1];
    struct tm result;
    static char wday_name[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static char month_name[12][4] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    profbuff[0] = '\0';
    unsigned left=255;
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

    fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
    fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emi, &es);
    (void)getrectypestr(entry->recurrence_type, 0, rectypename,16);
    (void)getrectypestr(entry->recurrence_type, 1, rectypelongname,16);

    // We need localtime to find the day of week for the start
    (void)localtime_r(&entry->ts_start, &result);

    if( ! format_repeat ) {
        if( use_csshtml ) {
            snprintf(buffer, bufflen, "<tr style=\"%s\">"
                                  "<td style=\"%s\">%03zu</td>\n"
                                  "<td style=\"%s\">%s</td>\n"
                                  "<td style=\"%s\">%s %s %02d</td>\n"
                                  "<td style=\"%s\">%02d:%02d</td>\n"
                                  "<td style=\"%s\">%02d:%02d</td>\n"
                                  "<td style=\"%s\">%s</td>\n"
                                  "<td style=\"%s\">%s</td></tr>\n",
                                rs->tr,
                                rs->td_l, idx,
                                rs->td_i, entry->channel,
                                rs->td_i, wday_name[result.tm_wday], month_name[sm-1], sd,
                                rs->td_i, sh, smi,
                                rs->td_i, eh, emi,
                                rs->td_i, entry->title,
                                rs->td_r, profbuff);
        } else {

            snprintf(buffer, bufflen, "%03zu "
                                  "%-8s"
                                  "%s %s %02d "
                                  "%02d:%02d "
                                  "%02d:%02d "
                                  "%-43s "
                                  "%-10s \n",
                                idx,
                                entry->channel,
                                wday_name[result.tm_wday], month_name[sm-1], sd,
                                sh, smi,
                                eh, emi,
                                entry->title,
                                profbuff);
        }
    } else {
        if( use_csshtml ) {
            snprintf(buffer, bufflen, "<tr style=\"%s\">"
                                  "<td style=\"%s\">%03zu</td>\n"
                                  "<td style=\"%s\">%s</td>\n"
                                  "<td style=\"%s\">%s %s %02d</td>\n"
                                  "<td style=\"%s\">%02d:%02d</td>\n"
                                  "<td style=\"%s\">%02d:%02d</td>\n"
                                  "<td style=\"%s\">%s</td>\n"
                                  "<td style=\"%s\">%02d / %02d</td>\n"
                                  "<td style=\"%s\">%s</td>\n"
                                  "<td style=\"%s\">%s</td></tr>\n",
                                rs->tr,
                                rs->td_l, idx,
                                rs->td_i, entry->channel,
                                rs->td_i, wday_name[result.tm_wday], month_name[sm-1], sd,
                                rs->td_i, sh, smi,
                                rs->td_i, eh, emi,
                                rs->td_i, rectypelongname,
                                rs->td_i, entry->recurrence_start_number, entry->recurrence_num+entry->recurrence_start_number-1,
                                rs->td_i, entry->recurrence_title,
                                rs->td_r, profbuff);
        } else {
            snprintf(buffer, bufflen,"%03zu "
                                      "%-8s"
                                      "%s %s %02d "
                                      "%02d:%02d "
                                      "%02d:%02d "
                                      "%-9s"
                                      "%03d/%03d  "
                                      "%-25s"
                                      "%-10s\n",
                                        idx,
                                        entry->channel,
                                        wday_name[result.tm_wday], month_name[sm-1], sd,
                                        sh, smi,
                                        eh, emi,
                                        rectypelongname,
                                        entry->recurrence_start_number, entry->recurrence_num+entry->recurrence_start_number-1,
                                        entry->recurrence_title,
                                        profbuff);
        }

    }
    buffer[bufflen-1] = '\0';
    
}

void
dumprecord_header2(char *buffer, size_t bufflen, struct css_record_style *rs, int use_csshtml) {

    if( use_csshtml ) {
        snprintf(buffer, bufflen,
            "<tr style=\"%s\">"
            "<th style=\"%s\">#</th>\n"
            "<th style=\"%s\">Ch</th>\n"
            "<th style=\"%s\">Date</th>\n"
            "<th style=\"%s\">Start</th>\n"
            "<th style=\"%s\">End</th>\n"
            "<th style=\"%s\">Title</th>\n"
            "<th style=\"%s\">Profile</th>\n"
            "</tr>\n",
            rs->tr,
            rs->td_l, 
            rs->td_i, 
            rs->td_i, 
            rs->td_i, 
            rs->td_i, 
            rs->td_i, 
            rs->td_r);
    } else {
        const unsigned header_length=85;
        char hdbuffer[128];
        rptchr_r('=', header_length,hdbuffer);  
        snprintf(buffer, bufflen,
                "%s\n"
                "%-4s"
                "%-8s"
                "%-11s"
                "%-6s"
                "%-6s"
                "%-43s"
                "%-10s\n"
                "%s\n",
                hdbuffer,
                "# ",
                "Ch ",
                "Date ",
                "Start ",
                "End ",
                "Title ",
                "Profile",
                hdbuffer);
    }
    buffer[bufflen-1] = '\0';
    
}

void
dumprepeatrecord_header(char *buffer, size_t bufflen, struct css_record_style *rs, int use_csshtml) {

    if (use_csshtml) {
        snprintf(buffer, bufflen,
                "<tr style=\"%s\">"
                "<th style=\"%s\">#</th>\n"
                "<th style=\"%s\">Ch</th>\n"
                "<th style=\"%s\">Date</th>\n"
                "<th style=\"%s\">Start</th>\n"
                "<th style=\"%s\">End</th>\n"
                "<th style=\"%s\">Repeat</th>\n"
                "<th style=\"%s\">Next/Tot</th>\n"
                "<th style=\"%s\">Title</th>\n"
                "<th style=\"%s\">Profile</th>\n"
                "</tr>\n",
                rs->tr,
                rs->td_l,
                rs->td_i,
                rs->td_i,
                rs->td_i,
                rs->td_i,
                rs->td_i,
                rs->td_i,
                rs->td_i,
                rs->td_r);
    } else {
        snprintf(buffer, bufflen,
                "=======================================================================================\n"
                "%-4s"
                "%-8s"
                "%-11s"
                "%-6s"
                "%-6s"
                "%-9s"
                "%-8s"
                "%-25s"
                "%-10s\n"
                "=======================================================================================\n",
                "# ",
                "Ch ",
                "Date ",
                "Start ",
                "End ",
                "Repeat ",
                "Next/Tot ",
                "Title ",
                "Profile");

    }
    buffer[bufflen - 1] = '\0';
}

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

int
listhtmlrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, size_t style, int only_nonrepeat, int use_csshtml) {
    struct recording_entry **entries;
    char tmpbuffer[2048];
    size_t const n_tmpbuff=2048;
    struct css_table_style ts;

    bzero(&ts, sizeof(struct css_table_style));
    set_listhtmlcss(&ts, style);

    entries = calloc((size_t)(max_video*max_entries),sizeof (struct recording_entry *));
    if( entries == NULL ) {
        logmsg(LOG_ERR,"_listrecs() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }
    bzero(tmpbuffer, n_tmpbuff);
    *buffer = '\0';

    // We combine all recordings on all videos in order to
    // give a combined sorted list of pending recordings
    size_t k=0;
    for (unsigned video = 0; video < max_video; video++) {
        for (unsigned i = 0; i < num_entries[video]; ++i) {
            entries[k++] = recs[REC_IDX(video, i)];
        }
    }

    qsort(entries, k, sizeof (struct recording_entry *), _cmprec);

    if( maxrecs > 0 )
        k = MIN(k,maxrecs);

    int max = maxlen;

    time_t ts_tmp = time(NULL);
    if( use_csshtml ) {
        snprintf(buffer,max,"<div style=\"%s\">Generated by: <strong>%s %s</strong>, %s</div>"
                        "<table border=0 style=\"%s\" cellpadding=4 cellspacing=0>\n",
                        ts.date,server_program_name,server_version,ctime(&ts_tmp),
                        ts.table);
    } else {
        snprintf(buffer,max,"Generated by: %s %s, %s",server_program_name,server_version,ctime(&ts_tmp));
    }
    max -= strlen(buffer);

    dumprecord_header2(tmpbuffer, n_tmpbuff, &ts.header_row, use_csshtml);
    strncat(buffer,tmpbuffer,max-1);
    max -= strlen(tmpbuffer);

    // The loop below gets a bit "overly" complicated due to the fact that when we want to
    // format the last row with another CSS style compared with the inner row. This means
    // that we must know when we are on the last row. If we could just loop through all the
    // entries that would be trivial. However, when we only want to print non-recurring
    // recordings we have no way of knowing in advance which entry is the last one. So to handle
    // this we keep a running recording of the last seen entry which gest printed outside the
    // loop with the special last row CSS. 


    // we need to identify the special case where we have only one row. In that case if must
    // be treated as the last row directly. To find this out we have to do the loop once first
    // just to count how many records we should print and then we print them. 

    struct recording_entry **recs_to_dump = calloc(k, sizeof (struct recording_entry *));
    size_t nbr_recs=0;
    struct recording_entry *entry;
    for( size_t i=0; i < k; ++i ) {
      entry = entries[i];
      if( !only_nonrepeat || (only_nonrepeat && ! entry->recurrence)) {
	  recs_to_dump[nbr_recs] = entry;
	  ++nbr_recs;
      }
    }

    
    for(size_t i=0; nbr_recs > 0 &&  i < nbr_recs-1 && max > 0; i++ ) {
      if( i % 2 )
	dumprecord_row(recs_to_dump[i], tmpbuffer, n_tmpbuff, i+1, &ts.odd_row, FALSE, use_csshtml);
      else
	dumprecord_row(recs_to_dump[i], tmpbuffer, n_tmpbuff, i+1, &ts.even_row, FALSE, use_csshtml);

      if( strlen(tmpbuffer) >= (size_t)max ) {
	max = -1;
	logmsg(LOG_ERR,"Internal error. Not enough memory allocated for recording list");
      } else {
	strncat(buffer,tmpbuffer,max-1);
	max -= strlen(tmpbuffer);
      }
    }

    // Now print the last row. This can either be a normal row with the formatting
    // for a last row or it could be the string "(No recordings)" to indicate an empty list

    if( 0==nbr_recs ) {
        if( use_csshtml ) {
            snprintf(tmpbuffer, n_tmpbuff,
                "<tr><td style=\"%s\">&nbsp;</td><td style=\"%s font-style:italic;text-align:center;\" colspan=5>(No recordings)</td><td style=\"%s\">&nbsp;</td></tr>",
                ts.last_even_row.td_l,ts.last_even_row.td_i,ts.last_even_row.td_r);
        } else {
            snprintf(tmpbuffer, n_tmpbuff,"                        (No recordings)\n\n");
        }      
    } else {
      if( nbr_recs % 2 )
	dumprecord_row(recs_to_dump[nbr_recs-1], tmpbuffer, n_tmpbuff, nbr_recs, &ts.last_odd_row, FALSE, use_csshtml);
      else
	dumprecord_row(recs_to_dump[nbr_recs-1], tmpbuffer, n_tmpbuff, nbr_recs, &ts.last_even_row, FALSE, use_csshtml);
      
    }

    if( strlen(tmpbuffer) >= (size_t)max ) {
        max = -1;
        logmsg(LOG_ERR,"Internal error. Not enough memory allocated for recording list");
    } else {
        strncat(buffer,tmpbuffer,max-1);
        max -= strlen(tmpbuffer);
    }
   

    if( max > 0 && use_csshtml ) {
        snprintf(tmpbuffer,n_tmpbuff,"</table>\n");
        strncat(buffer,tmpbuffer,max-1);
        buffer[maxlen-1] = '\0';
    }

    free(entries);

    return max > 0 ? 0 : -1;

}

int
_listrepeatrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, size_t style, int use_csshtml) {
    struct recording_entry **entries;
    char tmpbuffer[2048];
    size_t const n_tmpbuff = 2048;
    struct css_table_style ts;
    int max = maxlen;
    unsigned saved_recrec[2 * MAX_ENTRIES];

    bzero(&ts, sizeof (struct css_table_style));
    set_listhtmlcss(&ts, style);

    entries = calloc((size_t) (max_video * max_entries), sizeof (struct recording_entry *));
    if (entries == NULL) {
        logmsg(LOG_ERR, "_listrecs() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }
    bzero(tmpbuffer, n_tmpbuff);
    *buffer = '\0';

    // We combine all recordings on all videos in order to
    // give a combined sorted list of pending recordings
    size_t numrecs = 0;
    for (unsigned video = 0; video < max_video; video++) {
        for (unsigned i = 0; i < num_entries[video]; ++i) {
            entries[numrecs++] = recs[REC_IDX(video, i)];
        }
    }

    qsort(entries, numrecs, sizeof (struct recording_entry *), _cmprec);

    if (maxrecs > 0)
        numrecs = MIN(numrecs, maxrecs);

    time_t ts_tmp = time(NULL);

    if( use_csshtml ) {
        snprintf(buffer, max, "<div style=\"%s\">Generated by: <strong>%s %s</strong>, %s</div>"
            "<table border=0 style=\"%s\" cellpadding=4 cellspacing=0>\n",
            ts.date, server_program_name, server_version, ctime(&ts_tmp),
            ts.table);
    } else {
        snprintf(buffer, max, "Generated by: %s %s, %s",
                 server_program_name, server_version, ctime(&ts_tmp));
    }
    max -= strlen(buffer);

    dumprepeatrecord_header(tmpbuffer, n_tmpbuff, &ts.header_row, use_csshtml);
    strncat(buffer,tmpbuffer,max-1);
    max -= strlen(tmpbuffer);
    

    // Find out all the recurring entries first in order to figure out how many there are
    size_t nsaved_recrec = 0;
    size_t nprinted_recrec=0;
    for (size_t idx = 0; idx < numrecs; ++idx) {
        if (entries[idx]->recurrence == 0)
            continue;
        size_t i = 0;
        while (i < nsaved_recrec && (saved_recrec[i] != entries[idx]->recurrence_id)) {
            i++;
        }
        if (i == nsaved_recrec) {
            saved_recrec[nsaved_recrec] = entries[idx]->recurrence_id;
            nsaved_recrec++;
        }
    }

    for (size_t idx = 0; idx < numrecs; ++idx) {

        // We are only interested in recurring entries. Skip other
        if (entries[idx]->recurrence == 0)
            continue;

        size_t i = 0;
        while (i < nprinted_recrec && (saved_recrec[i] != entries[idx]->recurrence_id)) {
            i++;
        }
        if (i == nprinted_recrec) {
            // Not yet printed

            // Start by finding the lowest start number in the sequence, this will be the number
            // of the next recording
            unsigned min_start_number = entries[idx]->recurrence_start_number;
            for (unsigned j = idx + 1; j < numrecs; j++) {
                if (entries[j]->recurrence &&
                    entries[j]->recurrence_id == entries[idx]->recurrence_id) {
                    min_start_number = MIN(entries[j]->recurrence_start_number, min_start_number);
                }
            }
            saved_recrec[nprinted_recrec] = entries[idx]->recurrence_id;
            nprinted_recrec++;
                      
            if( nprinted_recrec == nsaved_recrec ) { // lastrow
                if( i % 2 )
                    dumprecord_row(entries[idx], tmpbuffer, n_tmpbuff, nprinted_recrec, &ts.last_odd_row, TRUE, use_csshtml);
                else
                    dumprecord_row(entries[idx], tmpbuffer, n_tmpbuff, nprinted_recrec, &ts.last_even_row, TRUE, use_csshtml);
            } else {
                if( i % 2 )
                    dumprecord_row(entries[idx], tmpbuffer, n_tmpbuff, nprinted_recrec, &ts.odd_row, TRUE, use_csshtml);
                else
                    dumprecord_row(entries[idx], tmpbuffer, n_tmpbuff, nprinted_recrec, &ts.even_row, TRUE, use_csshtml);
            }

            if( strlen(tmpbuffer) >= (size_t)max ) {
                max = -1;
                logmsg(LOG_ERR,"Internal error. Not enough memory allocated for recording list");
            } else {
                strncat(buffer,tmpbuffer,max-1);
                max -= strlen(tmpbuffer);
            }
        }
    }

    if( max > 0 && use_csshtml) {
        snprintf(tmpbuffer,n_tmpbuff,"</table>\n");
        strncat(buffer,tmpbuffer,max-1);
        buffer[maxlen-1] = '\0';
    }

    free(entries);

    return max > 0 ? 0 : -1;
}

int
listhtmlrepeatrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, size_t style) {
   return _listrepeatrecsbuff(buffer, maxlen, maxrecs, style, TRUE);
}

int
listrepeatrecsbuff(char *buffer, size_t maxlen, size_t maxrecs) {
   return _listrepeatrecsbuff(buffer, maxlen, maxrecs, 0, FALSE);
}


/*
 * Fill the supplied buffer with a textual representation of the recording
 * entry pointed to by 'entry'. The style affect how the string is formatted
 *
 * style	Format
 * ----------------------------------------------
 * 0		One line, short format
 * 1		Record, several lines, short format
 * 2		Record, several lines, long format
 * 3            Brief only channel,date,start,end,title,profile (human readable)
 * 4            Fancy, use "today" and "tomorrow" as applicable
 * 9            Just list timestamp for start and stop time for each recording
 *              this is a special format used for the shutdown daemon to get
 *              as list of upcoming recordings in order to know when to start up
 *              and when to shut down recording server automatically
 */
void
dumprecord(struct recording_entry* entry, int style, size_t idx,char *buffer, size_t bufflen) {
    int sy, sm, sd, sh, smi, ss;
    int ey, em, ed, eh, emi, es;
    char rectypename[16];
    char rectypelongname[16];
    struct tm result;
    static char wday_name[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static char month_name[12][4] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    fromtimestamp(entry->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
    fromtimestamp(entry->ts_end, &ey, &em, &ed, &eh, &emi, &es);
    (void)getrectypestr(entry->recurrence_type, 0, rectypename,16);
    (void)getrectypestr(entry->recurrence_type, 1, rectypelongname,16);

    // We need localtime to find the day of week for the start
    (void)localtime_r(&entry->ts_start, &result);

    char profbuff[256], profile[REC_MAX_TPROFILE_LEN+1];
    profbuff[0] = '\0';
    unsigned left=255;
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
        
    // One line short format
    if (style == 0) {


        snprintf(buffer, bufflen, "[%03d|%-8.8s|%s %s %02d|%02d:%02d|%02d:%02d|%-30s|%s]\n",
                entry->seqnbr,
                entry->channel,
                wday_name[result.tm_wday], month_name[sm-1], sd,
                sh, smi, eh, emi, entry->title,profbuff);

    } else if ( style == 3 ) {
           
 
        snprintf(buffer, bufflen, 
                "%03d "
                "%-8s" 
                "%s %s %02d "
                "%02d:%02d "
                "%02d:%02d "
                "%-40s"
                "%-10s\n",
                idx,
                entry->channel,
                wday_name[result.tm_wday], month_name[sm-1], sd,
                sh, smi,
                eh, emi,
                entry->title,
                profbuff);
                

    } else if ( style == 4 ) {
        // Fancy format
        time_t now = time(NULL);
        int now_y, now_m, now_d,now_h,now_mi,now_s;
        fromtimestamp(now, &now_y, &now_m, &now_d, &now_h, &now_mi, &now_s);

        if( sy==now_y && sm==now_m && sd==now_d ) {
            snprintf(buffer, bufflen, "today %02d:%02d-%02d:%02d %-7.7s \"%s\"\n",
                    sh, smi,
                    eh,emi,
                    entry->channel,
                    entry->title);
        } else if( sy==now_y && sm==now_m && sd==now_d+1 ) {
            snprintf(buffer, bufflen, "tomorrow %02d:%02d-%02d:%02d %-7.7s \"%s\"\n",
                    sh, smi,
                    eh,emi,
                    entry->channel,
                    entry->title);
        } else {
            snprintf(buffer, bufflen, "%s %s %02d %02d:%02d-%02d:%02d %-7.7s \"%s\"\n",
                    wday_name[result.tm_wday],
                    month_name[sm-1], sd,
                    sh, smi,
                    eh,emi,
                    entry->channel,
                    entry->title);
        }

    } else if ( style == 9 ) {
        // Only return timestamp
        snprintf(buffer, bufflen, "%ld %ld %s\n",entry->ts_start,entry->ts_end,entry->title);

    } else {
        // Format 1 or 2 so we must differentiate a recurring and single record
        // 1 Record several lines, long format
        // 2 Record several lines, short format
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
                        "Repeats", rectypelongname, entry->recurrence_num - 1, entry->recurrence_id,
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
                        "Repeats", rectypelongname, entry->recurrence_num - 1);

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
dumprecordid(unsigned seqnbr, int repeats, int style, size_t idx, char *buffer, size_t bufflen) {
    int found;
    unsigned i, video, rid;
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
        size_t left = bufflen ;
        if (entry->recurrence && repeats) {
            rid = entry->recurrence_id;
            for (i = 0; i < num_entries[video]; i++) {
                if (recs[REC_IDX(video, i)]->recurrence_id == rid) {
                    dumprecord(recs[REC_IDX(video, i)], style, idx, tmpbuff, 512);
                    if( left > strnlen(tmpbuff,511)) {
                        strncat(buffer, tmpbuff, left);
                        left -= strnlen(tmpbuff,sizeof(tmpbuff));
                    }
                    else {

                        if( left > 3 )
                            strncat( buffer, "...\n", bufflen-1-strlen(buffer));
                        return found;

                    }
                }
            }

        } else {
            dumprecord(entry, style, idx, buffer, bufflen);
        }
    }
    return found;
}

/*
 * Print a list of all recordings to specified file for the specified video stream
 */
void
listrecs(size_t maxrecs, int style, int fd) {
    struct recording_entry **entries;
    char buffer[2048];
    struct css_table_style ts;

    bzero(&ts, sizeof(struct css_table_style));
    set_listhtmlcss(&ts, style);    

    entries = calloc((size_t)(max_video*max_entries), sizeof (struct recording_entry *));
    if( entries == NULL ) {
        logmsg(LOG_ERR,"listrecs() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }
    bzero(buffer, sizeof(buffer));

    // We combine all recordings on all videos in order to
    // give a combined sorted list of pending recordings
    size_t k=0;
    for (size_t video = 0; video < max_video; video++) {
        for (size_t i = 0; i < num_entries[video]; ++i) {
            entries[k++] = recs[REC_IDX(video, i)];
        }
    }

    qsort(entries, k, sizeof (struct recording_entry *), _cmprec);

    if( maxrecs > 0 )
        k = MIN(k,maxrecs);

    dumprecord_header(style, buffer, sizeof(buffer));
    _writef(fd, buffer);
    
    for(size_t i=0; i < k; i++ ) {
        dumprecord(entries[i], style, i+1, buffer, sizeof(buffer));
        _writef(fd, buffer);
    }

    free(entries);
}

/*
 * Same as listrecs() but dump all records to the specified buffer instead
 * of a file descriptor
 */
int
listrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, int style) {
    struct recording_entry **entries;
    char tmpbuffer[2048];

    entries = calloc((size_t)(max_video*max_entries),sizeof (struct recording_entry *));
    if( entries == NULL ) {
        logmsg(LOG_ERR,"_listrecs() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }
    bzero(tmpbuffer, 2048);
    *buffer = '\0';

    // We combine all recordings on all videos in order to
    // give a combined sorted list of pending recordings
    size_t k=0;
    for (unsigned video = 0; video < max_video; video++) {
        for (unsigned i = 0; i < num_entries[video]; ++i) {
            entries[k++] = recs[REC_IDX(video, i)];
        }
    }

    qsort(entries, k, sizeof (struct recording_entry *), _cmprec);

    if( maxrecs > 0 )
        k = MIN(k,maxrecs);

    int max = maxlen;
    for(size_t i=0; i < k && max > 0; i++ ) {
        dumprecord(entries[i], style, i+1, tmpbuffer, 2048);

        if( strlen(tmpbuffer) >= (size_t)max ) {
            max = -1;
            logmsg(LOG_ERR,"Internal error. Not enough memory allocated for recording list");
        } else {
            strncat(buffer,tmpbuffer,max-1);
            max -= strlen(tmpbuffer);
        }
    }

    buffer[maxlen-1] = '\0';

    free(entries);

    return max > 0 ? 0 : -1;

}

/*
 * Dump a list of all recordings in an array suitable for a HTML SELECT
 * statment, i.e. the value in pos i and the record id in pos i+1
 * It is the calling routines responsibility to free the memory
 * allocated to the list argument.
 */

int
listrecskeyval(struct skeysval_t **list, int style) {
    struct recording_entry **entries;
    char tmpbuffer[2048];

    entries = calloc((size_t)(max_video*max_entries),sizeof (struct recording_entry *));
    if( entries == NULL ) {
        logmsg(LOG_ERR,"_listrecskeyval() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }

    *list = calloc((size_t)(2*max_entries),sizeof (struct skeysval_t));
    if( *list == NULL ) {
        logmsg(LOG_ERR,"_listrecskeyval() : Out of memory. Aborting program.");
        exit(EXIT_FAILURE);
    }

    // We combine all recordings on all videos in order to
    // give a combined sorted list of pending recordings
    size_t k=0;
    for (unsigned video = 0; video < max_video; video++) {
        for (unsigned i = 0; i < num_entries[video]; ++i) {
            entries[k++] = recs[REC_IDX(video, i)];
        }
    }

    qsort(entries, k, sizeof (struct recording_entry *), _cmprec);

    for(size_t i=0; i < k; i++ ) {
        dumprecord(entries[i], style, i+1, tmpbuffer, 2048);
        (*list)[i].val = strdup(tmpbuffer);
        snprintf(tmpbuffer,2048,"%d",(*entries[i]).seqnbr);
        (*list)[i].key = strdup(tmpbuffer);
    }

    free(entries);
    
    return (int)k;
}


/*
 * Delete the top recording in the list for the specified video stream
 * NOTE: This will also free the memory occupied by that record
 */
void
deletetoprec(const unsigned video) {
    if (num_entries[video] < 1) {
        logmsg(LOG_ERR, "Cannot delete records since there are no recordings for video %d\n", video);
    } else {
        if( recs[REC_IDX(video, 0)] ) {
            freerec(recs[REC_IDX(video, 0)]);
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
 * occupied by the record. This is useful when we just want to move the record
 * from the waiting recording list to the ongoing recording list. In this case
 * we want to remove it from the waiting que but not delete the record.
 */
void
removetoprec(const unsigned video) {
    if (num_entries[video] < 1) {
        logmsg(LOG_ERR, "Cannot delete records since there are no recordings for video %d.",video);
    } else {
        recs[REC_IDX(video, 0)] = recs[REC_IDX(video, num_entries[video] - 1)];
        recs[REC_IDX(video, num_entries[video] - 1)] = NULL;
        num_entries[video]--;
        sortrecs(video);
    }
}

/**
 * Update the recording profile in an already existing recording (with seq.nbr)
 * @param seqnbr
 * @param profile
 * @return
 */
int 
updateprofile(unsigned seqnbr, char *profile) {

    // Check that the named profile really exists
    if( ! transcoding_profile_exist(profile) ) {
        return 0;
    }

    // First find the record with this sequence number
    int foundidx=-1, foundvideo=-1,found = 0;
    for (unsigned video = 0; video < max_video && !found; video++) {
        for (unsigned i = 0; i < num_entries[video] && !found; i++) {
            if (recs[REC_IDX(video, i)]->seqnbr == seqnbr) {
                found = 1;
                foundidx = (int)i;
                foundvideo = (int)video;
            }
        }
    }
    if (!found) {
        return 0;
    } else {
        strncpy(recs[REC_IDX((unsigned)foundvideo, (unsigned)foundidx)]->transcoding_profiles[0],
                profile,
                (size_t)(REC_MAX_TPROFILE_LEN-1));
        recs[REC_IDX((unsigned)foundvideo, (unsigned)foundidx)]->transcoding_profiles[0][REC_MAX_TPROFILE_LEN-1] = '\0';
        return seqnbr;
    }
}

/*
 * Delete a recording with specified sequence number.
 * If "allrecurrences" is true then all recurrent recording
 * will be deleted if the specified record is part of a sequnce of recordings
 * (recurrent recording)
 */
int
deleterecid(unsigned seqnbr, int allrecurrences) {
    int found=0, foundidx=-1, foundvideo=-1;
    unsigned rid, nbr;

    // First find the record with this sequence number
    found = 0;
    for (unsigned video = 0; video < max_video && !found; video++) {
        for (unsigned i = 0; i < num_entries[video] && !found; i++) {
            if (recs[REC_IDX(video, i)]->seqnbr == seqnbr) {
                found = 1;
                foundidx = (int)i;
                foundvideo = (int)video;
            }
        }
    }

    if (!found) {

        return 0;
        
    } else {

        // Check if the is part of a recurrence sequence
        if (recs[REC_IDX((unsigned)foundvideo, (unsigned)foundidx)]->recurrence && allrecurrences) {

            // Delete all recordings part of this repeated recording
            rid = recs[REC_IDX((unsigned)foundvideo, (unsigned)foundidx)]->recurrence_id;
            for (unsigned i = 0; i < num_entries[foundvideo]; i++) {
                if (recs[REC_IDX((unsigned)foundvideo, i)]->recurrence_id == rid) {
                    freerec(recs[REC_IDX((unsigned)foundvideo, i)]);
                    recs[REC_IDX((unsigned)foundvideo, i)] = NULL;
                }
            }

            // Now we need to compact the array of recordings
            nbr = 0;
            struct recording_entry **tmprecs = calloc((size_t)max_entries, sizeof (struct recording_entry *));
            for (unsigned i = 0; i < num_entries[foundvideo]; i++) {
                if (recs[REC_IDX((unsigned)foundvideo, i)]) {
                    tmprecs[nbr] = recs[REC_IDX((unsigned)foundvideo, i)];
                    nbr++;
                }
            }
            for (unsigned i = 0; i < nbr; i++) {
                recs[REC_IDX((unsigned)foundvideo, i)] = tmprecs[i];
            }
            num_entries[foundvideo] = nbr;
            free(tmprecs);

        } else {
            // Just delete this record
            freerec(recs[REC_IDX((unsigned)foundvideo, (unsigned)foundidx)]);
            recs[REC_IDX((unsigned)foundvideo, (unsigned)foundidx)] = recs[REC_IDX((unsigned)foundvideo, num_entries[foundvideo] - 1)];
            recs[REC_IDX((unsigned)foundvideo, num_entries[foundvideo] - 1)] = NULL;
            num_entries[foundvideo]--;
        }
        sortrecs((unsigned)foundvideo);
        return 1;
    }
}

/* EOF */
