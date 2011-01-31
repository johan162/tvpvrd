/* =========================================================================
 * File:        LOCKFILE.C
 * Description: Functions to manage the lockfile used to avoid
 *              running to instances of the program at once.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009, 2010 Johan Persson
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
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <syslog.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "lockfile.h"
#include "tvpvrd.h"
#include "utils.h"


/*
 * Lockfile
 * Used to store the name of the lockfile used when starting up the server
 */
char lockfilename[256] = {0};


/**
 * We use a lockfile with the server PID stored to avoid that multiple
 * daemons are started. Since the lock file is stored in the standard /var/run
 * this also means that if the daemon is running as any other user than "root"
 * the lock file cannot be removed once it has been created (sine the server
 * changes ownert to a less powerfull user - it initially starts as root). However
 * this is not terrible sinceat startup the daemon witll check the lockfile and
 * if it exists also verify that the PID stored in the lockfile is a valid PID for a
 * tvpvrd process. If not it is regarded as a stale lockfile and overwritten.
 */
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
    snprintf(buff,255,"%s",lockfilename);
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
    snprintf(buff,255,"%s",lockfilename);
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
                snprintf(buff,255,"%s/%s",cwd,basename(lockfilename));
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


/* EOF */
