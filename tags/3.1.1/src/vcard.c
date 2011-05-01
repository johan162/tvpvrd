/* =========================================================================
 * File:        VCARD.C
 * Description: Some utility functions to setup the video card in preparation
 *              for capture
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
// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h>

#include "config.h"

#include "utils.h"
#include "tvpvrd.h"
#include "tvconfig.h"
#include "stats.h"
#include "transc.h"
#include "recs.h"
#include "vctrl.h"
#include "vcard.h"
#include "freqmap.h"
#include "tvplog.h"

/**
 * Set the video encoding HW parameters on the video card from the values specified in the
 * supplied profile
 * @param fd
 * @param profile
 * @return 0 on success, -1 on failure
 */
int
setup_hw_parameters(int fd, struct transcoding_profile_entry *profile) {
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
            "HW parameters using fd=%d set. Profile='%s' ["
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

/**
 * Open the video device and tune it to the correct channels for the
 * next recording. The recording to be dealt with is stored in the structure
 * ongoing_rec[] for the specified video.
 *
 * @param video
 * @param profile
 * @return
 */
int
setup_video(unsigned video,struct transcoding_profile_entry *profile) {
#ifndef DEBUG_SIMULATE
    char infobuff[256];
    int fdtuner = -1;

    logmsg(LOG_DEBUG, "setup_video() for video=%d",video);

    int fd = video_open(video,FALSE);
    if( fd == -1 ) {
        return -1;
    }

    if( tuner_devices[video] ) {
        fdtuner = video_open(video,TRUE);
        if (fdtuner == -1) {
            logmsg(LOG_ERR, "Cannot open video tuner device '%s' ( %d : %s )",
                   tuner_devices[video], errno, strerror(errno));
            video_close(fd);
            return -1;
        }

    } else {

        fdtuner = fd;
    }

#endif
    // Give the driver some breathing room after we open the device
    // and until we start changing the settings.
    usleep(500000);

    if( external_switch ) {

        logmsg(LOG_DEBUG,"setup_video(): Using external channel switching.");

#ifndef DEBUG_SIMULATE
        video_set_input_source(fdtuner,external_input);
#endif
        char csname[128];
        snprintf(csname,128,"%s/tvpvrd/%s",CONFDIR,external_switch_script);
        int csfd = open(csname,O_RDONLY) ;
        if( csfd == -1 ) {
            logmsg(LOG_CRIT,"FATAL: Cannot open channel switch script '%s' ( %d : %s )",csname,errno,strerror(errno));
#ifndef DEBUG_SIMULATE
            video_close(fd);
            if( fdtuner != fd && fdtuner > 0 ) {
                video_close(fdtuner);
            }
#endif
            return -1;
        }
        char cmd[255];
        snprintf(cmd,255,"%s -s %s > /dev/null 2>&1",csname,ongoing_recs[video]->channel);
        logmsg(LOG_DEBUG,"setup_video(): Running external channel switching cmd '%s'",cmd);
        int rc = system(cmd);
        if( rc==-1 || WEXITSTATUS(rc)) {
            logmsg(LOG_CRIT,"FATAL: Channel switch script ended with error code : %d ",WEXITSTATUS(rc));
#ifndef DEBUG_SIMULATE
            video_close(fd);
            if( fdtuner != fd && fdtuner > 0 ) {
                video_close(fdtuner);
            }
#endif
            return -1;
        }
    } else {
#ifdef DEBUG_SIMULATE
        logmsg(LOG_DEBUG,"Simulating channel switch to %s",ongoing_recs[video]->channel);
#else
        int ret,i=2;
        ret = video_set_channel(fdtuner, ongoing_recs[video]->channel);
        while( ret == -1 && errno == EBUSY && i > 0 ) {
            usleep((unsigned)(500*(3-i)));
            ret = video_set_channel(fdtuner, ongoing_recs[video]->channel);
            i--;
        }

        if( ret == -1 ) {
            video_close(fd);
            if( fdtuner != fd && fdtuner > 0 ) {
                video_close(fdtuner);
            }

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
                     "Tuner #%02d (fd=%d) set to channel '%s' @ %.3fMHz",
                     video, fdtuner, ongoing_recs[video]->channel, freq/1000000.0
            );

        }
        logmsg(LOG_DEBUG,infobuff);

        if( allow_profiles_adj_encoder ) {
            logmsg(LOG_DEBUG,"setup_video(): Adjusting HW encoder params for fd=%d, profile '%s'",
                   fd,profile->name);
            if( -1 == setup_hw_parameters(fd, profile) ) {
                video_close(fd);
                if( fdtuner != fd && fdtuner > 0 ) {
                    video_close(fdtuner);
                }

                return -1;
            }
        }
#endif
    }
#ifndef DEBUG_SIMULATE
    if( fdtuner != fd && fdtuner > 0 ) {
        video_close(fdtuner);
    }
    return fd;

#else
    return 0;
#endif
}

/**
 * Set the initial parameters for the TV-card so we know that they exist
 * and have a known state in case the profiles are not allowed to change
 * HW parameters.
 */
void
setup_capture_cards(void) {

    // If each profile is allowed to set the HW encoding parameters then we
    // do not need to do anything here.
    if( !allow_profiles_adj_encoder ) {
        struct transcoding_profile_entry *profile;
        get_transcoding_profile(default_transcoding_profile,&profile);
        for(unsigned video=0; video < max_video; video++) {
            int fd = video_open(video,FALSE);
            int ret = setup_hw_parameters(fd,profile);
            video_close(fd);
            if( -1 == ret  ) {
                // Nothing else to do than to quit
                logmsg(LOG_ERR,"Fatal error. Cannot initialize HW capture card(s) ( %d : %s )", errno, strerror(errno));
                _exit(EXIT_FAILURE);
            }
        }
    }
}
