/* =========================================================================
 * File:        TVPOWERD.C
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

// Standard UNIX includes
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

// Standard socket and IP address handling
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <arpa/inet.h>

// Aplication specific libs, parse inifiles
#ifdef HAVE_LIBINIPARSER
#include <iniparser.h>
#else
#include "../iniparser/iniparser.h"
#endif

// Needed for prctl()
#include <sys/prctl.h>
#include <netdb.h>

// Needed for regex
#include <sys/types.h>
#include <regex.h>

// Local header files
#include "../utils.h"
#include "tvpowerd.h"
#include "../lockfile.h"
#include "../config.h"
#include "wakelan.h"

// #define _DEBUG


/*
 * Server identification
 */
char server_version[] = PACKAGE_VERSION; // This define gets set by the config process
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
int send_mail_on_shutdown;
char send_mailaddress[64];

/*
 * The value of the following variables are read from the ini-file and initialized in
 * the main() function. They hold various run time limits and settings that
 * the user can adjust.
 */

// Should we run as a daemon or nothing
int daemonize=1;

// Names of the ini file used
char inifile[256];

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
 * The actual command used to shutdown the remote server
 */
#define DEFAULT_SHUTDOWN_COMMAND "/sbin/shutdown -h %d"
char shutdown_command[255];

/*
 * What locale to use, by default this is set to LOCALE_NAME (which in the original distribution
 * is set to en_US.UTF8)
 */
#define LOCALE_NAME "en_US.UTF8"
char locale_name[255];

/*
 * Password to login to the tvpvrd daemon if needed
 */
char tvpvrd_pwd[32];

/*
 * The user we should login as on the remote server in order to
 * issue the shutdown command. This should usually be the root user
 */
char server_user[32];

/*
 * Holds the IP address of the server running the tvpvrd daemon
 */
char server_ip[32];

/*
 * What port is the tvpvrd daemon listening on
 */
#define SERVER_PORT 9300
int server_port;

/*
 * Parameters to send the WakeonLan broadcast message
 */
#define TARGET_PORT 32767
#define TARGET_BROADCAST_ADDRESS "255.255.255.255"
char target_mac_address[32], target_broadcast_address[32] ;
int target_port;

/*
 * The maximu load we allow on the remote server for us to do a shutdown
 */
#define MAX_SHUTDOWN_5LOAD 0.1
double max_shutdown_5load;

/*
 * The minimum power off time we can have before er bother to shutdown
 * the server.
 */
#define MIN_POWEROFF_TIME 60
int min_poweroff_time;

/*
 * How often should we chekc the remote server to see if we should
 * shut it down.
 */
#define SERVER_REFRESH_TIME 30
int server_refresh_time;

/*
 * Warning time used for the shutdown command
 */
#define SHUTDOWN_WARNING_TIME 2
int shutdown_warning_time;

/*
 * How many seconds before the actual recording do wee need to start
 * the server
 */
#define WAKEUP_MARGIN_TIME 150
int wakeup_margin_time;

/*
 * If we should unload the ivtv driver in the recording server when we
 * power cycle
 */
#define DEFAULT_UNLOAD_DRIVER 0
int unload_driver;
/*
 * Keep track of the last signal we received.
 */
static int received_signal = 0;

/*
 * Handling of arguments to the server
 */
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
                        "(C) 2010 Johan Persson, (johan162@gmail.com) \n"
                        "This is free software; see the source for copying conditions.\nThere is NO "
                        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
                        "Usage: %s [options]\n"
                        "Synopsis:\n"
                        "'%s' - Power managment daemon for a tvpvrd server.\n"
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
                        "Copyright (C) 2010 Johan Persson (johan162@gmail.com)\n"
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

    send_mail_on_shutdown = iniparser_getboolean(dict,"config:sendmail_on_shutdown",SENDMAIL_ON_SHUTDOWN);

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

    wakeup_margin_time = iniparser_getint(dict, "config:wakeup_margin_time", WAKEUP_MARGIN_TIME);

    if( strlen(logfile_name) >= 127 ) {
                logmsg(LOG_CRIT,
                "** FATAL error. "
                "Illegal value for logfile_name in ioni-file");
        exit(EXIT_FAILURE);
    }

    if( strcmp("stdout",logfile_name) == 0 && daemonize ) {
        logmsg(LOG_CRIT,
                "** FATAL error. "
                "'stdout' is not a valid logfile when started in daemon mode.");
        exit(EXIT_FAILURE);
    }

    strncpy(shutdown_command,
            iniparser_getstring(dict, "config:shutdown_command", DEFAULT_SHUTDOWN_COMMAND),
            254);
    shutdown_command[254] = '\0';

    unload_driver = iniparser_getboolean(dict,"config:unload_ivtv_driver",DEFAULT_UNLOAD_DRIVER);


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

    unsigned char macaddr[6];

    if(  ! parse_mac(macaddr, target_mac_address) ) {
        logmsg(LOG_CRIT,
                "FATAL error. "
               "'Remote server MAC address not specified or has an unrecognized syntax");
        exit(EXIT_FAILURE);
    }

    if( strlen(server_ip) < 9  ) {
        logmsg(LOG_CRIT,
                "FATAL error. "
               "'Remote server IP address not correctly specified.");
        exit(EXIT_FAILURE);
    }

}

/**
 * Check what user we are running as and change the user (if allowed) to the
 * specified tvpowerd user.
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
 
        } else {
            logmsg(LOG_INFO,"The server is running as user 'root'.");
        }
    }

   // After a possible setuid() adn setgrp() the dumpable flag is reset which means
    // that no core is dumped in case of a SIGSEGV signal. We want a coredump in case
    // of a memory overwrite so we make sure this is allowed
    if( -1 == prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) ) {
        logmsg(LOG_ERR,"FATAL: Can not set PR_SET_DUMPABLE");
        exit(EXIT_FAILURE);
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

/*
 * Read a reply from a socket with 2s timeout.
 * We only read the first chunk of data available.
 * To real all datat on the soccket see wintreadn()
 */
int
waitread(int sock, char *buffer, int maxbufflen) {
    fd_set read_fdset;
    struct timeval timeout;

    FD_ZERO(&read_fdset);
    FD_SET(sock, &read_fdset);

    timerclear(&timeout);
    timeout.tv_sec = 2;

    int ret = select(sock + 1, &read_fdset, NULL, NULL, &timeout);
    if( 0 == ret ) {
        // Timeout
        return -1;
    } else {
        int nread = read(sock, buffer, maxbufflen);
        buffer[nread] = '\0';
    }
    return 0;
}

/*
 * Used to read an unknown amount of data from a socket
 * We keep filling the buffer until we get a timeout and there
 * is nothing more to read.
 */
int
waitreadn(int sock, char *buffer, int maxbufflen) {
    const int buffsize = 128*1024;
    char *pbuff = calloc(buffsize,sizeof(char));
    *buffer = '\0';
    int totlen=0;
    while( totlen < maxbufflen && waitread(sock,pbuff,buffsize) > -1 ) {
        strcat(buffer,pbuff);
        int len=strlen(pbuff);
        totlen += len;
        buffer[totlen] = '\0';
    }
    buffer[maxbufflen-1] = '\0';
    free(pbuff);
    if( *buffer == '\0' )
        return -1;
    else
        return 0;
}

#define TVPVRD_IDENTIFICATION "!TVPVRD!"
#define TVPVRD_PASSWORD "Password:"
/**
 * Send a specified command to the tvpvrd server
 * @param cmd
 * @param reply
 * @param maxreplylen
 * @param multiline
 * @return
 */
int
tvpvrd_command(char *cmd, char *reply, int maxreplylen, int multiline) {   
    char buffer[1024];

    // Open a TCP/IP socket on the remote server
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
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
            return -2;
        }
    }
    struct sockaddr_in remote_sockadr;
    remote_sockadr.sin_family      = AF_INET;
    remote_sockadr.sin_addr.s_addr = remote_adr.s_addr;
    remote_sockadr.sin_port        = htons(server_port);

    // Connect the socket to the address
    if( -1 == connect(sock, (struct sockaddr *)&remote_sockadr, sizeof(remote_sockadr)) ) {        
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return -3;
    }

    if( waitread(sock, buffer, 1023) ) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return -4;
    }

    // Check for the string "!TVPVRD!" as proof that there is a
    // a tvpvrd daemon on the otyher side
    if( strncmp(buffer,TVPVRD_IDENTIFICATION,strlen(TVPVRD_IDENTIFICATION)) ) {

        // Check for possible password question
        if( 0 == strncmp(buffer,TVPVRD_PASSWORD,strlen(TVPVRD_PASSWORD)) ) {

            snprintf(buffer,1023,"%s\r\n",tvpvrd_pwd);
            ssize_t nw = write(sock,buffer,strlen(buffer));
            if( nw != (ssize_t)strlen(buffer) ) {
                logmsg(LOG_CRIT,"Failed to write to socket.");
            } else {

                if( waitread(sock, buffer, 1023) ) {
                    logmsg(LOG_ERR,"Timeout on socket when trying to send password to server '%s'", server_ip);
                    shutdown(sock, SHUT_RDWR);
                    close(sock);
                    return -5;
                }

            }

        } else {

            logmsg(LOG_ERR, "It doesn't seem to be a tvpvrd daemon listening on the other side at '%s'. Aborting.", server_ip);
            shutdown(sock, SHUT_RDWR);
            close(sock);
            return -6;
        }
    } 

    // Send the command
    // logmsg(LOG_DEBUG,"Trying to send tvpvr command '%s' to '%s'",cmd,server_ip);
    char tmpbuff[128];
    snprintf(tmpbuff,127,"%s\r\n",cmd);
    
    ssize_t nw = write(sock,tmpbuff,strlen(tmpbuff)+1); // Include terminating 0
    if( nw != (ssize_t)strlen(tmpbuff)+1 ) {
        logmsg(LOG_CRIT,"Failed to write to socket.");
    } else {

        logmsg(LOG_DEBUG,"Command sent: %s [len=%d]",tmpbuff,strlen(tmpbuff));
        int rc;
        if( multiline ) {
            rc = waitreadn(sock, reply, maxreplylen);
        } else {
            rc = waitread(sock, reply, maxreplylen);
        }

        if( rc ) {
            logmsg(LOG_ERR, "Timeout waiting for reply on command '%s'", cmd);
            shutdown(sock, SHUT_RDWR);
            close(sock);
            return -7;
        }

    }
    // Close sockets
    logmsg(LOG_DEBUG, "Shutting down socket.");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 0;

}

/**
 * Send a remote command to another server over a ssh link. In order for
 * this to work the originating server must be a trusted server and have
 * its rsa key in teh remote servers authorized_keys
 * @param cmd
 * @param reply
 * @param len
 * @return
 */
int
remote_command(char *cmd, char *reply, int len) {

    if( strlen(cmd) > 450 )
        return -1;

    *reply = '\0';
    char *buffer = calloc(1024,sizeof(char));
    snprintf(buffer,1024,"/usr/bin/ssh %s@%s '%s'", server_user, server_ip, cmd );

    logmsg(LOG_DEBUG,"Excuting cmd: %s",buffer);
    FILE *fp = popen(buffer,"r");
    if( fp == NULL  ) {
/*
        logmsg(LOG_DEBUG, "Error when executing popen('%s'): ( %d : %s )",
               buffer, errno, strerror(errno));
*/
        free(buffer);
        return -1;
    } else {
        // Only read first line in the command output
        int nr;
        nr = fread(reply, sizeof(char), len, fp);
        if( nr > 0 ) {
            // Get rid of final '\n'
            if( reply[nr-1] == '\n' )
                reply[nr-1] = '\0';
        }
        reply[nr]='\0';
        pclose(fp);
        free(buffer);
        return 0;
    }
}

/**
 * Determine load average on a remote server
 * @param avg1load
 * @param avg5load
 * @param avg15load
 * @return
 */
int
remote_server_load(float *avg1load,float *avg5load,float *avg15load) {

#ifdef _DEBUG
    *avg1load=0.0; *avg5load=0.0; *avg15load=0.0;
#else

    char buffer[1024];

    if( remote_command("uptime",buffer,1024) ) {
        logmsg(LOG_ERR,"Error executing 'load' command ( %d : %s )",errno, strerror(errno));
        return -1;
    }

    // Extract load values from string
    char *pattern = "load average: "
                    "([0-9]+\\.[0-9]+), "
                    "([0-9]+\\.[0-9]+), "
                    "([0-9]+\\.[0-9]+)";
    
    regex_t preg;
    int rc = regcomp(&preg,pattern,REG_EXTENDED);
    if( rc ) {
        logmsg(LOG_ERR,"Error compiling regex ( %d : %s )",errno, strerror(errno));
        return -1;
    }

    regmatch_t matches[4];
    rc = regexec(&preg,buffer,4, matches, 0);
    if( rc ) {
        logmsg(LOG_ERR,"Cannot determine server load in '%s'",buffer);
        regfree(&preg);
        return -1;
    }
    regfree(&preg);

    char load[128];

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
#endif
    return 0;

}

/**
 * Find out number of logged on user on remote system
 * @param num
 * @return
 */
int
users_on_remote_server(int *num) {
    char buffer[512],reply[32];
    *reply = '\0';

    snprintf(buffer,512,"who | wc -l");
    int rc = remote_command(buffer,reply,32);
    *num = atoi(reply);
    logmsg(LOG_DEBUG,"Remote server has %d user(s) logged in",*num);
    return rc;
}

/**
 * Shutdown a remote server
 * @return
 */
int
shutdown_remote_server(void) {
    char command[512],buffer[512],reply[32];
    char scriptfile[255];

    if( unload_driver ) {
        logmsg(LOG_DEBUG,"Unloading ivtv driver");
        snprintf(command,512,"/sbin/modprobe -r ivtv");
        int rc = remote_command(command,reply,32);
        if( rc ) {
            logmsg(LOG_ERR,"Failed to unload ivtv driver. Shutdown aborted");
            return 1;
        }
    }

    // First run optional shutdown pre-script
    snprintf(scriptfile,255,"%s/tvpowerd/pre-shutdown.sh",CONFDIR);
    FILE *fp=fopen(scriptfile,"r");
    if( fp == NULL ) {
        logmsg(LOG_NOTICE,"Cannot find pre-shutdown script '%s' ( %d : %s)",scriptfile,errno,strerror(errno));
    } else {
        fclose(fp);
        snprintf(buffer,255,"%s > /dev/null 2>&1",scriptfile,CONFDIR);
        int rc= system(buffer);
        if( rc ) {
            logmsg(LOG_ERR,"Error executing pre-shutdown scriptfile");
        } else {
            logmsg(LOG_DEBUG,"Finished running pre-shutdown script '%s' ",scriptfile);
        }
    }

    logmsg(LOG_INFO,"Shutting down remote server.");
    snprintf(command,512,shutdown_command,shutdown_warning_time );
    int rc = remote_command(command,reply,32);
    logmsg(LOG_INFO,"Finished shutting down remote server.");

}

/**
 * Wakeup the remote server by issuing a WakeOnLan package
 * @return
 */
int
wakeup_remote_server(void) {

    logmsg(LOG_INFO,"Waking up remote server with MAC address '%s'.",target_mac_address);
    
    if( wakelan(target_mac_address,target_broadcast_address, target_port) ) {
        logmsg(LOG_ERR,"Cannot wake up target server!");
        return 1;
    }

    // Initially wait a bit for the server to get started before we
    // make a sanity check that it is awake
    sleep(60);

    // Try to login to the tvpvrd daemon and retrive the version as a proof that
    // the server is up and running
    char cmd[32],reply[128];

    snprintf(cmd,32,"v");
    int rc = tvpvrd_command(cmd,reply,128,0) ;
    if( rc ) {
         // sleep another 30s before we try again
        sleep(30);
        rc = tvpvrd_command(cmd,reply,128,0) ;
    }

    if( rc || strncmp(reply,"tvpvrd",strlen("tvpvrd")) ) {
        logmsg(LOG_ERR,"Cannot connect to server on target machine");
        return -1;
    }

    if( unload_driver ) {
        char command[512], reply[32];
        logmsg(LOG_DEBUG,"Loading ivtv driver");
        snprintf(command,512,"/sbin/modprobe ivtv");
        remote_command(command,reply,32);
        int rc = remote_command(command,reply,32);
        if( rc ) {
            logmsg(LOG_ERR,"CRITICAL Failed to load ivtv driver.Aborting rest of startup sequence");
            return 1;
        }
    }

    logmsg(LOG_DEBUG,"Remote server is up and running");
    
    // Finally run optional startup post-script
    char scriptfile[256], buffer[512];
    snprintf(scriptfile,255,"%s/tvpowerd/post-startup.sh",CONFDIR);
    FILE *fp=fopen(scriptfile,"r");
    if( fp == NULL ) {
        logmsg(LOG_NOTICE,"Cannot find post-startup script '%s' ( %d : %s)",scriptfile,errno,strerror(errno));
    } else {
        fclose(fp);
        snprintf(buffer,255,"%s > /dev/null 2>&1",scriptfile,CONFDIR);
        int rc= system(buffer);
        if( rc ) {
            logmsg(LOG_ERR,"Error executing post-startup scriptfile");
        } else {
            logmsg(LOG_DEBUG,"Finished running post-startup script '%s' ",scriptfile);
        }
    }

    return 0;
}

/**
 * Try to determin our ininfile
 */
void
locate_inifile(void) {

    // Check for inifile at common locations
    if( *inifile ) {
        // Specified on the command line. Overrides the default
        dict = iniparser_load(inifile);
    } else {
        snprintf(inifile,255,"%s/tvpowerd/%s",CONFDIR,INIFILE_NAME);
        inifile[255] = '\0';
        dict = iniparser_load(inifile);
        if( !dict ) {
            // As a last resort check the default /etc directory
            snprintf(inifile,255,"/etc/tvpowerd/%s",INIFILE_NAME);
            dict = iniparser_load(inifile);
            if( dict == NULL )
                *inifile = '\0';
        }
    }

    if( dict == NULL ) {
        fprintf(stderr,"Can not find the ini file : '%s'\n",INIFILE_NAME);
        exit(EXIT_FAILURE);
    }
}

/**
 * Global signal handler. We catch SIGHUP, SIGINT and SIGABRT
 * @param signal
 */
void
sighandler(int signal) {

    received_signal = signal;

}

/**
 * Setup signal handlers
 */
void
setup_sighandlers(void) {

    struct sigaction act;
    act.sa_handler = sighandler;
    act.sa_flags = 0;

    sigaction(SIGABRT,&act,(struct sigaction *)NULL);
    sigaction(SIGHUP,&act,(struct sigaction *)NULL);
    sigaction(SIGINT,&act,(struct sigaction *)NULL);

    // Setup exit() handler
    atexit(exithandler);

}

/**
 * Setup a lockfile based on the program name
 * @param argv
 */
void
setup_lockfile(void) {

    snprintf(lockfilename,255,"%s/%s.pid",LOCKFILE_DIR,server_program_name );
    // Set lockfile to avoid multiple instances running
    if( -1 == createlockfile() ) {
        fprintf(stderr,"Cannot start server. Check system log for more information.\n");
        _exit(EXIT_FAILURE);
    }
}

/**
 * Verify that the specified remote server have a tvpvrd daemon running 
 * @return 0 if tvpvrd is running
 */
int
verify_tvpvrd(void) {
    // We send the version command
    char cmd[32],reply[512];
    sprintf(cmd,"v");
    if( !tvpvrd_command(cmd, reply, 512, 0) ) {
        return 0;
    } else {
        return -1;
    }
}

/**
 * Check if the remote tvpvrd dameon is doing a recording
 * @param flag
 * @return
 */
int
remote_recording(int *flag) {
#ifdef _DEBUG
    *flag = 0;
    return 0;
#else
    // Check if remote server is doing recording
    char cmd[32],reply[512];
    sprintf(cmd,"o");
    if( !tvpvrd_command(cmd, reply, 512, 0) ) {
        *flag = strncmp("None.",reply,5);
        if( *flag )
            logmsg(LOG_DEBUG,"Ongoing recording: %s",reply);
        return 0;
    } else {
        logmsg(LOG_DEBUG,"Cannot determine ongoing recordings on remote sever");
        return -1;
    }
#endif
}

/*
 * Structure to hold all the recroding we read from the tvpvrd server
 * (In reality we wouldn't need to read all of the recordings. It would
 * suffice to only look at the very next one.
 */

#define MAX_ENTRIES 2096
struct rec_entry {
    time_t st;
    time_t et;
    char title[512];
};

struct rec_entry recordings[MAX_ENTRIES];
int numrecs=-1;

/**
 * Refresh recordings from the tvpvrs server
 * @return
 */
int
refresh_recordings(void) {

#ifdef _DEBUG
    logmsg(LOG_DEBUG,"Simulating refreshing recordings ...");
    return 0;
#else

    const int buffsize = sizeof(char)*10*1024;
    const int linesize = sizeof(char)*2*1024;
    char *buffer = calloc(buffsize,sizeof(char));
    char *line = calloc(linesize,sizeof(char));
    char *msgbuff = calloc(linesize,sizeof(char));
    static char prevmsgbuff[512] = {'\0'}; 
    int ret=0;

    numrecs = 0 ;
    if( !tvpvrd_command("lts",buffer,buffsize, 1) ) {
        int bc=0, rc=0 ;
        while( rc < MAX_ENTRIES && bc < buffsize && buffer[bc] ) {

            int lc=0;
            while(lc < linesize && bc < buffsize && buffer[bc] && buffer[bc] != '\n') {
                line[lc++] = buffer[bc++];
            }
            line[lc] = '\0';
            line[10] = '\0';
            line[21] = '\0';

            recordings[rc].st = atol(line);
            recordings[rc].et = atol(&line[11]);
            strncpy(recordings[rc].title,&line[22],127);
            recordings[rc].title[127] = '\0';

            rc++;

            if( buffer[bc] == '\n' )
                bc++;
        }
        numrecs = rc;
        int y,m,d,h,min,sec;
        fromtimestamp(recordings[0].st, &y,&m,&d,&h,&min,&sec);
        snprintf(msgbuff,512,"Refreshing recordings. Next recording '%s' at %02d/%02d %02d:%02d",recordings[0].title,d,m,h,min);
        if( strcmp(prevmsgbuff,msgbuff) ) {
            logmsg(LOG_INFO,msgbuff);
            strcpy(prevmsgbuff,msgbuff);
        }
    } else {
        logmsg(LOG_ERR,"Failed to refresh recordings from server. Have server been powered off manually?");
        ret = -1;
    }

    free(buffer);
    free(line);

    return ret;
#endif
    
}

/*
 * Return the time in seconds until next scheduled recording. In addition return
 * the absolute timestamp for the recording as well as the title
 */
int
time_nextrecording(time_t *ts,char **title) {

#ifdef _DEBUG
    *ts = time(NULL) + 4*3600;
    static char *dummytitle = "Fake DEBUG recording";
    *title = dummytitle;
#else
    if( numrecs < 1) {
        *ts = time(NULL)+999999;
        *title='\0';
        return -1;
    }

    *ts = recordings[0].st;
    *title = recordings[0].title;
#endif
    return 0;
}

/*
 * This is the place where we actually do all the work after
 * we set up all the boring admin stuff of the server like
 * reading inifile, setting up signal handlers, parsing arguments, etc.
 */

void
server_loop(void) {
#ifdef _DEBUG
server_refresh_time = 1;
#endif
    int n=server_refresh_time*60+1;
    time_t ts;
    char *title;
    char hname[256];
    remote_command("hostname",hname,256);

    logmsg(LOG_DEBUG,"Server loop: server_refresh_time=%d min, min_poweroff_time=%d min, wakeup_margin=%d s",
           server_refresh_time, min_poweroff_time, wakeup_margin_time);
    while( 1 ) {

        if( n >= server_refresh_time*60 ) {
            n = 0;

            int numusers=0;
            users_on_remote_server(&numusers);
            // Time to update recordings
            if( numusers < 1 ) {

                // Find out how long to next recording (in seconds)
                if( ! refresh_recordings() && ! time_nextrecording(&ts, &title) ) {
      
                    int ongoing=1;
                    remote_recording(&ongoing);

                    if( ! ongoing ) {
                        int ttn = ts - time(NULL);
                        if( ttn >= min_poweroff_time*60  ) {

                            // Check load on remote server
                            float a1,a5,a15;
                            if( remote_server_load(&a1,&a5,&a15) ) {
                                logmsg(LOG_ERR,"Cannot determine remote server load.");
                                a5 = 99;
                            }

                            if( a1 <= max_shutdown_5load && a5 <= max_shutdown_5load ) {

                                logmsg(LOG_INFO,"Initiating power off. Next recording in %d min and no ongoing recordings or transcodings.", ttn/60);
        #ifdef _DEBUG
                                logmsg(LOG_DEBUG,"Simulating shutdown ...");
                                sleep(3);
        #else
                                shutdown_remote_server();
                                // Wait until the server is shutdown before we continue
                                int swt = 0 ;
                                while( swt < shutdown_warning_time*60+30 ) {
                                    sleep(2);
                                    swt += 2;
                                    if( received_signal ) {
                                        return;
                                    }
                                }
                                
        #endif
                                // Now check that the server really is powered down by trying to execute a
                                // command on the server
                                if( 0 == verify_tvpvrd() ) {

                                    logmsg(LOG_NOTICE,"Failed to shutdown server. Shutdown possibly aborted by user?");

                                } else {

                                    int y,m,d,h,min,sec;
                                    int ny,nm,nd,nh,nmin,nsec;
                                    fromtimestamp(ts - wakeup_margin_time, &y,&m,&d,&h,&min,&sec);
                                    fromtimestamp(time(NULL), &ny,&nm,&nd,&nh,&nmin,&nsec);
                                    if( send_mail_on_shutdown ) {
                                        const int maxmailsize=5*1024;
                                        char *buffer = calloc(maxmailsize,sizeof(char));
                                        char mailsubj[256];
                                        snprintf(mailsubj,256,"Server \"%s\" powered off until %02d/%02d %02d:%02d",hname,d,m,h,min);
                                        snprintf(buffer,maxmailsize-1,
                                                 "Hi,\n\n"
                                                 " - Server '%s' (%s) powered off at: %02d/%02d %02d:%02d\n\n"
                                                 " - Server will be powered on at: %02d/%02d %02d:%02d to record '%s'\n\n"
                                                 "Have a nice day!\n",
                                                 hname,server_ip,nd,nm,nh,nmin,
                                                 d,m,h,min,title);
                                        send_mail(mailsubj,send_mailaddress,buffer);
                                        free(buffer);
                                    }

                                    logmsg(LOG_INFO,"Server is now powered off until %d-%02d-%02d %02d:%02d to record '%s'",
                                           y,m,d,h,min,title);

#ifdef _DEBUG
                                    logmsg(LOG_DEBUG,"Simulating power on  ...");
                                    sleep(30);
                                    logmsg(LOG_DEBUG,"Server is now awake.");
#else
                                    int user_started = 0 ;
                                    while( !user_started && (ts-wakeup_margin_time) > time(NULL) ) {
                                        sleep(4);

                                        if( received_signal ) {
                                            return;
                                        }

                                        // We also need to check in case the user actually cheated
                                        // and started the server manually. In that case we want to
                                        // move to the beginning state again. We do this by checking
                                        // if the server happens to be alive.
                                        if( 0 == verify_tvpvrd() ) {
                                            // Ooops ! The user has started the server without waiting for us!
                                            user_started = 1;
                                        }

                                        // The user can force a restart of the recording server by placing a file
                                        // with name "start_tvp" in the tmp dir
                                        int tstfd = open( "/tmp/start_tvp", O_RDONLY);                                        
                                        if( tstfd >= 0 ) {
                                            close(tstfd);
                                            logmsg(LOG_INFO,"Found start_tvp. Starting server ...");
                                            if( unlink("/tmp/start_tvp") ) {
                                                logmsg(LOG_ERR,"Cannot unlink file \"/tmp/start_tvp\" ( %d : %s) ", errno, strerror(errno));
                                            }
                                            break;
                                        } else {
                                            close(tstfd);
                                        }

                                    }

                                    if( ! user_started ) {
                                        if( wakeup_remote_server() ) {
                                            logmsg(LOG_ERR,"Failed to wakeup server.");
                                        } else {
                                            logmsg(LOG_DEBUG,"Server power on sequence initiated ...");
                                        }
                                    }
                                }
#endif
                            }
                        }
                    }
                }
            }

        } else {

            sleep(2);
            n += 2;

        }

        if( received_signal )
            return;

    }
}

/*
 *  MAIN  Acts as a hub to kickstart everythign else
 */
int
main(int argc, char** argv) {

    char inibuffer[4096] ;
    // Remember the program name we are started as
    strncpy(inibuffer,argv[0],256);
    strncpy(server_program_name,basename(inibuffer),31);
    server_program_name[31] = '\0';

    // Parse and set cmd line options
    parsecmdline(argc,argv);

    // Remember when the server was started
    ts_serverstart = time(NULL);

    // Setup signal handlers
    setup_sighandlers();

    // Setup and check lockfile
    setup_lockfile();

    // Setup inifile
    locate_inifile() ;

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
    logmsg(LOG_INFO,"Starting tvpowerd ver %s , Build date: %s",server_version, server_build_date);
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

    // Do the main work. This function will only return once the
    // process receives a signal to stop.
    server_loop();

    logmsg(LOG_INFO,"Received signal %d. Shutting down ...",received_signal);
    logmsg(LOG_INFO,"Bye.");
    exit(EXIT_SUCCESS);
}

/* EOF */
