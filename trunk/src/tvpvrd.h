/* =========================================================================
 * File:        TVPVRD.H
 * Description: This can be considered the "main" include file for the
 *              tvpvrd server. It contains definition of constants and
 *              global variables that need to be accessible throughout
 *              the rpogram.
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

#include <time.h>
#include <pthread.h>
#include <sys/param.h> // Needed to get MAX()
#include <errno.h>
#include <syslog.h>

#ifndef _TVPVRD_H
#define	_TVPVRD_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "config.h"

/*
 * Defining this constant makes it possible to run the server on a
 * computer that doesn't have a video card in order to be debug the
 * logic of the server on a computer missing video card.
 */
//#define DEBUG_SIMULATE

/*
 * VIDBUFSIZE integer
 * 300 KB Video buffer size. This is the data chunk size we will read from the
 * video stream into memory before storing it on the file in the file system.
 * This might not look very large but when we do the select() to wait for data
 * from the card the typical size returned is 80K-180K so 300K is enough.
 */
#define VIDBUFSIZE (300*1024)

/*
 * DEFAULT_PREFIX string
 * String for default prefix when mangling names in recurring recordings.
 * This string will used as a separator between the base title/filename and
 * the suffix computed for a recurring recording.
 */
#define DEFAULT_PREFIX "_"

/*
 * WELCOM_MSG
 * Server welcome message
 */
#define WELCOM_MSG \
	"Welcome to tvpvrd-%s  [%s] (build:%lu.%lu)\n"\
	"You are user number %d out of %d allowed.\n"\
	"Type 'exit' to disconnect.\n"\
	"Type 'h' to list available commands.\n"\
	"Connection will be closed after %d min of inactivity.\n\n"

// Clear variable section in memory
#define CLEAR(x) memset (&(x), 0, sizeof (x))

/*
 * Structure to hold the details on how to make the transcoding depending
 * on the profile. The actual transcoding is done via 'ffmpeg'
 */
struct ffmpeg_profile {
    char name[64];              /* Profile name */
    char mp4_preset[64];        /* Name of preset use -pre */
    int  mp4_bitrate;           /* Average bitrate -b */
    int  mp4_peakbitrate;       /* Peak bitrate -bt */
    int  mp4_pass;              /* Number of passes -pass */
    int  mp4_abitrate;          /* Audio bitrate -ab */
    int  mp4_croptop;           /* -croptop */
    int  mp4_cropbottom;        /* -cropbottom */
    int  mp4_cropleft;          /* -cropleft */
    int  mp4_cropright;         /* -cropright */
    int  mp2_bitrate;           /* HW Encoder average bitrate */
    int  mp2_peakbitrate;       /* HW Encoder peak bitrate */
    int  mp2_width;             /* HW Encoder video width */
    int  mp2_height;            /* HW Encoder video height */
};

/* Global information about the server */
extern char server_version[] ;
extern char server_program_name[32] ;

/*
 * abort_video int
 * Flag set by the main thread to signal to one of the recording thread
 * to cancel recording
 */
extern volatile int * volatile abort_video; //[MAX_VIDEO]

/*
 * ts_serverstart
 * Timestamp when server was started
 */
extern time_t ts_serverstart;

/*
 * cli_threads
 * The thread id for each thread that are handling a client
 */
extern pthread_t *cli_threads; //[MAX_CLIENTS];
extern int ncli_threads ;

/*
 * cli_ipadr
 * Keep track of IP address for all currrent connected clients.
 * Points to a dynamically allocated string buffer to hold the
 * human readable version of the address, e.g. 192.168.0.1
 */
extern char **client_ipadr; //[MAX_CLIENTS];

/*
 * client_tsconn
 * Keep track of timestamps when the client connected
 */
extern time_t *client_tsconn; //[MAX_CLIENTS];

// The size of the memory buffer used when reading video data from the device
extern int video_bufsize;

// The video buffers (one for each capture card)
extern char *video_buffer[];

// Whether all transcoding processes should also be killed when the server stops
extern int dokilltranscodings;

// Gloabl Mutex that must be called if the routine wants to access
// any datastructure that modifies the recordings
extern pthread_mutex_t recs_mutex;

void
tvp_mem_list(int sockd);

#ifdef	__cplusplus
}
#endif

#endif	/* _TVPVRD_H */

