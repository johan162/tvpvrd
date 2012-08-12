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
 *     and storing it on the file server.
 * 4.  Start the main thread to listen for incoming clients connections
 * 4.1 For each incoming client connection create a new thread to handle
 *     that client.
 *
 * Each created client thread listens to commands from the client and
 * giving the command to the command interpretator for parsing and
 * execution.
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

// We want the full POSIX and C99 standard
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __USE_GNU
#define __USE_GNU
#endif

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

// Standard UNIX includes
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <errno.h>
#include <pthread.h>
#include <locale.h>

// #include <mcheck.h>

// Standard socket and IP address handling
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "config.h"

// Aplication specific libs, parse inifiles as well as Perl regular expressions
#include <pcre.h>

// Needed to get symbolic constants for v4l2 controls
#include <linux/videodev2.h>

// Needed for prctl()
#include <sys/prctl.h>

// Local files
#include "tvpvrd.h"
#include "tvconfig.h"
#include "vctrl.h"
#include "recs.h"
#include "utils.h"
#include "futils.h"
#include "tvxmldb.h"
#include "tvcmd.h"
#include "freqmap.h"
#include "stats.h"
#include "transc.h"
#include "transcprofile.h"
#include "tvwebui.h"
#include "lockfile.h"
#include "build.h"
#include "pcretvmalloc.h"
#include "tvshutdown.h"
#include "rkey.h"
#include "mailutil.h"
#include "datetimeutil.h"
#include "xstr.h"
#include "vcard.h"
#include "tvplog.h"
#include "tvhistory.h"
#include "tvwebcmd.h"

/*
 * This structure mirrors the one found in /usr/include/asm/ucontext.h and is
 * used to create a stack backtrace if the program crashes
 */
typedef struct _sig_ucontext {
 unsigned long     uc_flags;
 struct ucontext   *uc_link;
 stack_t           uc_stack;
 struct sigcontext uc_mcontext;
 sigset_t          uc_sigmask;
} sig_ucontext_t;


/*
 * Server identification
 */
char server_version[] = PACKAGE_VERSION; // This define gets set by the config process
char server_program_name[32] = {0};

// Time delay (in sec) before the daemon really starts doing stuff if
// the dameon is started within 3 minutes of the machine power
// on. This might seem a really odd thing to have but it has one
// really useful purpose. On older machines where the bios clock
// is not correctly updated by the ntpd daemon at shutdown it
// could be that in the few seconds after the daemon starts
// and until the ntpd daemon kicks in the time is wrong. This is
// especially annoying when the switch to/from daylight saving time
// takes place. If the bios clock is not correctly updated at shutdown
// the first few seconds after the server starts the time will be off
// by one hour.
// This variable specifies a suitable number of seconds for the daemon to
// sleep immediately after startup so that the ntpd daemons has had time
// to correct the machine time.
//
// Of course this delay is only really necessary when the
// daaemon is started up just after the server has been powered on. So
// this delay only kicks in if the daemon is started within 3 minutes of the
// machine power on. This can also be adjusted as an argument when starting
// the daemon (-t)
int tdelay=30;

// The video buffer (used when reading the video stream from the capture card)
// One buffer for each video card. We support up to 4 simultaneous cards
char *video_buffer[MAX_VIDEO];


/*
 * Keep track of the last signal we received.
 */
static volatile sig_atomic_t received_signal = 0;

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
 * Keep track of IP address for all current connected clients.
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
 * 1) The data structure when multiple clients are connected
 * 2) The creation and killing of thread and thread counts
 */
pthread_mutex_t recs_mutex          = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t socks_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sig_mutex    = PTHREAD_MUTEX_INITIALIZER;

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
 * What user should we run as
 */
char username[64];

/*
 * Determine if we should kill any ongoing transcoding processes when we
 * kill this server.
 */
int dokilltranscodings = 1;

/*
 * What locale to use, by default this is set to LOCALE_NAME (which in the original distribution
 * is set to en_US.UTF8)
 */
char locale_name[255];

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
    int opt, idx;
    *inifile='\0';
    *xmldbfile='\0';
    *logfile_name='\0';
    *xawtv_channel_file='\0';
    verbose_log = -1;
    tcpip_port = 0;
    is_master_server = -1;
    opterr = 0; // Suppress error string from getopt_long()
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

    while (-1 != (opt = getopt_long (argc,argv,short_options, long_options, &idx)) ) {

        switch (opt) {
            case 0: /* getopt_long() flag */
                break;

            case 'h':
                fprintf(stdout,
                        "'%s' (C) 2009,2010,2011,2012 Johan Persson, (johan162@gmail.com) \n"
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
                fprintf(stdout,"%s %s (build: %lu.%lu)\n%s",
                        server_program_name,server_version,
                        (unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER,
#ifdef DEBUG_SIMULATE
                        " *** DEBUG BUILD. WILL NOT RECORD REAL VIDEO STREAMS *** \n"
#endif
                        "Copyright (C) 2009,2010,2011,2012 Johan Persson (johan162@gmail.com)\n"
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
                    tcpip_port = (short unsigned int)validate(0,99999,"TCP/IP port on command line",xatoi(optarg));
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
                    tdelay = validate(0,600,"tdelay on command line",xatoi(optarg));
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
 * Initialize global memory structures
 */
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

/*
 * This is the signal receiving thread. In order to guarantee that we don't get any
 * deadlocks all signals is masked in all other threads and only this thread can receive
 * signals. The function then sets the global variable handled_signal
 */
void *
sighand_thread(void *arg) {
    sigset_t signal_set;
    int sig;

    while (1) {

      // Here we wait for signals used to terminate the server
      // SIGINT   = User presses the C-C 
      // SIGTERM  = The normal way to stop a process
      // SIGHUP   = Standard way to stop daemon from the init.d script
      sigemptyset(&signal_set);

      // We don't want to handle interrupt since this is generated by the
      // debugger and if we cach it here debugging will not work.
      // sigaddset(&signal_set,SIGINT);
     
      sigaddset(&signal_set,SIGTERM);
      sigaddset(&signal_set,SIGHUP);
      
      sigwait(&signal_set, &sig);

      pthread_mutex_lock(&sig_mutex);
      received_signal = sig;
      pthread_mutex_unlock(&sig_mutex);

    }
    
    // Trick to make the compiler shut up on non used parameter
    arg =(void *)0;
    return arg;
}
// #define SIGSEGV_HANDLER

#ifdef SIGSEGV_HANDLER
void 
sigsegv_handler(int sig_num, siginfo_t * info, void * ucontext) {

    void * array[50];
    void * caller_address;
    int size;
    sig_ucontext_t * uc;

    uc = (sig_ucontext_t *) ucontext;
    caller_address = (void *) uc->uc_mcontext.eip;

    int fd = open("/tmp/tvpvrd_stack.crash",
                  O_TRUNC | O_WRONLY | O_CREAT,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if( 0 < fd ) {
        char buff[256];
        snprintf(buff,255, "signal %d (%s), address is %p from %p\n\nStack trace:\n",
                sig_num, strsignal(sig_num), info->si_addr,
                (void *) caller_address);
        int ret = write(fd,buff,strlen(buff));
        if( -1 < ret ) {
            size = backtrace(array, 50);
            array[1] = caller_address;
            backtrace_symbols_fd(array, size, fd);
        }
        close(fd);
    }

    // Re-raise the signal to get the full core dump
    raise(sig_num);

}
#endif


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
    int ret1 = dup(i);
    int ret2 = dup(i);
    if( -1 == ret1 || -1 == ret2 ) {
        syslog(LOG_ERR, "Cannot start daemon and set descriptors 0,1,2 to /dev/null.");
        exit(EXIT_FAILURE);    
    }
    logmsg(LOG_DEBUG,"Reopened descriptors 0,1,2 => '/dev/null'");
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
                int ret = system(cmdbuff);
                if( -1 == ret ) {
                    logmsg(LOG_ERR,"Cannot execute chown() command for datadir (%d : %s)",errno,strerror(errno));                       
                    exit(EXIT_FAILURE);    
                }
                
                snprintf(cmdbuff,63,"chgrp -R %d %s",pwe->pw_gid,datadir);
                ret = system(cmdbuff);
                if( -1 == ret ) {
                    logmsg(LOG_ERR,"Cannot execute chgrp() command (%d : %s)",errno,strerror(errno));                       
                    exit(EXIT_FAILURE);    
                }
     
                if( strcmp(logfile_name,"syslog") && strcmp(logfile_name,"stdout") ) {
                    snprintf(cmdbuff,63,"chown %s %s",username,logfile_name);
                    ret = system(cmdbuff);
                    if( -1 == ret ) {
                        logmsg(LOG_ERR,"Cannot execute chown() command for logfile (%d : %s)",errno,strerror(errno));                       
                        exit(EXIT_FAILURE);    
                    }
     
                    snprintf(cmdbuff,63,"chgrp %d %s",pwe->pw_gid,logfile_name);
                    ret = system(cmdbuff);
                    if( -1 == ret ) {
                        logmsg(LOG_ERR,"Cannot execute chgrp() command for logfile (%d : %s)",errno,strerror(errno));                       
                        exit(EXIT_FAILURE);    
                    }                         
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
        }
    }

    // After a possible setuid() and setgrp() the dumapable flag is reset which means
    // that no core is dumped in case of a SIGSEGV signal. We want a coredump in case
    // of a memory overwrite so we make sure this is allowed
    if( -1 == prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) ) {
        logmsg(LOG_ERR,"Can not set PR_SET_DUMPABLE");
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

/*
 * Start a recording on the specified video stream immediately using the information in the
 * current recording record.
 * This function is only run in its own thread that is created in chkrec() when it decides a
 * new recording should be started. After the recording have been successfully finished
 * the transcoding is initiated.
 */
void *
startrec(void *arg) {    
    ssize_t nread, nwrite;
    char full_filename[256], workingdir[256], short_filename[256];
    const mode_t dmode =  S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
    const mode_t fmode =  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    unsigned mp2size = 0;
    int doabort = 0 ;

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
    int multi_prof_flag = FALSE; // This flag gets set if we find more than one profile
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
            
    if (-1 == vh) {

       logmsg(LOG_ERR, "Cannot setup video stream %02d. '%s' recording aborted",video,recording->title);
       pthread_mutex_lock(&recs_mutex);
       free(recording);
       ongoing_recs[video] = (struct recording_entry *)NULL;
       pthread_mutex_unlock(&recs_mutex);

       pthread_exit(NULL);
       return (void *)NULL;

    } else {

        int k = (int)strnlen(recording->filename,REC_MAX_NFILENAME)-1;
        while ( k>0 && recording->filename[k] != '.' ) {
            k--;
        }
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
        int rc = mkdir(workingdir,dmode);
        if( rc ) {

            if( errno == EEXIST ) {

                // If the base name fails try 10 steps of a adding a number
                int i=0;
                while( rc && errno == EEXIST && i < 99 ) {
                    snprintf(workingdir,255,"%s/vtmp/vid%d/%s_%02d",datadir,video,recording->filename,i+1);
                    workingdir[255] = '\0';                    
                    rc = mkdir(workingdir,dmode);
                    ++i;
                }

            }

            if( rc ) {

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
        }
        recording->filename[k] = '.';
        snprintf(full_filename,255,"%s/%s",workingdir,recording->filename);
        full_filename[255] = '\0';
        strncpy(short_filename,basename(full_filename),255);
        short_filename[255] = '\0';

        int fh = open(full_filename, O_WRONLY | O_CREAT | O_TRUNC, fmode);
        if (-1 == fh) {

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
                FD_SET ((unsigned)vh, &fds);

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

                ret = select (vh + 1, &fds, NULL, NULL, &tv);

                if (-1 == ret && EINTR == errno) {
                    continue;
                } else if (0 == ret ) {
                    logmsg(LOG_ERR,"Timeout on video stream #%02d. Aborting recording to '%s'",video,full_filename);
                    doabort = 1;
                } else {      

                    nread = read(vh, video_buffer[video], VIDBUFSIZE);

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

                        if( -1 == nwrite ) {
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
            nread = nwrite = 1; // Some dummmy values to indicate that the fake recording was successful.

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

        //-------------------------------------------------------------------------------
        // Run post-recording optional script and wait until it has finished
        //-------------------------------------------------------------------------------
        if( use_postrec_processing ) {
            logmsg(LOG_DEBUG,"Post recording processing enabled.");
            char postrec_fullname[128];
            snprintf(postrec_fullname,128,"%s/tvpvrd/shellscript/%s",CONFDIR,postrec_script);
            int csfd = open(postrec_fullname,O_RDONLY) ;
            if( -1 == csfd ) {
                logmsg(LOG_WARNING,"Cannot open post recording script '%s' ( %d : %s )",
                       postrec_fullname,errno,strerror(errno));
            } else {
                char cmd[255];
                snprintf(cmd,255,"%s -f \"%s\" -t %ld > /dev/null 2>&1",
                         postrec_fullname, full_filename,recording->ts_end-recording->ts_start);
                logmsg(LOG_DEBUG,"Running post recording script '%s'",cmd);
                rc = system(cmd);
                if( rc==-1 || WEXITSTATUS(rc)) {
                    logmsg(LOG_ERR,"Post recording script '%s' ended with exit status %d",postrec_fullname,WEXITSTATUS(rc));
                } else {
                    logmsg(LOG_INFO,"Post recording script '%s' ended normally with exit status %d",postrec_fullname,WEXITSTATUS(rc));
                }
            }
        }

        //-------------------------------------------------------------------------------
        // Now do the transcoding for each profile associated with this recording
        //-------------------------------------------------------------------------------
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

                // If any of the profiles used requires the mp2 file to be kept explicitely or
                // that no transcoding will be done we keep the mp2 file.
                keep_mp2_file |= profile->encoder_keep_mp2file | !profile->use_transcoding;
                char updatedfilename[256];
                logmsg(LOG_NOTICE,"Transcoding using profile: %s",profile->name);
                int ret = transcode_and_move_file(datadir, workingdir, short_filename, 
                                                  recording->recurrence_title,
                                                  profile,
                                                  &mp4size, &transcode_time, &avg_5load, updatedfilename);
                transcoding_problem |= ret;
                if( 0 == ret ) {
                    stats_update(recording->transcoding_profiles[i],
                                 mp2size,
                                 (unsigned)(recording->ts_end - recording->ts_start),
                                 mp4size,
                                 &transcode_time, avg_5load);
                    
                
                    // Updated history file with this successful transcoding
                    hist_addrec(recording->title, recording->ts_start, recording->ts_end, updatedfilename, profile->name);
                    
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
 * This is the main thread that runs forever checking the list of recordings and kicks
 * of a recording thread when it is time for the recording to start.
 * This thread is started at the beginning of the server and run until the server
 * is shut down. It will loop and check every 'time_resolution' second if a queued
 * recoding is due to be started. There is no argument passed to this thread.
 */
void *
chkrec(void *arg) {
    time_t now;
    int diff, ret;
    volatile unsigned video;

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for each created thread
    pthread_detach(pthread_self());

    // Do some sanity checks of time_resolution
    time_resolution = MIN(MAX(time_resolution,1),10);

    // The main thread that runs forever
    while (1) {

        // Check all the video queues for the next possible recording
        // If auto shutdown has been enabled the the server will be
        // shutdown if there is no ongoing recording/transcoding and the
        // next recording is at least MIN_SHUTDOWN_TIME away.
        // If the function determines that it is time to shutdown it will
        // call an external script that will do the actual shutdown. In that
        // case this function will never return.
        check_for_shutdown();

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
                // opportunity. We remove this recording to be able to try the next one in
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
                    delete_toprec(video);
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
                            remove_toprec(video);

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
    
    // Trick to shut up the compiler warning aboutunused argument
    arg = (void *)0;
    return arg;
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
    int authenticated = 0;

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
            logmsg(LOG_ERR,"Failed to close socket %d to client %s. ( %d : %s )",my_socket,client_ipadr[i],errno,strerror(errno));
        }
        ncli_threads--;
        pthread_mutex_unlock(&socks_mutex);
        pthread_exit(NULL);
        return (void *) 0;
    }
    
    if( require_password ) {
        int tries = 3;

        while( tries > 0 && !authenticated) {
            _writef(my_socket, "Password: ");

            FD_ZERO(&read_fdset);
            FD_SET((unsigned)my_socket, &read_fdset);

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
                authenticated = (strcmp(buffer,password) == 0);
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
                logmsg(LOG_ERR,"Failed to close socket %d to client %s. ( %d : %s )",my_socket,client_ipadr[i],errno,strerror(errno));
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
            (unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER,
            ncli_threads, max_clients, max_idle_time/60);
    buffer[1023] = '\0';
    pthread_mutex_unlock(&socks_mutex);

    _writef(my_socket, buffer);

    // Keep track of client idle time
    unsigned idle_time=0;
    do  {

        FD_ZERO(&read_fdset);
        FD_SET((unsigned)my_socket, &read_fdset);

        timerclear(&timeout);
        timeout.tv_sec = 60;

        ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);
        if (ret == 0) {

            //Timeout
            idle_time += 60;

            if (idle_time >= max_idle_time) {
                numreads = -1; // Force a disconnect
                logmsg(LOG_INFO, "Client disconnected after being idle for more than %d seconds.", max_idle_time);
            } else {
                numreads = 1; // To keep the loop going
            }
            
        } else {

              idle_time = 0;
            numreads = read(my_socket, buffer, 1023);

            if( numreads > 0 ) {
                // When the remote socket closes the read will also succeed but return 0 read bytes
                // and there is no point in doing anything then.

                buffer[1023] = '\0';
                buffer[numreads] = 0;

                if ( 0 == strcmp("exit\r\n", buffer) || 0 == strcmp("quit\r\n", buffer) ) {
                    // Exit command. Exit the loop and close the socket
                    _writef(my_socket,"Goodbye.\n");
                    numreads = -1;
                    break;                                        
                } else {
                    buffer[MAX(strnlen(buffer,1023) - 2, 0)] = 0; // Remove trailing newline and carrige return
                    xstrtrim(buffer);
                    if( *buffer ) {
                        // Ignore empty command
                        pthread_mutex_lock(&recs_mutex);
                        logmsg(LOG_INFO, "Client (%s) sent command: %s [len=%d]", client_ipadr[i], buffer, strlen(buffer));
                        cmdinterp(buffer, my_socket);
                        pthread_mutex_unlock(&recs_mutex);
                    }
                }

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
        logmsg(LOG_ERR,"Failed to close socket %d to client %s. ( %d : %s )",my_socket,client_ipadr[i],errno,strerror(errno));
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
    size_t const maxbufflen=4096;
    char buffer[maxbufflen];
    fd_set read_fdset;
    struct timeval timeout;

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for each created thread
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
    FD_SET((unsigned)my_socket, &read_fdset);
    timerclear(&timeout);
    timeout.tv_sec = 2;
    ret = select(my_socket + 1, &read_fdset, NULL, NULL, &timeout);
    if (ret == 0) {
        logmsg(LOG_NOTICE, "WEB Browser disconnected due to timeout.");
    } else {

        // We have activity so read from the socket and try to interpret the
        // given command.
        numreads = read(my_socket, buffer, maxbufflen-1);
        buffer[maxbufflen-1] = '\0';
        buffer[numreads] = '\0';
        web_process_httprequest(my_socket,buffer);

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
    // within approx. 30s after it was shutdown. This is due to the extra safety wait
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
    socketaddress.sin_port = (short unsigned)htons(tcpip_port);

    if (bind(sockd, (struct sockaddr *) & socketaddress, sizeof (socketaddress)) != 0) {
        logmsg(LOG_ERR, "Unable to bind socket to port. Most likely some other application is using this port.");
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
        FD_SET((unsigned)sockd, &read_fdset);

        if( enable_webinterface ) {
            FD_SET((unsigned)websockd, &read_fdset);
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
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
        if( FD_ISSET((unsigned)sockd,&read_fdset) ) {

            logmsg(LOG_DEBUG, "Terminal connection.");
            terminal_connection = 1;
            tmpint = sizeof (socketaddress);
            newsocket = accept(sockd, (struct sockaddr *) & socketaddress, &tmpint);
            if( newsocket < 0 ) {
                // Unrecoverable error
                logmsg(LOG_ERR, "Could not create new client socket ( %d : %s ) ",errno,strerror(errno));
            }
            dotaddr = inet_ntoa(socketaddress.sin_addr);

        } else if( enable_webinterface && FD_ISSET((unsigned)websockd,&read_fdset) )  {

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
            logmsg(LOG_CRIT, "Internal serious error. Accepted port connection that we were not listening on.");
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

/**
 * See if user has supplied a startup script
 */
void
chk_startupscript(void) {

    // Check if the startupscript exists
    char script[256],indicator[256];
    snprintf(script, sizeof(script)-1, "%s/tvpvrd/shellscript/%s", CONFDIR, DEFAULT_STARTUP_SCRIPT);
    snprintf(indicator,sizeof(indicator)-1,"%s/%s",datadir,DEFAULT_AUTOSHUTDOWN_INDICATOR);
    char *flag = (0 == access(indicator, F_OK) ? "yes" : "no" );

    if ( 0 == access(script, F_OK) ) {

        char cmd[512];
        snprintf(cmd, 511, "%s -d \"%s\" -c \"%s/tvpvrd\" -a \"%s\"", script, datadir, CONFDIR, flag);

        int ret = system(cmd);
        if (-1 == ret || WEXITSTATUS(ret)) {
            logmsg(LOG_NOTICE, "Error when executing startup script '%s' ( %d : %s) ", cmd, errno, strerror(errno));
        } else {
            logmsg(LOG_DEBUG,"Executed startupscript: '%s'",cmd);
        }

    } else {
        logmsg(LOG_DEBUG,"Could not find any startup script: '%s'",script);
    }

    if ( 0 == access(indicator, F_OK) ) {
        (void)remove(indicator); // Ignore any errors
        logmsg(LOG_DEBUG,"Removed autoshutdown indicator: '%s'",indicator);
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


    //mtrace();

    // Remember the program name we are started as
    char prognamebuffer[256];
    strncpy(prognamebuffer,argv[0],256);
    strncpy(server_program_name,basename(prognamebuffer),31);
    server_program_name[31] = '\0';
    
    // Parse and set cmd line options
    parsecmdline(argc,argv);

    // Setup process lockfile that indicates that we are running to avoid
    // that the daemon can be started twice
    setup_lockfile();

    // Setup MALLOC to dump in case of memory corruption, i.e. double free
    // or overrun. This might be less efficient but this will be enabled
    // until we are 101% sure there are no mistakes in the code.
    static char *var = "MALLOC_CHECK";
    static char *val = "2";
    setenv(var,val,1);

    // Keep track ourself of PCRE memory so we use our own
    // memory routines. This is a necessary safety due to the way
    // the PCRE library is implemented. It is very easy to get a
    // memory leak in the usage for these routines unless one is
    // very careful and never makes a single mistake ...
    pcre_malloc = tvp_malloc;
    pcre_free = tvp_free;

    // Setup exit() handler
    atexit(exithandler);
    
    // Initialize the static frequency map. 
    initfreqtable();

    // Setup name for inifile and initialize dictionary
    setup_inifile();

    // Setup logfile name
    if( -1 == verbose_log ) {
        verbose_log = iniparser_getint(dict, "config:verbose_log", VERBOSE_LOG);
    }
    if( strlen(logfile_name) == 0 ) {
        strncpy(logfile_name,
                iniparser_getstring(dict, "config:logfile_name", LOGFILE_SYSLOG),
                255);
        logfile_name[255] = '\0';
    }

    // From now on we now the name of the logfile so we can use the log function
    logmsg(LOG_INFO,"Starting tvpvrd %s (build: %lu.%lu)",
           server_version,
           (unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER);
    logmsg(LOG_INFO,"Using ini-file '%s'",inifile);          


    /* In case we are started as a daemon from a boot script we most likely
     * have a faulty locale. If the locale is set in the INI file we will use
     * that one instead.
     */

    strncpy(locale_name,
            iniparser_getstring(dict, "config:locale_name", LOCALE_NAME),
            255);
    logfile_name[255] = '\0';
    //setenv("LC_MESSAGES",locale_name,1);
    setlocale(LC_ALL,locale_name); 
    logmsg(LOG_DEBUG,"Using locale '%s'",setlocale(LC_ALL,NULL));

    // Remember when the server was started
    tzset();
    ts_serverstart = time(NULL);

    // Hold the virtual breath if the daemon is started the same time as the server
    // is powered on. This will allow two things:
    // 1) the ntpd time daemon to correct a potential wrong system time before we start and
    // 2) any remote fs mounts that needs to finish
    int uptime=0, idletime=0;
    getuptime(&uptime,&idletime);
    logmsg(LOG_DEBUG,"Server uptime when daemon starts is: %d",uptime);
    if( uptime < 180 ) {
        logmsg(LOG_DEBUG,"Sleeping an extra %d seconds before we go to work",tdelay);
        sleep((unsigned)tdelay);
    }
    
    if( -1 == daemonize ) {
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
    
    // The initialization of web commands are partly controlled by the ini-file
    init_web_cmds();

    //----------------------------------------------------------------------------------------
    // Note: The order of the initialization below is somewhat important. The vital dependencies
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

    // Initialize all the data structures that stores our recording
    init_globs();
    
    // Run the optional startup script supplied by the user
    chk_startupscript();    

    // Read the initial recording DB from the specified file on either the
    // command line or from the ini-file. The command line always override
    // setting sin the ini-file
    if( is_master_server ) {
        init_tvxmldb();
    }

#ifndef DEBUG_SIMULATE
    // Initialize the Capture card(2)
    if( is_master_server ) {
        setup_capture_cards();
    }
#endif
    
    // We use a separate thread to receive all the signals so we must
    // block all signals in all other threads

    // We want to block all signals apart from the OS system signals
    // that indicates a serious error in the program (like a segmentation
    // violation or a /very unlikely bus error). We also allow SIGQUIT
    // as a mean to force a coredump in a different way.
    sigfillset( &signal_set );
    sigdelset( &signal_set , SIGSEGV);
    sigdelset( &signal_set , SIGQUIT);
    sigdelset( &signal_set , SIGBUS);
    if( ! daemonize ) {
        sigdelset( &signal_set , SIGINT);
    }

    // The signal thread will set a volatile global flag that is checked
    // in the recording thread to detect a shutdown and handle that gracefully.
    pthread_sigmask( SIG_BLOCK, &signal_set, NULL );
    pthread_create( &signal_thread, NULL, sighand_thread, NULL );
    
    // Start the thread that will be monitoring the recording list and
    // in turn setup a new thread to do a recording when the time has come
    if( is_master_server ) {
        (void) pthread_create(&chkrec_thread, NULL, chkrec, (void *) NULL);
    }

#ifdef SIGSEGV_HANDLER
    struct sigaction sigact;

    sigact.sa_sigaction = sigsegv_handler;
    sigact.sa_flags = SA_RESTART | SA_SIGINFO | SA_RESETHAND;

    if (sigaction(SIGSEGV, &sigact, (struct sigaction *) NULL) != 0) {
        fprintf(stderr, "Cannot create signal handler for signal %d (%s)\n", SIGSEGV, strsignal(SIGSEGV));
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGBUS, &sigact, (struct sigaction *) NULL) != 0) {
        fprintf(stderr, "Cannot create signal handler for signal %d (%s)\n", SIGBUS, strsignal(SIGBUS));
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGABRT, &sigact, (struct sigaction *) NULL) != 0) {
        fprintf(stderr, "Cannot create signal handler for signal %d (%s)\n", SIGABRT, strsignal(SIGABRT));
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGQUIT, &sigact, (struct sigaction *) NULL) != 0) {
        fprintf(stderr, "Cannot create signal handler for signal %d (%s)\n", SIGQUIT, strsignal(SIGQUIT));
        exit(EXIT_FAILURE);
    }
#endif

    
    // Initialize history from file (of previous recordings)
    hist_init();

    //*********************************************************************************
    //*********************************************************************************
    //**     This is the real main starting point of the program                     **
    //*********************************************************************************
    //*********************************************************************************

    // Startup the main socket server listener. The call to startupsrv() will
    // not return until the daemon is terminated by a user signal.
    if( EXIT_FAILURE == startupsrv() ) {
        logmsg(LOG_ERR,"Unable to start '%s' server.",program_invocation_short_name);
        exit(EXIT_FAILURE);
    }

    //*********************************************************************************
    //*********************************************************************************
    //**     This is the shutdown point of the program                               **
    //*********************************************************************************
    //*********************************************************************************    
    
    logmsg(LOG_INFO,"Received signal %d. Shutting down ...",received_signal);
    
    pthread_mutex_lock(&recs_mutex);

    // ---------------------------------------------------------------------------------
    // Close all clients
    // ---------------------------------------------------------------------------------
    for(unsigned i=0; i < max_clients; i++ ) {
        if( cli_threads[i] ) {
            _dbg_close(client_socket[i]);
            logmsg(LOG_INFO,"Disconnecting client with IP address '%s'",client_ipadr[i]);
        }
    }

    // ---------------------------------------------------------------------------------
    // Shutdown all ongoing recordings
    // ---------------------------------------------------------------------------------
    if( is_master_server ) {
        for(unsigned i=0; i < max_video; i++) {
            if( ongoing_recs[i] && abort_video[i] == 0 ) {
                abort_video[i] = 1;
                logmsg(LOG_INFO,"Aborting recording on video %d",i);
            } else {
                abort_video[i] = 0;
            }
        }
    }

    pthread_mutex_unlock(&recs_mutex);
		
    // Store the calculated statistics
    (void)write_stats();

    if( is_master_server ) {
        // Wait until all recordings have stopped or we have waited more than 15 seconds
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
            logmsg(LOG_INFO,"Waiting for video to stop [%d] ...", watchdog);
            watchdog--;
        }
    }

    // Stop being nice and just kill all ongoing transcodings
    if( dokilltranscodings ) {
        kill_all_ongoing_transcodings();
    }

    logmsg(LOG_INFO,"Bye. tvpvrd really terminated.");

    // We don't bother freeing globals since some data structures are checked by running
    // threads and we might get sigfaults if the thread is trying to check a deleted memory structure.
    // All dynamic memory will be returned when proram exis anyway. To do this properly we would
    // have to send cancellations to all threads and wait for eaxch of them to give up.
    // free_globs();
    exit(EXIT_SUCCESS);
}

/* EOF */
