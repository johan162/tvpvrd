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
#include "config.h"
#include "tvwebcmd.h"
#include "lockfile.h"

/*
 * Server identification
 */
char server_version[] = PACKAGE_VERSION; // This define gets set by the config process
char server_build_date[] = __DATE__;     // This is a builtin define set by the compiler
char server_program_name[32] = {0};

/*
 * The value of these variables are read from the ini-file and initialized in
 * the main() function. They hold various run time limits and settings that
 * the user can adjust. Some of these values can also be overridden by being
 * given as options when the daemon starts
 */

// Time delay before the daemon really starts doing stuff if
// the dameon is started within 3 minutes of the machine power
// on.
// This might seem a really odd thing to have but it has one
// really useful purpose. On older machines where the bios clock
// is not correctly updated by the ntpd daemon at shutdown it
// could be that in the few seconds after the machine starts up
// and until the ntpd daemon kicks in the time is wrong. This is
// especially annoying when the switch to/from daylight saving time
// takes place. If the bios clock is not correctly updated at shutdown
// the first few seconds after the server starts the time will be off
// by one hour.
// This variable species a suitable number of seconds for the daemon to
// sleep at startup so that the ntpd daemons has had time to correct the
// machine time. Of course this delay is only really necessary when the
// daaemon is started up just after the server has been powered on. So
// this delay only kicks in if the daemon is started within 3 minutes of the
// machine power on. This can also be adjusted as an argument when starting
// the daemon (-t)
int tdelay=30;

// Should we run as a daemon or nothing
int daemonize=-1;

// Maximum sizes
unsigned max_entries, max_video, max_clients, max_idle_time;

// Default recording length if nothing else is specified
int defaultDurationHour, defaultDurationMin;

// Record if we are master or slave
int is_master_server;

// TVP/IP Port to listen to
unsigned short int tcpip_port;

// Logfile details
int verbose_log;
char logfile_name[256] = {'\0'};

// Time resolution for checks
unsigned time_resolution;

// The video buffer (used when reading the video stream from the capture card)
// One buffer for each video card. We support up to 4 simultaneous cards
char *video_buffer[MAX_VIDEO];

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
 * Global variables for MP4 transcoding
 */

// Full path of ffmpeg bin. Read from ini-file
char ffmpeg_bin[64];

// Default transcoding profile. Read from ini-file
char default_transcoding_profile[32];

// Maximum average load on server to start a new transcoding process. Read from ini-file
int max_load_for_transcoding ;

// Maximum waiting time to start a new transcoding process. Read from ini-file
// (if this is 0 then we will wait indefinitely to transcode a file). The wait happens
// if the load on the server is too high to start a new transcoding.
int max_waiting_time_to_transcode ;

/*
 * abort_video int
 * Flag set by the main thread to signal to one of the recording thread
 * to cancel recording
 */
volatile int * volatile abort_video; 

/*
 * rec_threads
 * The thread id for each active recording thread
 */
static pthread_t *rec_threads; 

/*
 * cli_threads
 * The thread id for each thread that are handling a client
 */
pthread_t *cli_threads; 
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
char **client_ipadr; 

/*
 * client_tsconn
 * Keep track of timestamps when the client connected
 */
time_t *client_tsconn; 

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
static int *client_socket;

/*
 * video_idx
 * The video stream index. Used in the call to pthread_create() to avoid
 * local stack based variables as arguments in the thread creation
 */
static int *video_idx; 

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
char web_password[32];
char web_user[32];
int require_web_password;
int weblogin_timeout;


/*
 * Determine if we should use subdirectories for each profile or just
 * store all videos directly under mp2/ or mp4/
 */
int use_profiledirectories = 1;

/*
 * Mail setting. Determine if we should send mail on errors and what address
 * to use. Read from the inifile normally.
 */
int send_mail_on_error, send_mail_on_transcode_end;
char send_mailaddress[64];

/*
 * What locale to use, by default this is set to LOCALE_NAME (which in the original distribution
 * is set to en_US.UTF8)
 */
char locale_name[255];

/*
 * Should the WEB-interface available be enabled
 */
int enable_webinterface = 0 ;       

/*
 * The tuner input index on the card. This is necessaru so we know which
 * input to select that is the tuner.
 */
int tuner_input_index;

/*
 * The following memory routines are just used to double check that
 * all the calls to PCRE regep routines are matched with respect to
 * memory allocation.
 */

int pcre_mem_count=0;
struct tvp_mem_entry {
    void *ptr;
    size_t size;
    struct tvp_mem_entry *next;
};

struct tvp_mem_entry *pcre_mem_list = (void *)NULL;
int tvp_call_count=0;

void *tvp_malloc(size_t size) {
    struct tvp_mem_entry *ent = calloc(1,sizeof(struct tvp_mem_entry));
    ent->size=size;
    ent->ptr=malloc(size);
    
    struct tvp_mem_entry *walk = pcre_mem_list;
    
    if( walk == NULL ) {
        pcre_mem_list = ent;
    } else {
        while( walk->next ) {
            walk = walk->next;
        }
        walk->next = ent;
    }
    tvp_call_count++;
    //logmsg(LOG_DEBUG,"PCRE MALLOC: %06d bytes",size);
    return ent->ptr;
}

void tvp_free(void *ptr) {
    struct tvp_mem_entry *walk = pcre_mem_list;
    struct tvp_mem_entry *prev = NULL;

    while( walk && walk->ptr != ptr ) {
        prev = walk;
        walk = walk->next;
    }
    if( walk == NULL ) {
        logmsg(LOG_CRIT,"FATAL: Trying to deallocat PCRE without previous allocation !");
    } else {
        if( prev == NULL ) {
            // First entry in list
            pcre_mem_list = walk->next;
            //logmsg(LOG_DEBUG,"PCRE FREE: %06d bytes",walk->size);
            free(walk->ptr);
            free(walk);
        } else {
            prev->next = walk->next;
            //logmsg(LOG_DEBUG,"PCRE FREE: %06d bytes",walk->size);
            free(walk->ptr);
            free(walk);
        }
    }
    tvp_call_count--;
}

void
tvp_mem_list(int sockd) {
    struct tvp_mem_entry *walk = pcre_mem_list;
    int n=0;
    _writef(sockd,"PCRE MALLOC List: %02d\n",tvp_call_count);
    while( walk ) {
        ++n;
        _writef(sockd,"  #%0002d: size = %06d bytes\n",n,walk->size);
        walk = walk->next;
    }
}

void
init_globs(void) {

    // Initialize the recording data structures
    initrecs();
    cmdinit();

    if( is_master_server ) {
        rec_threads     = (pthread_t *) calloc(max_video,   sizeof (pthread_t));
        video_idx       =       (int *) calloc(max_video,   sizeof (int));
        abort_video     =       (int *) calloc(max_video,   sizeof (int));

        for (unsigned i = 0; i < max_video; ++i) {
            // Index of video stream. Used to avoid local stack based variables
            // to be sent in the pthread_create() call
            video_idx[i] = (int)i;
        }
    }

    cli_threads     = (pthread_t *) calloc(max_clients, sizeof (pthread_t));
    client_ipadr    =     (char **) calloc(max_clients, sizeof (char *));
    client_tsconn   =    (time_t *) calloc(max_clients, sizeof (time_t));
    client_socket   =       (int *) calloc(max_clients, sizeof (int));

    if( is_master_server ) {
        for(unsigned i=0; i < max_video; ++i) {
            video_buffer[i] = calloc(VIDBUFSIZE, sizeof(char *));
            if( video_buffer[i] == NULL ) {
                fprintf(stderr,"Cannot allocate video buffer memory. (%d : %s)",errno,strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
    }

    if ( is_master_server ) {
        if (rec_threads   == NULL || video_idx == NULL ||  abort_video   == NULL) {
            fprintf(stderr, "FATAL: Out of memory running as master. Aborting server.");
            exit(EXIT_FAILURE);
        }
    }

    if (cli_threads == NULL || client_ipadr == NULL || client_tsconn == NULL || client_socket == NULL ) {
        fprintf(stderr, "FATAL: Out of memory. Aborting server.");
        exit(EXIT_FAILURE);
    }

}

void
free_globs(void) {


    cmdfree();
    freerecs();

    for(unsigned i=0; i < max_clients; i++) {
        if( client_ipadr[i] )
            free(client_ipadr[i]);
        free(video_buffer[i]);
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


/**
 * Set the video encoding HW parameters on the video card from the values specified in the
 * supplied profile
 * @param fd
 * @param profile
 * @return 0 on success, -1 on failure
 */
int
set_enc_parameters(int fd, struct transcoding_profile_entry *profile) {
    char infobuff[256];
    double sampling[] = {44.1, 48.0, 32.0};
    int abps[] = {192, 224, 256, 320, 384};
    char *aspect[] = {"1x1","4x3","16x9","221x100"};
    int w,h;

    int ret = video_get_wh_fromname(&w, &h, profile->encoder_video_frame_size_name);
    if( ret == -1 ) {
        logmsg(LOG_ERR,"Unknown video frame size specified in prodile '%s' : '%s'",
                profile->name,profile->encoder_video_frame_size_name);
        return -1;
    }

    int i=2;
    ret = video_set_video_bitrate(fd, profile->encoder_video_bitrate, profile->encoder_video_peak_bitrate);
    while( ret == -1 && errno == EBUSY && i > 0 ) {
        usleep((unsigned)(700*(3-i)));
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
            "aspect:('%s'), "
            "framesize:('%s'=%dx%d) ]",
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
setup_video(unsigned video,struct transcoding_profile_entry *profile) {
#ifndef DEBUG_SIMULATE
    char infobuff[256];
#endif

#ifndef DEBUG_SIMULATE
    int fd = video_open(video);
    if( fd == -1 ) {
        return -1;
    }

    // Give the driver some breathing room after we open the device
    // and until we start changing the settings.
    usleep(500000);

    int ret,i=2;
    ret = video_set_channel(fd, ongoing_recs[video]->channel);
    while( ret == -1 && errno == EBUSY && i > 0 ) {
        usleep((unsigned)(500*(3-i)));
        ret = video_set_channel(fd, ongoing_recs[video]->channel);
        i--;
    }

    if( ret == -1 ) {
        video_close(fd);
        return -1;
    }

    if( 0 == strncmp(ongoing_recs[video]->channel,INPUT_SOURCE_PREFIX,strlen(INPUT_SOURCE_PREFIX)) ) {

        snprintf(infobuff,255,
                "Setting up video %d HW MP2 encoder to take input from source '%s'",
                video,ongoing_recs[video]->channel);

    } else {

        unsigned int freq=0;
        getfreqfromstr(&freq, ongoing_recs[video]->channel);
        snprintf(infobuff,255,
                 "Tuner #%02d set to channel '%s' @ %.3fMHz",
                 video,ongoing_recs[video]->channel, freq/1000000.0
        );

    }
    logmsg(LOG_DEBUG,infobuff);

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
                        struct transcoding_profile_entry *profile,
                        unsigned *filesize, struct timeall *transcode_time, float *avg_5load) {

    struct rusage usage;
    CLEAR(*transcode_time);
    int rh, rm, rs;

    // We do not start transcoding if the recording was aborted
    // If the bitrate is set to < 10kbps then this indicates that no
    // transcoding should be done. We just move the MP2 file to the mp2 directory
    int transcoding_done=0;
    if ( !profile->use_transcoding || profile->video_bitrate == 0) {

        // Do nothing. The MP2 file will be moved by the calling function.
        logmsg(LOG_DEBUG,"Transcoding disabled in profile '%s' for file '%s'",profile->name,short_filename);
        return 0;

    } else  {

        // Check that we can access ffmpeg binary
        if( -1 == check_ffmpeg_bin() ) {
            logmsg(LOG_ERR,"Profile '%s' specifies transcoding but 'ffmpeg' executable can not be found.",profile->name);
            return -1;
        }

        // If recording was successful then do the transcoding
        char cmdbuff[1024], cmd_ffmpeg[512], destfile[128] ;
        int runningtime = 0;

        // We remember all wating transcodings by storing them in global queue
        // This way we can easily list all transcoding that are waiting
        pthread_mutex_lock(&recs_mutex);
        int rid=remember_waiting_transcoding(short_filename,profile->name);
        pthread_mutex_unlock(&recs_mutex);

        if (0 == wait_to_transcode(short_filename)) {
            // The system load is below the treshold to start a new transcoding

            pthread_mutex_lock(&recs_mutex);
            forget_waiting_transcoding(rid);
            pthread_mutex_unlock(&recs_mutex);

            logmsg(LOG_INFO, "Using profile '%s' for transcoding of '%s'", profile->name, short_filename);

            create_ffmpeg_cmdline(short_filename, profile, destfile, 128, cmd_ffmpeg, 512);

            snprintf(cmdbuff, 1023, "cd %s;%s", workingdir, cmd_ffmpeg);

#ifdef DEBUG_SIMULATE
            
            snprintf(cmdbuff, 256, "%s/%s", workingdir, destfile);
            const mode_t fmode =  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
            int sfd = open(cmdbuff, O_WRONLY | O_CREAT | O_TRUNC, fmode);
            logmsg(LOG_INFO, "Simulation mode: No real transcoding. Creating fake file '%s'",cmdbuff);
            _writef(sfd, "Fake MP4 file created during simulation at ts=%u\n", time(NULL));
            (void) close(sfd);
            rh = rm = rs = -1;
            transcoding_done = 1;
            CLEAR(usage);

#else
            pid_t pid;
            if ((pid = fork()) == 0) {
                // In fork child process
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
                if ( -1 == nice(20) ) {
                    logmsg(LOG_ERR, "Error when calling 'nice()' : (%d : %s)",errno,strerror(errno));
                    _exit(EXIT_FAILURE);
                }

                if ( -1 == execl("/bin/sh", "sh", "-c", cmdbuff, (char *) 0) ) {
                    logmsg(LOG_ERR, "Error when calling execl() '/bin/sh/%s' : ( %d : %s )", cmdbuff, errno, strerror(errno));
                    _exit(EXIT_FAILURE);
                }
            } else if (pid < 0) {
                logmsg(LOG_ERR, "Fatal. Can not create process to do transcoding for file '%s' (%d : %s)",
                        short_filename, errno, strerror(errno));
            } else {

                // In parent process
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

                    // We only allow one transcoding to run for a maximum of 49 h any longer than
                    // that and we consider the transcoding process as hung
                    const int watchdog = 49 * 3600;
                    int ret;
                    float avg1,avg5,avg15;
                    getsysload(&avg1,&avg5,&avg15);
                    *avg_5load = avg5;
                    float avg_n = 1;
                    do {
                        // Transcoding usually takes hours so we don't bother waking up and check
                        // if we are done more often than once every couple of seconds. This
                        // might still seem like a short time but keep in mind that in the case the
                        // user termintaes the transcoding process the server will not notice this
                        // until this check is made. Therefore we want this to be reasonable
                        // short as well. Ideally the wait4() should have a timeout argument in
                        // which case we would have been notified sooner.

                        // Why exactly 6s wait? Well, in case the user terminates the process it
                        // means that on average it will take 3s before the structures are updated
                        // with the removed transcoding which is the longest time a user ever should
                        // have to waut for feedback.
                        sleep(6);
                        runningtime += 6;
                        getsysload(&avg1,&avg5,&avg15);
                        *avg_5load += avg5;
                        avg_n++;
                        rpid = wait4(pid, &ret, WCONTINUED | WNOHANG | WUNTRACED, &usage);

                    } while (pid != rpid && runningtime < watchdog);

                    *avg_5load /= avg_n;
                    
                    pthread_mutex_lock(&recs_mutex);
                    forget_ongoingtranscoding(tidx);
                    pthread_mutex_unlock(&recs_mutex);

                    rh = runningtime / 3600;
                    rm = (runningtime - rh*3600)/60;
                    rs = runningtime % 60;

                    if (runningtime >= watchdog) {
                        // Something is terrible wrong if the transcoding haven't
                        // finished after the watchdog timeout
                        logmsg(LOG_NOTICE, "Transcoding process for file '%s' seems hung. Have run more than %02d:%02d:%02d h",
                                short_filename, rh,rm,rs);
                        (void) kill(pid, SIGKILL);
                    } else {

                        if (WIFEXITED(ret)) {
                            transcoding_done = (WEXITSTATUS(ret) == 0);
                            if (WEXITSTATUS(ret) == 0) {
                                if( runningtime < 60 ) {
                                    logmsg(LOG_NOTICE, "Error in transcoding process for file '%s' after %02d:%02d:%02d h",
                                        short_filename, rh,rm,rs);

                                } else {
                                    logmsg(LOG_INFO, "Transcoding process for file '%s' finished normally after %02d:%02d:%02d h. (utime=%d s, stime=%d s))",
                                        short_filename, rh,rm,rs, usage.ru_utime.tv_sec, usage.ru_stime.tv_sec);

                                }
                            } else {
                                logmsg(LOG_INFO, "Error in transcoding process for file '%s' after %02d:%02d:%02d h",
                                        short_filename, rh,rm,rs);
                                return -1;
                            }
                        } else {
                            if (WIFSIGNALED(ret)) {
                                logmsg(LOG_NOTICE, "Transcoding process for file '%s' was terminated by signal=%d (possibly by user) after %02d:%02d:%02d h",
                                        short_filename, WTERMSIG(ret),rh,rm,rs);
                                return -1;
                            } else {
                                // Child must have been stopped. If so we have no choice than to kill it
                                logmsg(LOG_NOTICE, "Transcoding process for file '%s' was unexpectedly stopped by signal=%d after %02d:%02d:%02d h",
                                        short_filename, WSTOPSIG(ret),rh,rm,rs);
                                (void) kill(pid, SIGKILL);
                                return -1;
                            }

                        }
                    }
                }
            }
#endif
        } else {
            logmsg(LOG_NOTICE, "Can not start transcoding of '%s'. Server too busy.", short_filename);
            return -1;
        }

        // If transcoding was not successfull we give up and just leave the recording under the
        // vtmp directory
        if (transcoding_done) {
            char newname[256], tmpbuff2[256], tmpbuff[256];

            // Move MP4 file
            if( use_profiledirectories ) {
                snprintf(tmpbuff, 255, "%s/mp4/%s/%s", datadir, profile->name, destfile);
            } else {
                snprintf(tmpbuff, 255, "%s/mp4/%s", datadir, destfile);
            }
            tmpbuff[255] = '\0';
            snprintf(tmpbuff2, 255, "%s/%s", workingdir, destfile);
            tmpbuff2[255] = '\0';
            int ret = mv_and_rename(tmpbuff2, tmpbuff, newname, 256);
            if (ret) {
                logmsg(LOG_ERR, "Could not move '%s' to '%s'", tmpbuff2, newname);
                return -1;
            } else {
                logmsg(LOG_INFO, "Moved '%s' to '%s'", tmpbuff2, newname);
                struct stat fstat;
                // Find out the size of the transcoded file
                if( 0 == stat(newname,&fstat) ) {
                    
                    *filesize = (unsigned)fstat.st_size;
                    transcode_time->rtime.tv_sec = runningtime ;
                    transcode_time->utime.tv_sec = usage.ru_utime.tv_sec;
                    transcode_time->stime.tv_sec = usage.ru_stime.tv_sec;

                } else {
                    logmsg(LOG_ERR,"Can not determine size of transcoded file '%s'. ( %d : %s) ",
                           newname,errno,strerror(errno));
                }

                // The complete transcoding and file relocation has been successful. Now check
                // if we should send a mail with this happy news!
                if( send_mail_on_transcode_end ) {
                    char mailbuff[2048];
                    
                    // Include system load average in mail
                    float l1,l5,l15;
                    getsysload(&l1,&l5,&l15);

                    // Get full current time to include in mail
                    static const int tblen = 32;
                    char timebuff[tblen] ;
                    time_t now = time(NULL);
                    ctime_r(&now,timebuff);
                    timebuff[strnlen(timebuff,(size_t)(tblen-1))] = 0;

                    // Include the server name in the mail
                    char hostname[80];
                    gethostname(hostname,80);
                    hostname[79] = '\0';

                    // Include all ongoing transcodings
                    char ongtr_buff[1024];
                    list_ongoing_transcodings(ongtr_buff,1023,0);
                    ongtr_buff[1023] = '\0';
                    
                    // Also include all waiting transcodings
                    char waittr_buff[1024];
                    list_waiting_transcodings(waittr_buff,1023);
                    waittr_buff[1023] = '\0';

                    // Finally list the three next recordings
                    char nextrec_buff[1024];
                    listrecsbuff(nextrec_buff,1023,3,4);
                    nextrec_buff[1023] = '\0';

                    snprintf(mailbuff,2047,
                                "Transcoding of \"%s\" using profile \"@%s\" done.\n\n"
                                "Server: %s\n"
                                "Time: %s \n"
                                "Moved file to: \"%s\"\n"
                                "Transcoding time: %02d:%02d\n"
                                "System load: %.1f %.1f %.1f\n\n"
                                "Ongoing transcodings:\n%s\n"
                                "Waiting transcodings:\n%s\n"
                                "Upcoming recordings:\n%s\n\n",
                                short_filename,profile->name,
                                hostname,
                                timebuff,
                                tmpbuff,
                                rh,rm,
                                l1,l5,l15,
                                ongtr_buff,
                                waittr_buff,
                                nextrec_buff);

                    mailbuff[2047] = '\0';
                    char subjectbuff[256];

                    snprintf(subjectbuff,255,"Transcoding %s done",short_filename);
                    subjectbuff[255] = '\0';
                    logmsg(LOG_DEBUG,"Mail subject: %s",subjectbuff);
                    logmsg(LOG_DEBUG,"Mail body: %s",mailbuff);
                    send_mail(subjectbuff,send_mailaddress,mailbuff);

                }
            }
        }
    }
    return 0;
}

/*
 * Start a recording on the specified video stream immediately using the information in the
 * current recording record.
 * This function is only run in its own thread that is created in chkrec() when it decides a
 * new recording should be started. After the recording have been sucessfully finished
 * the transcoding is initiated.
 */

void *
startrec(void *arg) {
    ssize_t nread, nwrite;
    char full_filename[256], workingdir[256], short_filename[256];
    const mode_t dmode =  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    const mode_t fmode =  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    unsigned mp2size = 0;
    volatile int doabort = 0 ;

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for each created thread
    pthread_detach(pthread_self());

    unsigned video = *(unsigned *) arg;
    struct recording_entry *recording = ongoing_recs[video];
    struct transcoding_profile_entry *profile, *tmp_profile;

    // In case there are many profiles defined for this recording we use the profile with the
    // highest video bitrate quality to set the HW MP2 encoder.
    int chosen_profile_idx = 0 ;
    int multi_prof_flag = FALSE; // This flasg get set if we find more than one profile
                                 // Used to control the extra debug print out below
    get_transcoding_profile(recording->transcoding_profiles[0],&profile);
    for(int i=1; i < REC_MAX_TPROFILES && strlen(recording->transcoding_profiles[i]) > 0; i++) {
        multi_prof_flag = TRUE;
        get_transcoding_profile(recording->transcoding_profiles[i],&tmp_profile);
        if( tmp_profile->video_bitrate > profile->video_bitrate )
            chosen_profile_idx = i;
    }
    get_transcoding_profile(recording->transcoding_profiles[chosen_profile_idx],&profile);
    if( multi_prof_flag ) {
        logmsg(LOG_DEBUG,"Using profile '%s' for HW MP2 settings for recording of '%s'",
               recording->transcoding_profiles[chosen_profile_idx],
               recording->title);
    }

    int vh = setup_video(video,profile);

    abort_video[video] = 0;
            
    if (vh == -1) {

       logmsg(LOG_ERR, "Cannot open video stream %02d. '%s' recording aborted ( %d : %s )",video,recording->title,errno,strerror(errno));
       pthread_mutex_lock(&recs_mutex);
       free(recording);
       ongoing_recs[video] = (struct recording_entry *)NULL;
       pthread_mutex_unlock(&recs_mutex);

       pthread_exit(NULL);
       return (void *)NULL;

    } else {

        int k = (int)strnlen(recording->filename,REC_MAX_NFILENAME)-1;
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
                // First wait until we have some data available from the video capture card
                // via a select() call. We only wait for the descriptor associated with this capture
                // card. If there is no error and the call wasn't interrupted (EINTR) we go ahead
                // and read as much data as the card wants to give us. Normally this is a whole number of
                // frames. The data read is normnally in the ange ~8k to ~80k in size.
                // ---------------------------------------------------------------------------------------

                //logmsg(LOG_DEBUG,"    -- [%02d] select WAITING %d (%s)",video,vh,full_filename);
                ret = select (vh + 1, &fds, NULL, NULL, &tv);
                //logmsg(LOG_DEBUG,"    -- [%02d] select WAITING %d READY ret=%d, errno=%d (%s)",video,vh,ret,errno,full_filename);

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
                    //logmsg(LOG_DEBUG,"    -- [%02d] Trying to read bytes from fd=%d '%s'",video,vh, recording->title);
                    nread = read(vh, video_buffer[video], VIDBUFSIZE);
                    //logmsg(LOG_DEBUG,"    -- [%02d] DONE. Read %05d bytes from fd=%d '%s'",video,nread,vh,recording->title);

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

                        nwrite = write(fh, video_buffer[video], (size_t)nread);
                        //logmsg(LOG_DEBUG,"    -- [%02d] nwrite=%d after writing to '%s'",video,nwrite,full_filename);

                        if( nwrite == -1 ) {
                            logmsg(LOG_ERR, "Error while writing to '%s' while recording. (%d : %s) ",
                                    full_filename,errno,strerror(errno));
                            doabort = 1;
                        } else {
                            mp2size += (unsigned)nwrite;
                            doabort = abort_video[video];
                        }

                    } 
                }
            } while (recording->ts_end > time(NULL) && !doabort);

#else
            logmsg(LOG_INFO,"Started simulated recording to file '%s'.", full_filename);
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

            if( -1 == _dbg_close(fh) ) {
                logmsg(LOG_ERR,"Failed to close file handle of recorded file. ( % d : % s )",errno,strerror(errno));
            }
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
        unsigned keep_mp2_file = 0 ;

        if( !doabort && (nread == nwrite) ) {

            transcoding_problem = 0;
            unsigned mp4size=0;
            float avg_5load;
            struct timeall transcode_time;
            CLEAR(transcode_time);

            for(int i=0; i < REC_MAX_TPROFILES && strlen(recording->transcoding_profiles[i]) > 0; i++) {
                get_transcoding_profile(recording->transcoding_profiles[i],&profile);

                // If any of the profiles used requries the mp2 file to be kept explicietely or
                // that no transcoding will be done we keep the mp2 file.
                keep_mp2_file |= profile->encoder_keep_mp2file | !profile->use_transcoding;

                logmsg(LOG_NOTICE,"Transcoding using profile: %s",profile->name);
                int ret = transcode_and_move_file(datadir,workingdir,short_filename,
                                                  profile,
                                                  &mp4size,&transcode_time,&avg_5load);
                transcoding_problem |= ret;
                if( 0 == ret ) {
                    stats_update(recording->transcoding_profiles[i],
                                 mp2size,
                                 (unsigned)(recording->ts_end - recording->ts_start),
                                 mp4size,
                                 &transcode_time, avg_5load);
                }
            }

        }

        if (!transcoding_problem) {
            char tmpbuff[256], newname[512];

            // Move the original mp2 file if the user asked to keep it
            int delete_workingdir = 1;
            if (keep_mp2_file) {
                // Move MP2 file
                if( use_profiledirectories ) {
                    snprintf(tmpbuff, 255, "%s/mp2/%s/%s", datadir, profile->name, short_filename);
                } else {
                    snprintf(tmpbuff, 255, "%s/mp2/%s", datadir, short_filename);
                }
                tmpbuff[255] = '\0';

                if (mv_and_rename(full_filename, tmpbuff, newname, 512)) {
                    logmsg(LOG_ERR, "Could not move '%s' to '%s'", full_filename, newname);
                    delete_workingdir = 0;
                } else {
                    logmsg(LOG_INFO, "Moved '%s' to '%s'", full_filename, newname);
                }
            }

            // Delete the temporary directory used while recording and transcoding
            // unless there were a problem with the transcoding.
            if (!doabort && delete_workingdir) {
                if (removedir(workingdir)) {
                    logmsg(LOG_ERR, "Could not delete directory '%s'.", workingdir);
                } else {
                    logmsg(LOG_INFO, "Deleted directory '%s'.", workingdir);
                }
            }
        } else if( !doabort ) {
            logmsg(LOG_ERR,"Transcoding error. Leaving original MP2 file under '%s'",full_filename);
        }
    }

    free(recording);
    pthread_exit(NULL);
    return (void *) 0;
}





/*
 * This is the main thread to watch for starting new recordings. It is started at the beginning
 * of the server and run until the server is shut down. It will loop and check every 'time_resolution'
 * second if a queued recoding is due to be started.
 * There is no argument passed to this thread.
 */
void *
chkrec(void *arg) {
    time_t now;
    int diff, ret;
    volatile unsigned video;

    arg = NULL;  // Avoid warning on unused variable

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for exah created thread
    pthread_detach(pthread_self());

    // Do some sanity checks of time_resolution
    time_resolution = MIN(MAX(time_resolution,1),10);

    // We are string as a thread that runs forever
    while (1) {

        // We need the current time to compare against
        now = time(NULL);

        pthread_mutex_lock(&recs_mutex);

        // Each video has it's own queue which we check

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

                // Flag to keep track if we should update the XML DB file or not
                // We only update the file in case we have actually a) ignored a recording or
                // b) actually started a recording;
                int update_xmldb = 0;
                if (diff > 60 * 10) {
                    int sy,sm,sd,sh,smin,ssec;
                    fromtimestamp(recs[REC_IDX(video, 0)]->ts_start,&sy,&sm,&sd,&sh,&smin,&ssec);
                    logmsg(LOG_ERR, "Time for recording of ('%s' %d-%02d-%02d %02d:%02d) on video %d is too far in the past. Recording cancelled.",
                           recs[REC_IDX(video, 0)]->title, sy,sm,sd,sh,smin,video);
                    deletetoprec(video);
                    update_xmldb = 1; // We removed a entry so update DB file
                } else {
                    // Start the initiation at least (TIME_RESOLUTION-1) s before the exact start
                    // time since we cannot guarantee that the thread will be scheduled right
                    // away. It will also take a few ms to setup the card and tuner so we
                    // err on the safe side here. So in general this means that on average the
                    // recording will start ~TIME_RESOLUTION s before the scheduled time.
                    if (diff >= -(int)time_resolution) {
                        volatile void  *active;
                        active = ongoing_recs[video];

                        // Now, we have a potential issue here. If we have an existing recording
                        // that is set to stop at say, 14:00 then since that recording will only stop
                        // after the video buffer is full it could happen that it will actually not stop
                        // until a few seconds after 14:00.
                        // To make things worse this new recording could potentially start at
                        // 14:00 - time_resolution, and this will not work. So, we check if there is an
                        // existing recording ongoing and it it is we give a warning message
                        // and try again

                        if ( active != NULL) {
                            logmsg(LOG_ERR, "Can not start, '%s' using stream %02d. Previous recording (%s) has not yet stopped. Will try again.",
                                   recs[REC_IDX(video, 0)]->title,video,ongoing_recs[video]->title);
                        } else {
                            // Remember what recording is currently taking place for this video stream
                            ongoing_recs[video] = recs[REC_IDX(video, 0)];

                            // Remove it from the list of pending recordings
                            removetoprec(video);

                            update_xmldb = 1; // We removed a entry so update DB file

                            // Start the thread that will actually do the recording to the file system
                            ret = pthread_create(&rec_threads[video], NULL, startrec, (void *) & video_idx[video]);
                            if (ret != 0) {
                                logmsg(LOG_ERR, "Could not create thread for recording.");
                            }
                        }
                    }
                }
                if( update_xmldb ) {
                    if (writeXMLFile(xmldbfile) >= 0 ) {
                        char msgbuff[256];
                        snprintf(msgbuff, 255,"Database successfully updated '%s' after recording has been done", xmldbfile);
                        msgbuff[255] = 0 ;
                        logmsg(LOG_INFO,msgbuff);
                    } else {
                        char msgbuff[256];
                        snprintf(msgbuff, 255,"Failed to update database '%s' after recording has been done§    ", xmldbfile);
                        msgbuff[255] = 0 ;
                        logmsg(LOG_ERR,msgbuff);
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
clientsrv(void *arg) {
    int ret;
    unsigned i;
    ssize_t numreads;
    int my_socket = *(int *) arg;
    char buffer[1024];
    fd_set read_fdset;
    struct timeval timeout;

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for exah created thread
    pthread_detach(pthread_self());

    bzero(buffer, 1024);
    
    i = 0;
    while (i < max_clients && client_socket[i] != my_socket)
        i++;

    if( i >= max_clients ) {
        logmsg(LOG_ERR,"Internal error. Socket (%d) for new client is invalid! ", my_socket);
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
    
    if( require_password ) {
        int tries = 3;
        int authenticated = 0;

        while( tries > 0 && !authenticated) {
            _writef(my_socket, "Password: ");

            FD_ZERO(&read_fdset);
            FD_SET(my_socket, &read_fdset);

            timerclear(&timeout);
            timeout.tv_sec = 120; // 2 min timeout to give a password
            
            ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);   
            if( 0 == ret ) {
                // Timeout
                logmsg(LOG_INFO,"Timeout for password query from %s on socket %d", client_ipadr[i], my_socket);
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
    snprintf(buffer,1023, WELCOM_MSG, server_version, 
            is_master_server ? "Server" : "Client",
            server_build_date, ncli_threads, max_clients, max_idle_time/60);
    buffer[1023] = '\0';
    pthread_mutex_unlock(&socks_mutex);

    _writef(my_socket, buffer);

    // Keep track of client idle time
    unsigned idle_time=0;

    do  {

        FD_ZERO(&read_fdset);
        FD_SET(my_socket, &read_fdset);

        timerclear(&timeout);
        timeout.tv_sec = 60;

        ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);
        if (ret == 0) {

            //Timeout
            idle_time += 60;
            // logmsg(LOG_DEBUG, "Client %d terminal timeout. Idle for a total of %d seconds",i,idle_time);

            if (idle_time >= max_idle_time) {
                numreads = -1; // Force a disconnect
                logmsg(LOG_INFO, "Client disconnected after being idle for more than %d seconds.", max_idle_time);
            } else {
                numreads = 1; // To keep the loop going
            }
        } else {
            idle_time = 0;
            numreads = read(my_socket, buffer, 1023);
            buffer[1023] = '\0';
            buffer[numreads] = 0;
            
            if ( 0 == strcmp("exit\r\n", buffer) ) {
                // Exit command. Exit the loop and close the socket
                _writef(my_socket,"Goodbye.\n");
                numreads = -1;
                break;
            } else if( 0 == strncmp("GET",buffer,3) ) {
                // Web commands must always close the socket after each command
                char wcmd[1024];
                if( webconnection(buffer,wcmd,1023) ) {
                    logmsg(LOG_INFO, "Client (%s) sent WEB command: %s", client_ipadr[i], wcmd);
                    cmdinterp(buffer, my_socket);
                    numreads = -1;
                    break;
                } else {
                    logmsg(LOG_ERR, "Client (%s) sent ILLEGAL WEB command: %s", client_ipadr[i], buffer);
                    numreads = -1;
                    break;
                }
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
 * This is the main function that gets started when browser connects to
 * the server. This has been made into a separate function from clientsrv()
 * in order to keep the code clean since there are a number of subtle differences
 * in order to handle a browser request. This would otherwise force a number of
 * strange tests in clientsrv() that would make the code difficult to follow.
 *
 * This function is run in its own thread.
 */
void *
webclientsrv(void *arg) {
    ssize_t numreads;
    unsigned i;
    int ret;
    int my_socket = *(int *) arg;
    char buffer[1024];
    fd_set read_fdset;
    struct timeval timeout;

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for exah created thread
    pthread_detach(pthread_self());

    // First we need to find out what the index the socket that we will used
    // have in the array of all open sockets. This is necessary since we use the same
    // index to keep track of a lot of information about this connection. You can view the
    // idnex as a unique key for this connection.
    i = 0;
    while (i < max_clients && client_socket[i] != my_socket)
        i++;

    if( i >= max_clients ) {
        logmsg(LOG_ERR,"Internal error. Socket (%d) for new client is invalid.", my_socket);
        pthread_mutex_lock(&socks_mutex);

        client_socket[i] = 0;
        free(client_ipadr[i]);
        client_ipadr[i] = 0;
        client_tsconn[i] = 0;
        cli_threads[i] = 0;
        if( -1 == _dbg_close(my_socket) ) {
            logmsg(LOG_ERR,"Failed to close socket %d to client %s after error. ( %d : %s )",
                        my_socket,client_ipadr[i],errno,strerror(errno));
        }
        ncli_threads--;
        pthread_mutex_unlock(&socks_mutex);
        pthread_exit(NULL);
        return (void *) 0;
    }

    // FIXME: Password authentication from browser
    if( require_password ) {
        logmsg(LOG_ERR, "Browser connection does not support authentication (yet)");
    }

    // Now wait for activity on this port to execute one command and then
    // terminate.
    FD_ZERO(&read_fdset);
    FD_SET(my_socket, &read_fdset);
    timerclear(&timeout);
    timeout.tv_sec = 2;
    ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);
    if (ret == 0) {
        logmsg(LOG_ERR, "WEB Browser disconnected due to timeout.");
    } else {

        // We have activity so read from the socket and try to interpret the
        // given command.
        numreads = read(my_socket, buffer, 1023);
        buffer[1023] = '\0';
        buffer[numreads] = '\0';
        html_cmdinterp(my_socket,buffer);

    }

    // After the command has been executed we close the connection

    logmsg(LOG_INFO,"Connection from browser %s on socket %d closed.", client_ipadr[i], my_socket);

    pthread_mutex_lock(&socks_mutex);

    client_socket[i] = 0;
    free(client_ipadr[i]);
    client_ipadr[i] = 0;
    client_tsconn[i] = 0;
    cli_threads[i] = 0;
    if( -1 == _dbg_close(my_socket) ) {
        logmsg(LOG_ERR,"Failed to close socket %d to client %s after command has been interpretated. ( %d : %s )",
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
 * is started. If the WEB interface have been enable we also add a listener
 * to the specified WEB interface port.
 */
int
startupsrv(void) {
    int sockd=-1, websockd=-1, newsocket=-1;
    unsigned i;
    unsigned tmpint;
    struct sockaddr_in socketaddress, websocketaddress;
    int ret;
    char *dotaddr=NULL, tmpbuff[128];
    fd_set read_fdset;
    struct timeval timeout;

    // Create the socket for terminal connection (TCP)
    if ((sockd = socket(AF_INET, SOCK_STREAM, 0)) < 1) {
        logmsg(LOG_ERR, "Unable to create socket. (%d : %s)",errno,strerror(errno));
        exit(EXIT_FAILURE);
    }

    // To allow the server to be restarted quickly we set the SO_REUSEADDR flag
    // Otherwise the server would give a "Port in use" error if the server were restarted
    // within approx. 30s after it was shutdown. This is due to the extra safey wait
    // that Unix does after a socket has been shut down just to be really, really make sure
    // that there is no more data coming.
    int so_flagval = 1;
    if( -1 == setsockopt(sockd, SOL_SOCKET, SO_REUSEADDR, (char *)&so_flagval, sizeof(int)) ) {
        logmsg(LOG_ERR, "Unable to set socket options. (%d : %s)",errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&socketaddress, 0, sizeof (socketaddress));
    socketaddress.sin_family = AF_INET;
    socketaddress.sin_addr.s_addr = htonl(INADDR_ANY);
    socketaddress.sin_port = htons(tcpip_port);

    if (bind(sockd, (struct sockaddr *) & socketaddress, sizeof (socketaddress)) != 0) {
        logmsg(LOG_ERR, "Unable to bind socket to port. Most likely som other application is using this port.");
        exit(EXIT_FAILURE);
    }

    // Listen on socket, queue up to 5 connections
    if (listen(sockd, 5) != 0) {
        logmsg(LOG_ERR, "Unable to listen on socket ");
        exit(EXIT_FAILURE);
    }

    // We don't want to risk that a child holds this descriptor
    set_cloexec_flag(sockd, 1);

    if( enable_webinterface ) {

        // Create the socket for web connection (TCP)
        if ((websockd = socket(AF_INET, SOCK_STREAM, 0)) < 1) {
            logmsg(LOG_ERR, "Unable to create websocket. (%d : %s)",errno,strerror(errno));
            exit(EXIT_FAILURE);
        }

        so_flagval = 1;
        if( -1 == setsockopt(websockd, SOL_SOCKET, SO_REUSEADDR, (char *)&so_flagval, sizeof(int)) ) {
            logmsg(LOG_ERR, "Unable to set socket options. (%d : %s)",errno, strerror(errno));
            exit(EXIT_FAILURE);
        }

        memset(&websocketaddress, 0, sizeof (websocketaddress));
        websocketaddress.sin_family = AF_INET;
        websocketaddress.sin_addr.s_addr = htonl(INADDR_ANY);
        websocketaddress.sin_port = htons(tcpip_port+1);

        if (bind(websockd, (struct sockaddr *) & websocketaddress, sizeof (websocketaddress)) != 0) {
            logmsg(LOG_ERR, "Unable to bind socket to port. Most likely som other application is using this port.");
            exit(EXIT_FAILURE);
        }

        // Listen on socket, queue up to 5 connections
        if (listen(websockd, 5) != 0) {
            logmsg(LOG_ERR, "Unable to listen on socket ");
            exit(EXIT_FAILURE);
        }

        set_cloexec_flag(websockd, 1);

    }

    logmsg(LOG_INFO,"%s successfully initialized.",server_program_name);
    logmsg(LOG_INFO,"Listening on port=%d for connections.", tcpip_port);
    if( enable_webinterface ) {
        logmsg(LOG_INFO,"Listening on port=%d for WEB connections.", tcpip_port+1);
    }


    // Run until we receive a SIGQUIT or SIGINT awaiting client connections and
    // setting up new client communication channels
    while ( 1 ) {

        // =======================================================================
        // MAIN WAIT FOR NEW CONNECTIONS
        // =======================================================================

        // We must reset this each time since select() modifies it
        FD_ZERO(&read_fdset);
        FD_SET(sockd, &read_fdset);

        if( enable_webinterface ) {
            FD_SET(websockd, &read_fdset);
        }

        timeout.tv_sec = 0;
        timeout.tv_usec = 800;
        if( enable_webinterface ) {
            ret = select(websockd + 1, &read_fdset, NULL, NULL, &timeout);
        } else {
            ret = select(sockd + 1, &read_fdset, NULL, NULL, &timeout);
        }

        if( ret == 0 ) {
            // Timeout, so take the opportunity to check if we received a
            // signal telling us to quit. We don't care about what signal. Since the signal
	    // handler is only allowing signals that we consider stopping signals)
            if( received_signal ) 
                break;
            else
                continue;
        }

        int terminal_connection = 0;
        if( FD_ISSET(sockd,&read_fdset) ) {

            logmsg(LOG_DEBUG, "Terminal connection.");
            terminal_connection = 1;
            tmpint = sizeof (socketaddress);
            newsocket = accept(sockd, (struct sockaddr *) & socketaddress, &tmpint);
            if( newsocket < 0 ) {
                // Unrecoverable error
                logmsg(LOG_ERR, "Could not create new client socket ( %d : %s ) ",errno,strerror(errno));
            }
            dotaddr = inet_ntoa(socketaddress.sin_addr);

        } else if( enable_webinterface )  {

            logmsg(LOG_DEBUG, "Browser connection.");
            tmpint = sizeof (websocketaddress);
            newsocket = accept(websockd, (struct sockaddr *) & websocketaddress, &tmpint);
            if( newsocket < 0 ) {
                // Unrecoverable error
                logmsg(LOG_ERR, "Could not create new browser socket ( %d : %s ) ",errno,strerror(errno));
            }
            dotaddr = inet_ntoa(websocketaddress.sin_addr);

        } else {
            // This should never happen. This case indicates a network/kernel problem on the server
            logmsg(LOG_CRIT, "Internal serious error. Accepted port connection that we were not listening on. ");
        }

        set_cloexec_flag(newsocket,1);
        pthread_mutex_lock(&socks_mutex);

        logmsg(LOG_INFO, "Client number %d have connected from IP: %s on socket %d", ncli_threads + 1, dotaddr, newsocket);

        // Find the first empty slot for storing the client thread id
        i = 0;
        while (i < max_clients && cli_threads[i])
            i++;

        if (i < max_clients) {

            client_socket[i] = newsocket;
            client_ipadr[i] = strdup(dotaddr); // Released in clisrv()
            client_tsconn[i] = time(NULL);
            
            if( terminal_connection )
                ret = pthread_create(&cli_threads[i], NULL, clientsrv, (void *) & client_socket[i]);
            else
                ret = pthread_create(&cli_threads[i], NULL, webclientsrv, (void *) & client_socket[i]);

            if (ret != 0) {
                logmsg(LOG_ERR, "Could not create thread for client ( %d :  %s )",errno,strerror(errno));
            } else {
                ncli_threads++;
            }

        } else {

            logmsg(LOG_ERR, "Client connection not allowed. Maximum number of clients (%d) already connected.",max_clients);
            strcpy(tmpbuff, "Too many client connections.\n");
            _writef(newsocket, tmpbuff);
            _dbg_close(newsocket);

        }

        pthread_mutex_unlock(&socks_mutex);

    }
    
    logmsg(LOG_DEBUG,"Closing main listening socket.");
    if( -1 == _dbg_close(sockd) ) {
        logmsg(LOG_ERR,"Failed to close main listening socket. ( %d : %s )",errno,strerror(errno));
    }

    return EXIT_SUCCESS;
}

/*
 * This is the signal receiveing thread. In order to gurantee that we don't get any
 * deadlocks all signals is masked in all other threads and only this thread can receive
 * signals. The function then sets the global variable handled_signal
 */
void *
sighand_thread(void *arg) {
    sigset_t signal_set;
    int sig;
    arg = NULL;

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
        // The exit() performs the atexit() cleanup handler
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
        // The exit() performs the atexit() cleanup handler
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

    // Reopen stdin, stdout, stderr so they point harmlessly to /dev/null
    // (Some brain dead library routines might write to, for example, stderr)
    int i = open("/dev/null", O_RDWR);
    int dummy=dup(i);
    dummy=dup(i);
    logmsg(LOG_DEBUG,"Reopened descriptors 0,1,2 => '/dev/null'");
}

/**
 * Check that the directory structure for the stored recordigs are in place. In case
 * a directory is missing it is created.
 */
void
chkdirstructure(void) {
    char bdirbuff[512];

    if( strlen(datadir) > 255 ) {
        logmsg(LOG_ERR,"Base directory path can not be longer than 255 bytes.");
        exit(EXIT_FAILURE);
    }

    if( -1 == chkcreatedir(datadir,"") ||
        -1 == chkcreatedir(datadir,"vtmp") ||
        -1 == chkcreatedir(datadir,"mp2") ||
        -1 == chkcreatedir(datadir,"xmldb") ||
        -1 == chkcreatedir(datadir,"mp4") ||
        -1 == chkcreatedir(datadir,STATS_DIR) ) {
        exit(EXIT_FAILURE);
    }

    for(unsigned i=0; i < max_video; i++) {
        snprintf(bdirbuff,511,"vtmp/vid%d",i) ;
        if( -1 == chkcreatedir(datadir,bdirbuff) ) {
            exit(EXIT_FAILURE);
        }
    }

    // Create the profile directories under mp4/mp2 if user has enabled
    // profile subdirectory hierachy
    if( use_profiledirectories ) {
        struct transcoding_profile_entry **profiles;
        unsigned nprof = get_transcoding_profile_list(&profiles);
        for( unsigned i=0; i < nprof; i++) {
            snprintf(bdirbuff,511,"mp4/%s",profiles[i]->name);
            if( -1 == chkcreatedir(datadir,bdirbuff) ) {
                exit(EXIT_FAILURE);
            }
            snprintf(bdirbuff,511,"mp2/%s",profiles[i]->name);
            if( -1 == chkcreatedir(datadir,bdirbuff) ) {
                exit(EXIT_FAILURE);
            }
        }
    }
}

#define INIFILE_BUFFERSIZE 4096
static char inibuffer[INIFILE_BUFFERSIZE] = {0};

static const char short_options [] = "d:f:hi:l:p:vx:V:st:";
static const struct option long_options [] = {
    { "daemon",  required_argument,     NULL, 'd'},
    { "xmldb",   required_argument,     NULL, 'f'},
    { "help",    no_argument,           NULL, 'h'},
    { "inifile", required_argument,     NULL, 'i'},
    { "version", no_argument,           NULL, 'v'},
    { "logfile", required_argument,     NULL, 'l'},
    { "port",    required_argument,     NULL, 'p'},
    { "slave",   no_argument,           NULL, 's'},
    { "tdelay",  required_argument,     NULL, 't'},
    { "verbose", required_argument,     NULL, 'V'},
    { "xawtvrc", required_argument,     NULL, 'x'},
    { 0, 0, 0, 0}
};

/**
 * Parse all command line options given to the server at startup. The server accepts both
 * long and short version of command line options.
 * @param argc
 * @param argv
 */
void
parsecmdline(int argc, char **argv) {

    // Parse command line options
    int opt, index;
    *inifile='\0';
    *xmldbfile='\0';
    *logfile_name='\0';
    *xawtv_channel_file='\0';
    verbose_log = -1;
    tcpip_port = 0;
    is_master_server = -1;
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
                        " -x file, --xawtvrc=file    Override inifile and set station file\n"
                        " -s,      --slave           Run with slave configuration\n"
                        " -t,      --tdelay          Extra wait time when daemon is started at system power on\n",

                        server_program_name, server_program_name);
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
                if( *optarg == '1' || *optarg == '2' || *optarg == '3' )
                    verbose_log = *optarg - '0' ;
                else {
                    logmsg(LOG_ERR,"Illegal verbose level specified. must be in range [1-3]. Aborting.");
                    exit(EXIT_FAILURE);
                }
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
                    tcpip_port = (short unsigned int)validate(0,99999,"TCP/IP port on command line",atoi(optarg));
                }
                break;

            case 's':
                is_master_server = 0;
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

            case 't':
                if( optarg != NULL ) {
                    tdelay = validate(2,600,"tdelay on command line",atoi(optarg));
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

/**
 * Exit handler. Automatically called when the process calls "exit()"
 */
void
exithandler(void) {

    struct passwd *pwe = getpwuid(getuid());

    // The deletion of the lockile will only succeed if we are running as
    // root since the lockfile resides in a directory owned by root
    // no other uid can remove it.
    // This is not a problem though since the startup will check that the
    // pid in the lockfile really exists.
    if (strcmp(pwe->pw_name, "root") == 0) {
        deleteockfile();
    }
}

/**
 * Check what user we are running as and change the user (if allowed) to the
 * specified tvpvrd user.
 */
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

            if( is_master_server ) {
                // Make sure the data directory belongs to this new user
                char cmdbuff[64];

                logmsg(LOG_NOTICE,"Adjusting permission and owner on file structure (%s).",datadir);
                snprintf(cmdbuff,63,"chown -R %s %s",username,datadir);
                int dummyret = system(cmdbuff);

                snprintf(cmdbuff,63,"chgrp -R %d %s",pwe->pw_gid,datadir);
                dummyret = system(cmdbuff);

                if( strcmp(logfile_name,"syslog") && strcmp(logfile_name,"stdout") ) {
                    snprintf(cmdbuff,63,"chown %s %s",username,logfile_name);
                    dummyret = system(cmdbuff);

                    snprintf(cmdbuff,63,"chgrp %d %s",pwe->pw_gid,logfile_name);
                    dummyret = system(cmdbuff);
                }
            }

            // Make sure we run as belonging to the video group
            struct group *gre = getgrnam("video");
            if( gre == NULL ) {
                logmsg(LOG_ERR,"Specified group to run as, '%s', does not exist. (%d : %s) **",
                   "video",errno,strerror(errno));
                exit(EXIT_FAILURE);
            }
            
            gid_t groups[2];
            groups[0] = pwe->pw_gid;
            groups[1] = gre->gr_gid;
            if( -1 == setgroups(2, groups) ) {
                logmsg(LOG_ERR,"Cannot set groups. Check that '%s' belongs to the 'video' group. (%d : %s) **",
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
            logmsg(LOG_INFO,"The server is running as user 'root'. This is strongly discouraged. *");
        }
    }
}

/**
 * Get common master values from the ini file
 * Since iniparser is not reentrant we must do it here and not individually
 * in each thread. Since all of these are read only there is no need to
 * protect these with a mutex
 */
void
read_inisettings(void) {

    /*--------------------------------------------------------------------------
     * CONFIG Section
     *--------------------------------------------------------------------------
     */
    if( is_master_server == -1 ) {
        // Not specified on the command line
        is_master_server    = iniparser_getboolean(dict,"config:master",MASTER_SERVER);
    }

    tuner_input_index   = validate(0, 7,"tuner_input_index",
                                   iniparser_getint(dict, "config:tuner_input_index", DEFAULT_TUNER_INPUT_INDEX));

    max_entries         = (unsigned)validate(1,4096,"max_entries",
                                   iniparser_getint(dict, "config:max_entries", MAX_ENTRIES));
    max_clients         = (unsigned)validate(1,10,"max_clients",
                                   iniparser_getint(dict, "config:max_clients", MAX_CLIENTS));

    defaultDurationHour = validate(0,4,"recording_timehour",
                                   iniparser_getint(dict, "config:recording_timehour", DEFAULT_DURATIONHOUR));
    defaultDurationMin  = validate(0,59,"recording_timemin",
                                   iniparser_getint(dict, "config:recording_timemin", DEFAULT_DURATIONMIN));    

    if( tcpip_port == 0 ) {
        // Not specified on the command line
        tcpip_port          = (short unsigned)validate(1025,99999,"port",
                                       iniparser_getint(dict, "config:port", PORT));
    }

    max_idle_time       = (unsigned)validate(2*60,30*60,"client_idle_time",
                                    iniparser_getint(dict, "config:client_idle_time", CLIENT_IDLE_TIME));

    time_resolution     = (unsigned)validate(1,30,"time_resolution",
                                    iniparser_getint(dict, "config:time_resolution", TIME_RESOLUTION));

    allow_profiles_adj_encoder = iniparser_getboolean(dict,"config:allow_profiles_adj_encoder",0);
    
    require_password = iniparser_getboolean(dict,"config:require_password",REQUIRE_PASSWORD);

    enable_webinterface = iniparser_getboolean(dict,"config:enable_webinterface",ENABLE_WEBINTERFACE);
    require_web_password = iniparser_getboolean(dict,"config:require_web_password",REQUIRE_PASSWORD);
    strncpy(web_user,
            iniparser_getstring(dict, "config:web_user", WEB_USER),
            31);
    web_user[31] = '\0';
    strncpy(web_password,
            iniparser_getstring(dict, "config:web_password", WEB_PASSWORD),
            31);
    web_password[31] = '\0';
    weblogin_timeout = validate(0,120,"weblogin_timeout",
                                    iniparser_getint(dict, "config:weblogin_timeout", WEBLOGIN_TIMEOUT));
    weblogin_timeout *= 60; // Convert to seconds

    send_mail_on_transcode_end = iniparser_getboolean(dict,"config:sendmail_on_transcode_end",SENDMAIL_ON_TRANSCODE_END);
    send_mail_on_error = iniparser_getboolean(dict,"config:sendmail_on_error",SENDMAIL_ON_ERROR);

    strncpy(send_mailaddress,
            iniparser_getstring(dict, "config:sendmail_address", SEND_MAILADDRESS),
            63);
    send_mailaddress[63] = '\0';
    
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

    if( is_master_server ) {
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
    }

    strncpy(datadir,
            iniparser_getstring(dict, "config:datadir", DEFAULT_DATADIR),
            127);
    datadir[127] = '\0';

    use_profiledirectories =
            iniparser_getboolean(dict, "config:use_profile_directories", DEFAULT_USE_PROFILE_DIRECTORIES);

    datadir[127] = '\0';

    strncpy(device_basename,
            iniparser_getstring(dict, "config:video_device_basename", VIDEO_DEVICE_BASENAME),
            127);
    device_basename[127] = '\0';

    max_video           = (unsigned)validate(0, 5,"max_video",
                                   iniparser_getint(dict, "config:max_video", MAX_VIDEO));

    if( 0 == max_video ) {
        // Automatically determine the maximum number of cards
        max_video = (unsigned)_vctrl_getnumcards();
    }



    /*--------------------------------------------------------------------------
     * FFMPEG Section
     *--------------------------------------------------------------------------
     */

    max_load_for_transcoding      = validate(1,10,"max_load_for_transcoding",
                                             iniparser_getint(dict, "ffmpeg:max_load_for_transcoding", MAX_LOAD_FOR_TRANSCODING));
    max_waiting_time_to_transcode = validate(0,MAX_WAITING_TIME_TO_TRANSCODE,"max_waiting_time_to_transcode",
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
        _exit(EXIT_FAILURE);
    }

#ifndef DEBUG_SIMULATE
    if( is_master_server ) {
        // Verify that we can really open all the videos we are requsted to use
        for( unsigned i=0; i < max_video; i++ ) {
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
                "'stdout' is not a valid logfile when started in daemon mode.");
        exit(EXIT_FAILURE);
    }
}

/**
 * Initialize the recording database. This is a plain text file in XML format.
 * The full structure of the DB is defined with an XML RNG (grammar) stored in the
 * document folder in the distribution.
 */
void
init_tvxmldb(void) {    
    // If an XML DB file was given as an argument use this location as the xml db file
    // Otherwise use the XMLDB file specified in the ini-file
    if( strlen(xmldbfile) > 0 ) {
        logmsg(LOG_INFO,"Reading initial XML DB from: '%s'.", xmldbfile);
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
        logmsg(LOG_INFO,"Reading initial XML DB from: '%s'.", xmldbfile);
        if( -1 == readXMLFile(xmldbfile) ) {
            logmsg(LOG_INFO,
                "No DB file found. Will be created in '%s' when saved.",
                xmldbfile);
        }
    }
}

/**
 * Set the initial parameters for the TV-card so we know that they exist
 * and have a known state.
 */
void
init_capture_cards(void) {

    // If each profile is allowed to set the HW encoding paramters then we
    // do not need to do anything here.
    if( !allow_profiles_adj_encoder ) {
        struct transcoding_profile_entry *profile;
        get_transcoding_profile(default_transcoding_profile,&profile);
        for(unsigned video=0; video < max_video; video++) {
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


    // Remember the program name we are started as
    strncpy(inibuffer,argv[0],256);
    strncpy(server_program_name,basename(inibuffer),31);
    server_program_name[31] = '\0';

    // Parse and set cmd line options
    parsecmdline(argc,argv);
    
/*
 * #define TVPVRD_LOCKFILE "/var/run/tvpvrd.pid"
 */
    strcpy(lockfilename,"/var/run/tvpvrd.pid");

    // Set lockfile to avoid multiple instances running
    if( -1 == createlockfile() ) {
        fprintf(stderr,"Cannot start server. Check system log for more information.\n");
        _exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO,"%s","Starting tvpvrd daemon");

    // Setup MALLOC to dump in case of memory corruption, i.e. double free
    // or overrun. This might be less efficient but this will be enabled
    // until we are 101% sure there are no mistakes in the code.
    static char *var = "MALLOC_CHECK";
    static char *val = "2";
    setenv(var,val,1);

    // Keep track ourself of PCRE memory so we use our own
    // memory routines. This is a necessary safety due to the way
    // the PCRE library is implemeented. It is very easy to get a
    // memory leak in the usage fo these routines unless one is
    // very careful and never makes a single mistake ...
    pcre_malloc = tvp_malloc;
    pcre_free = tvp_free;

    // Setup exit() handler
    atexit(exithandler);

    
    // Initialize the static frequency map. 
    initfreqtable();
    


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

    /* In case we are started as a daemon from a boot script we most likely
     * have a faulty locale. If the locale is set in the INI file we will use
     * that one instead.
     */

    strncpy(locale_name,
            iniparser_getstring(dict, "config:locale_name", LOCALE_NAME),
            255);
    logfile_name[255] = '\0';
    setenv("LC_ALL",locale_name,1);
    logmsg(LOG_DEBUG,"Using locale '%s'",locale_name);


    // Hold the virtual breath if the dameon is started the same time as the server
    // is powerd on to allow for the ntpd time daemon to correct a potential wrong
    // system time.
    int uptime=0, idletime=0;
    getuptime(&uptime,&idletime);
    if( uptime < 180 ) {
        syslog(LOG_DEBUG,"Sleeping an extra %d seconds before we go to work",tdelay);
        sleep((unsigned)tdelay);
    }


    // Remember when the server was started
    tzset();
    ts_serverstart = time(NULL);



/*
    char *_loc = getenv("LC_ALL");
    char _locbuff[255];
    snprintf(_locbuff,254,"Current system locale at #1 : LC_ALL=%s",_loc);
    if( _loc ) {
        syslog(LOG_DEBUG,_locbuff);
    } else {
        syslog(LOG_DEBUG,"Current system locale at #1 : UNKNOWN");
    }
*/

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

        // Update the PID in the lockfile since we have a new PID
        if( -1 == updatelockfilepid() ) {
            logmsg(LOG_ERR,"Can't update lockfile with new daemon PID. Aborting.");
            exit(EXIT_FAILURE);
        }

    }

    // Get the overall settings from the ini-file
    read_inisettings();

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

    if( is_master_server ) {
        logmsg(LOG_NOTICE,"Starting server as MASTER");
    } else {
        logmsg(LOG_NOTICE,"Starting server as CLIENT");
    }

    // Check and create the necessary directory structure if it doesn't already exist
    // even if the daemon is running as a client the data directory should be a local
    // directory to avoid all kind of permission problem that occurs when having
    chkdirstructure();

    // Check if we should run as another user if we are started as root.
    // For security a daemon should never run as a root unless necessary
    // and in our case it is not necessary for us to run as root.
    chkswitchuser();

    // After a possible setuid() adn setgrp() the dumapable flag is reset which means
    // that no core is dumped in case of a SIGSEGV signal. We want a coredump in case
    // of a memory overwrite so we make sure this is allowed
    if( -1 == prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) ) {
        logmsg(LOG_ERR,"FATAL: Can not set PR_SET_DUMPABLE");
        exit(EXIT_FAILURE);
    }

    // Initialize all the data structures that stores our recording
    init_globs();

    // Read the initial recording DB from the specified file on either the
    // command line or from the ini-file. The command line always override
    // setting sin the ini-file
    if( is_master_server ) {
        init_tvxmldb();
    }

#ifndef DEBUG_SIMULATE
    // Initialize the Capture card(2)
    if( is_master_server ) {
        init_capture_cards();
    }
#endif
    
    // We use a separate thread to receive all the signals so we must
    // block all signals in all other threads
    sigfillset( &signal_set );
    pthread_sigmask( SIG_BLOCK, &signal_set, NULL );
    pthread_create( &signal_thread, NULL, sighand_thread, NULL );
    
    // Start the thread that will be monitoring the recording list and
    // in turn setup a new thread to do a recording when the time has come
    if( is_master_server ) {
        (void) pthread_create(&chkrec_thread, NULL, chkrec, (void *) NULL);
    }

    // Startup the main socket server listener. The call to startupsrv() will
    // not return until the daemon is terminated.
    if( EXIT_FAILURE == startupsrv() ) {
        logmsg(LOG_ERR,"Unable to start '%s' server.",program_invocation_short_name);
	exit(EXIT_FAILURE);
    }
        
    logmsg(LOG_INFO,"Received signal %d. Shutting down ...",received_signal);
    
    pthread_mutex_lock(&recs_mutex);
    // Shutdown all ongoing recordings
    if( is_master_server ) {
        for(unsigned i=0; i < max_video; i++) {
            if( ongoing_recs[i] && abort_video[i] == 0 ) {
                abort_video[i] = 1;
                logmsg(LOG_INFO,"  -- Aborting recording on video %d",i);
            } else {
                abort_video[i] = 0;
            }
        }
    }

    // Close all clients
    for(unsigned i=0; i < max_clients; i++ ) {
        if( cli_threads[i] ) {
            _dbg_close(client_socket[i]);
            logmsg(LOG_INFO,"  -- Disconnecting client from %s",client_ipadr[i]);
        }
    }        
    pthread_mutex_unlock(&recs_mutex);
			
    // Refresh DB file after a clean exit.
    if( is_master_server ) {
        UPDATE_DB();
    }

    // Store the calculated statistics
    (void)write_stats();

    if( is_master_server ) {
        // Wait until all recordings have stopped or we have waited more than 15s
        int watchdog = 15;
        volatile int ongoing = 0;
        for(unsigned i=0; i < max_video; i++) {
            ongoing |= abort_video[i];
        }
        while( ongoing && watchdog > 0 ) {
            ongoing=0;
            for(unsigned i=0; i < max_video; i++) {
                ongoing |= abort_video[i];
            }
            sleep(1);
            logmsg(LOG_INFO,"Waiting for video to stop [%d] ...",
                watchdog);
            watchdog--;
        }
    }

    if( dokilltranscodings ) {
        kill_all_ongoing_transcodings();
    }

    logmsg(LOG_INFO,"Bye.");
    free_globs();
    exit(EXIT_SUCCESS);
}

/* EOF */