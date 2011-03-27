/* =========================================================================
 * File:        TVPSHELL.C
 * Description: A basic shell interface for the tvpvrd daemon. This will
 *              allow a setup whereby this shell is specified as a users
 *              "normal" login shell. This way you can connect to the
 *              daemon by logging in to the server using ssh or telnet as
 *              that user.
 *
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009, 2010, 2011 Johan Persson
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
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <readline/readline.h>
#include <readline/history.h>

// Aplication specific libs, parse inifiles
#ifdef HAVE_LIBINIPARSER
#include <iniparser.h>
#else
#include "../libiniparser/iniparser.h"
#endif

// Needed for prctl() and getaddrinfo()
#include <sys/prctl.h>
#include <netdb.h>

// Needed for MAX macro
#include <sys/param.h>

// Local header files
#include "../datetimeutil.h"
#include "../xstr.h"
#include "../config.h"
#include "../build.h"

/*
 * Server identification
 */
char server_version[] = PACKAGE_VERSION; // This define gets set by the config process

static int tcpip_port = -1;             // Read from INI-file
static char *server_ip = "127.0.0.1";   // Localhost
static char tvpvrd_pwd[128]= {'\0'};    // Read from INI-file

// Clear variable section in memory
#define CLEAR(x) memset (&(x), 0, sizeof(x))

#define FALSE (0)
#define TRUE (-1)

#define INIFILE_NAME "tvpvrd.conf"
#define PORT 9300
#define SIGINT_INFO "(Type exit to quit)\n"

volatile sig_atomic_t received_signal;

/*
 * dict
 * Holds the read dictionary from the inifile
 */
dictionary *dict=NULL;


/*
 * Handling of arguments to the server
 */
static const char short_options [] = "hv";
static const struct option long_options [] = {
    { "help",    no_argument,           NULL, 'h'},
    { "version", no_argument,           NULL, 'v'},
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
    opterr = 0; // Supress error string from getopt_long()
    if( argc > 2 ) {
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
                        "(C) 2009,2010,2011 Johan Persson, (johan162@gmail.com) \n"
                        "This is free software; see the source for copying conditions.\nThere is NO "
                        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
                        "Synopsis:\n"
                        "'%s' - Interactive shell for tvpvrd recording daemon.\n"
                        "Usage: %s [-h] [-v]\n"
                        "Options:\n"
                        " -h,      --help            Print help and exit\n"
                        " -v,      --version         Print version string and exit\n",
                       "tvpsh", "tvpsh");
                exit(EXIT_SUCCESS);
                break;

            case 'v':
                fprintf(stdout,"%s %s (build: %lu.%lu)\n%s",
                        "tvpvsh",server_version,(unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER,
                        "Copyright (C) 2009,2010,2011 Johan Persson (johan162@gmail.com)\n"
                        "This is free software; see the source for copying conditions.\nThere is NO "
                        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
                exit(EXIT_SUCCESS);
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
 * Global signal handler. We catch SIGHUP, SIGINT and SIGABRT
 * @param signal
 */

void
sighandler(int signo) {
    received_signal = signo;
    ssize_t rc = write(1,SIGINT_INFO,strlen(SIGINT_INFO));
    if( rc == rc ) {
      /* empty */ ;
    }
}


void
exithandler(void) {
  /* empty */
}

/**
 * Setup signal handlers
 */
void
setup_sighandlers(void) {

    // Block all signals //except SIGINT
    sigset_t sigset;
    sigfillset(&sigset);
    // sigdelset(&sigset,SIGINT);
    sigprocmask(SIG_SETMASK,&sigset,NULL);
    
    // Setup SIGINT handler
    /*
    struct sigaction act;
    CLEAR(act);
    act.sa_handler = &sighandler;
    act.sa_flags = SA_RESTART;

    sigaction(SIGINT,&act,(struct sigaction *)NULL);
    */

    atexit(exithandler);

}


/*
 * Read a reply from a socket with 0.1s timeout.
 * We only read the first chunk of data available.
 * To read all data on the socket see waitreadn()
 */
int
waitread(int sock, char *buffer, int maxbufflen) {
    fd_set read_fdset;
    struct timeval timeout;

    FD_ZERO(&read_fdset);
    FD_SET(sock, &read_fdset);

    timerclear(&timeout);
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

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
    const int buffsize = 512*1024;
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


#define TVPVRD_PASSWORD_LABEL "Password:"
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
    struct addrinfo hints;
    struct addrinfo *servinfo; // will point to the results
    char service[32];

    CLEAR(hints);
    hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    snprintf(service,32,"%d",tcpip_port);
    int status = getaddrinfo(server_ip, service, &hints, &servinfo);
    if (status) {
        // logmsg(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }
    int sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sock < 0) {
        return -1;
    }
    if (-1 == connect(sock, servinfo->ai_addr, servinfo->ai_addrlen)) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return -3;
    }
    freeaddrinfo(servinfo);

    if( waitread(sock, buffer, 1023) ) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return -4;
    }

    // Check for possible password question
    if( 0 == strncmp(buffer,TVPVRD_PASSWORD_LABEL,strlen(TVPVRD_PASSWORD_LABEL)) ) {

        snprintf(buffer,1023,"%s\r\n",tvpvrd_pwd);
        ssize_t nw = write(sock,buffer,strlen(buffer));
        if( nw != (ssize_t)strlen(buffer) ) {
            // logmsg(LOG_CRIT,"Failed to write to socket.");
            return -1;
        } else {
            if( waitread(sock, buffer, 1023) ) {
                // logmsg(LOG_ERR,"Timeout on socket when trying to send password to server '%s'", server_ip);
                shutdown(sock, SHUT_RDWR);
                close(sock);
                return -5;
            }
        }
    }

    // Send the command
    // logmsg(LOG_DEBUG,"Trying to send tvpvr command '%s' to '%s'",cmd,server_ip);
    char tmpbuff[128];
    snprintf(tmpbuff,127,"%s\r\n",cmd);

    ssize_t nw = write(sock,tmpbuff,strlen(tmpbuff)+1); // Include terminating 0
    if( nw != (ssize_t)strlen(tmpbuff)+1 ) {
        // logmsg(LOG_CRIT,"Failed to write to socket.");
        return -1;
    } else {

        // logmsg(LOG_DEBUG,"Command sent: %s [len=%d]",tmpbuff,strlen(tmpbuff));
        int rc;
        if( multiline ) {
            rc = waitreadn(sock, reply, maxreplylen);
        } else {
            rc = waitread(sock, reply, maxreplylen);
        }

        if( rc ) {
            // logmsg(LOG_ERR, "Timeout waiting for reply on command '%s'", cmd);
            shutdown(sock, SHUT_RDWR);
            close(sock);
            return -7;
        }

    }
    // Close sockets
    // logmsg(LOG_DEBUG, "Shutting down socket.");
    shutdown(sock, SHUT_RDWR);
    close(sock);
    return 0;

}

/*
 * Find out the name of the config file
 */
int
setup_inifile(void) {
    char inifile[256];
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

    if( dict == NULL ) {
        fprintf(stderr,"Can not find the ini file : '%s'\n",INIFILE_NAME);
        return -1;
    }

    return 0;
}

/*
 * Read the values we need from the daemons config file
 */
int
read_inifile(void) {

    tcpip_port = iniparser_getint(dict, "config:port", PORT);
    strncpy(tvpvrd_pwd,
            iniparser_getstring(dict, "config:password", ""),
            127);
    tvpvrd_pwd[127] = '\0';

    return 0;
}

/*
 * Main command interpretation loop.
 * Read a command from the command line and pass it on to the daemon
 */
void
cmd_loop(void) {
    _Bool finished = FALSE;
    char *buffer=(char *)NULL;
    size_t const maxreplylen = 50*1024;
    char *reply = calloc(maxreplylen,sizeof(char));
    char prompt[64];

    snprintf(prompt,64,"%s-%s> ","tvpvsh",server_version);
    do {
        if( NULL == (buffer=readline(prompt)) ) {
            finished = TRUE;
        } else {
            xstrtrim(buffer);
            if( 0 == strncmp(buffer,"exit",4) ) {
                finished = TRUE;
            } else {
	      if( buffer && *buffer ) {
		add_history(buffer);
		if( -1 == tvpvrd_command(buffer, reply, maxreplylen, TRUE) ){
		  finished = TRUE;
		} else {
		  if( write(1,reply,strlen(reply)) != (ssize_t)strlen(reply) )
		    finished = TRUE;
		}
	      }
            }
	    if( buffer) {
	      free(buffer);
	      buffer=NULL;
	    }
        }
    } while( !finished);

    free(reply);
}

int
main(int argc, char **argv) {

  parsecmdline(argc,argv);

    setup_sighandlers();
    setup_inifile();
    read_inifile();

    /*
    printf("%s-%s (build:%lu.%lu)\n",
           basename(argv[0]), server_version,
           (unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER);
    */
    cmd_loop();
    
}

/* EOF */
