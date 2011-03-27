/* =========================================================================
 * File:        TVSHUTDOWN.C
 * Description: Routines for handling of automatic server shutdown
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010,2011 Johan Persson
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
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h>
#include <pthread.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "tvshutdown.h"
#include "recs.h"
#include "utils.h"
#include "stats.h"
#include "transc.h"
#include "rkey.h"
#include "mailutil.h"
#include "xstr.h"
#include "tvplog.h"

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
        nr = xatoi(reply);
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
        logmsg(LOG_DEBUG,"Aborting automatic shutdown. Server has not been powered on long enough to initiate a shutdown.");
        return;
    }

    size_t numusers = get_num_users();
    if( ! shutdown_ignore_users && (0 < numusers) ) {
        // If we are asked to have no user logged in before a shutdown
        // we have no other choice than to abort shutdown sequence.
        logmsg(LOG_DEBUG,"Aborting automatic shutdown, %d users connected to server.",numusers);
        return;
    }

    // Now we need to find the next closest recording amoung all video cards
    // to know when to wake up
    pthread_mutex_lock(&recs_mutex);
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
    pthread_mutex_unlock(&recs_mutex);
   
    // We need the current time to compare against
    time_t now = time(NULL);

    // Before shutting down we need to also check that shutting us down will allow
    // us to be turned off for at least as long as the minimum shutdown time
    if( nextrec - now > shutdown_min_time+(time_t)shutdown_pre_startup_time ) {
        // Yes we could potentially shutdown. Now check that
        // 1) No ongoing recordings
        // 2) No ongoing transcodings
        // 4) The server load is not too high (indicating other important work)

        logmsg(LOG_DEBUG,"Delta time to next recording %d min (minimum time is %d min)",(nextrec-now)/60,(shutdown_min_time+(time_t)shutdown_pre_startup_time)/60);
        logmsg(LOG_DEBUG,"(shutdown_min_time=%d sec, shutdown_pre_startup_time=%d sec)",shutdown_min_time,shutdown_pre_startup_time);
        
        float avg1, avg5, avg15;
        getsysload(&avg1, &avg5, &avg15);
        if( avg5 < shutdown_max_5load &&
            get_num_ongoing_transcodings() == 0 &&
            get_num_ongoing_recordings() == 0 ) {

            // Set RTC alarm so we will wake up a bit before the recording time
            nextrec -= shutdown_pre_startup_time;
            if( -1 == set_rtc_alarm(nextrec) ) {
                logmsg(LOG_ERR,"Automatic shutdown disabled since the wakeup alarm cannot be set.");
            } else {

                logmsg(LOG_DEBUG,"Initiating automatic shutdown");

                if( shutdown_send_mail ) {
                    char subj[255];
                    char timebuff[255],timebuff_now[255],timebuff_rec_st[255],timebuff_rec_en[255];
                    char *body = calloc(1,2048);
                    char *rtc_status = calloc(1,2048);
                    char hname[255];
                    if( 0 == gethostname(hname,255) ) {
                        size_t const maxkeys=16;
                        struct keypairs *keys = new_keypairlist(maxkeys);
                        size_t ki = 0 ;
                        struct tm tm_start, tm_end;

                        time_t now = time(NULL)+5;
                        ctime_r(&now,timebuff_now);
                        if( timebuff_now[strlen(timebuff_now)-1] == '\n')
                            timebuff_now[strlen(timebuff_now)-1]='\0'; // Remove trailing "\n"

                        ctime_r(&nextrec,timebuff);
                        if( timebuff[strlen(timebuff)-1] == '\n')
                            timebuff[strlen(timebuff)-1]='\0'; // Remove trailing "\n"
                        snprintf(subj,255,"Server %s shutdown until %s",hname,timebuff);

                        localtime_r(&recs[REC_IDX(nextrec_video, nextrec_idx)]->ts_start,&tm_start);
                        localtime_r(&recs[REC_IDX(nextrec_video, nextrec_idx)]->ts_end,&tm_end);
                        strftime(timebuff_rec_st,255,"%H:%M",&tm_start);
                        strftime(timebuff_rec_en,255,"%H:%M",&tm_end);

                        add_keypair(keys,maxkeys,"SERVER",hname,&ki);
                        add_keypair(keys,maxkeys,"WAKEUPTIME",timebuff,&ki);
                        add_keypair(keys,maxkeys,"TIME",timebuff_now,&ki);
                        add_keypair(keys,maxkeys,"NEXTREC",recs[REC_IDX(nextrec_video, nextrec_idx)]->title,&ki);
                        add_keypair(keys,maxkeys,"NEXTRECTIME_START",timebuff_rec_st,&ki);
                        add_keypair(keys,maxkeys,"NEXTRECTIME_END",timebuff_rec_en,&ki);
                        add_keypair(keys,maxkeys,"NEXTREC_CHANNEL",recs[REC_IDX(nextrec_video, nextrec_idx)]->channel,&ki);

                        if( verbose_log == 3 ) {
                            int stfd = open(RTC_STATUS_DEVICE,O_RDONLY);
                            if( -1 == read(stfd,rtc_status,2048) ) {
                                logmsg(LOG_ERR,"Cannot read RTC status ( %d : %s)",errno,strerror(errno));
                                *rtc_status = '\0';
                            }
                            close(stfd);
                            add_keypair(keys,maxkeys,"RTC_STATUS",rtc_status,&ki);
                        } else {
                            add_keypair(keys,maxkeys,"RTC_STATUS","",&ki);
                        }

                        if( -1 == send_mail_template(subj, daemon_email_from, send_mailaddress, "mail_shutdown", keys, ki) ) {
                            logmsg(LOG_ERR,"Failed to send mail using template \"mail_shutdown\"");
                        } else {
                            logmsg(LOG_DEBUG,"Sucessfully sent mail using template \"mail_shutdown\"!");
                        }

                        free_keypairlist(keys,ki);
                        sleep(5); // Make sure the email gets sent before we take down the server

                    } else {
                        logmsg(LOG_ERR,"Cannot read hostname for shutdown email. No shutdown email sent!");
                    }
                    free(body);
                    free(rtc_status);

                }

                // Call shutdown script
                char cmd[256] ;
                snprintf(cmd,255,"%s/tvpvrd/%s -t %d",CONFDIR,shutdown_script,shutdown_time_delay);
                logmsg(LOG_DEBUG,"Executing shutdown script: '%s'",cmd);
                int ret = system(cmd);
                if( -1 == ret || WEXITSTATUS(ret)) {
                    logmsg(LOG_ERR,"Could not execute shutdown script ( %d : %s) ",errno, strerror(errno));
                }

                snprintf(cmd,255,"touch %s/%s",datadir,DEFAULT_AUTOSHUTDOWN_INDICATOR);
                ret = system(cmd);
                if( -1==ret || WEXITSTATUS(ret)) {
                    logmsg(LOG_NOTICE,"Could not create autoshutdown indicator '%s' ( %d : %s) ",cmd,errno, strerror(errno));
                } else {
                    logmsg(LOG_DEBUG,"Created autoshutdown indicator with '%s'",cmd);
                }

                // Now wait for the SIGHUP signal we will receive when the server goes down
                sleep(15);

                // This should never happen !!
                logmsg(LOG_ERR,"Automatic shutdown failed. SIGHUP (or SIGKILL) was not received.");
                logmsg(LOG_DEBUG,"Trying to call shutdown directly.");
                int rc = system("shutdown now");
                logmsg(LOG_DEBUG,"rc=%d for shutdown() command.",rc);

                sleep(10);
                logmsg(LOG_ERR,"Unable to shutdown server!!");

            }
            
        } else {
            logmsg(LOG_DEBUG,"Aborting automatic shutdown. One or more of the conditions not fullfilled.");
        }
    } else {
      logmsg(LOG_DEBUG,"Aborting automatic shutdown. Too short off time (%d min)",(nextrec-now)/60);
    }

}

/* EOF */
