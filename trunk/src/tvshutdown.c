/* =========================================================================
 * File:        TVSHUTDOWN.C
 * Description: Routines for handling of automatic server shutdown
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

#include <stdlib.h>
#include <stdio.h>
#include <pwd.h>
#include <wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "tvshutdown.h"
#include "recs.h"
#include "utils.h"
#include "transc.h"

#define RTC_WAKEUP_DEVICE "/sys/class/rtc/rtc0/wakealarm"
#define RTC_STATUS_DEVICE "/proc/driver/rtc"

int
set_rtc_alarm(time_t alarmtime) {
    // The alarmtime must be in UTC time since the bios clock is
    // assumed to be that
    int rtfd = open(RTC_WAKEUP_DEVICE, O_WRONLY);
    if( -1 == rtfd ) {
        logmsg(LOG_ERR,"Cannot open RTC device %s ( %d : %s )",
               RTC_WAKEUP_DEVICE,errno,strerror(errno));
        return -1;
    }
    char timestr[128];

    logmsg(LOG_DEBUG,"Setting RTC wakeupalarm to %s",ctime(&alarmtime));

    snprintf(timestr,127,"0");
    int ret = write(rtfd,timestr,strlen(timestr));
    if( ret != (int)strlen(timestr) ) {
        logmsg(LOG_ERR,"Cannot write to RTC device %s ( %d : %s )",
               RTC_WAKEUP_DEVICE,errno,strerror(errno));
        close(rtfd);
        return -1;
    }

    snprintf(timestr,127,"%lu",alarmtime);
    ret = write(rtfd,timestr,strlen(timestr));
    if( ret != (int)strlen(timestr) ) {
        logmsg(LOG_ERR,"Cannot write to RTC device %s ( %d : %s )",
               RTC_WAKEUP_DEVICE,errno,strerror(errno));
        close(rtfd);
        return -1;
    }
    close(rtfd);
    return 0;
}

int
get_num_ongoing_recordings(void) {
    int recs=0;
    for (unsigned i = 0; i < (unsigned)max_video; i++) {
        if (ongoing_recs[i]) {
            recs++;
        }
    }
    return recs;
}

int
get_num_users(void) {
    char cmd[256];

    snprintf(cmd,255,"who|wc -l");
    FILE *fp = popen(cmd,"r");
    if( fp == NULL  ) {
        logmsg(LOG_DEBUG, "Error when executing popen('%s'): ( %d : %s )",
               cmd, errno, strerror(errno));
        return -1;
    } else {
        // Only read first line in the command output
        int nr;
        const int len = 1048;
        char *reply = calloc(1,len);

        nr = fread(reply, sizeof(char), len, fp);
        if( nr > 0 ) {
            // Get rid of final '\n'
            if( reply[nr-1] == '\n' )
                reply[nr-1] = '\0';
        } else  {
            logmsg(LOG_ERR, "Cannot read reply from command to get number of users on server, nr=%d",nr);
            return -1;
        }
        reply[nr]='\0';
        pclose(fp);
        nr=atoi(reply);
        free(reply);
        return nr;
    }
}

void
check_for_shutdown(void) {

    if ( ! shutdown_enable )
        return;

    // Check that we are running as root user
    // Check if we are starting as root
    struct passwd *pwe = getpwuid(getuid());

    if( strcmp(pwe->pw_name,"root") ) {
        logmsg(LOG_NOTICE,"Daemon is running as user='%s'. Must run as root to use automatic shutdown.",pwe->pw_name);
        return;
    }

    int uptime=0, idletime=0;
    getuptime(&uptime,&idletime);
    if( uptime < (int)shutdown_min_uptime ) {
        // We will not initiate a shutdown unless the server has been awake for at least
        // this amount of time
        return;
    }

    if( ! shutdown_ignore_users ) {
        int ret = get_num_users();
        if( ret > 0 ) {
            //logmsg(LOG_DEBUG,"Aborting shutdown sequence, %d users logged on.", ret);
            return;
        }
    }

    pthread_mutex_lock(&recs_mutex);

    // Check all video queues for the closest next recording
    time_t nextrec = recs[REC_IDX(0, 0)]->ts_start;
    int nextrec_video=0;
    int nextrec_idx=0;
    for (unsigned video = 1; video < max_video; ++video) {
        if( recs[REC_IDX(video, 0)]->ts_start < nextrec  ) {
            nextrec = recs[REC_IDX(video, 0)]->ts_start;
            nextrec_video = video;
            nextrec_idx = 0;
        }
    }

    // We need the current time to compare against
    time_t now = time(NULL);

    if( nextrec - now > shutdown_min_time ) {
        // Yes we could potentially shutdown. Now check that
        // 1) No ongoing recordings
        // 2) No ongoing transcodings
        // 4) The server load is not too high (indicating other important work)

        logmsg(LOG_DEBUG,"Verifying if conditions are fulfilled to do system shutdown");

        float avg1, avg5, avg15;
        getsysload(&avg1, &avg5, &avg15);
        if( avg5 < shutdown_max_5load &&
            get_num_ongoing_transcodings() == 0 &&
            get_num_ongoing_recordings() == 0 ) {

            // Set RTC alarm so we will wake up a bit before the recording time
            nextrec -= shutdown_pre_startup_time;
            if( -1 == set_rtc_alarm(nextrec) ) {
                logmsg(LOG_ERR,"Automatic shutdown disabled since the wakeup alarm cannot be set.");
                return;
            }

            logmsg(LOG_DEBUG,"Initiating automatic shutdown");
            // Call shutdown script
            char cmd[256] ;
            snprintf(cmd,255,"%s/tvpvrd/%s -t %d",CONFDIR,shutdown_script,shutdown_time_delay);

            logmsg(LOG_DEBUG,"Executing shutdown script: '%s'",cmd);
            if( shutdown_send_mail ) {
                char subj[255];
                char *body = calloc(1,2048);
                char *rtc_status = calloc(1,2048);
                char hname[255] ;
                if( 0 == gethostname(hname,255) ) {

                    int stfd = open(RTC_STATUS_DEVICE,O_RDONLY);
                    if( -1 == read(stfd,rtc_status,2048) ) {
                        logmsg(LOG_ERR,"Cannot read RTC status ( %d : %s)",
                               errno,strerror(errno));
                        *rtc_status = '\0';
                    }

                    snprintf(subj,255,"Server %s shutdown until %s",hname,ctime(&nextrec));
                    snprintf(body,2048,
                             "Server %s shutdown until %s\n"
                             "Next recording: '%s'\n\n"
                             "RTC Status:\n%s\n",
                             hname,ctime(&nextrec),recs[REC_IDX(nextrec_video, nextrec_idx)]->title,rtc_status);

                    send_mail(subj,daemon_email_from, send_mailaddress,body);
                    logmsg(LOG_DEBUG,"Sent shutdown mail.");
                    sleep(5); // Make sure the email gets sent before we take down the server

                } else {
                    logmsg(LOG_ERR,"Cannot read hostname for shutdown email. No shutdown email sent!");
                }

            }

            int ret = system(cmd);
            if( ret==-1 || WEXITSTATUS(ret)) {
                logmsg(LOG_ERR,"Could not execute shutdown script ( %d : %s) ",errno, strerror(errno));
            }

            // Wait for shutdown
            sleep(5);
            
        } else {
            logmsg(LOG_DEBUG,"One or more of the conditions not fullfilled. Aborting automatic shudown");
        }
    }

    pthread_mutex_unlock(&recs_mutex);



}

/* EOF */
