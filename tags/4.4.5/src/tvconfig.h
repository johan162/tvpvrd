/* =========================================================================
 * File:        TVCONFIG.H
 * Description: Read and handle config settings from the ini file.
 *              The header files defines the available variables as read
 *              from the inifile.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
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

#ifndef TVCONFIG_H
#define	TVCONFIG_H

#ifdef	__cplusplus
extern "C" {
#endif

/* The iniparser library used is dependent on the configuration. It can either be the
 * system wide existing library or the daemon can use an internal version. Hence we need
 * to adjust which ini file we will be reading. The controlling variable is defined in the
 * generated config.h file.
 */
#ifdef HAVE_LIBINIPARSER
#include <iniparser.h>
#else
#include "libiniparser/iniparser.h"
#endif

/*
 * All defines below gives the default values for various ini file settings
 * which are used in the case that the setting wasn't found in the ini-file.
 */

/*
 * INIFILE_NAME string
 * Name and path of inifile
 */
#define INIFILE_NAME "tvpvrd.conf"

/*
 * DEFAULT_DATADIR string
 * Deafult directory where video files will be stored if no directory path
 * is specified with the filenames
 */
#define DEFAULT_DATADIR "/data/pvr/"

/*
 * XMLDBFILE_NAME
 * Name of XML database file where all recordings are stored
 * This is stored in the xmldb directory under the datadir root
 *
 */
#define XMLDBFILE_NAME "tvpvrd_db.xml"

/*
 * DEFAULT_DAEMONIZE bool
 * If we should run as a daemon, this setting is the default if not specified on the command
 * line or in the ini-filename
 */
#define DEFAULT_DAEMONIZE 1

/*
 * DEFAULT_USERNAME string
 * What user should the server be running as
 */
#define DEFAULT_USERNAME "tvpvrd"

/*
 * MASTER_SERVER boolean
 * Should the server run as a master or slave by default
 */
#define MASTER_SERVER 1

/*
 * PORT integer
 * The port used to communicate with the server unless otherwise specified
 * in the main configuration.
 */
#define PORT 9300

/*
 * ENABLE_WEBINTERFACE boolean
 * The server can provoide a very rudimentary WEb-interface which only
 * allows non-modifying commands. This interface is available on PORT+1,
 * i.e. on 9301 with the default settings.
 */
#define ENABLE_WEBINTERFACE 0

/*
 * REQUIRE_WEB_PASSWORD int
 * Should the WEB-interface require logon
*/
#define REQUIRE_WEB_PASSWORD 0
#define WEB_USER ""
#define WEB_PASSWORD ""
#define WEBLOGIN_TIMEOUT 0

/*
 * DEFAULT_WEB_THEME string
 * Theme for the Web-interface
 */
#define DEFAULT_WEB_THEME "night"

/*
 * DEFAULT_THEME_SELECT boolean
 * Should theme selection dropdown box be displayed on Web-interface
 */
#define DEFAULT_THEME_SELECT 1

/*
 * DEFAULT_DISPLAY_WEB_QADD boolean
 * Should the web interface display the quick recording command module
 */
#define DEFAULT_DISPLAY_WEB_QADD 1

#define DEFAULT_AUTODISPLAY_TRANSC 0

/*
 * DEFAULT_DISPLAY_ADVANCED_REC_CONTROL boolean
 * Decide if the advanced recording controls should be displayed in the WEB-UI
 */
#define DEFAULT_DISPLAY_ADVANCED_REC_CONTROL 0

/*
 * Note: All the defines for general settings are read from an inifile
 * If the ini file is missing some values then the defines below will be used
 * as the default value.
 */

/*
 * MAX_VIDEO integer
 * The number of available video streams. This is usually the same as the number
 * of TV-Cards available in the system since most TV-Cards only have one MPEG encoder
 */
#define MAX_VIDEO 2

/*
 * REQUIRE_PASSWORD boolean
 * Require password in order to connect to server
 */
#define REQUIRE_PASSWORD 0

/*
 * MAX_ENTRIES integer
 * The maximum number of pending entries we can have per video stream
 */
#define MAX_ENTRIES 512

/*
 * TIME_RESOLUTION integer
 * This time determnes how often (in seconds) the daemon wakes up to check the pending list
 * of recordings if a recording is about to be started. In ractice this means that we can
 * be up to TIME_RESOLUTION s before the specified time when the recording starts. A loweer
 * value means better accuracy but higher system load. The default 3 seconds should be ok
 * for most circumstances.
 */
#define TIME_RESOLUTION 3

/*
 * VIDEO_DEVICE_BASENAME string
 * Basename of video device. Each stream will be assumed accessible as
 * VIDEO_DEVICE_BASENAME<stream-number>, e.g. /dev/video0
 */
#define VIDEO_DEVICE_BASENAME "/dev/video"

/*
 * DEFAULT_USE_PROFILE_DIRECTORIES boolean
 * Determine if we should use a directory hierarchy based on profile names
 * to store the transcoded and encoded videos
 */
#define DEFAULT_USE_PROFILE_DIRECTORIES 1

/*
 * DEFAULT_USE_REPEAT_REC_BASEDIR boolean
 * Should we store all recurring recordings under the same base directory
 * named by the basename of the recording
 */
#define DEFAULT_USE_REPEAT_REC_BASEDIR 1

/*
 * DEFAULT_REPEAT_NAME_MANGLE_TYPE integer
 * Select default type of name mangling for series recording
 */
#define DEFAULT_REPEAT_NAME_MANGLE_TYPE 1

/*
 * MAX_CLIENTS integer
 * Max number of clients that are allowed to connect to us
 */
#define MAX_CLIENTS 5

/*
 * CLIENT_IDLE_TIME integer
 * The length (in seconds) before an inactive client is automatically
 * disconnected. Deafult 30 min.
 */
#define CLIENT_IDLE_TIME 30*60

/*
 * DEFAULT_DURATIONHOUR integer
 * DEFAULT_DURATIONMIN integer
 * Default length of a recording if not specified otherwise
 */
#define DEFAULT_DURATIONHOUR 0
#define DEFAULT_DURATIONMIN 59

/*
 * LOGFILE_NAME string
 * Optional logfile name (full path).
 */
#define LOGFILE_NAME "syslog"
#define LOGFILE_SYSLOG "syslog"

/*
 * SENDMAIL_ON_ERROR boolean
 * SENDMAIL_ON_TRANSCODE_END boolean
 * Should we send notification of serious error and transcode ending to a mail address?
 */
#define SENDMAIL_ON_ERROR 0
#define SENDMAIL_ON_TRANSCODE_END 0

/*
 * SEND_MAILADDRESS string
 * Mail address to use if we have enabled sending notification of serious error
 */
#define SEND_MAILADDRESS "root@localhost"

/*
 * LOCALE_NAME string
 * The default locale to use in case no locale is defined. This is important to get
 * right since otherwise the system() will not interpret the filenames correctly.
 */
#define LOCALE_NAME "en_US.UTF8"

/*
 * DEFAULT_FREQUENCY_MAP string
 * The name of the default frequency map to use.
 * Note: If the xawtvrc also specifes a map that map will
 * override this setting.
 */
#define DEFAULT_FREQUENCY_MAP "europe-west"

/*
 * DEFAULT_XAWTV_CHANNEL_FILE string
 * The name of the channel map for broadcasting names of channels
 */
#define DEFAULT_XAWTV_STATION_FILE "/etc/X11/xawtvrc"

/*
 * MAX_LOAD_FOR_TRANSCODING integer
 * Maximum server load allowed to start a transcoding
 */
#define MAX_LOAD_FOR_TRANSCODING 5

/**
 * Maximum length of frequency map name string
 */
#define MAX_FMAPNAME_LENGTH    32

/*
 * TUNER_INPUT_INDEX integer
 * This is the input index for the tuner on the capture card. A tuner card
 * normally has multiple inputs and we must know which input is the tuner.
 * This is normally found at index 0.
 */
#define DEFAULT_TUNER_INPUT_INDEX 0

/*
 * DEFAULT_EXTERNAL_SWITCH integer
 * Should we switch channel via an external script
 */
#define DEFAULT_EXTERNAL_SWITCH 0

/*
 * DEFAULT_EXTERNAL_INPUT integer
 * Which video input should we read from when we use external channel switching
 */
#define DEFAULT_EXTERNAL_INPUT 0

/*
 * DEFAULT_EXTERNAL_INPUT string
 * Name of external channel switching script
 */
#define DEFAULT_EXTERNAL_SWITCH_SCRIPT "switchchannel.sh"

/*
 * MAX_WAITING_TIME_TO_TRANSCODE integer
 * The maximum time in seconds that can be set to wait for the
 * server load to fall below MAX_LOAD_FOR_TRANSCODING
 * Note: In the default conf file this is set to 0 which means
 * we can wait indefinitely. The value here just indicates the
 * maximum deterministic time that can be set.
 * (Default value is 1 days)
 */
#define MAX_WAITING_TIME_TO_TRANSCODE 1*24*60*60

/*
 * FFMPEG_BIN string
 * Default binary for ffmpeg
 */
#define FFMPEG_BIN "/usr/bin/ffmpeg"

/*
 * VERBOSE_LOG bool
 * Should the log be more verbose
 */
#define VERBOSE_LOG 1

/*
 * The value of the following variables are read from the ini-file.
 * They hold various run time limits and settings that
 * the user can adjust. Some of these values can also be overridden by being
 * given as options when the daemon starts
 */
extern int use_profiledirectories ;

/*
 * Should we store all recurring recordings under the same base directory
 * named by the basename of the recording
 */
extern int use_repeat_rec_basedir ;

/*
 * Select default type of series recording title/filename name mangling
 */
extern int default_repeat_name_mangle_type ;

/*
 * xawtv_channel_file string
 * Name of station to channel mapping file
 */
extern char xawtv_channel_file[256];

/*
 * default_transcoding_profile string
 * Name of default transcoding profile
 */
extern char default_transcoding_profile[32];

/*
 * max_load_for_transcoding int
 * The maximum load allowed to start a new transcoding process
 */
extern int max_load_for_transcoding ;

/*
 * max_waiting_time_to_transcode int
 * The maximum time we allow the server to wait to start a
 * new transcoding process. The server will wait as long as the
 * load on the server is too high.
 */
extern int max_waiting_time_to_transcode ;

/*
 * ffmpeg_bin string
 * The full path to the ffmpeg executable
 */
extern char ffmpeg_bin[64];

/*
 * Terminal login related settings
 */
extern int require_password;
extern char password[];

/*
 * WEB-login related settings
 */
extern int require_web_password;
extern char web_password[];
extern char web_user[];
extern int weblogin_timeout;

/*
 * Theme to use in Web-interface
 */
extern char web_theme[32];

/*
 * Should the quick recording module be displayed in the web interface
 */
extern int web_display_qadd;

/*
 * Should ongoing transcoding only be displayed when there are any transcodings?
 */
extern int web_autodisplay_transc;

/*
 * Should the theme selector be displayed on web-interface
 */
extern int disp_theme_select;

/*
 * Decide if advanced recording controls (name mangling and start number) should be displayed
 */
extern int display_advanced_rec_control ;

// Are we running as master or slave
extern int is_master_server;

// Maximum sizes
extern unsigned max_entries, max_video, max_clients, max_idle_time;

// Default recording length if nothing else is specified
extern int defaultDurationHour, defaultDurationMin;

// TVP/IP Port to listen to
extern unsigned short int tcpip_port;

// Should we run as a daemon or not
extern int daemonize;

// What username are we running as
extern char username[];

// Logfile details
extern int verbose_log;
extern char logfile_name[];

// Time resolution for checks
extern unsigned time_resolution;

// The default base data diectory
extern char datadir[];

// Names of the ini file and the db file used
extern char inifile[], xmldbfile[];

// Base name of video device ("/dev/video")
extern char device_basename[];

// The name of the frequency map we are using. This is set at the startup and cannot
// be changed at runtime.
extern char frequencymap_name[];

// Should we send mail on errors?
extern int send_mail_on_error;

// Mail address to use
extern char send_mailaddress[];

// Should we send mail on when transcoding finished?
extern int send_mail_on_transcode_end ;

// Should we try to use HTML when sending mail from templates
extern int use_html_mail ;

// Should we use an SMTP server to send mail
extern int smtp_use ;

// Name of SMTP server
extern char smtp_server[] ;

// Name of SMTP user
extern char smtp_user[] ;

// Name of SMTP password
extern char smtp_pwd[] ;

// Alternative SMTP port
extern int smtp_port ;

// Name of locale to use (used to set LC_ALL)
extern char locale_name[];

// Should we allow connection to the WEB-interface?
extern int enable_webinterface;

/*
 * Parameters to control the WEB-login
 */
extern int require_web_password;
extern char web_password[];
extern char web_user[];
extern int weblogin_timeout;

/*
 * The input source index that corresponds to the tuner on the capture card.
 */
extern int tuner_input_index ;

/*
 *  Optional user specified encoder devices
 */
extern char *encoder_devices[] ;
extern char *tuner_devices[] ;

/*
 * Name of optional post recording script to run
 */
extern char *postrec_script;

/*
 * Handle to dictionary which holds all variables read from the inifile
 */
extern dictionary *dict;

extern int allow_profiles_adj_encoder ;
/*
 * The tuner input index on the card. This is necessaru so we know which
 * input to select that is the tuner.
 */
extern int tuner_input_index;

/*
 * Should we switch channel via an external script
 */
extern int external_switch ;

/*
 * Which video input should we read from when we use external channel switching
 */
extern int external_input ;

/*
 * Name of script to be called for doing external channel switching
 */
extern char external_switch_script[];

/*
 * The station to be used when the external input is connected to the tuner
 */
extern char external_tuner_station[];

/*
 * Holds user specified optional encoder_devices and tuner_devices
 */
extern char *encoder_devices[] ;
extern char *tuner_devices[] ;

/*
 * Name of optional post recording script
 */
extern char *postrec_script;
extern int use_postrec_processing;

/*
 * Name of optional post transcoding script
 */
extern char *posttransc_script;
extern int use_posttransc_processing;

/*
 * Determine if we should use mobile Phone web-interface when phone is detected
 */
extern int use_mobile;
#define USE_MOBILE 1

/**
 * Startup script name
 */
#define DEFAULT_STARTUP_SCRIPT "startup.sh"


/**
 * Name of empty file stored in the data directory to indicate that the
 * daemon was last shutdown automatically. Used as information in the
 * startup script.
 */
#define DEFAULT_AUTOSHUTDOWN_INDICATOR "autoshutdown"

/**
 * Shutdown parameters
 */
#define DEFAULT_SHUTDOWN_SCRIPT "shutdown.sh"
#define DEFAULT_SHUTDOWN_ENABLE 0
#define DEFAULT_SHUTDOWN_MIN_TIME (30*60)
#define DEFAULT_SHUTDOWN_MAX_5LOAD 1.0
#define DEFAULT_SHUTDOWN_IGNORE_USERS 0
#define DEFAULT_SHUTDOWN_TIME_DELAY 0
#define DEFAULT_SHUTDOWN_MIN_UPTIME 15*60
#define DEFAULT_SHUTDOWN_SENDMAIL 0
#define DEFAULT_SHUTDOWN_PRE_STARTUP_TIME 180
#define DEFAULT_SHUTDOWN_NO_RECORDINGS 1

extern time_t shutdown_min_time ;
extern char shutdown_script[128] ;
extern float shutdown_max_5load ;
extern unsigned shutdown_enable ;
extern unsigned shutdown_ignore_users ;
extern unsigned shutdown_time_delay ;
extern unsigned shutdown_min_uptime ;
extern unsigned shutdown_send_mail ;
extern unsigned shutdown_pre_startup_time ;

extern char daemon_email_from[] ;
extern unsigned shutdown_no_recordings ;

/**
 * Setup the dictionary file (ini-file) name. Check if it is specified on
 * the command line otherwise check common locations.
 */
void
setup_inifile(void);

/**
 * Get common master values from the ini file
 * Since iniparser is not reentrant we must do it here and not individually
 * in each thread. Since all of these are read only there is no need to
 * protect these with a mutex
 */
void
read_inisettings(void);


/*
 * CSS formats for  HTML mail with list of recordings. Eventually these layout should be moved to an inifile
 * located in the config directory. But this is not a high priority right now since we expect very few people
 * to bother to change this very frequently.
 */
#define TBLCSS_VBORDER_INTERIOR "1px solid lightgray"
#define TBLCSS_VBORDER_OUTER "2px solid black"
#define TBLCSS_HBORDER_INTERIOR "0px dotted gray"
#define TBLCSS_HBORDER_OUTER "2px solid black"
#define TBLCSS_EVEN_ROW_BACKGROUND "white"
#define TBLCSS_ODD_ROW_BACKGROUND "#E6E6E6"
#define TBLCSS_HEADER_BACKGROUND "#404040"
#define TBLCSS_HEADER_TEXTCOLOR "white"
#define TBLCSS_HEADER_FONTFAMILY "sans-serif"
#define TBLCSS_FONTFAMILY "monospace"

// The following two settings specifies the full CSS style property for the table and
// the date
#define TBLCSS_TABLE "width:700px;"
#define TBLCSS_DATE "font-size:small;font-style:italic;margin-bottom:1px;color:#404040;"
/*
 * Formats for HTML recording table in mail ('lm' command)
 */
extern char tblcss_vborder_interior[];
extern char tblcss_vborder_outer[];
extern char tblcss_hborder_interior[];
extern char tblcss_hborder_outer[];
extern char tblcss_even_row_background[];
extern char tblcss_odd_row_background[];
extern char tblcss_header_background[];
extern char tblcss_header_textcolor[];
extern char tblcss_header_fontfamily[];
extern char tblcss_fontfamily[];
extern char tblcss_table[];
extern char tblcss_date[];


/*
 * Card controls. Default settings. Note that all cards do not support the treble and bass
 * controls. If the card doesn't support them they will just be ignored
 *
 */

#define DEFAULT_IMAGE_BRIGHTNESS 0
#define DEFAULT_IMAGE_CONTRAST 0
#define DEFAULT_IMAGE_HUE 0
#define DEFAULT_IMAGE_SATURATION 0
#define DEFAULT_AUDIO_TREBLE 0
#define DEFAULT_AUDIO_BASS 0
#define DEFAULT_AUDIO_VOLUME 85
#define DEFAULT_AUDIO_LOUDNESS 1

extern int card_image_brightness;
extern int card_image_contrast;
extern int card_image_hue;
extern int card_image_saturation;
extern int card_audio_treble;
extern int card_audio_bass;
extern int card_audio_volume;
extern int card_audio_loudness;

#ifdef	__cplusplus
}
#endif

#endif	/* TVCONFIG_H */

