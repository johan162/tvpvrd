/* =========================================================================
 * File:        TVSHUTDOWN.C
 * Description: Routines for handling of automatic server shutdown
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009-2014 Johan Persson
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

/**
 * Set the real time clock alarm on the server to wake up at a later time
 * @param alarmtime
 * @return 0 on success , -1 on failure
 */
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

/**
 * Extract the number of ongoing recordings
 * @return The number of recordings in progress
 */
int
get_num_ongoing_recordings(void) {
    int cnt=0;
    for (unsigned i = 0; i < (unsigned)max_video; i++) {
        if (ongoing_recs[i]) {
            cnt++;
        }
    }
    return cnt;
}

/**
 * Get the number of users logged in to the machine
 * @return Number of logged in users
 */
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
        const int len = 1048;
        char *reply = calloc(1,len);
        if( NULL == reply ) {
            logmsg(LOG_ERR, "get_num_users() : Cannot allocate memory.");
            return -1;
        }

        int nr = fread(reply, sizeof(char), len, fp);
        pclose(fp);
        if( nr > 0 ) {
            // Get rid of final '\n'
            if( reply[nr-1] == '\n' )
                reply[nr-1] = '\0';
        } else  {
            logmsg(LOG_ERR, "Cannot read reply from command to get number of users on server, nr=%d",nr);
            free(reply);
            return -1;
        }
        reply[nr]='\0';
        nr = xatoi(reply);
        free(reply);
        return nr;
    }
}

/**
 * Do the real shutdown of the server by executing a the shutdown shell script
 */
void
do_shutdown(void) {

    char cmd[256];
    logmsg(LOG_DEBUG, "Starting shutdown. Checking for shutdown script ...");
    snprintf(cmd, sizeof(cmd)-1, "%s/tvpvrd/shellscript/%s", CONFDIR, shutdown_script);
    if ( 0 == access(cmd, F_OK) ) {
        snprintf(cmd, sizeof (cmd) - 1, "%s/tvpvrd/shellscript/%s -t %d", CONFDIR, shutdown_script, shutdown_time_delay);
        logmsg(LOG_DEBUG, "Executing shutdown script: '%s'", cmd);
        int ret = system(cmd);
        if (-1 == ret || WEXITSTATUS(ret)) {
            if( errno ) {
                logmsg(LOG_ERR, "Could not execute shutdown script ( %d : %s) ", errno, strerror(errno));
            } else {
                logmsg(LOG_ERR, "Could not execute shutdown script");
            }
        }
    } else {
        logmsg(LOG_DEBUG, "Cannot access shutdown script: '%s'", cmd);
    }

    // To let the daemon know that the server was automatically shut down we add an indicator
    // file (empty) in the data directory
    snprintf(cmd, 255, "touch %s/%s", datadir, DEFAULT_AUTOSHUTDOWN_INDICATOR);
    int ret = system(cmd);
    if (-1 == ret || WEXITSTATUS(ret)) {
        if (errno) {
            logmsg(LOG_NOTICE, "Could not create autoshutdown indicator '%s' ( %d : %s)", cmd, errno, strerror(errno));
        } else {
            logmsg(LOG_NOTICE, "Could not create autoshutdown indicator '%s'", cmd);
        }
    } else {
        logmsg(LOG_DEBUG, "Created autoshutdown indicator with '%s'", cmd);
    }

    // Now wait for the SIGHUP signal we will receive when the server goes down
    sleep(10+shutdown_time_delay);

    // Normally we would not reach this point since we would have received the
    // shutdown signal while sleeping.

    logmsg(LOG_DEBUG, "Trying to shutdown directly. System going down in 1 minute");
    ret = system("shutdown -h +1");
    logmsg(LOG_DEBUG, "rc=%d for shutdown() command.", ret);

    // Now wait for the SIGHUP signal we will receive when the server goes down
    sleep(80);

    // We are out of luck. Nothing else to do!
    logmsg(LOG_CRIT, "Unable to shutdown server!!");

}

void
send_shutdown_mail(struct recording_entry *nextrec, time_t nextrec_ts) {

    if (shutdown_send_mail) {

        logmsg(LOG_DEBUG, "Preparing to send shutdown mail ...");
        const size_t BUFFLEN = 255;
        const size_t BUFFLEN_LONG = 2048;
        char subj[BUFFLEN], timebuff[BUFFLEN], timebuff_now[BUFFLEN], timebuff_rec_st[BUFFLEN], 
             timebuff_rec_en[BUFFLEN],hname[BUFFLEN];
        char *body = calloc(1, BUFFLEN_LONG);

        time_t now = time(NULL);
        ctime_r(&now, timebuff_now);
        if ('\n' == timebuff_now[strlen(timebuff_now) - 1])
            timebuff_now[strlen(timebuff_now) - 1] = '\0'; // Remove trailing "\n"

        if (gethostname(hname, BUFFLEN)) {
            logmsg(LOG_ERR, "Cannot read hostname for shutdown email.");
            strcpy(hname, "UNKNOWN");
        }

        size_t const maxkeys = 16;
        struct keypairs *keys = new_keypairlist(maxkeys);
        size_t ki = 0;

        if (nextrec && nextrec_ts) {
            char *rtc_status = calloc(1, BUFFLEN_LONG);
            struct tm tm_start, tm_end;
            CLEAR(tm_start);
            CLEAR(tm_end);

            ctime_r(&nextrec_ts, timebuff);
            if (timebuff[strlen(timebuff) - 1] == '\n')
                timebuff[strlen(timebuff) - 1] = '\0'; // Remove trailing "\n"
            snprintf(subj, BUFFLEN, "TVPVRD: Server %s shutdown until %s", hname, timebuff);

            // The recording is not actually started until after the startup time
            nextrec_ts += shutdown_pre_startup_time;

            localtime_r(&nextrec_ts, &tm_start);
            localtime_r(&nextrec->ts_end, &tm_end);
            strftime(timebuff_rec_st, BUFFLEN, "%H:%M", &tm_start);
            strftime(timebuff_rec_en, BUFFLEN, "%H:%M", &tm_end);

            add_keypair(keys, maxkeys, "SERVER", hname, &ki);
            add_keypair(keys, maxkeys, "WAKEUPTIME", timebuff, &ki);
            add_keypair(keys, maxkeys, "TIME", timebuff_now, &ki);
            add_keypair(keys, maxkeys, "NEXTREC", nextrec->title, &ki);
            add_keypair(keys, maxkeys, "NEXTRECTIME_START", timebuff_rec_st, &ki);
            add_keypair(keys, maxkeys, "NEXTRECTIME_END", timebuff_rec_en, &ki);
            add_keypair(keys, maxkeys, "NEXTREC_CHANNEL", nextrec->channel, &ki);

            if (nextrec && verbose_log >= 4) {
                logmsg(LOG_DEBUG, "Opening RTC_DEVICE ...");
                int stfd = open(RTC_STATUS_DEVICE, O_RDONLY);
                if( stfd >= 0 ) {
                    if (-1 == read(stfd, rtc_status, BUFFLEN_LONG)) {
                        logmsg(LOG_ERR, "Cannot read RTC status ( %d : %s)", errno, strerror(errno));
                        *rtc_status = '\0';
                    }
                    close(stfd);
                    add_keypair(keys, maxkeys, "RTC_STATUS", rtc_status, &ki);
                } else {
                    logmsg(LOG_ERR, "Cannot open RTC device ( %d : %s)", errno, strerror(errno));
                }
            } else if (nextrec) {
                add_keypair(keys, maxkeys, "RTC_STATUS", "", &ki);
            }
            free(rtc_status);

        } else {
            snprintf(subj, BUFFLEN, "TVPVRD: Server %s shutdown. No more scheduled recordings.", hname);
            add_keypair(keys, maxkeys, "SERVER", hname, &ki);
            add_keypair(keys, maxkeys, "WAKEUPTIME", "(none set)", &ki);
            add_keypair(keys, maxkeys, "TIME", timebuff_now, &ki);
            add_keypair(keys, maxkeys, "NEXTREC", "", &ki);
            add_keypair(keys, maxkeys, "NEXTRECTIME_START", "", &ki);
            add_keypair(keys, maxkeys, "NEXTRECTIME_END", "", &ki);
            add_keypair(keys, maxkeys, "NEXTREC_CHANNEL", "", &ki);
            add_keypair(keys, maxkeys, "RTC_STATUS", "", &ki);
        }

        logmsg(LOG_DEBUG, "Sending shutdown mail ...");
        if (-1 == send_mail_template(subj, daemon_email_from, send_mailaddress, "mail_shutdown", keys, ki)) {
            logmsg(LOG_ERR, "Failed to send mail using template \"mail_shutdown\"");
        } else {
            logmsg(LOG_DEBUG, "Successfully sent mail using template \"mail_shutdown\"!");
        }

        free_keypairlist(keys, ki);
        sleep(5); // Make sure the email gets sent before we take down the server


        free(body);
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

    // Now we need to find the next closest recording among all video cards
    // to know when to wake up
    struct recording_entry *nextrec;
    int nextrec_video;
    time_t nextrec_ts;

    pthread_mutex_lock(&recs_mutex);
    int ret=get_nextsched_rec(&nextrec, &nextrec_video, &nextrec_ts);
    pthread_mutex_unlock(&recs_mutex);

    // We need the current time to compare against
    time_t now = time(NULL);

    // After this loop 'nextrec' holds the timestamp for the next recording assuming
    // nextrec_video > -1. If nextrec_video==-1 then there are no future recordings
    // scheduled at all.
    // This is an abnormal case since we would then go to sleep forever and never
    // wake up again. We handle this by aborting the shutdown if there are no more
    // recordings to be made.
    if( !shutdown_no_recordings && (-1==ret || (time_t)0 == nextrec_ts || NULL == nextrec) ) {
        logmsg(LOG_DEBUG,"Automatic shutdown aborted. No future scheduled recordings.");
        return;
    }

    // Before shutting down we need to also check that shutting us down will allow
    // us to be turned off for at least as long as the minimum shutdown time
    if( 0==nextrec_ts || (nextrec_ts - now > shutdown_min_time+(time_t)shutdown_pre_startup_time) ) {
        // Yes we could potentially shutdown. Now check that
        // 1) No ongoing recordings
        // 2) No ongoing transcodings
        // 4) The server load is not too high (indicating other important work)

        //logmsg(LOG_DEBUG,"Delta time to next recording %d min (minimum time is %d min)",(nextrec-now)/60,(shutdown_min_time+(time_t)shutdown_pre_startup_time)/60);
        //logmsg(LOG_DEBUG,"(shutdown_min_time=%d sec, shutdown_pre_startup_time=%d sec)",shutdown_min_time,shutdown_pre_startup_time);

        float avg1, avg5, avg15;
        getsysload(&avg1, &avg5, &avg15);
        if( avg5 < shutdown_max_5load &&
            get_num_ongoing_transcodings() == 0 &&
            get_num_ongoing_recordings() == 0 ) {

            if( nextrec_ts > 0 ) {
                nextrec_ts -= shutdown_pre_startup_time;
                if( -1 == set_rtc_alarm(nextrec_ts) ) {
                    logmsg(LOG_ERR,"Automatic shutdown disabled since the wakeup alarm cannot be set.");
                } else {
                    logmsg(LOG_DEBUG,"Scheduled wakeup at ts=%u",(unsigned)nextrec_ts);                    
                }
            } else {
                logmsg(LOG_DEBUG,"NO scheduled wakeup time (no scheduled recordings)");
            }
            send_shutdown_mail(nextrec,nextrec_ts);                
            logmsg(LOG_DEBUG,"Initiating automatic shutdown");
            do_shutdown();                
            
        } else {
            logmsg(LOG_DEBUG,"Aborting automatic shutdown. One or more of the conditions not fulfilled.");
        }
    } else {
        // Comment out this too verbose setting.
        //logmsg(LOG_DEBUG,"Aborting automatic shutdown. Too short off time (%d min)",(nextrec_ts-now)/60);
        logmsg(LOG_DEBUG,"Aborting automatic shutdown. Too short off time");
    }

}

/* EOF */
