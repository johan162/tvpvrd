/* =========================================================================
 * File:        TVCONFIG.C
 * Description: Read and handle config settings from the ini file
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
#include <syslog.h>
#include <errno.h>

#include "tvconfig.h"
#include "tvpvrd.h"
#include "utils.h"
#include "tvxmldb.h"
#include "freqmap.h"
#include "vctrl.h"
#include "transc.h"


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
int allow_profiles_adj_encoder = 0;


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
 * SHOuld we switch channel via an external script
 */
int external_switch = 0;

/*
 * Which video input sohuld we read from when we use external channel switching
 */
int external_input = 0;

/*
 * Name of external channel switching script
 */
char external_switch_script[255];

/*
 * Holds user specified optional encoder_devices and tuner_devices
 */
char *encoder_devices[16] = {NULL};
char *tuner_devices[16] = {NULL};

/*
 * Name of optional post recording script
 */
char *postrec_script;
int use_postrec_processing=FALSE;

/*
 * Password settings. The server offer a basic authentication, The
 * authentication is disabled by default.
 */
int require_password = REQUIRE_PASSWORD;
char password[32];
char web_password[32];
char web_user[32];
int require_web_password;
int weblogin_timeout;


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

    external_switch   = iniparser_getboolean(dict, "config:external_switch", DEFAULT_EXTERNAL_SWITCH);

    external_input   = validate(0, 7,"external_input",
                                   iniparser_getint(dict, "config:external_input", DEFAULT_EXTERNAL_INPUT));

    strncpy(external_switch_script,
            iniparser_getstring(dict, "config:external_switch_script", DEFAULT_EXTERNAL_SWITCH_SCRIPT),
            254);
    external_switch_script[254] = '\0';


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

    max_video = (unsigned)validate(0, 5,"max_video",
                                   iniparser_getint(dict, "config:max_video", MAX_VIDEO));

    if( 0 == max_video ) {
        // Automatically determine the maximum number of cards
#ifdef DEBUG_SIMULATE
        max_video = 1;
#else
        max_video = (unsigned)_vctrl_getnumcards();
#endif
    }

    postrec_script = strdup(iniparser_getstring(dict, "config:postrec_processing_script", ""));
    use_postrec_processing = iniparser_getboolean(dict, "config:use_postrec_processing", FALSE);

    // Try to read explicitely specified encoder devices
    for(unsigned int i=0; i < max_video && i<16; ++i) {
        char encoderdevice[64];
        char tmpdevicename[128];
        snprintf(encoderdevice,64,"config:encoder_device%d",i);
        strncpy(tmpdevicename, iniparser_getstring(dict, encoderdevice, ""), 127);
        tmpdevicename[127] = '\0';
        if( *tmpdevicename ) {
            logmsg(LOG_DEBUG,"Found encoder_device%d=%s in config",i,tmpdevicename);
            encoder_devices[i] = strdup(tmpdevicename);
        } else {
            encoder_devices[i] = NULL;
        }
    }

    // Try to read explicitely specified tuner devices
    for(unsigned int i=0; i < max_video && i<16; ++i) {
        char tunerdevice[64];
        char tmpdevicename[128];
        snprintf(tunerdevice,64,"config:tuner_device%d",i);
        strncpy(tmpdevicename, iniparser_getstring(dict, tunerdevice, ""), 127);
        tmpdevicename[127] = '\0';
        if( *tmpdevicename ) {
            logmsg(LOG_DEBUG,"Found tuner_device%d=%s in config",i,tmpdevicename);
            tuner_devices[i] = strdup(tmpdevicename);
        } else {
            tuner_devices[i] = NULL;
        }
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
            int vh = video_open(i,TRUE);
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


/* EOF */