/* =========================================================================
 * File:        RECS.H
 * Description: The recs.c module hold all functions to manipulate the
 *              list of current defined recordings. This is mainatained
 *              as a vector for each video capture card which holds
 *              details on specific recordings.
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

#ifndef _RECS_H
#define	_RECS_H

#ifdef	__cplusplus
extern "C" {
#endif

// Calculate the correct offset i the memory for record with index 'i' for capture
// card 'video'
#define REC_IDX(a,b) (a*max_entries+b)

/*
 
 */
#define REC_MAX_NTITLE 256
#define REC_MAX_NCHANNEL 128
#define REC_MAX_NFILENAME 128
#define REC_MAX_NPREFIX 5
#define REC_MAX_TPROFILE_LEN 16
#define REC_MAX_TPROFILES  5

/**
 * This is the main structure to hold the information about one recording.
 * Each video stream has an array of pointers to these records which stores
 * the pending recordings. Up to MAX_ENTRIES recordings per video channel can
 * be stored
 *
 * Each entry corresponds to a one recording. A recording can either be
 * an individual recording or it can be part of a recurrent series of recordings.
 * Each recording has a uniqe sequence number and each recurrent recording has
 * in addition a recurrence id to be able to link  recording of the same
 * recurrent sequence together.
 */
struct recording_entry {
    // A unique sequence number for all recordings in memory
    unsigned seqnbr;

    // Title of recording
    char title[REC_MAX_NTITLE];

    // Channel (as text string)
    char channel[REC_MAX_NCHANNEL];

    // The full filename (including path)
    char filename[REC_MAX_NFILENAME];

    // Timestamp for start time
    time_t ts_start;

    // Timestamp for end time
    time_t ts_end;

    // The profile(s) to use for this recording
    char *transcoding_profiles[REC_MAX_TPROFILES];

    /* ------------------ */
    /* Recurrence setting */

    // Flag if this is a recurrent recording or not
    int recurrence;

    // The repeat type, daily, weekly, end so on
    int recurrence_type;

    // The number of recurrenceies in this serie
    unsigned recurrence_num;

    // How should the recurrene title be created,
    // 0 - add date/time to the base title
    // 1 - add a running number , e.g. 01 / 12
    int recurrence_mangling;

    // What character should be used to replace illegal file name characters
    char recurrence_mangling_prefix[REC_MAX_NPREFIX];

    // The unique id for this recurrent sequence
    unsigned recurrence_id; // A unique id for each recurrence sequence

    // The basename, i.e. the filename without the sequence information added
    char recurrence_filename[REC_MAX_NFILENAME]; // The basename

    // The title of this recurrent recording
    char recurrence_title[REC_MAX_NTITLE]; // The basename

    // The start number to use when mangling title/file names as 01 / 99
    unsigned recurrence_start_number; // What start number to use in the title mangling

    // What video card should be used for this recording
    unsigned video;
};

/* Basic key/val structure. Used with listreckeyval to return a list of recordings
 * with a title string as value and record index as key
 */
struct skeysval_t {
    char *key;
    char *val;
};


/**
 * The list with all planned recordings
 */
extern struct recording_entry **recs; // [MAX_VIDEO][MAX_ENTRIES];

/**
 * The number of entries/recordings for each video card
 */
extern unsigned *num_entries; //[MAX_VIDEO];

/**
 * A list of current ongogin recordings. When a recording is started
 * it is moved from the global list of planned recordings 'recs' to the
 * the position in this list thta corresponds to the video card used.
 */
extern struct recording_entry **ongoing_recs; //[MAX_VIDEO];

/**
 * Initialize memory and data structure to hold recordings
 */
void
initrecs(void);

/**
 * Free all memory used to hold all recordings
 */
void
freerecs(void);

/**
 * Delete the 'top' recording, i.e the next recording to be recorded.
 * This will also free all memory associated with this recording
 * @param video
 * @see removetoprec
 */
void
deletetoprec(const unsigned video);


/**
 * Remove the 'top' recording but do NOT deallocate memory associated with this
 * recording.
 * @param video
 * @see deletetoprec
 */
void
removetoprec(const unsigned video);

/**
 * Create a new recording post
 * @param title
 * @param filename
 * @param start
 * @param end
 * @param channel
 * @param recurrence
 * @param recurrence_type
 * @param recurrence_num
 * @param recurrence_mangling
 * @param profiles
 * @return The new entry initialized with the given arguments
 */
struct recording_entry *
newrec(const char *title, const char *filename, const time_t start,
        const time_t end, const char *channel, const int recurrence,
        const int recurrence_type, const unsigned recurrence_num,
        const int recurrence_mangling,
        char *profiles[]);

/**
 * Free memory associated with the gievn recording entry
 * @param entry The entry to be freed
 * @param caller The name of the calling function. USed in memory tracing
 */
void freerec(struct recording_entry *entry); //,char *caller);

/**
 * Try to insert a recording into the list for the specified video card
 * @param video
 * @param entry
 * @return Boolean., 0 = failed to insert, 1 sucess
 */
int
insertrec(unsigned video, struct recording_entry * entry);

/**
 * Dumpa  string that can serve as the title headline for a table of
 * recordings
 * @param style
 * @param buffer
 * @param bufflen
 */
void
dumprecord_header(int style, char *buffer, size_t bufflen);

/**
 * Dump a string representation of the given recording to the stated buffer
 * @param entry
 * @param style
 * @param buffer
 * @param bufflen
 */
void dumprecord(struct recording_entry* entry, int style, size_t idx, char *buffer, size_t bufflen);

/**
 * Dump a string representation of the record with the specified id to the stated buffer
 * @param seqnbr
 * @param repeats
 * @param style
 * @param buffer
 * @param bufflen
 * @return
 */
int
dumprecordid(unsigned seqnbr, int repeats, int style, size_t idx, char *buffer, size_t bufflen);

/**
 * Dump a list of all future recordings to the given file descriptor
 * @param style
 * @param fd
 */
void
listrecs(size_t maxrecs, int style, int fd);

/**
 * Dump a list of all future recordings in the specified buffer
 * @param buffer
 * @param maxlen
 * @param maxrecs
 * @param style
 * @return 0 on success, -1 on failure (most likely the buffer was to small to fit all records)
 */
int
listrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, int style);

/**
 * Dump a list of all future recordings in the specified buffer using HTML output format
 * @param buffer
 * @param maxlen
 * @param maxrecs
 * @param style
 * @param only_nonrepeat
 * @param use_csshtml
 * @return 0 on success, -1 on failure (most likely the buffer was to small to fit all records)
 */
int
listhtmlrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, size_t style, int only_nonrepeat, int use_csshtml);

/**
 * Dump a list of all future repeating recordings in the specified buffer using HTML output format
 * @param buffer
 * @param maxlen
 * @param maxrecs
 * @return 0 on success, -1 on failure (most likely the buffer was to small to fit all records)
 */
int
listrepeatrecsbuff(char *buffer, size_t maxlen, size_t maxrecs);

/**
 * Dump a list of all future repeating recordings in the specified buffer using HTML output format
 * @param buffer
 * @param maxlen
 * @param maxrecs
 * @param ts
 * @return 0 on success, -1 on failure (most likely the buffer was to small to fit all records)
 */
int
listhtmlrepeatrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, size_t style);

/**
 * Create a list of all currnet recordings
 * @param list
 * @param style
 * @return Number of records returned in list
 */
int
listrecskeyval(struct skeysval_t **list, int style);

/**
 * Delete the record with the specified id
 * @param seqnbr
 * @param allrecurrences
 * @return Boolean. 0 if failed, 1 otherwise
 */
int
deleterecid(unsigned seqnbr, int allrecurrences);

/**
 * Update the current profile settings for the recording with the specified seqnbr
 * @param seqnbr
 * @param profile
 * @return Boolean. 0 if failed, 1 otherwise
 */
int
updateprofile(unsigned seqnbr, char *profile);

#ifdef	__cplusplus
}
#endif

#endif	/* _RECS_H */

