/* =========================================================================
 * File:        TVCMD.C
 * Description: This module manage the command interface and is responsible
 *              to interpret and execute the commands given to the server.
 *              The implementation is based on a fairly standard command
 *              function jump table. Each command is parsed using
 *              pcre_grep() family of functions (Perl Compatible regex).
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

// We need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

// Standard UNIX includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <malloc.h>
#include <syslog.h>
#include <errno.h>


#include <time.h>
#include <pthread.h>
#include <sys/param.h> // Needed to get MIN()/MAX()
#include <errno.h>
#include <syslog.h>

#include "config.h"
#include <pcre.h>

// Application specific includes
#include "libsmtpmail/mailclientlib.h"

#include "tvcmd.h"
#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "recs.h"
#include "tvxmldb.h"
#include "freqmap.h"
#include "vctrl.h"
#include "stats.h"
#include "transc.h"
#include "tvwebcmd.h"
#include "build.h"
#include "datetimeutil.h"
#include "xstr.h"
#include "tvplog.h"
#include "tvhistory.h"
#include "mailutil.h"

/*
 * Indexes into the command table
 */
#define CMD_ADD 0
#define CMD_DELETE 1
#define CMD_LIST 2
#define CMD_LIST_CONTROLS 3
#define CMD_LIST_STATIONS 4
#define CMD_STATUS 5
#define CMD_HELP 6
#define CMD_INFO 7
#define CMD_TIME 8
#define CMD_UPDATEXMLFILE 9
#define CMD_GETXML 10
#define CMD_ONGOINGTRANS 11
#define CMD_ONGOINGREC 12
#define CMD_NEXTREC 13
#define CMD_VERSION 14
#define CMD_CARDINFO 15
#define CMD_ABORT 16
#define CMD_QUICKRECORDING 17
#define CMD_GETSETTINGS 18
#define CMD_SETPROFILE 19
#define CMD_REFRESHPROFILE 20
#define CMD_PRINTPROFILE 21
#define CMD_STATISTICS 22
#define CMD_RESETSTATS 23
#define CMD_KILLTRANSCODING 24
#define CMD_TRANSCODEFILE 25
#define CMD_TRANSCODEFILELIST 26
#define CMD_TRANSCODEDIR 27
#define CMD_LIST_QUEUEDTRANSC 28
#define CMD_SHOW_LASTLOG 29
#define CMD_LIST_PROFILES 30
#define CMD_LISTWAITINGTRANSC 31
#define CMD_GETXMLHTML 32
#define CMD_LIST_RECHUMAN 33
#define CMD_LIST_VIDEO_INPUTS 34
#define CMD_LIST_TS 35
#define CMD_ADD_FROMFILE 36
#define CMD_DISK_USED 37
#define CMD_MAILLIST_HTML 38
#define CMD_LIST_PROFILES_HTMLLINKS 39
#define CMD_LISTRECREC 40
#define CMD_LISTRECSINGLE 41
#define CMD_MAILLIST_RECSINGLE_HTML 42
#define CMD_MAIL_LOG 43
#define CMD_VIEWHIST 44
#define CMD_MAILHIST 45


#define CMD_UNDEFINED 46

#define MAX_COMMANDS (CMD_UNDEFINED+1)

/**
 * Array with all command function pointers
 */
typedef void (*ptrcmd)(const char *, int);

/**
 * Array of pointers to command functions
 */
static ptrcmd cmdtable[MAX_COMMANDS];

// Forward declaration
static ptrcmd _getCmdPtr(const char *cmd);

/* ----------------------------------------------------------------------------
 * Command functions.
 *
 * All functions named _cmd_<command-name> implements the specific of
 * the <command-name> command. All output from the command is writtent
 * to the socket descriptor given as argument to each command. The first
 * argument is the entire command string as given by the client.
 * It is up toeach specific command to parse that string and extract
 * releevant information.
 *
 * All commands should print a detailed help on syntax when receiving
 * the 'h' command and then do nothing else.
 * ----------------------------------------------------------------------------
 */

/**
 * Command: _cmd_help
 * List all available commands and there syntax
 * @param cmd Command string
 * @param sockfd The socket to the client used for communciation
 */
static void
_cmd_help(const char *cmd, int sockfd) {
    static char msgbuff_master[2048] =
                        "Commands:\n"\
			"  a    - Add recording\n"\
			"  ar   - Add repeated recording\n"\
                        "  af   - Add recording from list in file\n"\
			"  d    - delete single recording\n"\
                        "  df   - display total and used diskspace\n"
                        "  dp   - display all settings for specified profile\n"\
			"  dr   - delete all repeated recording\n"\
			"  h    - help\n"\
			"  i    - print detailed information on recording\n"\
                        "  kt   - kill all ongoing transcoding(s)\n"\
                        "  ktf  - set/unset kill transcoding flag at shutdown\n"\
			"  l    - list of recordings\n"\
                        "  lc   - list all controls for the capture card\n"\
                        "  li   - list all video inputs for the capture card\n"\
    			"  lh   - list of recordings, human format\n"\
                        "  lm   - send mail with list of all recordings\n"\
                        "  lmr  - send mail with list repeat and single recordings\n"\
                        "  lr   - list repeating recordings\n"\
                        "  lu   - list single recordings\n"\
                        "  lt   - list recordings, using timestamps good for m2m communications\n"\
                        "  log n -show the last n lines of the logfile\n"\
                        "  ls   - list all stations\n"\
                        "  lp   - list all profiles\n"\
                        "  lph  - list all profiles with HTML links\n"\
                        "  lq n - list queued transcodings\n"\
                        "  mlg  - mail logfile as attachment\n"\
			"  n    - list the immediate next recording on each video\n"\
			"  o    - list the ongoing recording(s)\n"\
                        "  ot   - list the ongoing transcoding(s)\n"\
			"  q    - quick recording\n"\
                        "  rst  - reset all statistics\n"\
                        "  rh   - view history of previous transcodings\n"\
                        "  rhm  - mail history of previous transcodings\n"\
                        "  rp   - refresh transcoding profiles from file\n"\
                        "  s    - print server status\n"\
                        "  sp   - set transcoding profile for specified recording\n"\
                        "  st   - print profile statistics\n"\
			"  t    - print server time\n"\
    			"  tf   - transcode specified file\n"\
    			"  tl   - read list of videos to transcode from file\n"\
                        "  td   - transcode all videos in directory\n"\
			"  u    - force update of database with recordings\n"\
    			"  v    - print version\n"\
                        "  vc <n> - print information on TV-Card <n>\n"\
                        "  wt   - list waiting transcodings\n"\
			"  x    - view database (in XML format) with recordings\n"\
                        "  z    - display all settings from ini-file\n"\
                        "  ! <n>  - cancel ongoing recording\n"\
                        "Type h <cmd> for syntax of each command\n";

    static char msgbuff_slave[2048] =
                        "Commands:\n"\
                        "  dp   - display all settings for specified profile\n"\
			"  h    - help\n"\
                        "  kt   - kill all ongoing transcoding(s)\n"\
                        "  ktf  - set/unset kill transcoding flag at shutdown\n"\
                        "  log n -show the last n lines of the logfile\n"\
                        "  lp   - list all profiles\n"\
                        "  lq n - list queued transcodings\n"\
                        "  ot   - list the ongoing transcoding(s)\n"\
                        "  rst  - reset all statistics\n"\
                        "  rh   - view history of previous transcodings\n"\
                        "  rhm  - mail history of previous transcodings\n"\
                        "  rp   - refresh transcoding profiles from file\n"\
                        "  s    - print server status\n"\
			"  t    - print server time\n"\
    			"  tf   - transcode specified file\n"\
    			"  tl   - read list of videos to transcode from file\n"\
                        "  td   - transcode all videos in directory\n"\
    			"  v    - print version\n"\
                        "  wt   - list waiting transcodings\n"\
                        "  z    - display all settings from ini-file\n"\
                        "  ! <n>  - cancel ongoing recording\n"\
                        "Type h <cmd> for syntax of each command\n";

    char **field = (void *)NULL;
    int ret;
    char *msgbuff;
    if( is_master_server ) {
        msgbuff = msgbuff_master;
        ret = matchcmd("^h[\\p{Z}]+(af|ar|a|df|dp|dr|d|h|i|ktf|kt|log|lc|lh|li|lmr|lm|lph|lp|lq|lr|ls|lu|l|mlg|n|ot|o|q|rst|rh|rp|sp|st|s|tf|tl|td|t|u|vc|v|wt|x|z|!)$", cmd, &field);
    } else {
        msgbuff = msgbuff_slave;
        ret = matchcmd("^h[\\p{Z}]+(dp|h|ktf|kt|log|lp|lq|ot|rst|rh|rp|st|s|tf|tl|td|t|v|wt|z)$", cmd, &field);
    }
    if( ret > 0 ) {
        (_getCmdPtr(field[1]))(cmd,sockfd);        
        matchcmd_free(&field);
    }
    else {
        _writef(sockfd, msgbuff);
    }
}

/**
 * Command: _cmd_undefined
 * Default action when the command is not recognized
 * @param cmd Command string
 * @param sockfd The socket to the client used for communciation
*/
static void
_cmd_undefined(const char *cmd, int sockfd) {
    char msgbuff[1024];

    snprintf(msgbuff, 1024,  "Unrecognized command '%s'. Try 'h' for a list of available commands.\n",cmd);
    _writef(sockfd, msgbuff);

}

static void
_cmd_syntaxerror(const char *cmd, int sockfd) {
    char msgbuff[1024];

    snprintf(msgbuff, 1024,  "Huh? Check syntax for '%s' command.\n",cmd);
    _writef(sockfd, msgbuff);

}

static void
_cmd_setprofile(const char *cmd, int sockfd) {
    char msgbuff[256];
    int err, ret;
    char **field = NULL;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Set profile for specified recording.\n"\
                "sp <id> <profile>\n"\
                "  - <profile> Is the name of the profile to use. The profile must\n"
                "    exist in the current ini-file for the command to succeed.\n"
                );
        return;
    }
    
    ret = matchcmd("^sp" _PR_S _PR_ID _PR_S _PR_AN _PR_E, cmd, &field);
    err = ret < 0;

    if ( ! err && ret > 2 ) {
        if( updateprofile((unsigned)xatoi(field[1]),field[2]) ) {
            snprintf(msgbuff,255,"Updated profile to '%s' on recording %03d\n",field[2],xatoi(field[1]));
        } else {
            snprintf(msgbuff,255,"Failed to set profile '%s' on recording %03d\n",field[2],xatoi(field[1]));
        }
        _writef(sockfd,msgbuff);
        matchcmd_free(&field);
    } else {
        _cmd_undefined(cmd,sockfd);
    }
}

/**
 * Command: _cmd_delete
 * Delete the specified record
 * Syntax:
 *  d <id>    Delete record with id
 *  dr <id>   Delete all records in the repeated recording that <id>
 *            is part of
 */
static void
_cmd_delete(const char *cmd, int sockfd) {
    char msgbuff[256];
    int err=0, ret;
    unsigned id;
    char **field=(void *)NULL;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Delete specified recording.\n"\
                "d  <id>\n"\
                "dr <id>\n"\
                "  - 'd'  Deletes only one specified recording\n"\
                "  - 'dr' All records in a repeated sequence are removed\n"
                );
        return;
    }

    if( (ret = matchcmd("^d" _PR_SO _PR_ID _PR_E, cmd, &field)) < 0 ) {
        err = ( matchcmd("^dr" _PR_SO _PR_ID _PR_E, cmd, &field) < 0 );
    }

    if ( ! err ) {

        id = (unsigned)xatoi(field[1]);
        ret = deleterecid(id, cmd[1] == 'r');

        if (ret) {
            snprintf(msgbuff, 256, "Deleted %s #%02d",
                    cmd[1] == 'r' ? "repeated recordings" : "recording", id);
        }
        else
            snprintf(msgbuff, 256, "Can not delete record #%02d", id);

        matchcmd_free(&field);

    } else {
        snprintf(msgbuff, 256, "Command not recognized.");
    }


    if( ! err ) {
        logmsg(LOG_INFO,msgbuff);
        char logmsgbuff[256];
        if (writeXMLFile(xmldbfile) >= 0 ) {
            snprintf(logmsgbuff, 255,"Database successfully updated '%s' after delete command", xmldbfile);
            logmsgbuff[255] = 0 ;
            logmsg(LOG_INFO,logmsgbuff);
        } else {
            snprintf(logmsgbuff, 255,"Failed to update database '%s' after delete command", xmldbfile);
            logmsgbuff[255] = 0 ;
            logmsg(LOG_ERR,logmsgbuff);
        }
    }
    else {
        logmsg(LOG_ERR,msgbuff);
    }
    _writef(sockfd, "%s\n", msgbuff);

}

/**
 * Command: _cmd_add
 * Add a new single or repeated recording
 * Syntax:
 *
 * Add new single recording on selected channel and time.
 * a <ch> <s.time> [<title>]
 * a <ch> <s.time> <e.time> <title>
 * a <ch> <s.date> <s.time> <e.time> <title>
 *
 *
 * Add new repeated recording on selected channel and time.
 * ar <type> <nbr> <ch> <s.time> [<title>]
 * ar <type> <nbr> <ch> <s.time> <e.time> <title>
 * ar <type> <nbr> <ch> <s.date> <s.time> <e.time> <title>
 *       type: 1=daily, 2=weekly, 3=monthly, 4=Mon-Fri, 5=Sat-Sun
 *       or: d=daily, w=weekly, m=monthly, f=Mon-Fri, s=Sat-Sun
 *       nbr: Number of repeats\n"
 *
 * - The lowest number video available will be used for recording
 * - When title is optional and not specified it will be created based on date and time
 * - If no end time is given the default recording length (%d:%02d) is used
 * - If s.time < current time the s.date will be set to tommorrow
 * - If e.time < s.time and time spans over midnight e.date will be set tomorrow
 *
 */
static void
_cmd_add(const char *cmd, int sockfd) {
    char msgbuff[2048],cmdbuff[256];
    int ret;
    int sy, sm, sd, sh, smin, ssec;
    int ey, em, ed, eh, emin, esec;
    char title[128], channel[64], filename[256];
    time_t now, ts_tmp, ts_start, ts_end;
    struct recording_entry *entry;
    char **field = (void *)NULL;
    char *profiles[5];
    static char *add_errstr[] = {
      "Unknown error",
      "Syntax error",
      "Recording can not be larger than 4 hours",
      "End time can not be erlier than start time",
      "Start time is in the past",
      "No free video resource at specified time",
      "Unknown profile specified",
      "Unknown relative date specification",
      "Incoherent date specification"
    };
    time_t end_repeat_time = 0;
    int repeat_type = 0, repeat_with_enddate=0;
    unsigned repeat_nbr = 0;
    int err = 0;
    int input_card = -1, input_idx = -1;

    for(int i=0; i < REC_MAX_TPROFILES; i++) {
        profiles[i] = calloc(1,16);
    }

    // Special case: Help info for this command requested
    if (cmd[0] == 'h') {
        if( cmd[3] == 'r') {
            _writef(sockfd,
                    "Add new repeated recording on selected channel and time.\n"\
                    "ar <type> <nbr> <ch> <s.time> [<title>] [@profile, @profile, ...]\n"\
                    "ar <type> <nbr> <ch> <s.time> <e.time> <title> [@profile, @profile, ...]\n"\
                    "ar <type> <nbr> <ch> <s.date> <s.time> <e.time> <title> [@profile, @profile, ...]\n"\
                    "    type: 1=daily, 2=weekly, 3=monthly, 4=Mon-Fri, 5=Sat-Sun, 6=Mon-Thu, 7=Tue-Fri\n"
                    "      or: d=daily, w=weekly, m=monthly, f=Mon-Fri, s=Sat-Sun, t=Mon-Thu, n=Tue-Fri\n"
                    "     nbr: Number of repeats\n"
                    );
        } else {
            _writef(sockfd,
                    "Add new single recording on selected channel and time.\n"\
                    "a <ch> <s.time> [<title>] [@profile, @profile, ...]\n"\
                    "a <ch> <s.time> <e.time> <title> [@profile, @profile, ...]\n"\
                    "a <ch> <s.date> <s.time> <e.time> <title> [@profile, @profile, ...]\n"\
                );
        }
        _writef(sockfd,
                " - The lowest number video available will be used for recording\n"\
                " - When title is optional and not specified it will be created based on date and time\n"\
                " - If no end time is given the default recording length (%d:%02d) is used\n"\
                " - If s.time < current time the s.date will be set to tommorrow\n"\
                " - If e.time < s.time and time spans over midnight e.date will be set tomorrow\n",
                defaultDurationHour, defaultDurationMin);
        return;
    }

    if (cmd[1] == 'r') {

        // Repeated variant 1
        // ar <type> <id> <...>
        ret = matchcmd("^ar" _PR_S "([1-7]|d|w|m|f|s|t|n)" _PR_S _PR_ID _PR_S _PR_ANY, cmd, &field);
        if( ret == 4 ) {
            if( isdigit(*field[1]) ) {
                repeat_type = xatoi(field[1]);
            } else {
                switch( *field[1] ) {
                    case 'd': repeat_type = 1; break;
                    case 'w': repeat_type = 2; break;
                    case 'm': repeat_type = 3; break;
                    case 'f': repeat_type = 4; break;
                    case 's': repeat_type = 5; break;
                    case 't': repeat_type = 6; break;
                    case 'n': repeat_type = 7; break;
                }
            }
            repeat_nbr = (unsigned)xatoi(field[2]);
            snprintf(cmdbuff,255, "a %s", field[3]);
            cmdbuff[255] = 0;
            matchcmd_free(&field);

        }
        else {
            // See if number of instances was specified with an end date instead
            ret = matchcmd("^ar" _PR_S "([1-7]|d|w|m|f|s|t|n)" _PR_S _PR_FULLDATE _PR_S _PR_ANY, cmd, &field);
            logmsg(LOG_DEBUG,"ret=%d after 'ar' command",ret);
            if( ret == 6 ) {

                if( isdigit(*field[1]) ) {
                    repeat_type = xatoi(field[1]);
                } else {
                    switch( *field[1] ) {
                        case 'd': repeat_type = 1; break;
                        case 'w': repeat_type = 2; break;
                        case 'm': repeat_type = 3; break;
                        case 'f': repeat_type = 4; break;
                        case 's': repeat_type = 5; break;
                        case 't': repeat_type = 6; break;
                        case 'n': repeat_type = 7; break;
                    }
                }

                // We should remember that an end date was specified since we
                // need to recalculate the number in case a start date was specified
                // in the normal add command that follows. In the calculation below we
                // assume that the earliest start is the current date/time
                repeat_with_enddate = 1;

                // Extract the end date in components
                int year = xatoi(field[2]);
                int month = xatoi(field[3]);
                int day = xatoi(field[4]);

                logmsg(LOG_DEBUG,"year=%d, month=%d, day=%d",year,month,day);
                
                end_repeat_time = totimestamp(year,month,day,23,59,59);

                // Find out how many repeats are required to reach the end date
                // from todays date. If a startdate is given then this will
                // be recalculated below in the case for 'a'
                time_t t1time = time(NULL);
                time_t t2time = t1time;

                // Sanity check of end date
                if( end_repeat_time < t1time ) {
                    err=8;
                    logmsg(LOG_ERR,"End date cannnot be earlier than current date/time.");
                } else {

                    (void)adjust_initital_repeat_date(&t1time, &t2time, repeat_type);

                    int t1y,t1m,t1d,t1h,t1min,t1sec;
                    int t2y,t2m,t2d,t2h,t2min,t2sec;
                    fromtimestamp(t1time,&t1y, &t1m, &t1d, &t1h, &t1min, &t1sec);
                    fromtimestamp(t2time,&t2y, &t2m, &t2d, &t2h, &t2min, &t2sec);
                    repeat_nbr = 0 ;
                    while( t1time <= end_repeat_time ) {
                        ++repeat_nbr;
                        increcdays(repeat_type,&t1time,&t2time,
                                   &t1y, &t1m, &t1d, &t1h, &t1min, &t1sec,
                                   &t2y, &t2m, &t2d, &t2h, &t2min, &t2sec);

                    }

                    logmsg(LOG_DEBUG,"repeat_nbr = %d",repeat_nbr);

                    snprintf(cmdbuff,255, "a %s", field[5]);
                    cmdbuff[255] = 0;

                    matchcmd_free(&field);

                }
            } else {
                err = 1;
            }            
        }
    }
    else {
        strncpy(cmdbuff,cmd,255);
    }

    if (!err) {

        // Get current time
        now = time(NULL);
        fromtimestamp(now, &sy, &sm, &sd, &sh, &smin, &ssec);
        fromtimestamp(now, &ey, &em, &ed, &eh, &emin, &esec);

        // Variant 0 :
        // a <channel> <starttime> [<title>] [@profile, @profile, ...]
        char _regex[256];
        strcpy(_regex,"^a" _PR_S _PR_CHANNEL _PR_S _PR_OPTIME _PR_OPTITLE _PR_PROFILES _PR_E);
        ret = matchcmd(_regex, cmdbuff, &field);
        err =  ret < 2 ;

        if( ! err ) {
            // Field 2 is always hour

            // Now determine how many more digits for time are specified
            size_t pos=2;
            sh = xatoi(field[pos++]);
            if( ret >= 5 && *field[pos++] == ':') {
                smin = xatoi(field[pos]);
            } else {
                smin = 0;
            }
            pos++;

            if( ret >= 7 &&  *field[pos++] == ':' ) {
                ssec = xatoi(field[pos]);
            } else {
                ssec = 0 ;
            }
            pos++;
            
            ts_start = totimestamp(sy, sm, sd, sh, smin, ssec);

            eh = sh + defaultDurationHour;
            emin = smin + defaultDurationMin;
            esec = ssec;
            ts_end = totimestamp(ey,em,ed,eh,emin,esec);
            fromtimestamp(ts_end,&ey,&em,&ed,&eh,&emin,&esec);

            if (ts_start < now) {
                // Start time in the future. Assume tomorrow
                ts_start = totimestamp(sy, sm, sd + 1, sh, smin, ssec);
                ts_end = totimestamp(ey, em, ed + 1, eh, emin, esec);
                fromtimestamp(ts_start, &sy, &sm, &sd, &sh, &smin, &ssec);
                fromtimestamp(ts_end, &ey, &em, &ed, &eh, &emin, &esec);
            }

            // This is simple.Channel is always in field 1

            strncpy(channel, field[1], 64);

            // Now the boring analysis on where we can find the title
            *title = '\0';
            if (ret >= 8) {
                pos = 7;
                if (*field[pos] != '@') {
                    // Ok this is not a profile
                    strncpy(title, field[pos + 1], 128);
                    pos += 3;
                }

                if (ret > (int)pos) {
                    // We must have a one or more profiles at the end
                    int k=0;
                    while( (int)pos < ret && !err) {
                        if (*field[pos] == '@') {
                            if( transcoding_profile_exist(&field[pos][1]) ) {
                                strncpy(profiles[k], &field[pos][1],15);
                                profiles[k++][15] = '\0';
                            } else {
                                logmsg(LOG_ERR,"Transcoding profile '%s' does not exist.",&field[pos][1]);
                                err=6;
                            }
                        }
                        pos++;
                    }
                }

                if( strlen(title) == 0 ) {
                    snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                }
            } else {
                // The case with no title and no profiles
                snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
            }
            title[128-1] = '\0';
            channel[64-1] = '\0';

            matchcmd_free(&field);

        } else {
            // Variant 1 :
            // a <channel> <starttime> <endtime> [<title>] [@profile, @profile, ...]
            //
            // Rules:
            // 1. Filename is same as name with ".mpg" added
            // 2. Date is assumed to be todays date
            // 3. if <endtime> is less than <starttime> a check is made if it is
            //    reasonable that this is a recording than spans midnight. The
            //    requirement is that the total recording time <= 4h.
            // Setup regex for parsing of add command
            ret = matchcmd("^a" _PR_S _PR_CHANNEL _PR_S _PR_OPTIME _PR_S _PR_OPTIME _PR_OPTITLE _PR_PROFILES _PR_E,
                           cmdbuff, &field);
            err = ret < 0;

            if (!err) {

                size_t pos = 2;

                sh = xatoi(field[pos++]);
                if( *field[pos++] == ':')
                    smin = xatoi(field[pos]);
                else
                    smin = 0;
                pos++;
                if( *field[pos++] == ':')
                    ssec = xatoi(field[pos]);
                else
                    ssec = 0;
                pos++;

                eh = xatoi(field[pos++]);
                if( *field[pos++] == ':')
                    emin = xatoi(field[pos]);
                else
                    emin = 0;
                pos++;
                if( *field[pos++] == ':')
                    esec = xatoi(field[pos]);
                else
                    esec = 0;
                pos++;
                
                strncpy(channel, field[1], 63);
                channel[63] = '\0';
                *title = '\0';

                if (ret >= 13) {
                    pos = 12;
                    if (*field[pos] != '@') {
                        // Ok we have a title
                        strncpy(title, field[pos + 1], 127);
                        title[127] = '\0';
                        pos += 3;
                    }

                    if (ret > (int)pos) {
                        // We must have a one or more profiles at the end
                        int k=0;
                        while( (int)pos < ret && !err) {
                            if (*field[pos] == '@') {
                                if( transcoding_profile_exist(&field[pos][1]) ) {
                                    strncpy(profiles[k], &field[pos][1],15);
                                    profiles[k++][15] = '\0';
                                } else {
                                    logmsg(LOG_ERR,"Transcoding profile '%s' does not exist.",&field[pos][1]);
                                    err=6;
                                }
                            }
                            pos++;
                        }
                    }

                    if( strlen(title) == 0 ) {
                        snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                    } 
                } else {
                    // The case with no title and no profiles
                    snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                }

                if (eh < sh) {
                    // Check if we are plausible passing over midnight
                    if ((24 - sh) + eh < 5) {
                        ed = sd + 1;
                        ts_tmp = totimestamp(ey, em, ed, eh, emin, esec);
                        fromtimestamp(ts_tmp, &ey, &em, &ed, &eh, &emin, &esec);
                    } else {
                        err = 3;
                    }
                }

                // If the time is earlier than the current time assume that the recording
                // is meant for tomorrow
                ts_start = totimestamp(sy, sm, sd, sh, smin, ssec);
                ts_end = totimestamp(ey, em, ed, eh, emin, esec);
                now = time(NULL);
                if (ts_start < now) {
                    int ny,nm,nd,nh,nmin,nsec;
                    fromtimestamp(now, &ny, &nm, &nd, &nh, &nmin, &nsec);
                    logmsg(LOG_INFO,"Adding: ts_start=%d, %02d:%02d:%02d, ts_end=%d , now=%d, %02d:%02d:%02d",
                            ts_start,sh,smin,ssec,
                            ts_end,now, nh,nmin,nsec);
                    // Start time in the future. Assume tomorrow
                    ts_start = totimestamp(sy, sm, sd + 1, sh, smin, ssec);
                    ts_end = totimestamp(ey, em, ed + 1, eh, emin, esec);
                    fromtimestamp(ts_start, &sy, &sm, &sd, &sh, &smin, &ssec);
                    fromtimestamp(ts_end, &ey, &em, &ed, &eh, &emin, &esec);
                }

                matchcmd_free(&field);

            } else {

                // Variant 2
                // a <channel> <startdate> <starttime> <endtime> [<title>] [@profile, @profile, ...]
                // Try with only start date and assume end date is same
                //
                ret = matchcmd("^a" _PR_S _PR_CHANNEL _PR_S _PR_DATE _PR_S _PR_OPTIME _PR_S _PR_OPTIME _PR_OPTITLE _PR_PROFILES _PR_E,
                               cmdbuff, &field);
                err = ret < 0;

                 if (!err) {

                    if( isalpha(field[3][0]) && strnlen(field[3],8) >= 3 ) {
                        // Relative date
                        field[3][3] = '\0'; // We only use the first three characters
                        if( getreldatefromdayname(field[3],&sy,&sm,&sd) == -1 ) {
                            err = 7;
                            logmsg(LOG_ERR,"Can not add record. ( %d : %s )", err,add_errstr[err]);
                        }
                        ey = sy;
                        em = sm;
                        ed = sd;

                    } else {
                        sy = ey = xatoi(field[4]);
                        sm = em = xatoi(field[5]);
                        sd = ed = xatoi(field[6]);
                    }

                    // Adjust a potential recurring count in cease an end date was specified
                    if( repeat_with_enddate ) {

                       // Find out how many repeats are required to reach the end date
                        // from todays date. If a startdate is given then this will
                        // be recalculated below in the case for 'a'
                        time_t t1time = totimestamp(sy,sm,sd,0,0,0);

                        if( t1time > end_repeat_time ) {
                            err=8;
                            logmsg(LOG_ERR,"Start date is after specified end date for recurring recording.");
                        } else {
                            time_t t2time = t1time;
                            (void)adjust_initital_repeat_date(&t1time, &t2time, repeat_type);

                            int t1y,t1m,t1d,t1h,t1min,t1sec;
                            int t2y,t2m,t2d,t2h,t2min,t2sec;
                            fromtimestamp(t1time,&t1y, &t1m, &t1d, &t1h, &t1min, &t1sec);
                            fromtimestamp(t2time,&t2y, &t2m, &t2d, &t2h, &t2min, &t2sec);
                            repeat_nbr = 0 ;
                            while( t1time <= end_repeat_time ) {
                                ++repeat_nbr;
                                increcdays(repeat_type,&t1time,&t2time,
                                           &t1y, &t1m, &t1d, &t1h, &t1min, &t1sec,
                                           &t2y, &t2m, &t2d, &t2h, &t2min, &t2sec);
                            }

                            logmsg(LOG_DEBUG,"Re-calculated repeat count to %d",repeat_nbr);

                        }
                    }

                    if (!err) {
                        size_t pos = 7;

                        sh = xatoi(field[pos++]);
                        if( *field[pos++] == ':')
                            smin = xatoi(field[pos]);
                        else
                            smin = 0;
                        pos++;
                        if( *field[pos++] == ':')
                            ssec = xatoi(field[pos]);
                        else
                            ssec = 0;
                        pos++;

                        eh = xatoi(field[pos++]);
                        if( *field[pos++] == ':')
                            emin = xatoi(field[pos]);
                        else
                            emin = 0;
                        pos++;

                        if( *field[pos++] == ':')
                            esec = xatoi(field[pos]);
                        else
                            esec = 0;
                        pos++;

                        strncpy(channel, field[1], 63);
                        channel[63] = '\0';
                        *title = '\0';

                        if (ret >= 18) {
                            pos = 17;
                            if (*field[pos] != '@') {
                                // Ok we have a title
                                strncpy(title, field[pos + 1], 127);
                                title[127] = '\0';
                                pos += 3;
                            }

                            if (ret > (int)pos) {
                                // We must have a one or more profiles at the end
                                int k=0;
                                while( (int)pos < ret && !err) {
                                    if (*field[pos] == '@') {
                                        if( transcoding_profile_exist(&field[pos][1]) ) {
                                            strncpy(profiles[k], &field[pos][1],15);
                                            profiles[k++][15] = '\0';
                                        } else {
                                            logmsg(LOG_ERR,"Transcoding profile '%s' does not exist.",&field[pos][1]);
                                            err=6;
                                        }
                                    }
                                    pos++;
                                }
                            }

                            if( strlen(title) == 0 ) {
                                snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                            }
                        } else {
                            // The case with no title and no profiles
                            snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                        }

                        if (eh < sh) {
                            // Check if we are plausible passing over midnight
                            if ((24 - sh) + eh < 5) {
                                ed = sd + 1;
                                ts_tmp = totimestamp(ey, em, ed, eh, emin, esec);
                                fromtimestamp(ts_tmp, &ey, &em, &ed, &eh, &emin, &esec);
                            } else {
                                err = 1;
                            }
                        }

                        matchcmd_free(&field);
                    }
                } else {

                    // Variant 3
                    // a <channel> <startdate> <starttime> <endtime> [<title>] [@profile, @profile, ...]
                    // Try with only start date and assume end date is same
                    //
                    ret = matchcmd("^a" _PR_S _PR_CHANNEL _PR_S _PR_DATE _PR_S _PR_OPTIME _PR_OPTITLE _PR_PROFILES _PR_E,
                                   cmdbuff, &field);
                    err = ret < 0;

                     if (!err) {

                        if( isalpha(field[3][0]) && strnlen(field[3],8) >= 3 ) {
                            // Relative date
                            field[3][3] = '\0'; // We only use the first three characters
                            if( getreldatefromdayname(field[3],&sy,&sm,&sd) == -1 ) {
                                err = 7;
                                logmsg(LOG_ERR,"Can not add record. ( %d : %s )", err,add_errstr[err]);
                            }
                            ey = sy;
                            em = sm;
                            ed = sd;

                        } else {
                            sy = ey = xatoi(field[4]);
                            sm = em = xatoi(field[5]);
                            sd = ed = xatoi(field[6]);
                        }

                        if (!err) {
                            size_t pos = 7;

                            sh = xatoi(field[pos++]);
                            if( *field[pos++] == ':')
                                smin = xatoi(field[pos]);
                            else
                                smin = 0;
                            pos++;
                            if( *field[pos++] == ':')
                                ssec = xatoi(field[pos]);
                            else
                                ssec = 0;
                            pos++;

                            eh = sh + defaultDurationHour;
                            emin = smin + defaultDurationMin;
                            esec = 0;
                            ts_end = totimestamp(ey, em, ed, eh, emin, esec);
                            fromtimestamp(ts_end,&ey,&em,&ed,&eh,&emin,&esec);

                            strncpy(channel, field[1], 63);
                            channel[63] = '\0';
                            *title = '\0';

                            if (ret >= 13) {
                                pos = 12;
                                if (*field[pos] != '@') {
                                    // Ok we have a title
                                    strncpy(title, field[pos + 1], 127);
                                    title[127] = '\0';
                                    pos += 3;
                                }

                                if (ret > (int)pos) {
                                    // We must have a one or more profiles at the end
                                    int k=0;
                                    while( (int)pos < ret && !err) {
                                        if (*field[pos] == '@') {
                                            if( transcoding_profile_exist(&field[pos][1]) ) {
                                                strncpy(profiles[k], &field[pos][1],15);
                                                profiles[k++][15] = '\0';
                                            } else {
                                                logmsg(LOG_ERR,"Transcoding profile '%s' does not exist.",&field[pos][1]);
                                                err=6;
                                            }
                                        }
                                        pos++;
                                    }
                                }

                                if( strlen(title) == 0 ) {
                                    snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                                }
                            } else {
                                // The case with no title and no profiles
                                snprintf(title,128,"%s_%d%02d%02d_%02d%02d",channel,sy,sm,sd,sh,smin);
                            }

                            if (eh < sh) {
                                // Check if we are plausible passing over midnight
                                if ((24 - sh) + eh < 5) {
                                    ed = sd + 1;
                                    ts_tmp = totimestamp(ey, em, ed, eh, emin, esec);
                                    fromtimestamp(ts_tmp, &ey, &em, &ed, &eh, &emin, &esec);
                                } else {
                                    err = 1;
                                }
                            }
                        }

                        matchcmd_free(&field);
                    }

                }
            }
        }
    }

    if( !err ) {
        // Verify that this is either a named station or a recognized channel in the
        // current frequence map
        unsigned tmp;
        xstrtolower(channel);

        // Check if this is a regular channel or a direct specifciation of a
        // video input on the card, i.e. in the form "_inp01"

        const size_t inp_len = strlen(INPUT_SOURCE_PREFIX);
        if( strlen(channel) <= inp_len+2 &&
            0 == strncmp(INPUT_SOURCE_PREFIX, channel,inp_len) ) {

            // Sanity check. Only allow card to be specified as an integer 0-4
            // Only allow input source to be specified as integer 0-7
            if( ( channel[inp_len] >= '0'  && (unsigned)(channel[inp_len]-'0') <  max_video ) &&
                ( channel[inp_len+1] >= '0' && channel[inp_len+1] <= '7'  ) ) {
                input_card = channel[inp_len] - '0';
                input_idx  = channel[inp_len+1] - '0';

                logmsg(LOG_DEBUG, "Input source set to video=%d, source=%d", input_card, input_idx);

            } else {

                sprintf(msgbuff, "Specified video input source '%s' is not recognized.\n", channel);
                logmsg(LOG_ERR, "Specified video input source '%s' is not recognized.", channel);
                err = 1;
                
            }

        } else {
            if( -1 == getfreqfromstr(&tmp, channel) ) {
                sprintf(msgbuff, "Specified station/channel '%s' is not recognized.\n", channel);
                logmsg(LOG_ERR, "Specified station/channel '%s' is not recognized.", channel);
                err = 1;
            }
        }
    }

    if (!err) {

        // Create a new recording. This means that a recurrent recording is expanded with
        // a single record for all its occurrences
        ts_start = totimestamp(sy, sm, sd, sh, smin, ssec);
        ts_end = totimestamp(ey, em, ed, eh, emin, esec);

        if (ts_end - ts_start > 4 * 60 * 60) {
            // Check that recording time is not over 4 h
            err = 2;
        } else if (ts_end < ts_start) {
            // Check that end time is not earlier than start time
            err = 3;
        } else if (ts_start < now) {
            // We cannot start a recording in the past
            err = 4;
        } else {

            // Remove possible spaces at end of title.
            // Since we don't need a real character count (only the end of the
            // string as a byte position it is safe to use strlen() even for
            // multi-byte locales.
            int k = (int)strlen(title)-1;
            while( k >= 0 && title[k] == ' ') {
                k--;
            }
            title[k+1] = '\0';

            // Create filename from title replacing punctuation and space chars with "_"
            // TODO: This is not multi-byte character safe !
            strncpy(filename, title,123);
            filename[123-1] = '\0';
            for (unsigned i = 0; i < strlen(filename); i++) {
                if (filename[i] == ' ' || filename[i] == '&' ||
                    filename[i] == ':' || filename[i] == '!' ||
                    filename[i] == '#' || filename[i] == '?' ||
                    filename[i] == '/' || filename[i] == '\\' ||
                    filename[i] == '@') {

                    filename[i] = '_';
                }
            }
            strcat(filename, ".mpg");
            xstrtolower(filename);

            int repeat_name_mangle_type=1;
            entry = newrec(title, filename,
                    ts_start, ts_end,
                    channel,
                    repeat_type > 0,
                    repeat_type, repeat_nbr, repeat_name_mangle_type,
                    profiles);


            ret = -1;
            if( input_card >= 0 ) {
                // Recording should be made on the specific video card
                // and input source

                ret = insertrec((unsigned)input_card, entry);

            } else {
                
                // Take the first video stream available
                for (size_t video = 0; video < max_video && (-1 == ret); video++) {
                    ret = insertrec(video, entry);
                }
            }

            if (-1 == ret) {
                freerec(entry);
                err = 5;
            } else {
                dumprecordid((unsigned)ret, 1, 0, 0, msgbuff, 2047);
            }
        }
    }

    if (err) {
        sprintf(msgbuff, "Error:%d:%s:%s", err,add_errstr[err],last_logmsg+26);
        logmsg(LOG_ERR,"Can not add record. ( %d : %s )", err,add_errstr[err]);
    } else {
        // Synchronize the DB file with the added recordings
        char logmsgbuff[256];
        if (writeXMLFile(xmldbfile) >= 0 ) {
            snprintf(logmsgbuff, 255,"Database successfully updated '%s' after add command", xmldbfile);
            logmsgbuff[255] = 0 ;
            logmsg(LOG_INFO,logmsgbuff);
        } else {
            snprintf(logmsgbuff, 255,"Failed to update database '%s' after add command", xmldbfile);
            logmsgbuff[255] = 0 ;
            logmsg(LOG_ERR,logmsgbuff);
        }
    }

    /*
    if( field != (void *)NULL ) {
        pcre_free_substring_list((const char **)field);
    }
    */

    for(int i=0; i < REC_MAX_TPROFILES; i++) {
        free(profiles[i]);
    }

    _writef(sockfd, msgbuff);

}


/**
 * Read add commands from named file
 * @param cmd
 * @param sockfd
 */
void
_cmd_addfromfile(const char *cmd, int sockfd) {
    char msgbuff[256]={'\0'};
    int err=0, ret;
    char **field=(void *)NULL;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Read recordings to add from file.\n"\
                "af <filename> - Use <filename> as source for lines of add command\n"
                );
        return;
    }

    ret = matchcmd("^af" _PR_S _PR_FILEPATH _PR_E, cmd, &field);
    err = ret < 0;

    if ( ! err ) {
        // Check that file exists
        if( 0 == access(field[1],R_OK) ) {
            FILE *fp = fopen(field[1],"r");
            char linebuff[255];
            while ( !feof(fp) && NULL != fgets(linebuff,255,fp) && strlen(linebuff) > 2 ) {
                linebuff[strlen(linebuff)-1] = '\0'; // remove trailing newline
                _cmd_add(linebuff,sockfd);
            }

        } else {
            snprintf(msgbuff, 256, "Cannot access file with add commands: '%s' ( %d : %s )",field[1],errno,strerror(errno));
            logmsg(LOG_ERR,msgbuff);
        }
        matchcmd_free(&field);

    } else {
        snprintf(msgbuff, 256, "Command not recognized.");
        logmsg(LOG_ERR,msgbuff);
    }

    _writef(sockfd, "%s\n", msgbuff);

}

/**
 * Command: _cmd_list
 * List all pending recordings
 * Syntax:
 * l
 */
static void
_cmd_list(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "l <n>         - List all pending recordings. If <n> is given, only list the first n records\n"
                 );
        return;
    }

    int ret = matchcmd("^l" _PR_SO _PR_OPID _PR_E, cmd, &field);
    int n;
    if( ret > 1 ) {
        // User has limited the list

        n = xatoi(field[1]);

        if( n < 1 || n > 99 ) {
            _writef(sockfd,"Error. Number of lines must be in range [1,99]\n");
            matchcmd_free(&field);
            return;
        }

    } else if ( ret == 1 ) {

        n = -1; // Default to all records

    } else {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }
    
    matchcmd_free(&field);
    listrecs((size_t)n, 0, sockfd);
}

static void
_cmd_list_human(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lh <n>         - List all pending recordings. If <n> is given, only list the first n records\n"
                 );
        return;
    }

    int ret = matchcmd("^lh" _PR_SO _PR_OPID _PR_E, cmd, &field);
    size_t n;

    if( ret > 1 ) {
        // User has limited the list

        n = (size_t)xatoi(field[1]);

        if( n < 1 || n > 99 ) {
            _writef(sockfd,"Error. Number of lines must be in range [1,99]\n");
            matchcmd_free(&field);
            return;
        }

    } else if ( ret == 1 ) {

        n = 0; // Default to all records

    } else {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }

    matchcmd_free(&field);
    listrecs(n, 3, sockfd);
}

/*
 */
static void
_cmd_maillist_html(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lm <n>         - Send a HTML mail with all pending recordings. If <n> is given, only list the first n records\n"
                 );
        return;
    }

    int ret = matchcmd("^lm" _PR_SO _PR_OPID _PR_E, cmd, &field);
    size_t n;

    if( ret > 1 ) {
        // User has limited the list

        n = (size_t)xatoi(field[1]);

        if( n < 1 || n > 99 ) {
            _writef(sockfd,"Error. Number of lines must be in range [1,99]\n");
            matchcmd_free(&field);
            return;
        }

    } else if ( ret == 1 ) {

        n = 0; // Default to all records

    } else {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }

    matchcmd_free(&field);

    size_t const maxlen=(max_video*max_entries)*1024;

    char *buffer_plain = calloc(maxlen, sizeof(char));
    char *buffer_html = calloc(maxlen, sizeof(char));

    if( buffer_plain == NULL || buffer_html == NULL ) {
        logmsg(LOG_ERR,"Out of memory when trying to allocate buffer! CRITICAL!");
        exit(1);
    }

    time_t ts_tmp = time(NULL);
    snprintf(buffer_plain,maxlen,"Generated by: %s %s, %s\n",
             server_program_name,server_version,ctime(&ts_tmp));

    size_t len = strlen(buffer_plain);
    listrecsbuff(buffer_plain+len, maxlen-len, n, 3);
    listhtmlrecsbuff(buffer_html, maxlen, n, 0, FALSE, TRUE);

    char subject[255];
    snprintf(subject,255,"List of upcoming recordings");

    if( -1 == sendmail_helper(subject, buffer_plain, buffer_html) ) {
        _writef(sockfd,"Failed to create and send mail.\n");
    } else {
        _writef(sockfd,"List of recordings sent to '%s'\n",send_mailaddress);
    }
    free(buffer_html);
    free(buffer_plain);

}

/*
 * Send mail with repeating recordings
 
static void
_cmd_maillistrep_html(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lmr <n>         - Send a HTML mail with all pending repeated recordings. If <n> is given, only list the first n records\n"
                 );
        return;
    }

    int ret = matchcmd("^lmr" _PR_SO _PR_OPID _PR_E, cmd, &field);
    size_t n;

    if( ret > 1 ) {
        // User has limited the list

        n = xatoi(field[1]);

        if( n < 1 || n > 99 ) {
            _writef(sockfd,"Error. Number of lines must be in range [1,99]\n");
            matchcmd_free(&field);
            return;
        }

    } else if ( ret == 1 ) {

        n = 0; // Default to all records

    } else {

        _writef(sockfd,"Syntax error.\n",ret);
        return;

    }

    matchcmd_free(&field);

    size_t const maxlen=(max_video*max_entries)*1024;

    char *buffer_plain = calloc(maxlen, sizeof(char));
    char *buffer_html = calloc(maxlen, sizeof(char));

    if( buffer_plain == NULL || buffer_html == NULL ) {
        logmsg(LOG_ERR,"Out of memory when trying to allocate buffer! CRITICAL!");
        exit(1);
    }

    listrepeatrecsbuff(buffer_plain, maxlen, n);
    listhtmlrepeatrecsbuff(buffer_html, maxlen, n, 0);

    char subject[255];
    snprintf(subject,255,"List of upcoming repeating recordings");

    if( -1 == _sendmail_helper(subject, buffer_plain, buffer_html) ) {
        _writef(sockfd,"Failed to create and send mail.\n");
    } else {
        _writef(sockfd,"List of recordings sent to '%s'\n",send_mailaddress);
    }
    free(buffer_html);
    free(buffer_plain);
}

 */


static void
_cmd_maillist_recsingle(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lmr <n>         - Send a HTML mail with upcoming repeated and single recordings. If <n> is given, only list the first n records\n"
                 );
        return;
    }

    int ret = matchcmd("^lmr" _PR_SO _PR_OPID _PR_E, cmd, &field);
    size_t n;

    if( ret > 1 ) {
        // User has limited the list

        n = (size_t)xatoi(field[1]);

        if( n < 1 || n > 99 ) {
            _writef(sockfd,"Error. Number of lines must be in range [1,99]\n");
            matchcmd_free(&field);
            return;
        }

    } else if ( ret == 1 ) {

        n = 0; // Default to all records

    } else {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }

    matchcmd_free(&field);

    size_t const maxlen=(max_video*max_entries)*1024;

    char *buffer_plain = calloc(maxlen, sizeof(char));
    char *buffer_html = calloc(maxlen, sizeof(char));
    char *buffer = calloc(maxlen, sizeof(char));

    if( buffer_plain == NULL || buffer_html == NULL || buffer == NULL ) {
        logmsg(LOG_ERR,"Out of memory when trying to allocate buffer! CRITICAL!");
        exit(1);
    }
// listhtmlrecsbuff(char *buffer, size_t maxlen, size_t maxrecs, size_t style, int only_nonrepeat, int use_csshtml)
    listrepeatrecsbuff(buffer_plain, maxlen, n); // Get plain list of repeating recordings
    listhtmlrecsbuff(buffer, maxlen ,n ,0, TRUE, FALSE); // Get plain list of single recordings
    strcat(buffer_plain,"\n\n");
    strncat(buffer_plain,buffer,maxlen-1-strlen(buffer_plain));

    listhtmlrepeatrecsbuff(buffer_html, maxlen, n, 0); // Get list of repeating recs i HTML format
    listhtmlrecsbuff(buffer, maxlen ,n ,0, TRUE, TRUE); // Get list of single recordings in HTML format
    
    strncat(buffer_html,"\n<p>&nbsp;</p>\n",maxlen-1-strlen(buffer_html));
    strncat(buffer_html,buffer,maxlen-1-strlen(buffer_html));
    free(buffer);

    char subject[255];
    snprintf(subject,255,"List of upcoming repeating recordings");

    if( -1 == sendmail_helper(subject, buffer_plain, buffer_html) ) {
        _writef(sockfd,"Failed to create and send mail.\n");
    } else {
        _writef(sockfd,"List of recordings sent to '%s'\n",send_mailaddress);
    }
    free(buffer_html);
    free(buffer_plain);
}

static void
_cmd_listsingle(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lu             - List single recordings (not part of a recurring sequence).\n"
                 );
        return;
    }

    int ret = matchcmd("^lu" _PR_E, cmd, &field);

    if ( ret != 1 ) {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }

    matchcmd_free(&field);

    size_t const maxlen=(max_video*max_entries)*1024;
    char *buffer = calloc(maxlen, sizeof(char));
    if( buffer == NULL ) {
        _writef(sockfd,"Fatal. Server out of memory.\n");
        return;
    }

    listhtmlrecsbuff(buffer, maxlen ,0 ,0, TRUE, FALSE); // Get plain list of single recordings
    _writef(sockfd,"%s",buffer);
    free(buffer);
}

/*
 * List repeating recordings
 */
static void
_cmd_listrep(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lr             - List repeated recordings.\n"
                 );
        return;
    }

    int ret = matchcmd("^lr" _PR_E, cmd, &field);

    if ( ret != 1 ) {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }

    matchcmd_free(&field);

    size_t const maxlen=(max_video*max_entries)*1024;
    char *buffer = calloc(maxlen, sizeof(char));
    if( buffer == NULL ) {
        _writef(sockfd,"Error. Server out of memory.\n");
        return;
    }

    listrepeatrecsbuff(buffer, maxlen, 0);
    _writef(sockfd,"%s",buffer);

    free(buffer);

}

static void
_cmd_list_ts(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lts         - List all pending recordings in timestamp format\n"
                 );
        return;
    }

    int ret = matchcmd("^lts" _PR_E, cmd, &field);
    int n;
     if ( ret == 1 ) {

        matchcmd_free(&field);
        n = -1; // Default to all records

    } else {

        _cmd_syntaxerror(cmd, sockfd);
        return;

    }

    listrecs((size_t)n, 9, sockfd);
}


/**
 * Command: _cmd_list_stations
 * List all stored stations
 * Syntax:
 * ls
 */
static void
_cmd_list_stations(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "ls           - List defined stations.\n"
                 );
        return;
    }
    list_stations(sockfd);
}


// Helper function for _cmd_list_video_inputs command
void
helper_list_video_inputs(unsigned video, int sockfd) {
    int fd = video_open(video,TRUE);
    if( fd >= 0 ) {

        // Print the identifier for the card as the first line
        char *driver, *card, *version;
        unsigned int capflags;
        if( 0 == _vctrl_get_cardinfo(fd, &driver, &card, &version, &capflags) ) {
            _writef(sockfd, "%d: %s\n",video, card);
        }

        // Get current video input so we can mark this
        int current = -1;
        video_get_input_source(fd,&current);

        // Now list all the available inputs
        char *input_names[32];
        int nbrinputs;
        video_get_input_source_list(fd, &nbrinputs, input_names);
        for(int i=0; i < nbrinputs; i++) {
            _writef(sockfd,"%02d :%s%s\n",
                    i,
                    current == i ? "*" : " ",
                    input_names[i]);
            free(input_names[i]);
        }

        // ... and close the cideo input
        video_close(fd);
    } else {
        _writef(sockfd,"Cannot access video card '%d'.\n",video);
    }
}
/**
 * Command: _cmd_list_video_inputs
 * List all available video inputs for the capture card specified
 * Syntax:
 * li <video>
 */
static void
_cmd_list_video_inputs(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "li [<video>]   - List all video inputs for the capture card.\n"
                 );
        return;
    }
    char **field=(void *)NULL;
    int ret = matchcmd("^li" _PR_S _PR_VIDEO _PR_E, cmd, &field);
    if( ret == 2 ) {
        unsigned video = (unsigned)xatoi(field[1]);
        helper_list_video_inputs(video, sockfd);
    } else {
        ret = matchcmd("^li" _PR_E, cmd, &field);
        if( ret == 1 ) {
            for(unsigned i=0; i < (unsigned)max_video; ++i ) {
                helper_list_video_inputs(i, sockfd);
            }
        } else {
            _cmd_syntaxerror(cmd, sockfd);
        }
    }
    matchcmd_free(&field);
}

/**
 * Command: _cmd_list_video_controls
 * List all settings for the capture card specified
 * Syntax:
 * lc
 */
static void
_cmd_list_controls(const char *cmd, int sockfd) {
    struct vidcontrol vctl[32];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lc <video>   - List all controls for the capture card.\n"
                 );
        return;
    }
    char **field=(void *)NULL;
    int ret = matchcmd("^lc" _PR_S _PR_VIDEO _PR_E, cmd, &field);
    if( ret == 2 ) {
        unsigned video = (unsigned)xatoi(field[1]);
        matchcmd_free(&field);

        int fd = video_open(video,TRUE);
        if( fd >= 0 ) {
            int n=_vctrl_getcontrols(fd, vctl, 32);
            if( n > -1 ) {
                for(int i=0; i < n; i++) {
                    char buff[1024];
                    _vctrl_vidcontrol_tostr(&vctl[i], buff, 1024, FALSE); // Use shortformat which is more human readable
                    _writef(sockfd,buff);
                }
            }
            video_close(fd);
        } else {
            _writef(sockfd,"Cannot access video card '%d'.\n",video);
        }
    } else {
        _cmd_syntaxerror(cmd, sockfd);
    }
}

/**
 * Command: _cmd_diskused
 * Give back the used space of the disk in the dat directory
 * <used> / <tot> (<percent_used>%)
 * For example: 345GG / 2.5TB (15% used)
 * Syntax:
 * df
 */
static void
_cmd_diskused(const char *cmd, int sockfd) {
    char msgbuff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Print the total and used disk space\n"\
                "df \n"
                );
        return;
    }
    char fs[256], size[128], used[128], avail[128];
    int use;
    int ret = get_diskspace(datadir, fs, size, used, avail, &use);
    if( 0 == ret ) {
        snprintf(msgbuff,2048,"%s of %s (%d percent used)\n",used,size,use);
    } else {
        snprintf(msgbuff,2048,"Error. Cannot determine disk space.\n");
    }

    _writef(sockfd, msgbuff);
}


/**
 * Command: _cmd_info
 * Give detailed information about specified recording
 * Syntax:
 * i <id>
 */
static void
_cmd_info(const char *cmd, int sockfd) {
    char msgbuff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "(Not implemented) Give detailed information on specified recording.\n"\
                "i <id>\n"
                );
        return;
    }
    // TODO: Finish this function
    snprintf(msgbuff,2048,"Not (yet) implmented.\n");
    _writef(sockfd, msgbuff);
}

/**
 * Command: _cmd_ongoingtrans
 * List all ongoing transcodings
 * @param cmd
 * @param sockfd
 */
static void
_cmd_ongoingtrans(const char *cmd, int sockfd) {
    char msgbuff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "List current ongoing transcodings.\n"
                );
        return;
    }
    list_ongoing_transcodings(msgbuff,2048,cmd[2]=='l');
    if( strlen(msgbuff) == 0 ) {
        strcpy(msgbuff,"None.\n");
    }
    _writef(sockfd, msgbuff);
}

/**
 * Command: _cmd_refresh_profiles
 * Re-read all specified profiles from disk
 * @param cmd
 * @param sockfd
 */
static void
_cmd_refresh_profiles(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Refresh (re-read from file) all defined transcoding profiles.\n"
                );
        return;
    }
    refresh_transcoding_profiles();
    _writef(sockfd,"All transcoding profiles updated.\n");
}

/**
 * Command: _cmd_ongoingrec
 * List all curent ongoing recordings
 * Syntax:
 * o
 */
static void
_cmd_ongoingrec(const char *cmd, int sockfd) {
    char tmpbuff[512], msgbuff[2048];
    unsigned i, left=2047;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "List ongoing recording for each video.\n"
                );
        return;
    }
    *msgbuff = '\0';
    for (i = 0; i < (unsigned)max_video; i++) {
        if (ongoing_recs[i]) {
            dumprecord(ongoing_recs[i], 0, 0, tmpbuff, 511);
            strncat(msgbuff, tmpbuff, left);
            left -= strlen(tmpbuff);
        }
    }
    if( *msgbuff == '\0' )
        strncpy(msgbuff,"None.\n",2047);
    msgbuff[2047] = '\0';
    _writef(sockfd, msgbuff);
}

/**
 * Command: _cmd_status
 * Give status information on the daemon
 * Syntax:
 * s
 */
static void
_cmd_status(const char *cmd, int sockfd) {
    char msgbuff[1024], currtime[32];

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Print general server status information.\n"
                );
        return;
    }

    time_t now = time(NULL);
    strncpy(currtime, ctime(&now),32);
    currtime[31] = '\0';

    time_t ts_tmp = now - ts_serverstart;
    int sh = ts_tmp / 3600;
    int sday = sh / 24 ;
    int smin = (ts_tmp - sh * 3600) / 60;
    sh = sh - sday*24;
    
    float avg1, avg5, avg15;
    getsysload(&avg1, &avg5, &avg15);
    
    int totaluptime,totalidletime;
    getuptime(&totaluptime, &totalidletime);
    int uh = totaluptime / 3600;
    int uday = uh / 24;
    int umin = (totaluptime - uh*3600) / 60;
    uh = uh - uday*24;

    int wsize=-1;
    char unit[256]={'\0'};
    int nthreads=-1;
    (void)getwsetsize(getpid(), &wsize, unit, &nthreads);

    // Get som additional information of the malloc strategy
    //struct mallinfo {
    //  int arena;    /* non-mmapped space allocated from system */
    //  int ordblks;  /* number of free chunks */
    //  int smblks;   /* number of fastbin blocks */
    //  int hblks;    /* number of mmapped regions */
    //  int hblkhd;   /* space in mmapped regions */
    //  int usmblks;  /* maximum total allocated space */
    //  int fsmblks;  /* space available in freed fastbin blocks */
    //  int uordblks; /* total allocated space */
    //  int fordblks; /* total free space */
    //  int keepcost; /* top-most, releasable (via malloc_trim) space */
    //};

    struct mallinfo minfo = mallinfo();

    if( verbose_log == 3 ) {

        snprintf(msgbuff,511,
                "%-16s: %s"
                "%-16s: %s"
                "%-16s: %02d days %02d hours %02d min\n"
                "%-16s: %02d days %02d hours %02d min\n"
                "%-16s: %.1f %.1f %.1f\n"
                "%-16s: %d %s\n"
                "%-16s: %d\n"
                "MALLOC Info:\n"
                "%-16s: %d\n"
                "%-16s: %d\n"
                "%-16s: %d\n"
                ,
                "Current time", currtime,
                "tvpvrd started", ctime(&ts_serverstart),
                "tvpvrd uptime", sday, sh, smin,
                "Server uptime",uday,uh,umin,
                "Server load",avg1,avg5,avg15,
                "Virtual memory",wsize,unit,
                "Threads",nthreads,
                "Tot. allocated:",minfo.uordblks,
                "Tot. free space:",minfo.fordblks,
                "Max. allocated:",minfo.usmblks
                );

    } else {
        
        snprintf(msgbuff,511,
                "%-16s: %s"
                "%-16s: %s"
                "%-16s: %02d days %02d hours %02d min\n"
                "%-16s: %02d days %02d hours %02d min\n"
                "%-16s: %.1f %.1f %.1f\n",
                "Current time", currtime,
                "tvpvrd started", ctime(&ts_serverstart),
                "tvpvrd uptime", sday, sh, smin,
                "Server uptime",uday,uh,umin,
                "Server load",avg1,avg5,avg15);
    }

    msgbuff[511] = 0 ;

    _writef(sockfd, msgbuff);

    // Get IP from all current connected clients:
    char ctitle[16] = {"Clients"};
    for (unsigned i = 0, clinbr = 1; i < max_clients; i++) {
        if (cli_threads[i]) {
            snprintf(msgbuff, 511,"%-16s: %s%02d: %s, %s", ctitle, "#",  clinbr, client_ipadr[i], ctime(&client_tsconn[i]));
            msgbuff[511] = 0 ;
            _writef(sockfd,msgbuff);
            clinbr++;
            *ctitle='\0'; // We only want the title on the first line
        }
    }

    if( verbose_log == 3 ) {
        tvp_mem_list(sockfd);
    }
}

/**
 * Command: _cmd_time
 * return  current server time
 * Syntax:
 * t
 */
static void
_cmd_time(const char *cmd, int sockfd) {
    char msgbuff[128];

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Give current time on server.\n"
                );
        return;
    }

    time_t ts_tmp = time(NULL);
    snprintf(msgbuff, 127, "%s", ctime(&ts_tmp));
    msgbuff[127] = 0 ;
    _writef(sockfd, msgbuff);
}

/**
 * Command: _cmd_getxmlsfile
 * Show the pending recordings in XMLDB format
 * Syntax:
 * x
 */
static void
_cmd_getxmlfile(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Display XML database of pending recordings.\n"
                );
        return;
    }
    _writeXMLFile(sockfd);
}

/**
 * Command: _cmd_getxmlsfile
 * Show the pending recordings in XMLDB format using HTML encoding
 * Syntax:
 * x
 */
static void
_cmd_getxmlfilehtml(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Display XML database of pending recordings.\n"
                );
        return;
    }
    _writeXMLFileHTML(sockfd);
}

/**
 * Command: _cmd_getSettings
 * List all the settings used, normally adjusted in the ini-file
 * Syntax:
 * z
 */
static void
_cmd_getSettings(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Display all the current settings.\n"
                );
        return;
    }

    _writef(sockfd,
            "%-30s: %s\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %s\n"
            "%-30s: %d\n"
            "%-30s: %s\n"

            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %s\n"
            "%-30s: %s\n"

            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %s\n"
            "%-30s: %s\n"

            "%-30s: %d\n"

            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %s\n"
            "%-30s: %s\n"
            "%-30s: %d\n"

            "%-30s: %s\n"
            "%-30s: %s\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d (%0.1fMB)\n"
            "%-30s: %02d:%02d (h:min)\n"
            "%-30s: %s\n"
            "%-30s: %s\n"
            "%-30s: %s\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %s\n"
            "%-30s: %d\n"
            "%-30s: %s\n"

            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %d\n"
            "%-30s: %s\n"

            ,
            "datadir",datadir,
            "use_profile_directories",use_profiledirectories,
            "use_repeat_rec_basedir",use_repeat_rec_basedir,
            "logfile",logfile_name,
            "verbose_log",verbose_log,
            "inifile_name",inifile,

            "sendmail_on_error",send_mail_on_error,
            "sendmail_on_transcode_end",send_mail_on_transcode_end,
            "sendmail_on_shutdown",shutdown_send_mail,
            "sendmail_address",send_mailaddress,
            "locale_name",locale_name,

            "smtp_use",smtp_use,
            "use_html",use_html_mail,
            "smtp_server",smtp_server,
            "smtp_user",smtp_user,

            "daemonize",daemonize,

            "enable_webinterface",enable_webinterface,
            "require_web_password",require_web_password,
            "web_user",web_user,
            "web_password",web_password,
            "weblogin_timeout",weblogin_timeout,

            "username",username, 
            "xmldbfile_name",xmldbfile,
            "max_video",max_video,
            "max_entries",max_entries,
            "max_clients",max_clients,
            "client_idle_time",max_idle_time,
            "port",tcpip_port,
            "time_resolution",time_resolution,
            "video_buffer_size",VIDBUFSIZE,(float)VIDBUFSIZE/1024.0/1024.0,
            "default_recording_time",defaultDurationHour,defaultDurationMin,
            "xawtv_station file",xawtv_channel_file,
            "default_profile",default_transcoding_profile,
            "frequency_map",frequencymap_name,

            "allow_profiles_adj_encoder",allow_profiles_adj_encoder,
            "external_switch",external_switch,
            "external_input", external_input,
            "external_switch_script", external_switch_script,
            "use_postrec_processing", use_postrec_processing,
            "postrec_processing_script", postrec_script,

            "shutdown_enable",shutdown_enable,
            "shutdown_min_time",shutdown_min_time,
            "shutdown_ignore_users",shutdown_ignore_users,
            "shutdown_min_uptime",shutdown_min_uptime,
            "shutdown_pre_startup_time",shutdown_pre_startup_time,
            "shutdown_script_name",shutdown_script
             
            );
}

/**
 * Command: _cmd_dump_tprofile
 * Print all settings of the named profile
 */
static void
_cmd_dump_tprofile(const char *cmd, int sockfd) {
    char msgbuff[1024];

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "dp @profile\nPrint all the settings of the specified profile.\n",
                xmldbfile
                );
        return;
    }
    char **field=(void *)NULL;
    int ret = matchcmd("^dp" _PR_S _PR_PROFN _PR_E, cmd, &field);
    if( ret == 2 ) {
        dump_transcoding_profile(&field[1][1], msgbuff, 1024);
        matchcmd_free(&field);
	_writef(sockfd,msgbuff);
    } else {
        _cmd_syntaxerror(cmd, sockfd);
    }
}

/**
 * Command: _cmd_updatexmlfile
 * Force all the current recordings to be written to the predefined XML Db file
 * Syntax:
 * u
 */
void
_cmd_updatexmlfile(const char *cmd, int sockfd) {
    char msgbuff[256],tmpbuff[256];

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "u\nForce an update of XML database (%s).\n",
                xmldbfile
                );
        return;
    }

    // First make a backup copy of the old BD
    tmpbuff[0] = '\0';
    strncpy(tmpbuff,xmldbfile,255);
    tmpbuff[255] = '\0';
    strcat(tmpbuff,".backup");
    unlink(tmpbuff);
    rename(xmldbfile,tmpbuff);

    if (writeXMLFile(xmldbfile) >= 0 ) {
        snprintf(msgbuff, 255,"Database successfully updated '%s'\n", xmldbfile);
        msgbuff[255] = 0 ;
        logmsg(LOG_INFO,msgbuff);
    }

    if( sockfd > -1 ) {
        strcat(msgbuff,"\n");
        _writef(sockfd, msgbuff);
    }
}

/**
 * Command: _cmd_nextrec
 * List the immediate upcoming recording on each video
 * Syntax:
 * n
 */
static void
_cmd_nextrec(const char *cmd, int sockfd) {
    char tmpbuff[512];

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "n\nList the next recording for each video.\n"
                );
        return;
    }

    for (unsigned video = 0; video < max_video; video++) {
        if (num_entries[video] > 0) {
            /*
            time_t ts_now = time(NULL);
            time_t until = recs[REC_IDX(video, 0)]->ts_start - ts_now;
            //int days = until / (24*3600);
            int hours = until/3600;
            int minutes = (until - hours*3600)/60;
            _writef(sockfd,"(%02d:%02d) : ",hours,minutes);
            */
            dumprecord(recs[REC_IDX(video, 0)], cmd[1] == 'l' ? 2 : 0, 0,  tmpbuff, 512);
            _writef(sockfd,tmpbuff);
        }
    }
}

/**
 * Command: _cmd_version
 * Return the daemon version
 * Syntax:
 * v
 */
static void
_cmd_version(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "v\nPrint server version.\n"
                );
        return;
    }
    _writef(sockfd, 
            "%s %s [%s] (build: %lu.%lu)"
#ifdef DEBUG_SIMULATE
                        "\n *** DEBUG BUILD *** WILL NOT RECORD REAL VIDEO STREAMS. THIS iS ONLY A DEBUG BUILD.\n"
#endif

            "\n",server_program_name,server_version,            
            is_master_server ? "master" : "client",
            (unsigned long)&__BUILD_DATE,(unsigned long)&__BUILD_NUMBER);
}

/**
 * Command: _cmd_statistics
 * Print statistic information for all existing profiles
 * @param cmd
 * @param sockfd
 */
static void
_cmd_statistics(const char *cmd, int sockfd) {
    char buff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Return statistics for each profile.\n"
                );
        return;
    }
    dump_profilestats(buff,2048);
    buff[2047] = '\0';
    _writef(sockfd,buff);
}

/**
 * Command: _cmd_resetstatistics
 * Reset all statistics
 * @param cmd
 * @param sockfd
 */
static void
_cmd_resetstatistics(const char *cmd, int sockfd) {
    char buff[512];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Reset all collected profile statistics to 0.\n"
                );
        return;
    }
    clear_stats();
    strncpy(buff,"Ok. Statistics reset.\n",511);
    buff[511] = '\0';
    _writef(sockfd,buff);
}


/**
 * Command: _cmd_cardinfo
 * Return basic information on capture card
 * Syntax:
 * v
 */
static void
_cmd_cardinfo(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Print name of capture card and driver information.\n"
                "vc <video>    Print info for specified card\n"
                "vc            Print info for all cards\n"
                );
        return;
    }
    char **field=(void *)NULL;
    int ret = matchcmd("^vc" _PR_S _PR_VIDEO _PR_E, cmd, &field);
    
    if( ret == 2 ) {
        unsigned video = (unsigned)xatoi(field[1]);

        int fd = video_open(video,TRUE);
        if( fd >= 0 ) {
            char *driver, *card, *version;
            unsigned int capflags;
            if( 0 == _vctrl_get_cardinfo(fd, &driver, &card, &version, &capflags) ) {
                video_close(fd);
                _writef(sockfd, "%s, driver=%s v%s\n",card,driver,version);
            }
        } else {
            _writef(sockfd,"Cannot access video card '%d'.\n",video);
        }

    } else {
        ret = matchcmd("^vc" _PR_SO _PR_E, cmd, &field);
        if( ret >= 1 ) {
            // Print info for all available cards
            for(unsigned video=0; video < (unsigned)max_video; video++) {
                int fd = video_open(video,TRUE);
                if( fd >= 0 ) {
                    char *driver, *card, *version;
                    unsigned int capflags;
                    if( 0 == _vctrl_get_cardinfo(fd, &driver, &card, &version, &capflags) ) {
                        video_close(fd);
                        _writef(sockfd, "%d: %s, driver=%s v%s\n",video,card,driver,version);
                    }
                }
            }

        } else {
            _cmd_syntaxerror(cmd, sockfd);
        }
    }

    matchcmd_free(&field);
}


/**
 * Command: _cmd_abort
 * Abort the ongoing recording n the specified video
 * Syntax:
 * ! <id>
 */
static void
_cmd_abort(const char *cmd, int sockfd) {
    char **field = (void *)NULL;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Cancel ongoing recording on specified video.\n"\
                "! <video>\n"
                );
        return;
    }

    int ret = matchcmd("^!" _PR_SO _PR_ID _PR_E,cmd,&field);
    if( ret == 2)  {
        unsigned video = (unsigned)xatoi(field[1]);
        if( video < max_video ) {
            if( ongoing_recs[video] ) {
                _writef(sockfd, "Cancelling recording to '%s' on video %d on user request\n",
                        ongoing_recs[video]->filename,video);
                abort_video[video] = 1;
            }
            else {
                _writef(sockfd, "No recording on video (%d)\n",video);
            }
        }
        else {
            _writef(sockfd, "Video does not exist. (%d)\n",video);
        }
    }
    else {
        _cmd_syntaxerror(cmd, sockfd);
    }

    matchcmd_free(&field);
}


/**
 * Command: _cmd_quickrecording
 * An easy way to start a recording imemdiately only having to specify
 * the absolute minimum of parameters.
 * Syntax:
 * q <ch> [<title>]
 * q <ch> <duration> [<title>]
 *
 * - The recording will start immediate (<= 1 sec)
 * - If no title is specified a title based on current time will be created
 * - Duration is specified as h:mm
 * - If no duration is specified the default duration (%d:%02d) will be used
 */
static void
_cmd_quickrecording(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    time_t ts_end, now = time(NULL);
    char title[128]={0}, cmdbuff[256]={0};

    int sy,sm,sd,sh,smin,ssec;
    int ey,em,ed,eh,emin,esec;
    int dh=defaultDurationHour, dmin=defaultDurationMin;


    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Start recording immediate.\n"\
                "q <ch> [<title>] [@profile]\n"
                "q <ch> <duration> [<title>] [@profile]\n"
                "  - The recording will start immediate (<= 1 sec)\n"
                "  - If no title is specified a title based on current time will be created\n"
                "  - Duration is specified as h:mm\n"
                "  - If no duration is specified the default duration (%d:%02d) will be used\n",
                defaultDurationHour,defaultDurationMin);
        return;
    }


    // We add 5s just to be sure since the recording thread
    // could be busy and it might take some time before this
    // new entry gets checked and we don't want it to be in
    // the past.
    fromtimestamp(now+1,&sy,&sm,&sd,&sh,&smin,&ssec);
    ey=sy; em = sm; ed = sd;
    eh=sh; emin=smin; esec=ssec;
    int profile = 0 ;
    // Variant 1
    // q <channel> <duration> <name> [@profile ...]
    int ret = matchcmd("^q" _PR_S _PR_CHANNEL _PR_S _PR_DURATION _PR_S _PR_TITLE _PR_PROFILES _PR_E,
                       cmd, &field);

    if (ret < 0) {
        // Variant 2
        // q <channel> <duration> [@profile ...]
        ret = matchcmd("^q" _PR_S _PR_CHANNEL _PR_S _PR_DURATION _PR_PROFILES _PR_E,
                       cmd, &field);
        if (ret < 0) {
            // Variant 3
            // q <channel> <name> [@profile ...]
            ret = matchcmd("^q" _PR_S _PR_CHANNEL _PR_S _PR_TITLE _PR_PROFILES  _PR_E,
                    cmd, &field);
            if( ret < 0 ) {
                // Variant 3
                // q <channel> [@profile ...]
                ret = matchcmd("^q" _PR_S _PR_CHANNEL _PR_PROFILES _PR_E,
                    cmd, &field);

                if( ret > 2 ) {
                    profile=2;
                }
            }
            else {
                strncpy(title,field[2],127);
                title[127]=0;
                if( ret > 3) {
                    profile=3;
                }
            }
        }
        else {
            dh = xatoi(field[2]);
            dmin = xatoi(field[3]);
            if( ret > 4 ) {
                profile=4;
            }
        }
    }
    else {
        dh = xatoi(field[2]);
        dmin = xatoi(field[3]);
        strncpy(title,field[4],127);
        title[127]=0;
        if( ret > 5 ) {
            profile=5;
        }
    }
    if (ret > 0) {
        eh += dh;
        emin += dmin;
        ts_end = totimestamp(ey,em,ed,eh,emin,esec);
        fromtimestamp(ts_end,&ey,&em,&ed,&eh,&emin,&esec);
        if( *title == 0 ) {
            sprintf(title,"%s_%d%02d%02d_%02d%02d",field[1],sy,sm,sd,sh,smin);
        }

        // Create the real add command and execut that
        sprintf(cmdbuff,"a %s %02d:%02d:%02d %02d:%02d:%02d %s",field[1],sh,smin,ssec,eh,emin,esec,title);
        if( profile ) {
            strcat(cmdbuff,field[profile]);
        }
        logmsg(LOG_NOTICE,"Sending command: %s",cmdbuff);
        matchcmd_free(&field);
        _cmd_add(cmdbuff,sockfd);
    } else {
        _cmd_syntaxerror(cmd, sockfd);
    }
}

/**
 * Command: _cmd_killtranscoding
 * Kill all ongoing transcodings
 * @param cmd
 * @param sockfd
 */
static void
_cmd_killtranscoding(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "kt        - Kill all ongoing transcoding processes\n"
                "ktf [y/n] - Set/unset kill transcoding(s) flag at server shutdown\n"
                );
        return;
    }

    int ret = matchcmd("^kt" _PR_S _PR_VIDEO _PR_E, cmd, &field);

    if( ret == 2 ) {

        int tidx = xatoi(field[1]);
        char *filename = (char *)NULL;
        if( ongoing_transcodings[tidx] && ongoing_transcodings[tidx]->filename ) {

            filename = strdup(ongoing_transcodings[tidx]->filename);
            if( -1 == kill_ongoing_transcoding(tidx) ) {
                logmsg(LOG_ERR,"Transcoding with index=%d does not exist.",tidx);
                _writef(sockfd,"Transcoding with index=%d does not exist.\n",tidx);
            } else {
                logmsg(LOG_DEBUG,"Stopped transcoding with index=%d ('%s')",tidx,filename);
                _writef(sockfd,"Stopped transcoding of '%s'\n",filename);
            }
            free(filename);

        } else {
            logmsg(LOG_ERR,"Transcoding with index=%d does not exist.",tidx);
            _writef(sockfd,"Transcoding with index=%d does not exist.\n",tidx);
            return;
        }

    } else {

        if( cmd[2] == 'f' ) {
            ret = matchcmd("^ktf" _PR_S "(y|n)"  _PR_E, cmd, &field);

            if( ret > 0 ) {
                dokilltranscodings = field[1][0] == 'y' ? 1 : 0 ;
                _writef(sockfd,"killflag=%c\n",dokilltranscodings ? 'y' : 'n' );

            } else {
                _cmd_syntaxerror(cmd, sockfd);
            }

        } else {
            kill_all_ongoing_transcodings();
            _writef(sockfd,"All ongoing transcodings killed.\n");
        }
    }
    
    matchcmd_free(&field);
}

/**
 * Command: _cmd_transcodefile
 * Transcode a specified file
 * @param cmd
 * @param sockfd
 */
static void
_cmd_transcodefile(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    char profile[32] ;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "tf <file> [@profile] - Transcode specified file using optional named profile\n"
                );
        return;
    }
    
    int ret = matchcmd("^tf" _PR_S _PR_FILEPATH _PR_SO _PR_PROFN _PR_E, cmd, &field);
    if( ret > 0 ) {

        // Check that filename exists
        struct stat sbuff;
        if( -1 == stat(field[1], &sbuff) ) {
            _writef(sockfd,"File does not exist '%s'.\n",field[1]);
            return;
        }

        *profile = '\0';
        if( ret > 2 ) {
            if( !transcoding_profile_exist(&field[2][1]) ) {
                _writef(sockfd,"Specified profile does not exist '%s'.\n",field[2]);
                return;
            }
            strncpy(profile,&field[2][1],31);
        } else {
            strncpy(profile,default_transcoding_profile,31);
        }
        profile[31] = '\0';
        
        (void)transcode_file(field[1], profile);

        _writef(sockfd,"Ok. Transcoding of '%s' using profile '%s' queued.\n",basename(field[1]),profile);
        
        matchcmd_free(&field);

    } else {

        _cmd_syntaxerror(cmd, sockfd);

    }

}

/**
 * Command: _cmd_transcodefilelist
 * Transcode a number of files stored in a file
 * @param cmd
 * @param sockfd
 */
static void
_cmd_transcodefilelist(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    char profile[32] ;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "tl <filename> [@profile] - Read list of videos to transcode from file 'filename'\n"
                );
        return;
    }

    int ret = matchcmd("^tl" _PR_S _PR_FILEPATH _PR_SO _PR_PROFN _PR_E, cmd, &field);
    if( ret > 0 ) {
        *profile = '\0';
        if( ret > 2 ) {
            if( !transcoding_profile_exist(&field[2][1]) ) {
                _writef(sockfd,"Specified profile does not exist '%s'.\n",field[2]);
                return;
            }
            strncpy(profile,field[2],31);
        } else {
            strncpy(profile,default_transcoding_profile,31);
        }
        profile[31] = '\0';

        (void)read_transcode_filelist(field[1],profile);
        _writef(sockfd,"Ok. Transcoding of filelist started.\n");

        matchcmd_free(&field);

    } else {
        _cmd_syntaxerror(cmd, sockfd);
    }
}

/**
 * Command: _cmd_transcodefilesindirectory
 * Transcode all files in the specified directory
 * @param cmd
 * @param sockfd
 */
static void
_cmd_transcodefilesindirectory(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    char profile[32] ;
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "td <dirpath> [@profile] - Transcode all videos in named directory\n"
                );
        return;
    }

    int ret = matchcmd("^tl" _PR_S _PR_FILEPATH _PR_SO _PR_PROFN _PR_E, cmd, &field);
    if( ret > 0 ) {
        *profile = '\0';
        if( ret > 2 ) {
            if( !transcoding_profile_exist(&field[2][1]) ) {
                _writef(sockfd,"Specified profile does not exist '%s'.\n",field[2]);
                return;
            }
            strncpy(profile,field[2],31);
        } else {
            strncpy(profile,default_transcoding_profile,31);
        }
        profile[31] = '\0';

        (void)transcode_whole_directory(field[1],profile);
        _writef(sockfd,"Ok. Transcoding of directory.\n");

        matchcmd_free(&field);

    } else {
        _cmd_syntaxerror(cmd, sockfd);
    }
}

/**
 * Command: _cmd_list_queued_transcodings
 * List information on the specified list of transcodings
 * @param cmd
 * @param sockfd
 */
static void
_cmd_list_queued_transcodings(const char *cmd, int sockfd) {
    char **field = (void *)NULL;
    char buffer[4096] = {'\0'};

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "lq <num> - List information on specified queued file list <num>\n");
        return;
    }

    int ret = matchcmd("^lq" _PR_S _PR_ID _PR_E, cmd, &field);
    if( ret > 1 ) {

        if( -1 == get_queued_transc_filelists_info( xatoi(field[1]), buffer ,4095, 1) ) {
            _writef(sockfd,"Filelist does not exist\n");
        } else {
            _writef(sockfd,buffer);
        }

        matchcmd_free(&field);

    } else {

        ret = matchcmd("^lq" _PR_E, cmd, &field);
        if( ret > 0 ) {
            // Assume that num==1 if no number was specified
            if( -1 == get_queued_transc_filelists_info( 1, buffer ,4095, 1) ) {
                _writef(sockfd,"Filelist does not exist\n");
            } else {
                _writef(sockfd,buffer);
            }

            matchcmd_free(&field);

        } else {
            _cmd_syntaxerror(cmd, sockfd);
        }
    }
}

static void
_cmd_show_last_log(const char *cmd, int sockfd) {
    char **field = (void *)NULL;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "log n - Show the last n lines of the log file\n");
        return;
    }

    int ret = matchcmd("^log" _PR_SO _PR_OPID _PR_E, cmd, &field);
    unsigned n;
    if( ret > 1 ) {

        n = (unsigned)xatoi(field[1]);
        if( n < 1 || n > 999 ) {
            _writef(sockfd,"Error. Number of lines must be in range [1,999]\n");
            return;
        }

        matchcmd_free(&field);

    } else if ( ret == 1 ) {

        n = 10; // Default to the last 10 lines

        matchcmd_free(&field);

    } else {

        _cmd_syntaxerror(cmd, sockfd);
        return;        
        
    }

    unsigned len = 1024*(n+1);
    char *buffer = calloc(1,len*sizeof(char));
    if( buffer == NULL ) {
        logmsg(LOG_ERR,"Out of memory. Cannot allocate buffer to view logfile.");
        return;
    }
    if( -1 == tail_logfile(n,buffer,len) ) {
        _writef(sockfd,"Error. Can not show logfile.\n");
    } else {
        _writef(sockfd,"Last %d lines of logfile:\n",n);
        _writef(sockfd,buffer);
    }
    free(buffer);
}

/**
 * Mail log file
 * @param cmd
 * @param sockfd
 */
static void
_cmd_mail_log(const char *cmd, int sockfd) {
    char subject[128];
    char message[512];


    if (cmd[0] == 'h') {
        _writef(sockfd,
                "mlg - Send entire logfile as attachment to predefined mail address in the config file\n");
        return;
    }

    snprintf(subject,127,"%s","tvpvrd logfile");
    snprintf(message,511,"%s","Logfile from tvpvrd included as attachment.\n\nBest regards\nYour friendly tvpvrd daemon\n\n");
    int rc = smtp_simple_sendmail_with_fileattachment(smtp_server,smtp_user,smtp_pwd,
                                                      subject,
                                                      daemon_email_from, send_mailaddress,NULL,
                                                      message, FALSE,
                                                      logfile_name, -1, -1);

    if( -1 == rc ) {
        _writef(sockfd,"Failed to create and send mail with logfile.\n");
    } else {
        _writef(sockfd,"Logfile sent to '%s'\n",send_mailaddress);
    }
}

static void
_cmd_list_profiles(const char *cmd, int sockfd) {
    char buff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Return a list with names of all defined profiles.\n"
                );
        return;
    }
    list_profile_names(buff,2048);
    buff[2047] = '\0';
    _writef(sockfd,buff);
}

static void
_cmd_list_profiles_htmllinks(const char *cmd, int sockfd) {
    size_t const n_buff=1024;
    char buff[n_buff];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Return a list with names of all defined profiles.\n"
                );
        return;
    }
    list_profile_names_htmllinks(buff,n_buff);
    buff[n_buff-1] = '\0';
    
    // This is a bit of a kludge. Normally we HTML encode all output but in
    // this special case we really want to have the HTML proper interpretation
    // so we turn off the HTML encoding by resetting the global falg which controls
    // the output of the _writef() function.
    int old_flag = htmlencode_flag;
    htmlencode_flag = 0;
    _writef(sockfd,buff);
    htmlencode_flag = old_flag;
}


static void
_cmd_list_waiting_transcodings(const char *cmd, int sockfd) {
    char buff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Return a list of all videos waiting to be transcoded.\n"
                );
        return;
    }
    list_waiting_transcodings(buff,2048);
    buff[2047] = '\0';
    _writef(sockfd,buff);
}

static void
_cmd_view_history(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Return a list of previous N transcodings\n"
                );
        return;
    }   
    hist_list(sockfd);
}

static void
_cmd_mail_history(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Mail a list of previous N transcodings\n"
                );
        return;
    }   
    if( -1 == hist_mail() ) {
        _writef(sockfd,"Could NOT send mail. Unknown error.");
    } else {
        _writef(sockfd,"History mail sent.");
    }
}

/**
 * Reserved for future use
 */
void
cmdfree(void) {
    // Empty
}


/*
 * Initialize command table. Must be called at program startup
 */
void
cmdinit(void) {

    cmdtable[CMD_ADD]               = _cmd_add;
    cmdtable[CMD_DELETE]            = _cmd_delete;
    cmdtable[CMD_LIST]              = _cmd_list;
    cmdtable[CMD_LIST_RECHUMAN]     = _cmd_list_human;
    cmdtable[CMD_LIST_CONTROLS]     = _cmd_list_controls;
    cmdtable[CMD_LIST_STATIONS]     = _cmd_list_stations;
    cmdtable[CMD_LIST_PROFILES]     = _cmd_list_profiles;
    cmdtable[CMD_STATUS]            = _cmd_status;
    cmdtable[CMD_HELP]              = _cmd_help;
    cmdtable[CMD_TIME]              = _cmd_time;
    cmdtable[CMD_INFO]              = _cmd_info;
    cmdtable[CMD_UPDATEXMLFILE]     = _cmd_updatexmlfile;
    cmdtable[CMD_GETXML]            = _cmd_getxmlfile;
    cmdtable[CMD_GETXMLHTML]        = _cmd_getxmlfilehtml;
    cmdtable[CMD_ONGOINGREC]        = _cmd_ongoingrec;
    cmdtable[CMD_NEXTREC]           = _cmd_nextrec;
    cmdtable[CMD_VERSION]           = _cmd_version;
    cmdtable[CMD_CARDINFO]          = _cmd_cardinfo;
    cmdtable[CMD_ABORT]             = _cmd_abort;
    cmdtable[CMD_QUICKRECORDING]    = _cmd_quickrecording;
    cmdtable[CMD_GETSETTINGS]       = _cmd_getSettings;
    cmdtable[CMD_SETPROFILE]        = _cmd_setprofile;
    cmdtable[CMD_UNDEFINED]         = _cmd_undefined;
    cmdtable[CMD_ONGOINGTRANS]      = _cmd_ongoingtrans;
    cmdtable[CMD_REFRESHPROFILE]    = _cmd_refresh_profiles;
    cmdtable[CMD_PRINTPROFILE]      = _cmd_dump_tprofile;
    cmdtable[CMD_STATISTICS]        = _cmd_statistics;
    cmdtable[CMD_RESETSTATS]        = _cmd_resetstatistics;
    cmdtable[CMD_KILLTRANSCODING]   = _cmd_killtranscoding;
    cmdtable[CMD_TRANSCODEFILE]     = _cmd_transcodefile;
    cmdtable[CMD_TRANSCODEFILELIST] = _cmd_transcodefilelist;
    cmdtable[CMD_TRANSCODEDIR]      = _cmd_transcodefilesindirectory;
    cmdtable[CMD_LIST_QUEUEDTRANSC] = _cmd_list_queued_transcodings;
    cmdtable[CMD_SHOW_LASTLOG]      = _cmd_show_last_log;
    cmdtable[CMD_LISTWAITINGTRANSC] = _cmd_list_waiting_transcodings;
    cmdtable[CMD_LIST_VIDEO_INPUTS] = _cmd_list_video_inputs;
    cmdtable[CMD_LIST_TS]           = _cmd_list_ts;
    cmdtable[CMD_ADD_FROMFILE]      = _cmd_addfromfile;
    cmdtable[CMD_DISK_USED]         = _cmd_diskused;
    cmdtable[CMD_MAILLIST_HTML]     = _cmd_maillist_html;
    cmdtable[CMD_LIST_PROFILES_HTMLLINKS] = _cmd_list_profiles_htmllinks;
    cmdtable[CMD_LISTRECREC]        = _cmd_listrep;
    cmdtable[CMD_MAILLIST_RECSINGLE_HTML] = _cmd_maillist_recsingle;
    cmdtable[CMD_MAIL_LOG]          = _cmd_mail_log;
    cmdtable[CMD_LISTRECSINGLE]     = _cmd_listsingle;
    cmdtable[CMD_VIEWHIST]          = _cmd_view_history;
    cmdtable[CMD_MAILHIST]          = _cmd_mail_history;
}

/**
 * Command interpretator. The input is a command string and the
 * return value is the specific command function that will handle
 * that command. This method is not meant to be called directly.
 * @see cmdinterp
 * @return pointer to specific command function
 */
struct cmd_entry {
    char cmd_name[5];
    int  cmd_idx;
};

static ptrcmd
_getCmdPtr(const char *cmd) {

    // We dispatch to command handler based on the first character
    // value. It is then up to the command handler to verify the
    // rest of the command string.
    static struct cmd_entry cmdfunc_master[] = {
        {"af", CMD_ADD_FROMFILE},
        {"ar", CMD_ADD},
        {"a",  CMD_ADD},
        {"df", CMD_DISK_USED},
        {"dp", CMD_PRINTPROFILE},
        {"dr", CMD_DELETE},
        {"d",  CMD_DELETE},
        {"h",  CMD_HELP},
        {"i",  CMD_INFO},
        {"ktf",CMD_KILLTRANSCODING},
        {"kt", CMD_KILLTRANSCODING},
        {"lts",CMD_LIST_TS},
        {"lq", CMD_LIST_QUEUEDTRANSC},
        {"lc", CMD_LIST_CONTROLS},
        {"lh", CMD_LIST_RECHUMAN},
        {"ls", CMD_LIST_STATIONS},
        {"li", CMD_LIST_VIDEO_INPUTS},
        {"lm", CMD_MAILLIST_HTML},
        {"lr", CMD_LISTRECREC},
        {"lu", CMD_LISTRECSINGLE},
        {"lmr",CMD_MAILLIST_RECSINGLE_HTML},
        {"lph",CMD_LIST_PROFILES_HTMLLINKS},
        {"lp", CMD_LIST_PROFILES},
        {"log", CMD_SHOW_LASTLOG},
        {"mlg", CMD_MAIL_LOG},
        {"l",  CMD_LIST},
        {"n",  CMD_NEXTREC},
        {"otl",CMD_ONGOINGTRANS},
        {"ot", CMD_ONGOINGTRANS},
        {"o",  CMD_ONGOINGREC},
        {"q",  CMD_QUICKRECORDING},
        {"rst",CMD_RESETSTATS},
        {"rhm",CMD_MAILHIST},
        {"rh", CMD_VIEWHIST},        
        {"rp", CMD_REFRESHPROFILE},
        {"sp", CMD_SETPROFILE},
        {"st", CMD_STATISTICS},
        {"s",  CMD_STATUS},
        {"tf", CMD_TRANSCODEFILE},
        {"tl", CMD_TRANSCODEFILELIST},
        {"t",  CMD_TIME},
        {"u",  CMD_UPDATEXMLFILE},
        {"vc", CMD_CARDINFO},
        {"v",  CMD_VERSION},
        {"wt", CMD_LISTWAITINGTRANSC},
        {"xx", CMD_GETXMLHTML},
        {"x",  CMD_GETXML},
        {"z",  CMD_GETSETTINGS},
        {"!",  CMD_ABORT}
    };

    static struct cmd_entry cmdfunc_slave[] = {
        {"dp", CMD_PRINTPROFILE},
        {"h",  CMD_HELP},
        {"ktf",CMD_KILLTRANSCODING},
        {"kt", CMD_KILLTRANSCODING},
        {"lp", CMD_LIST_PROFILES},
        {"lq", CMD_LIST_QUEUEDTRANSC},
        {"log",CMD_SHOW_LASTLOG},
        {"otl",CMD_ONGOINGTRANS},
        {"ot", CMD_ONGOINGTRANS},
        {"rst",CMD_RESETSTATS},
        {"rhm", CMD_MAILHIST},
        {"rh", CMD_VIEWHIST},
        {"st", CMD_STATISTICS},
        {"s",  CMD_STATUS},
        {"tf", CMD_TRANSCODEFILE},
        {"tl", CMD_TRANSCODEFILELIST},
        {"t",  CMD_TIME},
        {"v",  CMD_VERSION},
        {"wt", CMD_LISTWAITINGTRANSC},
        {"z",  CMD_GETSETTINGS},
        {"!",  CMD_ABORT}
    };

    static struct cmd_entry *cmdfunc;
    int cmdlen;

    if( is_master_server ) {
        cmdfunc = cmdfunc_master;
        cmdlen = sizeof (cmdfunc_master) / sizeof(struct cmd_entry);
    } else {
        cmdfunc = cmdfunc_slave;
        cmdlen = sizeof (cmdfunc_slave) / sizeof(struct cmd_entry);
    }
    
    int i = 0;
    int inhelp=0;

    if( cmd[0] == 'h' && cmd[1] != '\0'  ) {
        inhelp=1;
        cmd += 2;
    }

    while (i < cmdlen ) {
        size_t len = strlen(cmdfunc[i].cmd_name);
        if( 0 == strncmp(cmd,cmdfunc[i].cmd_name,len) ) {
            if( cmd[len] == ' ' || cmd[len]=='\0' )
                break;
        }
        i++;
    }

    if( inhelp )
        cmd -= 2;
    
    if (i < cmdlen) {
        return cmdtable[cmdfunc[i].cmd_idx];
    } else {
        return cmdtable[CMD_UNDEFINED];
    }
}

/**
 * Read a command string and execute the corresponding command giving the rest
 * of the command string as argument for that specific command to deal with
 *
 */
void
cmdinterp(char *cmd, int sockfd) {

    // Remove trailing spaces
    size_t n=strlen(cmd);
    if( n>=1 && cmd[n-1] == ' ' ) {
        while( n > 0 && cmd[n-1] == ' ')
            --n;
        cmd[n] = '\0';
    }
    (_getCmdPtr(cmd))(cmd,sockfd);
    _writef(sockfd,"\r\n"); // Add \r\n as an indication that the output from the command is finished
    
}


