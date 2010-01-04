/* =========================================================================
 * File:        TVPVRSRV.C
 * Description: Main file for TV PVR daemon
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Synopsis:
 * The main function is responsible for parsing any command line options
 * and then
 * 0. Initialize and setup all the needed data structures.
 * 1.  Possible create new daemon,
 * 3.  Start a new signal receiving thread
 * 3.  Start the thread to monitor the recordings
 * 3.1 If it is time to start a recording create a new thread to handle
 *     the recording, reading the file stream from the video device
 *     and storing it on the fileserver.
 * 4.  Start the main thread to listen for incoming clients connections
 * 4.1 For each incoming client connection create a new threa to handle
 *     that client.
 *
 * Each created client thread listens to commands from the client and
 * giving the command to the command interpretator for parsing and
 * execution.
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

// Standard UNIX includes
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <sys/resource.h>

// Standard socket and IP address handling
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <arpa/inet.h>

// Aplication specific libs, parse inifiles as well as Perl regular expressions
#include <iniparser.h>
#include <pcre.h>
#include <errno.h>

// Needed to get symbolic constants for v4l2 controls
#include <linux/videodev2.h>

// Needed for prctl()
#include <sys/prctl.h>

// Local files
#include "tvpvrd.h"
#include "vctrl.h"
#include "recs.h"
#include "utils.h"
#include "tvxmldb.h"
#include "tvcmd.h"
#include "freqmap.h"
#include "transc.h"
#include "stats.h"
#include "confpath.h"
#include "config.h"

/*
 * Server identification
 */
char server_version[] = PACKAGE_VERSION;
char server_build_date[] = __DATE__;
char server_program_name[32] = {0};

/*
 * The value of these variables are read from the ini-file and initialized in
 * the main() function. They hold various run time limits and settings that
 * the user can adjust.
 */
// Should we run as a daemon or nothing
int daemonize=-1;
// Maximum sizes
int max_entries, max_video, max_clients, max_idle_time;
// Default recording length if nothing else is specified
int defaultDurationHour, defaultDurationMin;
// TVP/IP Port to listen to
int tcpip_port;
// Logfile details
int verbose_log;
char logfile_name[256];
// Time resolution for checks
int time_resolution;
// The size of the memory buffer used when reading video data from the device
int video_bufsize;
// The default base data diectory
char datadir[256];
// Names of the ini file and the db file used
char inifile[256], xmldbfile[256];
// Base name of video device ("/dev/video")
char device_basename[128];
// Name of the currently used frequency map
char frequencymap_name[MAX_FMAPNAME_LENGTH];
// The name of the xawtv channel file used
char xawtv_channel_file[256];

/*
 * Global variable for MP4 encoding
 */

char ffmpeg_bin[64];
char vpre[32];
char default_transcoding_profile[32];
int max_load_for_transcoding ;
int max_waiting_time_to_transcode ;

/*
 * abort_video int
 * Flag set by the main thread to signal to one of the recording thread
 * to cancel recording
 */
volatile int * volatile abort_video; //[MAX_VIDEO]

/*
 * rec_threads
 * The thread id for each active recording thread
 */
static pthread_t *rec_threads; //[MAX_VIDEO];

/*
 * cli_threads
 * The thread id for each thread that are handling a client
 */
pthread_t *cli_threads; //[MAX_CLIENTS];
int ncli_threads = 0;

/*
 * chkrec_thread
 * The threadn that moitors the recordig list when to start new
 * recordings
 */
static pthread_t chkrec_thread;

/*
 * cli_ipadr
 * Keep track of IP address for all currrent connected clients.
 * Points to a dynamically allocated string buffer to hold the
 * human readable version of the address, e.g. 192.168.0.1
 */
char **client_ipadr; //[MAX_CLIENTS];

/*
 * client_tsconn
 * Keep track of timestamps when the client connected
 */
time_t *client_tsconn; //[MAX_CLIENTS];

/*
 * Mutexes to protect
 * 1) The data structure when mutliple clients are connected
 * 2) The creation and killing of thread and thread counts
 */
pthread_mutex_t recs_mutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t socks_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sig_mutex    = PTHREAD_MUTEX_INITIALIZER;

/*
 * Keep track of the last signal we received.
 */
static int received_signal = 0;

/*
 * Keep track of the socket that each client uses
 */
static int *client_socket; //[MAX_CLIENTS];

/*
 * video_idx
 * The video stream index. Used in the call to pthread_create() to avoid
 * local stack based variables as arguments in the thread creation
 */
static int *video_idx; //[MAX_VIDEO];

/*
 * ts_serverstart
 * Timestamp when server was started
 */
time_t ts_serverstart;

/*
 * dict
 * Holds the read dictionary from the inifile
 */
dictionary *dict;

/*
 * Lockfile
 * Used to store the name of the lockfile used when starting up the server
 */
static char lockfilename[256] = {0};

/*
 * Some cards/driver combinations have problem adjusting the cards HW
 * settings, like bitrate, format size and so on too frequently.
 * By keeping this variable to false the individual profiles will not
 * be allowed to adjust the settings of the HW encoder.
 * The settings of the HW encoder will instead be done once, when the
 * program starts up and the settings will ge gathered from the "normal"
 * profile.
 */
static int allow_profiles_adj_encoder = 0;

/*
 * What user should we run as
 */
char username[64];

/*
 * Determine if we should kill any ongoing transcoding processes when we
 * kill this server.
 */
int dokilltranscodings = 1;

/*
 * Password settings. The server offer a basic authentication, The
 * authentication is disabled by default.
 */
static int require_password = REQUIRE_PASSWORD;
static char password[32];

void
init_globs(void) {

    // Initialize the recording data structures
    initrecs();
    cmdinit();

    rec_threads     = (pthread_t *) calloc(max_video,   sizeof (pthread_t));
    cli_threads     = (pthread_t *) calloc(max_clients, sizeof (pthread_t));
    client_ipadr    =     (char **) calloc(max_clients, sizeof (char *));
    client_tsconn   =    (time_t *) calloc(max_clients, sizeof (time_t));
    client_socket   =       (int *) calloc(max_clients, sizeof (int));
    video_idx       =       (int *) calloc(max_video,   sizeof (int));
    abort_video     =       (int *) calloc(max_video,   sizeof (int));

    if (rec_threads   == NULL || cli_threads   == NULL ||
        client_ipadr  == NULL || client_tsconn == NULL ||
        client_socket == NULL || video_idx     == NULL ||
        abort_video   == NULL) {

        fprintf(stderr, "FATAL: Out of memory. Aorting program.");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < max_video; ++i) {
        // Index of video stream. Used to avoid local stack based variables
        // to be sent in the pthread_create() call
        video_idx[i] = i;
    }
}

#ifdef DEBUG_SIMULATE
#define _dbg_close(fd) _x_dbg_close(fd)
#else
#define _dbg_close(fd) close(fd)
#endif


int
_x_dbg_close(int fd) {

    logmsg(LOG_INFO,"dbg_close() : fd=%d",fd);
    return close(fd);
    
}

void
free_globs(void) {


    cmdfree();
    freerecs();

    for(int i=0; i < max_clients; i++) {
        if( client_ipadr[i] )
            free(client_ipadr[i]);
    }
    free(client_ipadr);

    free(rec_threads);
    free(cli_threads);
    free(video_idx);
    free((void *)abort_video);

    free(client_tsconn);
    free(client_socket);
    iniparser_freedict(dict);

}

int
set_enc_parameters(int fd, struct transcoding_profile_entry *profile) {
    char infobuff[256];
    float sampling[] = {44.1, 48.0, 32.0};
    int abps[] = {192, 224, 256, 320, 384};
    char *aspect[] = {"1x1","4x3","16x9","221x100"};
    int w,h;

    video_get_wh_fromname(&w, &h, profile->encoder_video_frame_size_name);

    int i=2;
    int ret = video_set_video_bitrate(fd, profile->encoder_video_bitrate, profile->encoder_video_peak_bitrate);
    while( ret == -1 && errno == EBUSY && i > 0 ) {
        usleep(700*(3-i));
        ret = video_set_video_bitrate(fd, profile->encoder_video_bitrate, profile->encoder_video_peak_bitrate);
        i--;
    }
    if( ret == -1 ) {
        return -1;
    }

    i=2;
    ret = video_set_audio_bitrate(fd, profile->encoder_audio_sampling, profile->encoder_audio_bitrate);
    while( ret == -1 && errno == EBUSY && i-- > 0 ) {
        usleep(500);
        ret = video_set_audio_bitrate(fd, profile->encoder_audio_sampling, profile->encoder_audio_bitrate);
    }
    if( ret == -1 ) {
        return -1;
    }

    i=2;
    ret = video_set_video_aspect(fd, profile->encoder_video_aspect);
    while( ret == -1 && errno == EBUSY && i-- > 0 ) {
        usleep(500);
        ret = video_set_video_aspect(fd, profile->encoder_video_aspect);
    }
    if( ret == -1 ) {
        return -1;
    }

    i=2;
    ret = video_set_named_size(fd, profile->encoder_video_frame_size_name);
    while( ret == -1 && errno == EBUSY && i-- > 0 ) {
        usleep(500);
        ret = video_set_named_size(fd, profile->encoder_video_frame_size_name);
    }
    if( ret == -1 ) {
        return -1;
    }

    snprintf(infobuff,255,
            "HW parameters for video descriptor %d set. Profile='%s' ["
            "vcodec:(%.1f Mbps,%.1f Mbps), "
            "acodec:(%.1f kHz,%d kbps), "
            "aspect:(\"%s\"), "
            "framesize:(\"%s\"=%dx%d) ]",
            fd,profile->name,
            profile->encoder_video_bitrate/1000000.0,profile->encoder_video_peak_bitrate/1000000.0,
            sampling[profile->encoder_audio_sampling], abps[MAX(profile->encoder_audio_bitrate-9,0)],
            aspect[profile->encoder_video_aspect],
            profile->encoder_video_frame_size_name,w,h
            );
    logmsg(LOG_NOTICE,infobuff);
    return 0;
}

int
setup_video(int video,struct transcoding_profile_entry *profile) {
#ifndef DEBUG_SIMULATE
    char infobuff[256];
#endif
    unsigned int freq;

    getfreqfromstr(&freq, ongoing_recs[video]->channel);

#ifndef DEBUG_SIMULATE
    int fd = video_open(video);
    if( fd == -1 ) {
        return -1;
    }

    // Give the driver some breathing room after we open the device
    // and until we start changing the settings.
    sleep(1);

    int ret,i=2;
    ret = video_set_channel(fd,  ongoing_recs[video]->channel);
    while( ret == -1 && errno == EBUSY && i > 0 ) {
        usleep(500*(3-i));
        ret = video_set_channel(fd,  ongoing_recs[video]->channel);
        i--;
    }
    if( ret == -1 ) {
        video_close(fd);
        return -1;
    }

    snprintf(infobuff,255,
                    "Tuner #%02d set to channel '%s' @ %.3fMHz",
                    video,ongoing_recs[video]->channel, freq/1000000.0
    );
    logmsg(LOG_NOTICE,infobuff);

    if( allow_profiles_adj_encoder ) {
        if( -1 == set_enc_parameters(fd, profile) ) {
            video_close(fd);
            return -1;
        }
    }
    return fd;
#else
    return 0;
#endif
}

int
transcode_and_move_file(char *datadir, char *workingdir, char *short_filename,
                        char *full_filename, struct transcoding_profile_entry *profile,
                        int *filesize, struct timeall *transcode_time) {

    struct rusage usage;
    CLEAR(*transcode_time);

    // We do not start transcoding if the recording was aborted
    // If the bitrate is set to < 10kbps then this indicates that no
    // transcoding should be done. We just move the MP2 file to the mp2 directory
    int transcoding_done=0;
    if (profile->video_bitrate < 10) {

        // Do nothing. The MP2 file will be moved by the calling
        // function.

        /*
        char newname[256], tmpbuff[256];
        // Move the original mp2 file if the user asked to keep it
        snprintf(tmpbuff, 256, "%s/mp2/%s", datadir, short_filename);
        int ret = mv_and_rename(full_filename, tmpbuff, newname, 512);
        if (ret) {
            logmsg(LOG_ERR, "Could not move '%s' to '%s'", full_filename, newname);
        } else {
            logmsg(LOG_INFO, "Moved '%s' to '%s'", full_filename, newname);
        }
         */

    } else  {
        // If recording was successful then do the transcoding
        // tiny 32, xsmall 128, small 256, med 512, large 1024, xlarge 2048, huge 4096
        char cmdbuff[1024], cmd_ffmpeg[512], destfile[128] ;
        int runningtime = 0;
        
        if (0 == wait_to_transcode(short_filename)) {

            logmsg(LOG_INFO, "Using profile '%s' for transcoding of '%s'", profile->name, short_filename);

            create_ffmpeg_cmdline(short_filename, profile, destfile, 128, cmd_ffmpeg, 512);

            snprintf(cmdbuff, 1023, "cd %s;%s", workingdir, cmd_ffmpeg);

#ifdef DEBUG_SIMULATE
            
            snprintf(cmdbuff, 256, "%s/%s", workingdir, destfile);
            int sfd = open(cmdbuff, O_WRONLY | O_CREAT | O_TRUNC);
            logmsg(LOG_INFO, "Simulation mode: No real transcoding. Creating fake file '%s'",cmdbuff);
            _writef(sfd, "Fake MP4 file created during simulation at ts=%u\n", time(NULL));
            (void) close(sfd);
            transcoding_done = 1;
            CLEAR(usage);

#else
            pid_t pid;
            if ((pid = fork()) == 0) {
                // In the child process
                // Make absolutely sure everything is cleaned up except the standard
                // descriptors
                for (int i = getdtablesize(); i > 2; --i) {
                    (void)close(i);
                }

                // Since the ffmpeg command is run as a child process (via the sh comamnd)
                // we need to make sure all of this is in the same process group. This is
                // done in order so that we can kill the ffmpeg command if the server
                // is stopped by the user. The pid returned by the fork() will not be
                // the same process as is running the 'ffmpeg' command !
                setpgid(getpid(),0); // This sets the PGID to be the same as the PID
                if( -1 == nice(20) ) {
                    logmsg(LOG_ERR, "Error when calling 'nice()' : (%d:%s)",errno,strerror(errno));
                }
                execl("/bin/sh", "sh", "-c", cmdbuff, (char *) 0);
            } else if (pid < 0) {
                logmsg(LOG_ERR, "Fatal. Can not create process to do transcoding for file \"%s\" (%d : %s)",
                        short_filename, errno, strerror(errno));
            } else {

                logmsg(LOG_INFO, "Successfully started process pid=%d for transcoding '%s'.",pid,short_filename);

                pthread_mutex_lock(&recs_mutex);
                int tidx = record_ongoingtranscoding(workingdir, short_filename, cmd_ffmpeg, profile,pid);
                pthread_mutex_unlock(&recs_mutex);

                if (tidx != -1) {

                    // Now wait for the transcoding to finish and print the status of the
                    // transcoding to the log. We do this in a busy waiting loop for simplicity.
                    // since we can have such a long sleep period without really affecting the
                    // performance.
                    pid_t rpid;

                    // We only allow one transcoding to run for a maximum of 24 h
                    const int watchdog = 24 * 3600;
                    int ret;
                    do {
                        // Transcoding usually takes hours so we don't bother
                        // waking up and check if we are done more often than once a minute
                        sleep(60);
                        runningtime += 60;
                        rpid = wait4(pid, &ret, WCONTINUED | WNOHANG | WUNTRACED, &usage);

                    } while (pid != rpid && runningtime < watchdog);

                    pthread_mutex_lock(&recs_mutex);
                    forget_ongoingtranscoding(tidx);
                    pthread_mutex_unlock(&recs_mutex);

                    if (runningtime >= watchdog) {
                        // Something is terrible wrong if the transcoding haven't
                        // finished after the watchdog timeout
                        logmsg(LOG_ERR, "Transcoding process for file '%s' seems hung (have run for > %d hours). Killing process.",
                                short_filename, watchdog / 3600);
                        (void) kill(pid, SIGKILL);
                    } else {
                        if (WIFEXITED(ret)) {
                            transcoding_done = (WEXITSTATUS(ret) == 0);
                            if (WEXITSTATUS(ret) == 0) {
                                if( runningtime < 60 ) {
                                    logmsg(LOG_ERR, "Error in transcoding process for file '%s'.",
                                        short_filename, runningtime / 60);

                                } else {
                                    logmsg(LOG_INFO, "Transcoding process for file '%s' finished normally after %d:%d min of execution. (utime=%d s, stime=%d s))",
                                        short_filename, runningtime / 60, runningtime%60, usage.ru_utime.tv_sec, usage.ru_stime.tv_sec);

                                }
                            } else {
                                logmsg(LOG_INFO, "Error in transcoding process for file '%s' after %d min of execution.",
                                        short_filename, runningtime / 60);
                                return -1;
                            }
                        } else {
                            if (WIFSIGNALED(ret)) {
                                logmsg(LOG_ERR, "Transcoding process for file \"%s\" was unexpectedly terminated by signal=%d .",
                                        short_filename, WTERMSIG(ret));
                                return -1;
                            } else {
                                // Child must have been stopped. If so we have no choice than to kill it
                                logmsg(LOG_ERR, "Transcoding process for file \"%s\" was unexpectedly stopped by signal=%d. Killing process.",
                                        short_filename, WSTOPSIG(ret));
                                (void) kill(pid, SIGKILL);
                                return -1;
                            }

                        }
                    }
                }
            }
#endif
        } else {
            logmsg(LOG_ERR, "Can not start transcoding of \"%s\". Server too busy.", short_filename);
            return -1;
        }

        // If transcoding was not successfull we give up and just leave the recording under the
        // vtmp directory
        if (transcoding_done) {
            char newname[256], tmpbuff2[256], tmpbuff[256];

            // Move MP4 file
            snprintf(tmpbuff, 255, "%s/mp4/%s/%s", datadir, profile->name, destfile);
            tmpbuff[255] = '\0';
            snprintf(tmpbuff2, 255, "%s/%s", workingdir, destfile);
            tmpbuff2[255] = '\0';
            int ret = mv_and_rename(tmpbuff2, tmpbuff, newname, 256);
            if (ret) {
                logmsg(LOG_ERR, "Could not move \"%s\" to \"%s\"", tmpbuff2, newname);
                return -1;
            } else {
                logmsg(LOG_INFO, "Moved \"%s\" to \"%s\"", tmpbuff2, newname);
                struct stat fstat;
                // Find out the size of the transcoded file
                if( stat(newname,&fstat) != -1 ) {
                    
                    *filesize = fstat.st_size;
                    transcode_time->rtime.tv_sec = runningtime ;
                    transcode_time->utime.tv_sec = usage.ru_utime.tv_sec;
                    transcode_time->stime.tv_sec = usage.ru_stime.tv_sec;

                } else {
                    logmsg(LOG_ERR,"Can not determnine size of transcoded file '%s'. ( %d : %s) ",
                           newname,errno,strerror(errno));
                }
                
            }
        }
    }
    return 0;
}

/*
 * Start a recording on the specified video stream immediately using the information in the
 * current recording record.
 * This function is only run in its own thread created in chkrec()
 */
void *
startrec(void *arg) {
    int nread, nwrite;
    char full_filename[256], workingdir[256], short_filename[256];
    const mode_t dmode =  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    const mode_t fmode =  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int mp2size = 0;
    volatile int doabort = 0 ;

    int video = *(int *) arg;
    struct recording_entry *recording = ongoing_recs[video];
    struct transcoding_profile_entry *profile;
    
    // Get primary profile
    get_transcoding_profile(recording->transcoding_profiles[0],&profile);

#ifndef DEBUG_SIMULATE
    int *buffer = calloc(video_bufsize, sizeof (unsigned char));
#endif
    int vh = setup_video(video,profile);
    abort_video[video] = 0;
            
    if (vh == -1) {

       logmsg(LOG_ERR, "Cannot open video stream %02d. '%s' recording aborted ( %d : %s ). ",video,recording->title,errno,strerror(errno));
       pthread_mutex_lock(&recs_mutex);
       free(recording);
       ongoing_recs[video] = (struct recording_entry *)NULL;
       pthread_mutex_unlock(&recs_mutex);
       pthread_exit(NULL);
       return (void *)NULL;

    } else {

        int k = strnlen(recording->filename,REC_MAX_NFILENAME)-1;
        while ( k>0 && recording->filename[k] != '.' )
            k--;
        if( k <= 0 ) {
            logmsg(LOG_ERR,"Corrupt filename. No file extension found - recording aborted.");
#ifndef DEBUG_SIMULATE
            video_close(vh);
#endif
            pthread_mutex_lock(&recs_mutex);
            free(recording);
            ongoing_recs[video] = (struct recording_entry *)NULL;
            pthread_mutex_unlock(&recs_mutex);
            pthread_exit(NULL);
            return (void *)NULL;
        }
        recording->filename[k] = '\0';
        snprintf(workingdir,255,"%s/vtmp/vid%d/%s",datadir,video,recording->filename);
        workingdir[255] = '\0';
        if( mkdir(workingdir,dmode) ) {
            logmsg(LOG_ERR, "Cannot create recording directory (%s). Recording aborted. ( %d : %s)  ",workingdir,errno,strerror(errno));
#ifndef DEBUG_SIMULATE
            video_close(vh);
#endif
            pthread_mutex_lock(&recs_mutex);
            free(recording);
            ongoing_recs[video] = (struct recording_entry *)NULL;
            pthread_mutex_unlock(&recs_mutex);
            pthread_exit(NULL);
            return (void *)NULL;
        }
        recording->filename[k] = '.';
        snprintf(full_filename,255,"%s/%s",workingdir,recording->filename);
        full_filename[255] = '\0';
        strncpy(short_filename,basename(full_filename),255);
        short_filename[255] = '\0';
        int fh = open(full_filename, O_WRONLY | O_CREAT | O_TRUNC, fmode);
        if (fh == -1) {

            logmsg(LOG_ERR, "Cannot open '%s' for writing. Recording aborted. ( %d : %s ) ",
                   full_filename,errno,strerror(errno));
#ifndef DEBUG_SIMULATE
            video_close(vh);
#endif
            pthread_mutex_lock(&recs_mutex);
            free(recording);
            ongoing_recs[video] = (struct recording_entry *)NULL;
            pthread_mutex_unlock(&recs_mutex);
            pthread_exit(NULL);

        } else {

#ifndef DEBUG_SIMULATE

            // Do the actual recording by reading chunks of data from the
            // MP2 stream and store it in the recording file
            logmsg(LOG_INFO,"Started recording using video card #%02d, fd=%d to '%s'.", video,vh, full_filename);
            fd_set fds;
            struct timeval tv;
            int ret;

            do {
                nread = 0 ;
                nwrite = 0 ;

                FD_ZERO (&fds);
                FD_SET (vh, &fds);

                /* Timeout. */
                tv.tv_sec = 10;
                tv.tv_usec = 0;

                // ---------------------------------------------------------------------------------------
                // First wait until we have some data avialable from the video capture card
                // via a select() call. We only wait for the descriptor assocuated with this capture
                // card. If there is no error and the call wasn't interrupted (EINTR) we go ahead
                // and read as much data as the card wants to give us. Normally this is a whole number of
                // frames the the data read vay in size from  ~8k to ~80k in size.
                // ---------------------------------------------------------------------------------------

                logmsg(LOG_DEBUG,"    -- [%02d] select WAITING %d (%s)",video,vh,full_filename);
                ret = select (vh + 1, &fds, NULL, NULL, &tv);
                logmsg(LOG_DEBUG,"    -- [%02d] select WAITING %d READY ret=%d, errno=%d (%s)",video,vh,ret,errno,full_filename);

                if (-1 == ret && EINTR == errno) {
                    continue;
                } else

#ifdef IGNORE_CARD_TIMEOUTS
                    {
#else                  
               if (0 == ret ) {
                    logmsg(LOG_ERR,"Timeout on video stream #%02d. Aborting recording to '%s'",video,full_filename);
                    doabort = 1;
                } else {      
#endif
                    logmsg(LOG_DEBUG,"    -- [%02d] Trying to read bytes from fd=%d '%s'",video,vh, recording->title);
                    nread = read(vh, buffer, video_bufsize);
                    logmsg(LOG_DEBUG,"    -- [%02d] DONE. Read %05d bytes from fd=%d '%s'",video,nread,vh,recording->title);

                    if (-1 == nread ) {
                            switch (errno) {
                                case EAGAIN:
                                    // No data available so just try again
                                    logmsg(LOG_ERR,"No data yet available from stream #%02d on fd=%d",video,vh);
                                    continue;
                                default:
                                    // Serious problem.
                                    logmsg(LOG_ERR,"Unable to read from video stream #%02d on fd=%d. ( %d : %s )",
                                            video,vh,errno,strerror(errno));
                                    doabort = 1;
                            }
                    } else {

                        nwrite = write(fh, buffer, nread);
                        logmsg(LOG_DEBUG,"    -- [%02d] nwrite=%d after writing to '%s'",video,nwrite,full_filename);

                        if( nwrite == -1 ) {
                            logmsg(LOG_ERR, "Error while writing to '%s' while recording. (%d : %s) ",
                                    full_filename,errno,strerror(errno));
                            doabort = 1;
                        } else {
                            mp2size += nwrite;
                            doabort = abort_video[video];
                        }

                    } 
                }
            } while (recording->ts_end > time(NULL) && !doabort);
#else
            logmsg(LOG_INFO,"Started simulated recording to file \"%s\".", full_filename);
            _writef(fh, "Simulated writing at ts=%u\n", (unsigned)time(NULL));
            int used_time=0;
            time_t now;
            do {
                sleep(10);
                used_time += 10;
                now = time(NULL);
            } while(recording->ts_end > now && !doabort);
            if( doabort ) {
                _writef(fh, "Simulated writing aborted by user after %d seconds at ts=%u\n", used_time, (unsigned)time(NULL));                    
            } else {
                _writef(fh, "Simulated writing ended normally after %d seconds at ts=%u\n", used_time, (unsigned)time(NULL));
            }
            nread = nwrite = 1; // Some dummmy alues to indicate that the fake recording was successfull.

#endif

            _dbg_close(fh);
            if( doabort ) {
                logmsg(LOG_ERR, "Aborted recording to '%s' due to error. (%d : %s) ",full_filename,errno,strerror(errno));
            } else {
                logmsg(LOG_INFO,"Recording to '%s' stopped. End of recording time.",full_filename);
            }
        }

#ifndef DEBUG_SIMULATE
        video_close(vh);
#endif
        pthread_mutex_lock(&recs_mutex);
        abort_video[video]=0;
        ongoing_recs[video] = (struct recording_entry *)NULL;
        pthread_mutex_unlock(&recs_mutex);     

        // Now do the transcoding for each profile associated with this recording
        int transcoding_problem = 1 ;

        if( !doabort && (nread == nwrite) && (check_ffmpeg_bin()==0)) {
            transcoding_problem = 0;
            int mp4size=0;
            struct timeall transcode_time;
            CLEAR(transcode_time);

            for(int i=0; i < REC_MAX_TPROFILES && strlen(recording->transcoding_profiles[i]) > 0; i++) {
                get_transcoding_profile(recording->transcoding_profiles[i],&profile);
                printf("Transcoding using profile: %s\n",profile->name);
                time_t start = time(NULL);
                int ret = transcode_and_move_file(datadir,workingdir,short_filename,
                                                  full_filename,profile,
                                                  &mp4size,&transcode_time);
                transcoding_problem |= ret;
                if( ret > -1 ) {
                    stats_update(recording->transcoding_profiles[i],
                                 mp2size,
                                 recording->ts_end - recording->ts_start,
                                 mp4size,
                                 &transcode_time, time(NULL)-start);
                }
            }
        }

        if ( ! transcoding_problem ) {
                char tmpbuff[256], newname[512];
                // Move the original mp2 file if the user asked to keep it
                int delete_workingdir = 1;
                if (profile->encoder_keep_mp2file) {
                        snprintf(tmpbuff, 255, "%s/mp2/%s", datadir, short_filename);
                        tmpbuff[255] = '\0';
                        if ( mv_and_rename(full_filename, tmpbuff, newname, 512) ) {
                                logmsg(LOG_ERR, "Could not move \"%s\" to \"%s\"", full_filename, newname);
                                delete_workingdir = 0;
                        } else {
                                logmsg(LOG_INFO, "Moved \"%s\" to \"%s\"", full_filename, newname);
                        }
                }

                // Delete the temporary directory used while recording and transcoding
                // unless there were a problem with the transcoding. 
                if( !doabort  && delete_workingdir ) {
                        if( removedir(workingdir) ) {
                                logmsg(LOG_ERR, "Could not delete directory \"%s\".", workingdir);
                        } else {
                                logmsg(LOG_INFO, "Deleted directory \"%s\".", workingdir);
                        }
                }
        }
    }
    free(recording);
    pthread_exit(NULL);
    return (void *) 0;
}

/*
 * This function get called regulary and checks if there is a pending recording that
 * should be started.
 */
void *
chkrec(void *arg) {
    time_t now;
    int diff, ret;
    volatile int video;

    // Do some sanity checks of time_resolution
    time_resolution = MIN(MAX(time_resolution,1),10);

    // We are string as a thread that runs forever
    while (1) {

        // We need the current time to compare against
        now = time(NULL);

        pthread_mutex_lock(&recs_mutex);

        // Recordings are always kept sorted sorted by start time
        // so that any record to be started will be available
        // in the first slot for each stream
        for (video = 0; video < max_video; ++video) {

            // First check if we have any potential new recording to start for
            // this video stream
            if (num_entries[video] > 0) {

                // Find out how far of we are to hae to start this recording
                // If diff > 0 then the start time have already passed
                diff = now - recs[REC_IDX(video, 0)]->ts_start;

                // If the recording is more than 10 min off then we consider this a missed
                // oppportunity. We remove this recording to be able to try the next one in
                // sequence.
                if (diff > 60 * 10) {
                    int sy,sm,sd,sh,smin,ssec;
                    fromtimestamp(recs[REC_IDX(video, 0)]->ts_start,&sy,&sm,&sd,&sh,&smin,&ssec);
                    logmsg(LOG_ERR, "Time for recording of ('%s' %d-%02d-%02d %02d:%02d) on video %d is too far in the past. Recording cancelled.",
                           recs[REC_IDX(video, 0)]->title, sy,sm,sd,sh,smin,video);
                    deletetoprec(video);
                } else {
                    // Start the initiation at least (TIME_RESOLUTION-1) s before the exact start
                    // time since we cannot guarantee that the thread will be scheduled right
                    // away. It will also take a few ms to setup the card and tuner so we
                    // err on the safe side here. So in general this means that on average the
                    // recording will start ~TIME_RESOLUTION s before the scheduled time.
                    if (diff >= -time_resolution) {
                        volatile void  *active;
                        active = ongoing_recs[video];

                        // Now, we have a potential issue here. If we have an existing recording
                        // that is set to stop at say, 14:00 then since that recording will only stop
                        // after the video buffer is full it could happen that it will actually not stop
                        // until a few seconds after 14:00.
                        // To make things worse this new recording could potentially start at
                        // 14:00 - time_resolution, and this will not work. So, we check if there is an
                        // existing recording ongoing and it it is we give a wanring message
                        // and try again

                        if ( active != NULL) {
                            logmsg(LOG_ERR, "Can not start, '%s' using stream %02d. Previous recording (%s) has not yet stopped.",
                                   recs[REC_IDX(video, 0)]->title,video,ongoing_recs[video]->title);
                        } else {
                            // Remember what recording is currently taking place for this video stream
                            ongoing_recs[video] = recs[REC_IDX(video, 0)];

                            // Remove it from the list of pending recordings
                            removetoprec(video);

                            // Start the thread that will actually do the recording to the file system
                            ret = pthread_create(&rec_threads[video], NULL, startrec, (void *) & video_idx[video]);
                            if (ret != 0) {
                                logmsg(LOG_ERR, "Could not create thread for recording.");
                            }
                        }
                    }
                }
            }
        }
        pthread_mutex_unlock(&recs_mutex);
        sleep(time_resolution);
    }
}

/*
 * This is the main function that gets started for each client that
 * connects to us. It is run as its own thread
 */
void *
clientsrv(void *client_socket_ptr) {
    int numreads, i, ret;
    int my_socket = *(int *) client_socket_ptr;
    char buffer[1024];
    fd_set read_fdset;
    struct timeval timeout;
    int idle_time=0;

    bzero(buffer, 1024);
    
    i = 0;
    while (i < max_clients && client_socket[i] != my_socket)
        i++;    
    
    if( require_password ) {
        int tries = 3;
        int authenticated = 0;
        while( tries > 0 && !authenticated) {
            _writef(my_socket, "Password: ");
            FD_SET(my_socket, &read_fdset);
            timeout.tv_sec = 60;
            timeout.tv_usec = 0;

            ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);   
            if( ret == 0 ) {
                // Timeout
                break;
            } else {
                numreads = read(my_socket, buffer, 1023);
                buffer[1023] = '\0';
                buffer[numreads] = 0;
                if( strlen(buffer) > 2 ) {
                    buffer[strlen(buffer)-2] = '\0';
                }
                if( strcmp(buffer,password) == 0 ) {
                    authenticated = 1;
                }
            }
            --tries;
        } 
        if( !authenticated ) {
            logmsg(LOG_INFO,"Authentication failed. Connection from %s on socket %d closed.", client_ipadr[i], my_socket);

            pthread_mutex_lock(&socks_mutex);

            client_socket[i] = 0;
            free(client_ipadr[i]);
            client_ipadr[i] = 0;
            client_tsconn[i] = 0;
            cli_threads[i] = 0;
            if( -1 == _dbg_close(my_socket) ) {
                logmsg(LOG_ERR,"Failed to close socket %d to client %s. ( %d : %s )",
                            my_socket,client_ipadr[i],errno,strerror(errno));
            }
            ncli_threads--;

            pthread_mutex_unlock(&socks_mutex);
            
            _writef(my_socket,"Authentication error. Disconnecting.\n");
            
            pthread_exit(NULL);
            return (void *) 0;                
        }        
    }

    // We must mutex this call since we are accessing the nclient_thread
    pthread_mutex_lock(&socks_mutex);
    snprintf(buffer,1023, WELCOM_MSG, server_version, server_build_date, ncli_threads, max_clients, max_idle_time/60);
    buffer[1023] = '\0';
    pthread_mutex_unlock(&socks_mutex);

    _writef(my_socket, buffer);

    do  {

        FD_SET(my_socket, &read_fdset);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);
        if (ret == 0) {
            //Timeout
            idle_time++;
            if (idle_time >= max_idle_time) {
                numreads = -1; // Force a disconnect
                logmsg(LOG_INFO, "Client disconnected after being idle for %d seconds.", max_idle_time);
            }
            else
                numreads = 1;
        } else {
            idle_time = 0;
            numreads = read(my_socket, buffer, 1023);
            buffer[1023] = '\0';
            buffer[numreads] = 0;
            if (strcmp("exit\r\n", buffer) == 0) {
                _writef(my_socket,"Goodbye.\n");
                numreads = -1;
                break;
            } else {
                buffer[MAX(strnlen(buffer,1023) - 2, 0)] = 0;
                pthread_mutex_lock(&recs_mutex);
                logmsg(LOG_INFO, "Client (%s) sent command: %s", client_ipadr[i], buffer);
                cmdinterp(buffer, my_socket);
                pthread_mutex_unlock(&recs_mutex);
            }
        }
    } while (numreads > 0);

    logmsg(LOG_INFO,"Connection from %s on socket %d closed.", client_ipadr[i], my_socket);

    pthread_mutex_lock(&socks_mutex);

    client_socket[i] = 0;
    free(client_ipadr[i]);
    client_ipadr[i] = 0;
    client_tsconn[i] = 0;
    cli_threads[i] = 0;
    if( -1 == _dbg_close(my_socket) ) {
        logmsg(LOG_ERR,"Failed to close socket %d to client %s. ( %d : %s )",
                    my_socket,client_ipadr[i],errno,strerror(errno));
    }
    ncli_threads--;

    pthread_mutex_unlock(&socks_mutex);

    pthread_exit(NULL);
    return (void *) 0;
}

/*
 * Start the main socket server that listens for clients that connects
 * to us. For each new client a thread that will manage that client
 * is started.
 */
int
startupsrv(void) {
    int sockd, newsocket, i;
    unsigned tmpint;
    struct sockaddr_in socketaddress;
    int ret;
    char *dotaddr, tmpbuff[128];
    fd_set read_fdset;
    struct timeval timeout;

    memset(&socketaddress, 0, sizeof (socketaddress));
    socketaddress.sin_family = AF_INET;
    socketaddress.sin_addr.s_addr = htonl(INADDR_ANY);
    socketaddress.sin_port = htons(tcpip_port);

    // Create the socket (TCP)
    if ((sockd = socket(AF_INET, SOCK_STREAM, 0)) < 1) {
        logmsg(LOG_ERR, "Unable to create socket.");
        return EXIT_FAILURE;
    }

    // Bind socket to socketaddress
    if (bind(sockd, (struct sockaddr *) & socketaddress, sizeof (socketaddress)) != 0) {
        logmsg(LOG_ERR, "Unable to bind socket to port. Most likely som other application is using this port.");
        return EXIT_FAILURE;
    }

    // Listen on socket, queue up to 5 connections
    if (listen(sockd, 5) != 0) {
        logmsg(LOG_ERR, "Unable to listen on socket ");
        return EXIT_FAILURE;
    }

    // We don't want to risk that a child holds this descriptor
    set_cloexec_flag(sockd, 1);

    // Run until we receive a SIGQUIT or SIGINT awaiting client connections and
    // setting up new client communication channels

    logmsg(LOG_INFO,"%s successfully initialized. Listening on port=%d for connections.",server_program_name, tcpip_port);

    while ( 1 ) {

        // We must reset this each time since select() modifies it
        FD_ZERO(&read_fdset);
        FD_SET(sockd, &read_fdset);

        timeout.tv_sec = 0;
        timeout.tv_usec = 800;
        ret = select(sockd + 1, &read_fdset, NULL, NULL, &timeout);

        if( ret == 0 ) {
            // Timeout, so take the opportunity to check if we received a
            // signal telling us to quit. We don't care about what signal. Since the signal
	    // handler is only allowing signals that we consider stopping signals)
            if( received_signal ) 
                break;
            else
                continue;
        }

        tmpint = sizeof (socketaddress);

        // =======================================================================
        // MAIN WAIT FOR NEW CONNECTIONS
        // =======================================================================
        newsocket = accept(sockd, (struct sockaddr *) & socketaddress, &tmpint);

        if( newsocket < 0 ) {
            // Unrecoverable error
            logmsg(LOG_ERR, "Could not create new client socket ( %d : %s ) ",errno,strerror(errno));
        } else {

            set_cloexec_flag(newsocket,1);

            dotaddr = inet_ntoa(socketaddress.sin_addr);

            pthread_mutex_lock(&socks_mutex);

            logmsg(LOG_INFO, "Client number %d have connected from IP: %s on socket %d", ncli_threads + 1, dotaddr, newsocket);

            // Find the first empty slot for storing the client thread id
            i = 0;
            while (i < max_clients && cli_threads[i])
                i++;

            if (i < max_clients) {
                client_socket[i] = newsocket;
                client_ipadr[i] = strdup(dotaddr);
                client_tsconn[i] = time(NULL);
                ret = pthread_create(&cli_threads[i], NULL, clientsrv, (void *) & client_socket[i]);
                if (ret != 0) {
                    logmsg(LOG_ERR, "Could not create thread for client ( %d :  %s )",errno,strerror(errno));
                } else {
                    // Save some system resources by detaching his thread since
                    // we will never do a join on threads
                    pthread_detach(cli_threads[i]);
                    ncli_threads++;
                }
            } else {
                logmsg(LOG_ERR, "Client connection not allowed. Too many clients connected.");
                strcpy(tmpbuff, "Too many connections.");
                _writef(newsocket, tmpbuff);
                _dbg_close(newsocket);
            }

            pthread_mutex_unlock(&socks_mutex);
        }
    }
    
    logmsg(LOG_DEBUG,"Closing main listening socket.");
    if( -1 == _dbg_close(sockd) ) {
        logmsg(LOG_ERR,"Failed to close main listening socket. ( %d : %s )",errno,strerror(errno));
    }

    return EXIT_SUCCESS;
}

/*
 * This is the signal receiveing thread. In order to gurantee that we don't get any
 * deadlocks all signals is masked in all oyther threads and only this thread can receive
 * signals. The function then set the global variable handled_signal
 */
void *
sighand_thread(void *ptr) {
    sigset_t signal_set;
    int sig;

    while (1) {

        sigfillset(&signal_set);
        sigwait(&signal_set, &sig);

        switch (sig) {

#ifdef DEBUG_SIMULATE
            case SIGSEGV:
                abort();
#endif
            case SIGQUIT:
#ifndef DEBUG_SIMULATE
            case SIGINT:
	    case SIGHUP:
#endif
	    case SIGTERM:
                pthread_mutex_lock(&sig_mutex);
                received_signal = sig;
                pthread_mutex_unlock(&sig_mutex);
                break;

            default:
                pthread_mutex_lock(&sig_mutex);
                received_signal = 0;
                pthread_mutex_unlock(&sig_mutex);
                break;
        }

    }
    return (void*) 0;
}

/*
 * All necessary low level household stuff to kick us off as a 
 * daemon process, i.e. fork, disconnect from any tty, close
 * the standard file handlers and so on
 */
void startdaemon(void) {

    // Fork off the child
    pid_t pid = fork();
    if( pid < 0 ) {
        syslog(LOG_ERR, "Cannot fork daemon.");
        exit(EXIT_FAILURE);
    }

    if( pid > 0 ) {
        // Exit parent. Note the use of _exit() rather than exit()
        // The _exit() performs the atexit() cleanup handler
	// which we do not want since that would delete the lockfile
        _exit(EXIT_SUCCESS);
    }

    // Get access to files written by the daemon
    // This is not quite necessay since we explicetly set
    // the file attributes on the database and the log file
    // when they are created.
    umask(0);

    // Create a session ID so the child isn't treated as an orpphan
    pid_t sid = setsid();
    if( sid < 0 ) {
	syslog(LOG_ERR, "Cannot fork daemon and create session ID.");
        exit(EXIT_FAILURE);
    }

    // Fork again to ensure we are not a session group leader
    // and hence can never regain a controlling terminal
    pid = fork();
    if( pid < 0 ) {
	syslog(LOG_ERR, "Cannot do second fork to create daemon.");
        exit(EXIT_FAILURE);
    }

    if( pid > 0 ) {
	// Exit parent. Note the use of _exit() rather than exit()
        // The _exit() performs the atexit() cleanup handler
	// which we do not want since that would delete the lockfile
        _exit(EXIT_SUCCESS);
    }

    // Use root as working directory
    if( chdir("/") < 0 ) {
        syslog(LOG_ERR, "Cannot change working directory to '/' for daemon.");
        exit(EXIT_FAILURE);
    }

    // Close all file descriptors
    logmsg(LOG_DEBUG,"Closing all predefined descriptors (num=%d)",getdtablesize());
    for (int i = getdtablesize(); i >= 0; --i) {
        (void)_dbg_close(i);
    }

    // Reopen stdin, stdout, stderr so the point harmlessly to /dev/null
    // (Some brain dead library routines might write to, for example, stderr)
    int i = open("/dev/null", O_RDWR);
    int dummy=dup(i);
    dummy=dup(i);
    logmsg(LOG_DEBUG,"Reopened descriptors 0,1,2 => '/dev/null'");
}

/*
 * Check and setup the assumed directory structure
 */
static void
_chkcreatedir(const char *basedir,char *dir) {
    const mode_t mode =  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    char bdirbuff[512];
    struct stat fstat ;
    
    snprintf(bdirbuff,511,"%s/%s",basedir,dir);
    bdirbuff[511] = '\0';
    if( -1 == stat(bdirbuff,&fstat) ) {
        if( -1 == mkdir(bdirbuff,mode) ) {
            logmsg(LOG_ERR,"FATAL: Cannot create directory %s (%d : %s).",
                   bdirbuff,errno,strerror(errno));
            exit(EXIT_FAILURE);
        } else {
	    logmsg(LOG_NOTICE,"Created directory '%s'",bdirbuff);
	}
    }
}

void
chkdirstructure(void) {
    char bdirbuff[512];

    if( strlen(datadir) > 255 ) {
        logmsg(LOG_ERR,"Base directory path can not be longer than 255 bytes.");
        exit(EXIT_FAILURE);
    }

    _chkcreatedir(datadir,"");
    _chkcreatedir(datadir,"vtmp");
    _chkcreatedir(datadir,"mp2");
    _chkcreatedir(datadir,"xmldb");
    _chkcreatedir(datadir,"mp4");
    _chkcreatedir(datadir,STATS_DIR);

    for(int i=0; i < max_video; i++) {
        snprintf(bdirbuff,511,"vtmp/vid%d",i) ;
        _chkcreatedir(datadir,bdirbuff);
    }

    // Create the profile directories under mp4
    struct transcoding_profile_entry **profiles;
    int nprof = get_transcoding_profile_list(&profiles);
    for( int i=0; i < nprof; i++) {
        snprintf(bdirbuff,511,"mp4/%s",profiles[i]->name);
        _chkcreatedir(datadir,bdirbuff);
    }
}

#define INIFILE_BUFFERSIZE 4096
static char inibuffer[INIFILE_BUFFERSIZE] = {0};

static const char short_options [] = "d:f:hi:l:p:vx:";
static const struct option long_options [] = {
    { "daemon",  required_argument,     NULL, 'd'},
    { "xmldb",   required_argument,     NULL, 'f'},
    { "help",    no_argument,           NULL, 'h'},
    { "inifile", required_argument,     NULL, 'i'},
    { "version", no_argument,           NULL, 'v'},
    { "logfile", required_argument,     NULL, 'l'},
    { "port",    required_argument,     NULL, 'p'},
    { "verbose", required_argument,     NULL, 'V'},
    { "xawtvrc", required_argument,     NULL, 'x'},
    { 0, 0, 0, 0}
};

void
parsecmdline(int argc, char **argv) {

    // Parse command line options
    int opt, index;
    *inifile='\0';
    *xmldbfile='\0';
    *logfile_name='\0';
    *xawtv_channel_file='\0';
    verbose_log = -1;
    tcpip_port = -1;
    opterr = 0; // Supress error string from getopt_long()
    if( argc > 8 ) {
        fprintf(stderr,"Too many arguments. Try '-h'.");
        exit(EXIT_FAILURE);
    }

    /*
     * Loop through all given input strings and check maximum length.
     * No single argument may be longer than 256 bytes (this could be
     * an indication of a buffer overflow attack)
     */
    for(int i=1; i < argc; i++) {
        if( strnlen(argv[i],256) >= 256  ) {
            fprintf(stderr, "Argument %d is too long.",i);
            exit(EXIT_FAILURE);
        }
    }

    while (-1 != (opt = getopt_long (argc,argv,short_options, long_options, &index)) ) {

        switch (opt) {
            case 0: /* getopt_long() flag */
                break;

            case 'h':
                fprintf(stdout,
                        "'%s' (C) 2009,2010 Johan Persson, (johan162@gmail.com) \n"
                        "This is free software; see the source for copying conditions.\nThere is NO "
                        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
#ifdef DEBUG_SIMULATE
                        " ** DEBUG BUILD ** WILL NOT RECORD REAL VIDEO STREAMS. THIS iS ONLY SIMULATION.\n"
#endif                        
                        "Usage: %s [options]\n"
                        "Synopsis:\n"
                        "TV PVR Server to schedule and handle recordings from a TV Card with HW MP2 encoder.\n"
                        "Options:\n"
                        " -h,      --help            Print help and exit\n"
                        " -v,      --version         Print version string and exit\n"
                        " -i file, --inifile=file    Use specified file as ini file\n"
                        " -d y/n,  --daemon          Run as daemon\n"
                        " -f file, --xmldbfile=file  Override initial XML database and load from file\n"
                        " -l file, --logfile=file    Override logfile setting in inifile and use file as logfile\n"
                        " -V n,    --verbose=n       Override inifile and set verbose level\n"
                        " -p n,    --port=n          Override inifile and set TCP/IP listen port\n"
                        " -x file, --xawtvrc=file    Override inifile and set station file\n",
                        server_program_name,server_program_name);
                exit(EXIT_SUCCESS);
                break;

            case 'v':
                fprintf(stdout,"%s %s (%s)\n%s",
                        server_program_name,server_version,server_build_date,
#ifdef DEBUG_SIMULATE
                        " *** DEBUG BUILD. WILL NOT RECORD REAL VIDEO STREAMS *** \n"
#endif

#ifdef _LARGEFILE64_SOURCE
                        "Compiled with LFS (Large File Support). Can handle files > 2GB.\n"
#endif
                        "Copyright (C) 2009 Johan Persson (johan162@gmail.com)\n"
                        "This is free software; see the source for copying conditions.\nThere is NO "
                        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
                exit(EXIT_SUCCESS);
                break;

            case 'i':
                if( optarg != NULL ) {
                    strncpy(inifile,optarg,255);
                    inifile[255] = '\0';
                    if( strlen(inifile) == 255 ) {
                        fprintf(stderr,"ini file given as argument is invalid. Too long.");
                        exit(EXIT_FAILURE);
                    }
                }
                break;

            case 'd':
                if( optarg != NULL ) {
                    daemonize = *optarg == 'y' ? 1 : 0;
                }
                else
                    daemonize = 1 ;
                break;

            case 'f':
                if( optarg != NULL ) {
                    strncpy(xmldbfile,basename(optarg),255);
                    xmldbfile[255] = '\0';
                    if( strlen(xmldbfile) == 255 ) {
                        fprintf(stderr,"xmldb file given as argument is invalid. Too long.");
                        exit(EXIT_FAILURE);
                    }
                }
                break;

            case 'V':
                verbose_log = *optarg == '1' ? 1 : 0;
                break;

            case 'l':
                if( optarg != NULL ) {
                    strncpy(logfile_name,optarg,255);
                    logfile_name[255] = '\0';
                    if( strlen(logfile_name) == 255 ) {
                        fprintf(stderr,"logfile file given as argument is invalid. Too long.");
                        exit(EXIT_FAILURE);
                    }
                }
                break;

            case 'p':
                if( optarg != NULL ) {
                    tcpip_port = validate(0,99999,"TCP/IP port on command line",atoi(optarg));
                }
                break;

            case 'x':
                if( optarg != NULL ) {
                    strncpy(xawtv_channel_file,optarg,255);
                    xawtv_channel_file[255] = '\0';
                    if( strlen(xawtv_channel_file) == 255 ) {
                        fprintf(stderr,"xawtvrc file given as argument is invalid. Too long.");
                        exit(EXIT_FAILURE);
                    }
                }
                break;
                
            case ':':
                fprintf(stderr, "Option `%c' needs a file name.\n", optopt);
                exit(EXIT_FAILURE);
                break;

            case '?':
                fprintf(stderr, "Invalid specification of program option(s). See --help for more information.\n");
                exit(EXIT_FAILURE);
                break;
        }
    }
#ifdef DEBUG_SIMULATE
    daemonize = 0 ;
#endif
    if (argc>1 && optind < argc) {
        fprintf(stderr, "Options not valid.\n");
        exit(EXIT_FAILURE);        
    }
}

void 
deleteockfile(void) {
  
    logmsg(LOG_NOTICE,"Removing lockfile '%s'.",lockfilename);
    if( -1 == unlink(lockfilename) ) {
	int gid = (int)getgid();
	int uid = (int)getuid();
	logmsg(LOG_ERR,"Cannot remove lock-file (%s) while running as uid=%d, gid=%d. (%d : %s)",
	       lockfilename,uid,gid,errno,strerror(errno));
    }
}

int
updatelockfilepid(void) {
    const mode_t fmode =  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    char buff[256];
    pid_t pid = getpid();
    *buff = '\0';
    snprintf(buff,255,"%s",TVPVRD_LOCKFILE);
    buff[255] = '\0';
    int fd = open(buff,O_CREAT|O_WRONLY,fmode);
    if( fd == -1 ) {        
        return -1;
    }    
    _writef(fd,"%d",pid);
    (void)_dbg_close(fd);
    return 0;
}

int
createlockfile(void) {
    struct stat fstat;
    pid_t pid = getpid();
    char buff[256];
    const mode_t fmode =  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    // First try to create it under "/var/run"
    *buff = '\0';
    snprintf(buff,255,"%s",TVPVRD_LOCKFILE);
    if( stat(buff,&fstat) == 0 ) {
        // File exists
        // Another instance of us are already running
	
	// Get the old pid
	int fd = open(buff, O_RDONLY);
	char pidbuff[32];
	int oldpid;
        if( -1 == read(fd,pidbuff,31) ) {
            _vsyslogf(LOG_ERR,"FATAL: Failed to read file '%s'",buff);
        }
	_dbg_close(fd);
	pidbuff[31]='\0';
	sscanf(pidbuff,"%d",&oldpid);	

	// Now check if the old pid exists under /proc
	snprintf(pidbuff,31,"/proc/%d",oldpid);
	_vsyslogf(LOG_NOTICE,"Lockfile %s exists. Checking proc entry for pid=%d",buff,oldpid);
	if( stat(pidbuff,&fstat) == 0 ) {
	    // File exists so this process really does exist
            _vsyslogf(LOG_NOTICE,"/proc/ entry for %d exists so this is really a running process.",oldpid);
            _vsyslogf(LOG_ERR,"Can't start server, another instance of '%s' is running with pid=%d.\n",
		    program_invocation_short_name,oldpid);
            return -1;
	} else {
            _vsyslogf(LOG_NOTICE,"There is no proc entry for pid=%d so this must be a stale lockfile.",oldpid);
	    // Process does not exist so this must be a stale lock file
	    // so we update the pid with our new pid
	    fd = open(buff,O_WRONLY|O_TRUNC,fmode);
	    if( fd == -1 ) {
                _vsyslogf(LOG_ERR,"Cannot clean up stale lockfile '%s'. Check permissions.",buff);
                return -1;
            } else {
                _writef(fd,"%d",pid);
                (void)_dbg_close(fd);
                strncpy(lockfilename,buff,255);
                lockfilename[255] = '\0';
	    }
	}
    }
    else {
        // Try to create the lockfile
        int fd = open(buff,O_CREAT|O_WRONLY,fmode);
        if( fd == -1 ) {
            char cwd[256];
            // Try using current working directory
            char *ret = getcwd(cwd,256);
            if( ret != NULL ) {
                snprintf(buff,255,"%s/%s",cwd,basename(TVPVRD_LOCKFILE));
                if( stat(buff,&fstat) == 0 ) {
                    // File exists
                    // Another instance of us could possible be running but to
                    // be sure we check if there is a process entry under /proc
                    // as well and if not then we assume that this is a stale
                    // lockfile that was left open due to a unclean shutdown.
		    // In that case we just remove the old lockfile and create a 
		    // new lockfile.

                    // Get the old pid
                    int fd = open(buff, O_RDONLY);
                    char pidbuff[32];
                    int oldpid;
                    if( -1 == read(fd,pidbuff,31) ) {
                        _vsyslogf(LOG_ERR,"FATAL: Failed to read file '%s'",buff);
                    }

                    _dbg_close(fd);
                    pidbuff[31]='\0';
                    sscanf(pidbuff,"%d",&oldpid);

                    // Now check if the old pid exists under /proc
		    snprintf(pidbuff,31,"/proc/%d",oldpid);
		    if( stat(pidbuff,&fstat) == 0 ) {
			// File exists so this process really does exist
			char buff[256];
			snprintf(buff,255,"Can't start server, another instance of '%s' is running with pid=%d.\n",
				program_invocation_short_name,oldpid);
                        _vsyslogf(LOG_ERR,buff);
			return -1;
		    } else {
			// Process does not exist so this must be a stale lock file
			// so we update the pid with our new pid
			fd = open(buff,O_WRONLY|O_TRUNC,fmode);
			if( fd == -1 ) {
                            _vsyslogf(LOG_ERR,"Cannot clean up stale lockfile '%s'. Check permissions.",buff);
			    return -1;
			} else {
			    _writef(fd,"%d",pid);
			    (void)_dbg_close(fd);
			    strncpy(lockfilename,buff,255);
			    lockfilename[255] = '\0';
			}					    
		    }
                } else {
                    fd = open(buff,O_CREAT|O_WRONLY,fmode);
                    if( fd == -1 ) {
			_vsyslogf(LOG_ERR,"Can't start server, unable to create lockfile. Permission problem ?");
                        return -1;
                    }
                    _writef(fd,"%d",pid);
                    (void)_dbg_close(fd);
		    strncpy(lockfilename,buff,255);
		    lockfilename[255] = '\0';
                }
            }
        } else {
            _writef(fd,"%d",pid);
            (void)_dbg_close(fd);
	    strncpy(lockfilename,buff,255);
	    lockfilename[255] = '\0';
        }
    }
    return 0;
}

void
exithandler(void) {
  
  // The deletion of the lockile will only succeed if we are runnign as
  // root since the lockfile resides in a directory owned by root
  // no other uid can remove it.
  // This is not a problem though since the startup will check that there
  // pid in the lockfile really eists.
  // deleteockfile();
  
}

void
chkswitchuser(void) {      
    // Check if we are starting as root
    struct passwd *pwe = getpwuid(getuid());
    
    if( strcmp(pwe->pw_name,"root") == 0 ) {
        strncpy( username, iniparser_getstring(dict, "config:username", DEFAULT_USERNAME), 63 );
        username[63] = '\0';
        if( strcmp(username,"root") != 0 ) {
            errno=0;
            pwe = getpwnam(username);
            if( pwe == NULL ) {
                logmsg(LOG_ERR,"Specified user to run as, '%s', does not exist. (%d : %s)",
                       username,errno,strerror(errno));
                exit(EXIT_FAILURE);
            }
            
            /*
            if( -1 == chown(lockfilename,pwe->pw_uid,pwe->pw_gid) ) {
                logmsg(LOG_ERR,"** Cannot change owner of lockfile to '%s'. (%d : %s)",username,errno,strerror(errno));
                exit(EXIT_FAILURE);
            } else {
             */
            
            // Make sure the data directory belongs to this new user
            char cmdbuff[64];

            logmsg(LOG_NOTICE,"Adjusting permission and owner on file structure (%s).",datadir);
            snprintf(cmdbuff,63,"chown -R %s %s",username,datadir);
            FILE *fp = popen(cmdbuff,"r");
            pclose(fp);

            snprintf(cmdbuff,63,"chgrp -R %d %s",pwe->pw_gid,datadir);
            fp = popen(cmdbuff,"r");
            pclose(fp);

            if( strcmp(logfile_name,"syslog") && strcmp(logfile_name,"stdout") ) {
                snprintf(cmdbuff,63,"chown %s %s",username,logfile_name);
                FILE *fp = popen(cmdbuff,"r");
                pclose(fp);

                snprintf(cmdbuff,63,"chgrp %d %s",pwe->pw_gid,logfile_name);
                fp = popen(cmdbuff,"r");
                pclose(fp);
            }

            // Make sure we run as belonging to the video group
            struct group *gre = getgrnam("video");
            if( gre == NULL ) {
                logmsg(LOG_ERR,"** Specified group to run as, '%s', does not exist. (%d : %s) **",
                   "video",errno,strerror(errno));
                exit(EXIT_FAILURE);
            }
            gid_t groups[2];
            groups[0] = pwe->pw_gid;
            groups[1] = gre->gr_gid;
            if( -1 == setgroups(2, groups) ) {
                logmsg(LOG_ERR,"** Cannot set groups. Check that '%s' belongs to the 'video' group. (%d : %s) **",
                   username,errno,strerror(errno));
                exit(EXIT_FAILURE);
            }
            setgid(pwe->pw_gid);
            setuid(pwe->pw_uid);
            logmsg(LOG_DEBUG,"Changing user,uid to '%s',%d",pwe->pw_name,pwe->pw_uid);
                /*
            }
                 */
        } else {
            logmsg(LOG_INFO,"*** The server is running as user 'root'. This is strongly discouraged. *");
        }
    }
}

void
read_inisettings(void) {
    // Get common master values from the ini file
    // Since iniparser is not reentant we must do it here and not individually
    // in each thread. Since all of these are read only there is no need to
    // protect these with a mutex

    /*--------------------------------------------------------------------------
     * CONFIG Section
     *--------------------------------------------------------------------------
     */
    max_video           = validate(1, 4,"max_video",
                                    iniparser_getint(dict, "config:max_video", MAX_VIDEO));
    max_entries         = validate(1,4096,"max_entries",
                                    iniparser_getint(dict, "config:max_entries", MAX_ENTRIES));
    max_clients         = validate(1,10,"max_clients",
                                    iniparser_getint(dict, "config:max_clients", MAX_CLIENTS));

    defaultDurationHour = validate(0,4,"recording_timehour",
                                    iniparser_getint(dict, "config:recording_timehour", DEFAULT_DURATIONHOUR));
    defaultDurationMin  = validate(0,59,"recording_timemin",
                                    iniparser_getint(dict, "config:recording_timemin", DEFAULT_DURATIONMIN));    

    if( tcpip_port == -1 ) {
        tcpip_port          = validate(1025,99999,"port",
                                        iniparser_getint(dict, "config:port", PORT));
    }

    max_idle_time       = validate(2*60,30*60,"client_idle_time",
                                    iniparser_getint(dict, "config:client_idle_time", CLIENT_IDLE_TIME));

    time_resolution     = validate(1,30,"time_resolution",
                                    iniparser_getint(dict, "config:time_resolution", TIME_RESOLUTION));

    video_bufsize       = validate(100*1024, 1000*1024,"video_bufsize",
                                    iniparser_getint(dict, "config:video_bufsize", VIDBUFSIZE));

    allow_profiles_adj_encoder = iniparser_getboolean(dict,"config:allow_profiles_adj_encoder",0);
    
    require_password = iniparser_getboolean(dict,"config:require_password",REQUIRE_PASSWORD);
    
    strncpy(password,
            iniparser_getstring(dict, "config:password", ""),
            31);
    password[31] = '\0';

    if( strlen(xawtv_channel_file) == 0 ) {
        strncpy(xawtv_channel_file,
                iniparser_getstring(dict, "config:xawtv_station_file", DEFAULT_XAWTV_STATION_FILE),
                255);
        xawtv_channel_file[255] = '\0';
    }

    if( -1 == read_xawtvfile(xawtv_channel_file) ) {
        logmsg(LOG_ERR,
                "FATAL error. "
                "Could not read specified xawtv station file '%s'",xawtv_channel_file);
        exit(EXIT_FAILURE);
    }
    strncpy(frequencymap_name,
            iniparser_getstring(dict, "config:frequency_map", DEFAULT_FREQUENCY_MAP),
            MAX_FMAPNAME_LENGTH-1);
    frequencymap_name[MAX_FMAPNAME_LENGTH-1] = '\0';
    if( -1 == set_current_freqmap(frequencymap_name) ) {
        logmsg(LOG_ERR,
                "FATAL error. "
                "Invalid frequency map specified (%s).\n",frequencymap_name);
        exit(EXIT_FAILURE);
    }

    strncpy(datadir,
            iniparser_getstring(dict, "config:datadir", DEFAULT_DATADIR),
            127);
    datadir[127] = '\0';

    strncpy(device_basename,
            iniparser_getstring(dict, "config:video_device_basename", VIDEO_DEVICE_BASENAME),
            127);
    device_basename[127] = '\0';

    /*--------------------------------------------------------------------------
     * FFMPEG Section
     *--------------------------------------------------------------------------
     */

    max_load_for_transcoding      = validate(1,10,"max_load_for_transcoding",
                                             iniparser_getint(dict, "ffmpeg:max_load_for_transcoding", MAX_LOAD_FOR_TRANSCODING));
    max_waiting_time_to_transcode = validate(60,43200,"max_waiting_time_to_transcode",
                                             iniparser_getint(dict, "ffmpeg:max_waiting_time_to_transcode", MAX_WAITING_TIME_TO_TRANSCODE));

    strncpy(ffmpeg_bin,
            iniparser_getstring(dict, "ffmpeg:ffmpeg_bin", FFMPEG_BIN),
            63);
    ffmpeg_bin[63] = '\0';

    strncpy(default_transcoding_profile,
            iniparser_getstring(dict, "ffmpeg:default_transcoding_profile", DEFAULT_TRANSCODING_PROFILE),
            31);
    default_transcoding_profile[31] = '\0';


    if( -1 == read_transcoding_profiles() ) {
        logmsg(LOG_ERR,"FATAL: No transcoding profiles defined. Aborting.");
        exit(EXIT_FAILURE);
    }

#ifndef DEBUG_SIMULATE
    // Verify that we can really open all the videos we are requsted to use
    for( int i=0; i < max_video; i++ ) {
        int vh = video_open(i);
        if( vh == -1 ) {
            logmsg(LOG_ERR,
                    "** FATAL error. "
                    "Cannot open video device '/dev/video%d' (%d : %s).\n",
                    i,errno,strerror(errno));
            exit(EXIT_FAILURE);
        }
        video_close(vh);
    }
#endif

    if( strlen(datadir) >= 127 || strlen(logfile_name) >= 127 ||
        strlen(device_basename) >= 127  ) {
                logmsg(LOG_ERR,
                "** FATAL error. "
                "Illegal value for either datadir, logfile_name, device_basename or video_frame_size_name. "
                "Specified parameter is too long. Corrupt ini file ?");
        exit(EXIT_FAILURE);
    }
    
    if( datadir[strlen(datadir)-1] == '/' )
        datadir[strlen(datadir)-1] = '\0';
    
    if( strcmp("stdout",logfile_name) == 0 && daemonize ) {
        logmsg(LOG_ERR,
                "** FATAL error. "
                "\"stdout\" is not a valid logfile when started in daemon mode.");
        exit(EXIT_FAILURE);
    }
}

void
init_tvxmldb(void) {    
    // If an XML DB file was given as an argument use this location as the xml db file
    // Otherwise use the XMLDB file specified in the ini-file
    if( strlen(xmldbfile) > 0 ) {
        logmsg(LOG_INFO,"Reading initial XML DB from: \"%s\".", xmldbfile);
        if( -1 == readXMLFile(xmldbfile) ) {
        logmsg(LOG_ERR,
                "FATAL error. "
                "Could not read XML DB file '%s'.",xmldbfile);
            exit(EXIT_FAILURE);
        }
    }
    else {
        snprintf(xmldbfile,255,"%s/xmldb/%s",
                datadir,
                basename(iniparser_getstring(dict, "config:xmldbfile_name", XMLDBFILE_NAME))
                );
        xmldbfile[255] = '\0';
        if( strlen(xmldbfile) >= 255 ) {
            logmsg(LOG_ERR,
                    "FATAL error. "
                    "Name of XML DB file is not valid. String too long.\n");
            exit(EXIT_FAILURE);
        }
        // If the XML DB File doesn't exist we will create an empty one
        struct stat fstat;
        if( -1 == stat(xmldbfile,&fstat) ) {
            if( -1 == writeXMLFile(xmldbfile) ) {
                logmsg(LOG_ERR,"Failed to initialize xmldb datafile. (%d : %s)",errno,strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
        logmsg(LOG_INFO,"Reading initial XML DB from: \"%s\".", xmldbfile);
        if( -1 == readXMLFile(xmldbfile) ) {
            logmsg(LOG_INFO,
                "No DB file found. Will be created in '%s' when saved.",
                xmldbfile);
        }
    }
}

void
init_capture_cards(void) {

    // If each profile is allowed to set the HW encoding paramters then we
    // do not need to do anything here.
    if( !allow_profiles_adj_encoder ) {
        struct transcoding_profile_entry *profile;
        get_transcoding_profile(default_transcoding_profile,&profile);
        for(int video=0; video < max_video; video++) {
            int fd = video_open(video);
            int ret = set_enc_parameters(fd,profile);
            video_close(fd);
            if( -1 == ret  ) {
                // Nothing else to do than to quit
                logmsg(LOG_ERR,"Fatal error. Cannot initialize HW capture card(s) ( %d : %s )", errno, strerror(errno));
                _exit(EXIT_FAILURE);
            }
        }
    }
}


/* ==================================================================================
 * M A I N
 * ==================================================================================
 */
int
main(int argc, char *argv[]) {
    sigset_t signal_set;
    pthread_t signal_thread;

    // Set lockfile to avoid multiple instances running
    if( -1 == createlockfile() ) {
        fprintf(stderr,"Cannot start server. Check system log for more information.\n");
        _exit(EXIT_FAILURE);
    }

    atexit(exithandler);

    // Remember the program name we are started as
    strncpy(inibuffer,argv[0],256);
    strncpy(server_program_name,basename(inibuffer),31);
    server_program_name[31] = '\0';
    
    // Remember when the server was started
    ts_serverstart = time(NULL);
    
    // Initialize the static frequency map. 
    initfreqtable();
    
    // Parse and set cmd line options
    parsecmdline(argc,argv);

    // Get current directory
    char cwd_buff[256];
    char *res = getcwd(cwd_buff,255);
    if( res == NULL ) {
        fprintf(stderr,"FATAL: Cannot determine working directory: (%d:%s)\n",errno,strerror(errno));
        exit(EXIT_FAILURE);
    }
    cwd_buff[255] = '\0';

    // Check for inifile at common locations
    if( *inifile ) {
        // Specified on the command line. Overrides the default
        dict = iniparser_load(inifile);
    } else {        
        snprintf(inifile,255,"%s/tvpvrd/%s",CONFDIR,INIFILE_NAME);
        inifile[255] = '\0';
        dict = iniparser_load(inifile);     
        if( !dict ) {
            // As a last resort check the default /etc directory
            snprintf(inifile,255,"/etc/tvpvrd/%s",INIFILE_NAME);
            dict = iniparser_load(inifile);
            if( dict == NULL )
                *inifile = '\0';
        }
    }

    if( dict == NULL ) {
        fprintf(stderr,"Can not find the ini file : '%s'\n",INIFILE_NAME);
        exit(EXIT_FAILURE);
    }

    if( verbose_log == -1 ) {
        verbose_log = iniparser_getint(dict, "config:verbose_log", VERBOSE_LOG);
    }
    if( strlen(logfile_name) == 0 ) {
        strncpy(logfile_name,
                iniparser_getstring(dict, "config:logfile_name", LOGFILE_SYSLOG),
                255);
        logfile_name[255] = '\0';
    }

    // From now on we now the name of the logfile so we can use the log function
    logmsg(LOG_INFO,"Starting up ... ");
    logmsg(LOG_INFO,"Using ini-file '%s'",inifile);          
    
    if( daemonize == -1 ) {
        daemonize = iniparser_getboolean(dict, "config:daemonize", DEFAULT_DAEMONIZE);
    }
    
    if( daemonize ) {
        startdaemon();
	logmsg(LOG_DEBUG,"Reborn as a daemon");

        // Update the PID in the lockfile since have a new PID
        if( -1 == updatelockfilepid() ) {
            logmsg(LOG_ERR,"Can't update lockfile with new daemon PID. Aborting.");
            exit(EXIT_FAILURE);
        }

    }
    
    //----------------------------------------------------------------------------------------
    // Note: The order of the initialization below is somewhat important. The vital dependcies
    // are:
    // - Lockfile must be created while in possible root mode (Since we are accessing /var/run)
    // - Lockfile must be created after a possible switch to daemon mode since that changes
    //	 pid.
    // - The ini-setting must have been read before we create the directory structure.
    // - The global data structures must ne initialized after the ini filename
    // - The global data structures must exist before we read the xml db filename
    //----------------------------------------------------------------------------------------


    // Get the overall settings from the ini-file
    read_inisettings();

    // Check and create the necessary directory structure if it doesn't already exist
    chkdirstructure();

    // Check if we should run as another user if we are started as root.
    // For security a daemon should never run as a root unless necessary
    // and in our case it is not necessary for us to run as root.
    chkswitchuser();

    // After a possible setuid() adn setgrp() the dumapable falg is reset which means
    // that no core is dumped in case of a SIGSEGV signal. We want a coredump in case
    // of a memory overwrite so we make sure this is allowed
    if( -1 == prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) ) {
        logmsg(LOG_ERR,"FATAL: Can not set PR_SET_DUMPABLE");
        _exit(EXIT_FAILURE);
    }

    // Initialize all the data structures that stores our recording
    init_globs();

    // Read the initial recording DB from the specified file on either there
    // command line or from the ini-file. The command line always override
    // setting sin the ini-file
    init_tvxmldb();

#ifndef DEBUG_SIMULATE
    // Initialize the Capture card(2)
    init_capture_cards();
#endif
    
    // We use a separate thread to receive all the signals so we must
    // block all signals in all other threads
    sigfillset( &signal_set );
    pthread_sigmask( SIG_BLOCK, &signal_set, NULL );
    pthread_create( &signal_thread, NULL, sighand_thread, NULL );
    
    // Start the thread that will be monitoring the recording list and
    // in turn setup a new thread to do a recording when the time has come
    (void) pthread_create(&chkrec_thread, NULL, chkrec, (void *) NULL);

    // Startup the main socket server listener. 
    if( EXIT_FAILURE == startupsrv() ) {
        logmsg(LOG_ERR,"Unable to start '%s' server.",program_invocation_short_name);
	exit(EXIT_FAILURE);
    }
        
    logmsg(LOG_INFO,"Received signal %d. Shutting down ...",received_signal);
    
    pthread_mutex_lock(&recs_mutex);
    // Shutdown all ongoing recordings
    for(int i=0; i < max_video; i++) {
        if( ongoing_recs[i] && abort_video[i] == 0 ) {
            abort_video[i] = 1;
            logmsg(LOG_INFO,"  -- Aborting recording on video %d",i);
        } else {
            abort_video[i] = 0;
        }
    }

    // Close all clients
    for(int i=0; i < max_clients; i++ ) {
        if( cli_threads[i] ) {
            _dbg_close(client_socket[i]);
            logmsg(LOG_INFO,"  -- Disconnecting client from %s",client_ipadr[i]);
        }
    }        
    pthread_mutex_unlock(&recs_mutex);
			
    // Refresh DB file after a clean exit.    
    UPDATE_DB();

    // Store the calculated statistics
    (void)write_stats();

    // Wait until all recordings have stopped or we have waited more than 15s        
    int watchdog = 15;
    volatile int ongoing = 0;
    for(int i=0; i < max_video; i++) {
        ongoing |= abort_video[i];
    }
    while( ongoing && watchdog > 0 ) {            
        ongoing=0;
        for(int i=0; i < max_video; i++) {
            ongoing |= abort_video[i];                
        }             
        sleep(1);
        logmsg(LOG_INFO,"Waiting for video to stop [%d] ...",
            watchdog);
        watchdog--;
    }

    if( dokilltranscodings ) {
        kill_all_ongoing_transcodings();
    }

    logmsg(LOG_INFO,"Bye.");
    free_globs();
    exit(EXIT_SUCCESS);
}

/* EOF */
