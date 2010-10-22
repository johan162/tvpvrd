/* =========================================================================
 * File:        TVPPWRD.C
 * Description: A watcher daemon which is meant to run on a separate machine
 *              from the tvpvrd. This watcher daemon communicates with
 *              the tvpvrd and if the server running tvpvrd has nothing
 *              to do and the next recording is not due to start until a
 *              a specified treshold time in the future it will shut
 *              down the server. Since this watcher knows when the next
 *              recording starts it will then wake up the server at an
 *              appropriate time.
 *              For this daemon to work the server running tvpvrd must
 *              support wake-on-lan
 *
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2010 Johan Persson
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

#include <time.h>
#include <netdb.h>

// Needed for regex
#include <sys/types.h>
#include <regex.h>

#include "utils.h"
#include "tvppwrd.h"
#include "confpath.h"
#include "lockfile.h"

/*
 * Server identification
 */
char server_version[] = "1.0"; //PACKAGE_VERSION; // This define gets set by the config process
char server_build_date[] = __DATE__;     // This is a builtin define set by the compiler
char server_program_name[32] = {0};

// Logfile details
int verbose_log;
char logfile_name[256] = {'\0'};

/*
 * Mail setting. Determine if we should send mail on errors and what address
 * to use. Read from the inifile normally.
 */
int send_mail_on_error;
char send_mailaddress[64];

/*
 * The value of these variables are read from the ini-file and initialized in
 * the main() function. They hold various run time limits and settings that
 * the user can adjust.
 */
// Should we run as a daemon or nothing
int daemonize=-1;

#define INIFILE_BUFFERSIZE 4096
static char inibuffer[INIFILE_BUFFERSIZE] = {0};

// Names of the ini file and the db file used
char inifile[256];
/*
 * Password settings. The server offer a basic authentication, The
 * authentication is disabled by default.
 */
// static int require_password = REQUIRE_PASSWORD;
static char password[32];

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
 * What user should we run as
 */
char username[64];

/*
 * What locale to use, by default this is set to LOCALE_NAME (which in the original distribution
 * is set to en_US.UTF8)
 */
#define LOCALE_NAME "en_US.UTF8"
char locale_name[255];

/*
 *
 */
char tvpvrd_pwd[32];

/*
 *
 */
char server_user[32];

/*
 * 
 */
char server_ip[32];

/*
 *
 */
#define SERVER_PORT 9300
int server_port;

/*
 * 
 */
#define TARGET_PORT 32767
#define TARGET_BROADCAST_ADDRESS "255.255.255.255"
char target_mac_address[32], target_broadcast_address[32] ;
int target_port;

/*
 *
 */
#define MAX_SHUTDOWN_5LOAD 0.1
double max_shutdown_5load;

/*
 *
 */
#define MIN_POWEROFF_TIME 60
int min_poweroff_time;

/*
 *
 */
#define SERVER_REFRESH_TIME 30
int server_refresh_time;

/*
 *
 */
#define SHUTDOWN_WARNING_TIME 2
int shutdown_warning_time;

/*
 * Keep track of the last signal we received.
 */
static int received_signal = 0;

static const char short_options [] = "d:hi:l:v:V:";
static const struct option long_options [] = {
    { "daemon",  required_argument,     NULL, 'd'},
    { "help",    no_argument,           NULL, 'h'},
    { "inifile", required_argument,     NULL, 'i'},
    { "version", no_argument,           NULL, 'v'},
    { "logfile", required_argument,     NULL, 'l'},
    { "verbose", required_argument,     NULL, 'V'},
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
    *logfile_name='\0';
    verbose_log = -1;
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
                        "Usage: %s [options]\n"
                        "Synopsis:\n"
                        "TVPVR Power Managment Daemon.\n"
                        "Options:\n"
                        " -h,      --help            Print help and exit\n"
                        " -v,      --version         Print version string and exit\n"
                        " -i file, --inifile=file    Use specified file as ini file\n"
                        " -d y/n,  --daemon          Run as daemon\n"
                        " -l file, --logfile=file    Override logfile setting in inifile and use file as logfile\n"
                        " -V n,    --verbose=n       Override inifile and set verbose level\n",
                        server_program_name, server_program_name);
                exit(EXIT_SUCCESS);
                break;

            case 'v':
                fprintf(stdout,"%s %s (%s)\n%s",
                        server_program_name,server_version,server_build_date,
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

    send_mail_on_error = iniparser_getboolean(dict,"config:sendmail_on_error",SENDMAIL_ON_ERROR);

    strncpy(send_mailaddress,
            iniparser_getstring(dict, "config:sendmail_address", SEND_MAILADDRESS),
            63);
    send_mailaddress[63] = '\0';

    strncpy(tvpvrd_pwd,
            iniparser_getstring(dict, "config:tvpvrd_pwd", ""),
            31);
    tvpvrd_pwd[31] = '\0';

    strncpy(server_user,
            iniparser_getstring(dict, "config:server_user", ""),
            31);
    server_user[31] = '\0';

    max_shutdown_5load = iniparser_getdouble(dict, "config:max_shutdown_5load", MAX_SHUTDOWN_5LOAD);

    min_poweroff_time = iniparser_getint(dict, "config:min_poweroff_time", MIN_POWEROFF_TIME);

    server_refresh_time = iniparser_getint(dict, "config:server_refresh_time", SERVER_REFRESH_TIME);

    shutdown_warning_time = iniparser_getint(dict, "config:shutdown_warning_time", SHUTDOWN_WARNING_TIME);

    if( strlen(logfile_name) >= 127 ) {
                logmsg(LOG_CRIT,
                "** FATAL error. "
                "Illegal value for logfile_name in ioni-file");
        exit(EXIT_FAILURE);
    }

    if( strcmp("stdout",logfile_name) == 0 && daemonize ) {
        logmsg(LOG_ERR,
                "** FATAL error. "
                "'stdout' is not a valid logfile when started in daemon mode.");
        exit(EXIT_FAILURE);
    }

    /*--------------------------------------------------------------------------
     * NETWORK Section
     *--------------------------------------------------------------------------
     */

    strncpy(server_ip,
            iniparser_getstring(dict, "network:server_ip", ""),
            31);
    server_ip[31] = '\0';

    server_port = iniparser_getint(dict, "network:server_port", SERVER_PORT);

    strncpy(target_mac_address,
            iniparser_getstring(dict, "network:target_mac_address", ""),
            31);
    target_mac_address[31] = '\0';

    strncpy(target_broadcast_address,
            iniparser_getstring(dict, "network:target_broadcast_address", ""),
            31);
    target_broadcast_address[31] = '\0';

    target_port = iniparser_getint(dict, "network:target_port", TARGET_PORT);

}


/**
 * Check what user we are running as and change the user (if allowed) to the
 * specified tvppwrd user.
 */
void
chkswitchuser(void) {
    // Check if we are starting as root
    struct passwd *pwe = getpwuid(getuid());

    if( strcmp(pwe->pw_name,"root") == 0 ) {
        strncpy( username, iniparser_getstring(dict, "config:username", DEFAULT_USERNAME), 63 );
        username[63] = '\0';

        if( strcmp(username,"root") ) {
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

            int dummyret;
            char cmdbuff[64];
            if( strcmp(logfile_name,"syslog") && strcmp(logfile_name,"stdout") ) {
                snprintf(cmdbuff,63,"chown %s %s",username,logfile_name);
                dummyret = system(cmdbuff);
                snprintf(cmdbuff,63,"chgrp %d %s",pwe->pw_gid,logfile_name);
                dummyret = system(cmdbuff);
            }

            gid_t groups[1];
            groups[0] = pwe->pw_gid;
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


void init_globs(void) {
    // Initialize global structure
}

void free_globs(void) {
    // Free global structure
}


int parse_mac(unsigned char *mac, char *str) {
    int i;
    int count;
    char c;
    unsigned char val;
    int colon_ok = 1;
    for (i = 0; i < 6; i++) {
        mac[i] = 0;
    }
    for (i = 0; i < 6; i++) {
        count = 0;
        val = 0;
        do {
            c = toupper(*str++);
            if (c >= '0' && c <= '9') {
                val = (val * 16) + (c - '0');
            } else if (c >= 'A' && c <= 'F') {
                val = (val * 16) + (c - 'A') + 10;
            } else if (c == ':') {
                if (colon_ok || count-- != 0)
                    break;
            } else if (c == '\0') {
                str--;
                break;
            } else {
                return 0;
            }
            colon_ok = 1;
        } while (++count < 2);
        colon_ok = (count < 2);
        *mac++ = val;
    }
    if (*str)
        return 0;
    return 1;
}

int
wakelan(char *mac, char *target, int str_bport) {

    struct in_addr inaddr;
    unsigned char macaddr[6];
    short bport = -1;

    if (!parse_mac(macaddr, mac)) {
        logmsg(LOG_ERR,"Illegal MAC address ( %s )",mac);
        return -1;
    }

    if ( !inet_aton(target, &inaddr) ) {
        struct hostent *he = gethostbyname(target);
        if( he ) {
            inaddr = *(struct in_addr *)he->h_addr_list[0];
        } else {
            logmsg(LOG_ERR,"Illegal target host name ( %d  %s )",errno,strerror(errno));
            return -1;
        }
    }

    int msglen=0;
    char msg[1024];
    for (int i = 0; i < 6; i++) {
        msg[msglen++] = (char)0xff;
    }

    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < sizeof(macaddr); j++) {
            msg[msglen++] = macaddr[j];
        }
    }

    if( str_bport ) {
        bport = htons(str_bport);
    }

    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family      = AF_INET;
    bcast.sin_addr.s_addr = inaddr.s_addr;
    bcast.sin_port        = bport;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logmsg(LOG_ERR,"Cannot allocate socket for wakeup datagram ( %d : %s)",errno, strerror(errno));
        return -1;
    }

    int optval = 1;
    int rc=setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
    if ( rc < 0) {
        logmsg(LOG_ERR, "Can'tset  socket option SO_BROADCAST: rc = %d, ( %d : %s )\n",
               rc,  errno, strerror(errno));
        return -1;
    }
    
    sendto(sock, &msg, msglen, 0, (struct sockaddr *)&bcast, sizeof(bcast));
    return 0;

}

/*
 * Read a reply from a socket with 5s timeout
 */
int
waitread(int sock, char *buffer, int maxbufflen) {
    fd_set read_fdset;
    struct timeval timeout;

    FD_ZERO(&read_fdset);
    FD_SET(sock, &read_fdset);

    timerclear(&timeout);
    timeout.tv_sec = 5;

    int ret = select(sock + 1, &read_fdset, NULL, NULL, &timeout);
    if( 0 == ret ) {
        // Timeout
        return -1;
    } else {
        int numreads = read(sock, buffer, maxbufflen);
        buffer[maxbufflen-1] = '\0';
        buffer[numreads] = 0;
    }
    return 0;
}

#define TVPVRD_IDENTIFICATION "!TVPVRD!"
#define TVPVRD_PASSWORD "Password:"
int
tvpvrd_command(char *cmd, char *reply, int maxreplylen) {
    char buffer[1024];

    // Open a TCP/IP socket on the remote server
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        logmsg(LOG_ERR, "Cannot create command socket to server ( %d : %s)",errno, strerror(errno));
        return -1;
    }

    // Setup address structure to the remote machine
    struct in_addr remote_adr;
    CLEAR(remote_adr);
    if ( !inet_aton(server_ip, &remote_adr) ) {
        struct hostent *he = gethostbyname(server_ip);
        if( he ) {
            remote_adr = *(struct in_addr *)he->h_addr_list[0];
        } else {
            logmsg(LOG_ERR, "Illegal server host name '%s' ( %d  %s )",server_ip,errno,strerror(errno));
            return -1;
        }
    }
    struct sockaddr_in remote_sockadr;
    remote_sockadr.sin_family      = AF_INET;
    remote_sockadr.sin_addr.s_addr = remote_adr.s_addr;
    remote_sockadr.sin_port        = htons(server_port);

    // Connect the socket to the address
    if( -1 == connect(sock, (struct sockaddr *)&remote_sockadr, sizeof(remote_sockadr)) ) {
        logmsg(LOG_ERR, "Cannot connect command socket to server '%s' ( %d : %s)",server_ip,errno, strerror(errno));
        close(sock);
        return -1;
    }

    logmsg(LOG_INFO, "Connected to server '%s'\n",server_ip);

    if( waitread(sock, buffer, 1023) ) {
        logmsg(LOG_ERR, "Timeout on socket when trying to connect to server '%s'", server_ip);
        close(sock);
        return -1;
    } else {
        logmsg(LOG_DEBUG, "Server responded (len=%d): %s",strlen(buffer),buffer);
    }

    // Check for the string "!TVPVRD!" as proof that there is a
    // a tvpvrd daemon on the otyher side
    if( strncmp(buffer,TVPVRD_IDENTIFICATION,strlen(TVPVRD_IDENTIFICATION)) ) {

        // Check for possible password question
        if( 0 == strncmp(buffer,TVPVRD_PASSWORD,strlen(TVPVRD_PASSWORD)) ) {

            snprintf(buffer,1023,"%s\r\n",password);
            write(sock,buffer,strlen(buffer));

            if( waitread(sock, buffer, 1023) ) {
                logmsg(LOG_ERR,"Timeout on socket when trying to send password to server '%s'", server_ip);
                close(sock);
                return -1;
            }

        } else {

            logmsg(LOG_ERR, "It deosn't seem to be a tvpvrd daemon listening on the other side at '%s'. Aborting.", server_ip);
            close(sock);
            return -1;
        }
    } else {
        logmsg(LOG_DEBUG, "Connected to tvpvrd server on '%s'", server_ip);
    }

    // Send the command
    write(sock,cmd,strlen(cmd));

    if( waitread(sock, reply, maxreplylen) ) {
        logmsg(LOG_ERR, "Timeout waiting for reply on command '%s'", cmd);
        close(sock);
        return -1;
    }

    // Close sockets
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 0;

}

int
remote_command(char *cmd, char *reply, int len) {
    char buffer[512];

    if( strlen(cmd) > 450 )
        return -1;

    snprintf(buffer,512,"/usr/bin/ssh %s@%s '%s'", server_user, server_ip, cmd );

    FILE *fp = popen(buffer,"r");
    if( fp == NULL  ) {
        logmsg(LOG_ERR, "Error when executing popen('%s'): ( %d : %s )",
               buffer, errno, strerror(errno));
        return -1;
    } else {
        // Only read first line in the command output
        fread(reply, sizeof(char), len, fp);
        pclose(fp);
        return 0;
    }
}

int
remote_server_load(float *avg1load,float *avg5load,float *avg15load) {
    char buffer[1024];

    remote_command("uptime",buffer,1024);

    // Extract load values from string
    char *pattern = "load average: "
        "\\([[:digit:]]\\+\\.[[:digit:]]\\+\\), "
        "\\([[:digit:]]\\+\\.[[:digit:]]\\+\\), "
        "\\([[:digit:]]\\+\\.[[:digit:]]\\+\\)";
    regex_t preg;
    int rc = regcomp(&preg,pattern,0);
    if( rc ) {
        logmsg(LOG_ERR,"Error compiling regex ( %d : %s )",errno, strerror(errno));
        return -1;
    }

    regmatch_t matches[4];
    rc = regexec(&preg,buffer,4, matches, 0);
    if( rc ) {
        logmsg(LOG_ERR,"Cannot locate pattern ( %d : %s )",errno, strerror(errno));
        regfree(&preg);
        return -1;
    }
    regfree(&preg);

    char load[6];

    int j=matches[1].rm_so;
    if( j >= 0 ) {
        int i=0;
        while(i < 6 && j < matches[1].rm_eo ) load[i++] = buffer[j++];
        load[i]='\0';
        *avg1load = atof(load);
    }

    j=matches[2].rm_so;
    if( j >= 0 ) {
        int i=0;
        while(i < 6 && j < matches[2].rm_eo ) load[i++] = buffer[j++];
        load[i]='\0';
        *avg5load = atof(load);
    }

    j=matches[3].rm_so;
    if( j >= 0 ) {
        int i=0;
        while(i < 6 && j < matches[3].rm_eo ) load[i++] = buffer[j++];
        load[i]='\0';
        *avg15load = atof(load);
    }

    return 0;

}

/**
 *
 * @return
 */
int
shutdown_remote_server(void) {
    char buffer[512],reply[32];
    snprintf(buffer,512,"shutdown -h %d > /dev/null &",shutdown_warning_time );
    return remote_command(buffer,reply,32);
}

int
wakeup_remote_server(void) {

    if( wakelan(target_mac_address,target_broadcast_address, target_port) ) {
        logmsg(LOG_ERR,"Cannot wake up target server!");
        return 1;
    }

    // Initially wait 40s for the server to get started before we
    // make a sanity check that it is awake
    sleep(40);

    // Try to login to the tvpvrd daemon and retrive the time as a proof that
    // the server is up and running
    char cmd[32],reply[128];

    snprintf(cmd,32,"v");
    int rc = tvpvrd_command(cmd,reply,128) ;
    if( rc ) {
         // sleep another 20s before we try again
        sleep(30);
        rc = tvpvrd_command(cmd,reply,128) ;
    }
    if( rc ) {
         // sleep another 20s before we try again
        sleep(20);
        rc = tvpvrd_command(cmd,reply,128) ;
    }

    if( rc || strncmp(reply,"tvpvrd",strlen("tvpvrd")) ) {
        return -1;
    }

    return 0;
}


void
sighandler(int signal) {

    received_signal = signal;

}

/*
 * This is the place where we actually do all the work after
 * we set up all the boring admin stuff of the server like
 * reading inifile, setting up signal handlers, parsing arguments, etc.
 */
void
server_loop(void) {
    while( 1 ) {

        if( received_signal )
            break;

        sleep(2);

    }
}

int
locate_inifile(void) {

    // Check for inifile at common locations
    if( *inifile ) {
        // Specified on the command line. Overrides the default
        dict = iniparser_load(inifile);
    } else {
        snprintf(inifile,255,"%s/tvppwrd/%s",CONFDIR,INIFILE_NAME);
        inifile[255] = '\0';
        dict = iniparser_load(inifile);
        if( !dict ) {
            // As a last resort check the default /etc directory
            snprintf(inifile,255,"/etc/tvppwrd/%s",INIFILE_NAME);
            dict = iniparser_load(inifile);
            if( dict == NULL )
                *inifile = '\0';
        }
    }

    if( dict == NULL ) {
        return -1;
    } else {
        return 0;
    }
}

/*
 * 
 */
int
main(int argc, char** argv) {
    struct sigaction act;
    act.sa_handler = sighandler;
    act.sa_flags = 0;

    sigaction(SIGABRT,&act,(struct sigaction *)NULL);
    sigaction(SIGHUP,&act,(struct sigaction *)NULL);
    sigaction(SIGINT,&act,(struct sigaction *)NULL);

    strcpy(lockfilename,"/var/run/tvppwrd.pid");

    // Set lockfile to avoid multiple instances running
    if( -1 == createlockfile() ) {
        fprintf(stderr,"Cannot start server. Check system log for more information.\n");
        _exit(EXIT_FAILURE);
    }

    // Setup MALLOC to dump in case of memory corruption, i.e. double free
    // or overrun. This might be less efficient but this will be enabled
    // until we are 101% sure there are no mistakes in the code.
    static char *var = "MALLOC_CHECK";
    static char *val = "2";
    setenv(var,val,1);

      // Setup exit() handler
    atexit(exithandler);

    // Remember the program name we are started as
    strncpy(inibuffer,argv[0],256);
    strncpy(server_program_name,basename(inibuffer),31);
    server_program_name[31] = '\0';

    // Remember when the server was started
    ts_serverstart = time(NULL);

    // Parse and set cmd line options
    parsecmdline(argc,argv);

    // Setup inifile
    if( locate_inifile() ) {
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

    // Check if we should run as another user if we are started as root.
    // For security a daemon should never run as a root unless necessary
    // and in our case it is not necessary for us to run as root.
    chkswitchuser();

    // After a possible setuid() adn setgrp() the dumpable flag is reset which means
    // that no core is dumped in case of a SIGSEGV signal. We want a coredump in case
    // of a memory overwrite so we make sure this is allowed
    if( -1 == prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) ) {
        logmsg(LOG_ERR,"FATAL: Can not set PR_SET_DUMPABLE");
        exit(EXIT_FAILURE);
    }

    // Initialize all the data structures that stores our recording
    init_globs();

    // Do the main work. This function will only return once the
    // process receives a signal to stop.
    server_loop();

    logmsg(LOG_INFO,"Received signal %d. Shutting down ...",received_signal);

    logmsg(LOG_INFO,"Bye.");
    free_globs();
    exit(EXIT_SUCCESS);
}

