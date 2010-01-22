/* =========================================================================
 * File:        TVPVRD.H
 * Description: This can be considered the "main" include file for the
 *              tvpvrd server. It contains definition of constants and
 *              global variables that need to be accessible throughout
 *              the rpogram.
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

#include <time.h>
#include <pthread.h>
#include <sys/param.h> // Needed to get MAX()
#include <iniparser.h>
#include <errno.h>

#ifndef _TVPVRD_H
#define	_TVPVRD_H

#ifdef	__cplusplus
extern "C" {
#endif


/*
 * Defining this constant makes it possible to run the server on a
 * computer that doesn't have a video card in order to be debug the
 * logic of the server on a computer missing video card.
 */
//#define DEBUG_SIMULATE
/*
 * Defining this constant will make the server keep on waiting
 * for a read from the capture card indefinitely. This should only
 * be set when debugging a potential HW issue with the capture card
 */
//#define IGNORE_CARD_TIMEOUTS


/*
 * Note: All the defines for general settings are read from an inifile
 * If the ini file is missing some values then the defines below will be used.
 */

/*
 * INIFILE_NAME string
 * Name and path of inifile
 */
#define INIFILE_NAME "tvpvrd.conf"

/*
 * DEFAULT_DATADIR string
 * Deafult directory where video files will be stored if no directory path
 * is specified with the filenames
 */
#define DEFAULT_DATADIR "/data/pvr/"

/*
 * XMLDBFILE_NAME
 * Name of XML database file where all recordings are stored
 * This is stored in the xmldb directory under the datadir root
 *
 */
#define XMLDBFILE_NAME "tvpvrd_db.xml"

/*
 * DEFAULT_DAEMONIZE bool
 * If we should run as a daemon, this setting is the default if not specified on the command
 * line or in the ini-filename
 */
#define DEFAULT_DAEMONIZE 1

/*
 * DEFAULT_USERNAME string
 * What user should the server be running as
 */
#define DEFAULT_USERNAME "tvpvrd"

/*
 * PORT integer
 * The port used to communicate with the server unless otherwise specified
 * in the main configuration.
 */
#define PORT 9300

/*
 * MAX_VIDEO integer
 * The number of available video streams. This is usually the same as the number
 * of TV-Cards available in the system since most TV-Cards only have one MPEG encoder
 */
#define MAX_VIDEO 2


/*
 * REQUIRE_PASSWORD boolean
 * Require password in order to connect to server
 */
#define REQUIRE_PASSWORD 0

/*
 * MAX_ENTRIES integer
 * The maximum number of pending entries we can have per video stream
 */
#define MAX_ENTRIES 512

/*
 * TIME_RESOLUTION integer
 * This time determnes how often (in seconds) the daemon wakes up to check the pending list
 * of recordings if a recording is about to be started. In ractice this means that we can
 * be up to TIME_RESOLUTION s before the specified time when the recording starts. A loweer
 * value means better accuracy but higher system load. The default 3 seconds should be ok
 * for most circumstances.
 */
#define TIME_RESOLUTION 3

/*
 * VIDBUFSIZE integer
 * 16 MB Video buffer size. This is the data chunk size we will read from the
 * video stream into memory before storing it on the file in the file system.
 * A larger buffer size reduces disk load. On the other hand a recording will only
 * stop after a chunk has been read. So the recoring might in worst case
 * be one chunk size larger recorded after the ending time.
 */
#define VIDBUFSIZE 4*1024*1024

/*
 * VIDEO_DEVICE_BASENAME string
 * Basename of video device. Each stream will be assumed accessible as
 * VIDEO_DEVICE_BASENAME<stream-number>, e.g. /dev/video0
 */
#define VIDEO_DEVICE_BASENAME "/dev/video"


/*
 * DEFAULT_VIDEO_BITRATE integer
 * DEFAULT_VIDEO_PEAK_BITRATE integer
 * Deafult video bitrates 3 Mbps , peak 3.5 Mbps
 * Values should be in range 500000 up to 8000000
 */
#define DEFAULT_VIDEO_BITRATE 3000000
#define DEFAULT_VIDEO_PEAK_BITRATE 3500000

/*
 * DEFAULT_VIDEO_FRAME_SIZE string
 * Default MP2 frame size 3-quarter
 * Supported format at present are
 *  "default = 720,576
 *  "qvga = 320,240
 *  "qqvga" = 160,120
 *  "vga" = 640,480
 *  "cif" = 352,288
 *  "3q" = 480,384
 *  "3qmp4" = 480,352
 *   "half" = 360,288
 */
#define DEFAULT_VIDEO_FRAME_SIZE "3qmp4"

/*
 * Deafult audio sampling freq
 */
#define DEFAULT_AUDIO_SAMPLING V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100

/*
 * Default audio bitrate for MPEG Layer 2
 */
#define DEFAULT_AUDIO_BITRATE V4L2_MPEG_AUDIO_L2_BITRATE_256K

/*
 * Default video aspect rate
 * A possible value would also be V4L2_MPEG_VIDEO_ASPECT_4x3
 */
#define DEFAULT_VIDEO_ASPECT V4L2_MPEG_VIDEO_ASPECT_16x9

/*
 * DEFAULT_PREFIX string
 * String for default prefix when mangling names in recurring recordings.
 * This string will used as a separator between the base title/filename and
 * the suffix computed for a recurring recording.
 */
#define DEFAULT_PREFIX "_"

/*
 * MAX_CLIENTS integer
 * Max number of clients that are allowed to connect to us
 */
#define MAX_CLIENTS 2

/*
 * CLIENT_IDLE_TIME integer
 * The length (in seconds) before an inactive client is automatically
 * disconnected. Deafult 30 min.
 */
#define CLIENT_IDLE_TIME 30*60

/*
 * DEFAULT_DURATIONHOUR integer
 * DEFAULT_DURATIONMIN integer
 * Default length of a recording if not specified otherwise
 */
#define DEFAULT_DURATIONHOUR 0
#define DEFAULT_DURATIONMIN 59

/*
 * LOGFILE_NAME string
 * Optional logfile name (full path).
 */
#define LOGFILE_NAME "syslog"
#define LOGFILE_SYSLOG "syslog"

/*
 * DEFAULT_FREQUENCY_MAP string
 * The name of the default frequency map to use.
 * Note: If the xawtvrc also specifes a map that map will
 * override this setting.
 */
#define DEFAULT_FREQUENCY_MAP "europe-west"

/*
 * DEFAULT_XAWTV_CHANNEL_FILE string
 * The name of the channel map for broadcasting names of channels
 */
#define DEFAULT_XAWTV_STATION_FILE "/etc/X11/xawtvrc"

/*
 * KEEP_MP2FILE bool
 * Keep original MP2 file from video card even after transcoding
 */
#define KEEP_MP2FILE 1

/*
 * VERBOSE_LOG bool
 * Should the log be more verbose
 */
#define VERBOSE_LOG 1

/*
 * MAX_LOAD_FOR_TRANSCODING integer
 * Maximum server load allowd to start a transcoding
 */
#define MAX_LOAD_FOR_TRANSCODING 4

/*
 * MAX_WAITING_TIME_TO_TRANSCODE integer
 * The maximum time in seconds that can be set to wait for the
 * server load to fall below MAX_LOAD_FOR_TRANSCODING
 * Note: In the default conf file this is set to 0 which means
 * we can wait indefinitely. The value here just indicates the
 * maximum deterministic time that can be set.
 * (Default value is 7 days)
 */
#define MAX_WAITING_TIME_TO_TRANSCODE 7*24*60*60

/*
 * FFMPEG_BIN string
 * Default binary for ffmpeg
 */
#define FFMPEG_BIN "/usr/bin/ffmpeg"

/*
 * TVPVRD_LOCKFILE string
 * Full name for lockfile to use
 */
#define TVPVRD_LOCKFILE "/var/run/tvpvrd.pid"

/*
 * WELCOM_MSG
 * Server welcome message
 */
#define WELCOM_MSG \
        "!TVPVRD!\n"\
	"Welcome to TVPVR-Server %s (%s)\n"\
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
extern char server_build_date[] ;
extern char server_program_name[32] ;

/*
 * xawtv_channel_file string
 * Name of station to channel mapping file
 */
extern char xawtv_channel_file[256];

/*
 * default_transcoding_profile string
 * Name of default transcoding profile
 */
extern char default_transcoding_profile[32];

/*
 * max_load_for_transcoding int
 * The maximum load allowed to start a new transcoding process
 */
extern int max_load_for_transcoding ;

/*
 * max_waiting_time_to_transcode int
 * The maximum time we allow the server to wait to start a
 * new transcoding process. The server will wait as long as the
 * load on the server is too high.
 */
extern int max_waiting_time_to_transcode ;

/*
 * ffmpeg_bin string
 * The full path to the ffmpeg executable
 */
extern char ffmpeg_bin[64];

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

// Maximum sizes
extern int max_entries, max_video, max_clients, max_idle_time;

// Default recording length if nothing else is specified
extern int defaultDurationHour, defaultDurationMin;

// TVP/IP Port to listen to
extern int tcpip_port;

// Should we run as a daemon or not
extern int daemonize;

// What username are we running as
extern char username[];

// Logfile details
extern int verbose_log;
extern char logfile_name[];

// Time resolution for checks
extern int time_resolution;

// The size of the memory buffer used when reading video data from the device
extern int video_bufsize;

// The video buffers (one for each capture card)
extern char *video_buffer[];

// The default base data diectory
extern char datadir[];

// Names of the ini file and the db file used
extern char inifile[], xmldbfile[];

// Base name of video device ("/dev/video")
extern char device_basename[];

// Whether all transcoding processes should also be killed when the server stops
extern int dokilltranscodings;

// The name of the frequency map we are using. This is set at the startup and cannot
// be changed at runtime.
extern char frequencymap_name[];

// Gloabl Mutex that must be called if the routine wants to access
// any datastructure that modifies the recordings
extern pthread_mutex_t recs_mutex;

#ifdef	__cplusplus
}
#endif

#endif	/* _TVPVRD_H */

