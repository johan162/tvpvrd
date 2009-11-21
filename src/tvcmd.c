/* =========================================================================
 * File:        TVCMD.C
 * Description: This module manage the command interface and is responsible
 *              to interpret and execute the commands given to the server.
 *              The implementation is based on a fairly standard command
 *              function jump table. Each command is parsed using
 *              pcre_grep() family of functions (Perl Compatible regex).
 * Author:      Johan Persson (johanp@aditus.nu)
 * SVN:         $Id: tvcmd.c 144 2009-11-21 10:48:04Z ljp $
 *
 * Copyright (C) 2009 Johan Persson
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
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <pcre.h>
#include <string.h>
#include <ctype.h>

// Application specific includes
#include "tvcmd.h"
#include "tvpvrd.h"
#include "utils.h"
#include "recs.h"
#include "tvxmldb.h"
#include "freqmap.h"
#include "vctrl.h"
#include "transc.h"
#include "stats.h"

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

#define CMD_UNDEFINED 25

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
void cmdinterp(const char *cmd, int sockfd);
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
    static char msgbuff[2048] =
                        "Commands:\n"\
			"  h    - help\n"\
			"  v    - print version\n"\
			"  t    - print server time\n"\
			"  s    - print server status\n"\
                        "  st   - print profile statistics\n"\
                        "  rst  - reset all statistics\n"\
                        "  vc n - print information on TV-Cards(s)\n"\
			"  l    - list all pending recordings\n"\
                        "  ls   - list all stations\n"\
                        "  lc   - list all controls for the capture card\n"\
			"  d    - delete single recording\n"\
			"  dr   - delete all repeated recording\n"\
                        "  rp   - refresh transcoding profiles from file\n"\
                        "  sp   - set transcoding profile for specified recording\n"\
			"  i    - print detailed information on recording\n"\
			"  a    - Add recording\n"\
			"  ar   - Add repeated recording\n"\
			"  u    - force update of database with recordings\n"\
			"  x    - view database (in XML format) with recordings\n"\
			"  n    - list the immediate next recording on each video\n"\
			"  o    - list the ongoing recording(s)\n"\
                        "  ot   - list the ongoing transcoding(s)\n"\
                        "  kt   - kill all ongoing transcoding(s)\n"\
                        "  ktf  - set/unset kill transcoding flag at shutdown\n"\
			"  q    - quick recording\n"\
            "  z    - display all settings from ini-file\n"\
            "  zp   - display all settings for specified profile\n"\
            "  ! n  - cancel ongoing recording\n"\
                        "Type h <cmd> for syntax of each command\n";
    char **field = (void *)NULL;
    int ret = matchcmd("^h[\\p{Z}]+(v|t|kt|rst|s|sp|ls|lc|l|d|dr|i|a|ar|u|x|n|o|ot|q|zp|z|!)$", cmd, &field);
    if( ret > 0 ) {
        (_getCmdPtr(field[1]))(cmd,sockfd);
        if( field != (void *)NULL ) {
            pcre_free_substring_list((const char **)field);
        }
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

    snprintf(msgbuff, 1024,  "Unrecognized command. Try 'h' for a list of available commands.\n");
    write(sockfd, msgbuff, strlen(msgbuff));

}

//-----------------------------------------------------------------------------
// Various defines for Regular expression matching of commands
//-----------------------------------------------------------------------------

/*
 * First a number of generic unicode regex defines
 */
// Required space(s)
#define _PR_S "[\\p{Z}]+"

// Optional space(s)
#define _PR_SO "[\\p{Z}]*"

// Required alphanumeric sequence
#define _PR_AN "([\\p{L}\\p{N}]+)"

// Required alphanumeric and punctuation sequence
//#define _PR_ANP "([\\p{L}\\p{N}\\p{P}]+)"
#define _PR_ANP "([\\p{L}\\p{N}\\p{P}]+)"

// Required alphanumeric, punctuation and space sequence
#define _PR_ANPS "([\\p{L}\\p{N}\\p{P} ]+)"

// Any sequence of symbols
#define _PR_ANY "(\\X+)"

#define _PR_E "$"

/*
 * Symbolic names for entitis in the command strings
 */
// Recording ID
#define _PR_ID "([\\p{N}]{1,3})"

// Required full time (h:m)
#define _PR_TIME "([0-1][0-9]|2[0-3]):([0-5][0-9])"

// Required full time with optional seconds
#define _PR_TIMS "([0-1][0-9]|2[0-3]):([0-5][0-9])(:[0-5][0-9])?"

// Optional time. Only hour required
#define _PR_OPTIME "([0-1][0-9]|2[0-3])(:([0-5][0-9]))?(:([0-5][0-9]))?"

// required full date
#define _PR_FULLDATE "(201[0-9]|2009)-(0[1-9]|1[0-2])-([0-2][0-9]|3[0-2])"

// Relative date from today
#define _PR_RELDATE "(today|tomorrow|mon|monday|tue|tuesday|wed|wednesday|thu|thursday|fri|friday|sat|saturday|sun|sunday)"
#define _PR_DATE "(" _PR_RELDATE "|" _PR_FULLDATE ")"

#define _PR_VIDEO "([0-5])"
#define _PR_DURATION "(0?[0-3]):([0-5][0-9])"
#define _PR_CHANNEL "([\\p{L}][\\p{L}\\p{N}\\p{P}\\+]*)"

#define _PR_TITLE "(\\p{L}[\\p{L}\\p{N} _-]+)"
#define _PR_OPTITLE "(" _PR_S "(\\p{L}[\\p{L}\\p{N} _-]*))?"

#define _PR_PROFN "(@[\\p{L}\\p{N}]+)?"
#define _PR_PROFE "(" _PR_S _PR_PROFN ")?"
#define _PR_PROFILES "(" _PR_PROFN _PR_SO _PR_PROFN _PR_SO _PR_PROFN ")?"

static void
_cmd_setprofile(const char *cmd, int sockfd) {
    char msgbuff[256];
    int err, ret;
    char **field;

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
        if( updateprofile(atoi(field[1]),field[2]) ) {
            snprintf(msgbuff,255,"Updated profile to '%s' on recording %03d\n",field[2],atoi(field[1]));
        } else {
            snprintf(msgbuff,255,"Failed to set profile '%s' on recording %03d\n",field[2],atoi(field[1]));
        }
        _writef(sockfd,msgbuff);        
    } else {
        _writef(sockfd,"ret=%d\n",ret);
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
    int err, ret, id;
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

    ret = matchcmd("^d" _PR_SO _PR_ID _PR_E, cmd, &field);
    err = ret < 0;

    if ( err ) {
        ret = matchcmd("^dr" _PR_SO _PR_ID _PR_E, cmd, &field);
        err = ret < 0;
    }

    if ( ! err ) {

        id = atoi(field[1]);
        ret = deleterecid(id, cmd[1] == 'r');

        if (ret) {
            snprintf(msgbuff, 256, "Deleted %s #%02d",
                    cmd[1] == 'r' ? "all repeated recordings" : "recording", id);
        }
        else
            snprintf(msgbuff, 256, "Can not delete record #%02d", id);

    } else {
        snprintf(msgbuff, 256, "Command not recognized.");
    }
    if( !ret || err )
        logmsg(LOG_INFO,msgbuff);
    else
        logmsg(LOG_ERR,msgbuff);
    _writef(sockfd, "%s\n", msgbuff);
    if( field != (void *)NULL ) {
        pcre_free_substring_list((const char **)field);
    }

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
    int err, ret;
    int repeat_type, repeat_nbr;
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
      "Unknown relative date specification"
    };

    repeat_type = 0;
    repeat_nbr = 0;
    err = 0;

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
                    "    type: 1=daily, 2=weekly, 3=monthly, 4=Mon-Fri, 5=Sat-Sun\n"
                    "      or: d=daily, w=weekly, m=monthly, f=Mon-Fri, s=Sat-Sun\n"
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
        ret = matchcmd("^ar" _PR_S "([1-5]|d|w|m|f|s)" _PR_S _PR_ID _PR_S _PR_ANY, cmd, &field);
        if( ret == 4 ) {
            if( isdigit(*field[1]) ) {
                repeat_type = atoi(field[1]);
            } else {
                switch( *field[1] ) {
                    case 'd': repeat_type = 1; break;
                    case 'w': repeat_type = 2; break;
                    case 'm': repeat_type = 3; break;
                    case 'f': repeat_type = 4; break;
                    case 's': repeat_type = 5; break;
                }
            }
            repeat_nbr = atoi(field[2]);
            snprintf(cmdbuff,255, "a %s", field[3]);
            cmdbuff[255] = 0;
        }
        else {
            err = 1;
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
        ret = matchcmd(_regex,
                       cmdbuff, &field);
        err =  ret < 2 ;

        if( ! err ) {
            int havemin=0, havesec=0;
            // Field 2 is always hour


            // Now determine how many more digits for time are specified
            int pos=2;
            sh = atoi(field[pos++]);
            if( ret >= 5 && *field[pos++] == ':') {
                smin = atoi(field[pos]);
                havemin=1;
            } else {
                smin = 0;
            }
            pos++;

            if( ret >= 7 &&  *field[pos++] == ':' ) {
                ssec = atoi(field[pos]);
                havesec=1;
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

            // Now the boring analysis on wher we can find the title
            *title = '\0';
            if (ret >= 8) {
                int pos = 7;
                if (*field[pos] != '@') {
                    // Ok this is not a profile
                    strncpy(title, field[pos + 1], 128);
                    pos += 3;
                }

                if (ret > pos) {
                    // We must have a one or more profiles at the end
                    int k=0;
                    while( pos < ret && !err) {
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

                int pos = 2;

                sh = atoi(field[pos++]);
                if( *field[pos++] == ':')
                    smin = atoi(field[pos]);
                else
                    smin = 0;
                pos++;
                if( *field[pos++] == ':')
                    ssec = atoi(field[pos]);
                else
                    ssec = 0;
                pos++;

                eh = atoi(field[pos++]);
                if( *field[pos++] == ':')
                    emin = atoi(field[pos]);
                else
                    emin = 0;
                pos++;
                if( *field[pos++] == ':')
                    esec = atoi(field[pos]);
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

                    if (ret > pos) {
                        // We must have a one or more profiles at the end
                        int k=0;
                        while( pos < ret && !err) {
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
                        sy = ey = atoi(field[4]);
                        sm = em = atoi(field[5]);
                        sd = ed = atoi(field[6]);
                    }

                    if (!err) {
                        int pos = 7;

                        sh = atoi(field[pos++]);
                        if( *field[pos++] == ':')
                            smin = atoi(field[pos]);
                        else
                            smin = 0;
                        pos++;
                        if( *field[pos++] == ':')
                            ssec = atoi(field[pos]);
                        else
                            ssec = 0;
                        pos++;

                        eh = atoi(field[pos++]);
                        if( *field[pos++] == ':')
                            emin = atoi(field[pos]);
                        else
                            emin = 0;
                        pos++;

                        if( *field[pos++] == ':')
                            esec = atoi(field[pos]);
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

                            if (ret > pos) {
                                // We must have a one or more profiles at the end
                                int k=0;
                                while( pos < ret && !err) {
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
                            sy = ey = atoi(field[4]);
                            sm = em = atoi(field[5]);
                            sd = ed = atoi(field[6]);
                        }

                        if (!err) {
                            int pos = 7;

                            sh = atoi(field[pos++]);
                            if( *field[pos++] == ':')
                                smin = atoi(field[pos]);
                            else
                                smin = 0;
                            pos++;
                            if( *field[pos++] == ':')
                                ssec = atoi(field[pos]);
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

                                if (ret > pos) {
                                    // We must have a one or more profiles at the end
                                    int k=0;
                                    while( pos < ret && !err) {
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
                    }

                }
            }
        }
    }

    if( !err ) {
        // Verify that this is either a named station or a recognized channel in the
        // current frequence map
        unsigned tmp;
        strtolower(channel);
        if( -1 == getfreqfromstr(&tmp, channel) ) {
            sprintf(msgbuff, "Specified station/channel \"%s\" is not recognized.\n", channel);
            logmsg(LOG_ERR, "Specified station/channel \"%s\" is not recognized.", channel);
            err = 1;
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
            int k=strlen(title)-1;
            while( k >= 0 && title[k] == ' ') {
                k--;
            }
            title[k+1] = '\0';

            // Create filename from title replacing punctuation and space chars with "_"
            // TODO: This is not multi-byte character safe !
            strncpy(filename, title,123);
            filename[123-1] = '\0';
            for (int i = 0; i < strlen(filename); i++) {
                if (filename[i] == ' ' || filename[i] == '&' ||
                    filename[i] == ':' || filename[i] == '!' ||
                    filename[i] == '#' || filename[i] == '?' ||
                    filename[i] == '/' || filename[i] == '\\' ||
                    filename[i] == '@') {

                    filename[i] = '_';
                }
            }
            strcat(filename, ".mpg");
            strtolower(filename);

            int repeat_name_mangle_type=1;
            entry = newrec(title, filename,
                    ts_start, ts_end,
                    channel,
                    repeat_type > 0,
                    repeat_type, repeat_nbr, repeat_name_mangle_type,
                    profiles);

            // Take the first video stream available
            ret = 0;
            for (int video = 0; video < max_video && !ret; video++) {
                ret = insertrec(video, entry);
            }

            if (ret) {
                dumprecordid(ret, 1, 0, msgbuff, 2047);
            } else {
                freerec(entry,"_cmd_add()");
                err = 5;
            }
        }
    }

    if (err) {
        sprintf(msgbuff, "Error:%d:%s\n", err,add_errstr[err]);
        logmsg(LOG_ERR,"Can not add record. ( %d : %s )", err,add_errstr[err]);
    }

    if( field != (void *)NULL ) {
        pcre_free_substring_list((const char **)field);
    }

    for(int i=0; i < REC_MAX_TPROFILES; i++) {
        free(profiles[i]);
    }

    _writef(sockfd, msgbuff);

}

/**
 * Command: _cmd_list
 * List all pending recordings
 * Syntax:
 * l
 */
static void
_cmd_list(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "l            - List all pending recordings.\n"
                "ls           - List defined stations.\n"
                 );
        return;
    }
    listrecs(0, sockfd);
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

/**
 * Command: _cmd_list_video_controls
 * List all settings for the capture specified
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
    char **field;
    int ret = matchcmd("^lc" _PR_S _PR_VIDEO _PR_E, cmd, &field);
    if( ret == 2 ) {
        int video = atoi(field[1]);
        int fd = video_open(video);
        if( fd >= 0 ) {
            int n=_vctrl_getcontrols(fd, vctl, 32);
            if( n > -1 ) {
                for(int i=0; i < n; i++) {
                    char buff[1024];
                    _vctrl_vidcontrol_tostr(&vctl[i], buff, 1024);
                    _writef(sockfd,buff);
                }
            }
            video_close(fd);
        }
    } else {
        _cmd_undefined(cmd,sockfd);
    }
}


/**
 * Command: _cmd_info
 * Give detailed inform ation about specified recording
 * Syntax:
 * i <id>
 */
static void
_cmd_info(const char *cmd, int sockfd) {
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Give detailed information on specified recording.\n"\
                "i <id>\n"
                );
        return;
    }
    // TODO: Finish this function
}

static void
_cmd_ongoingtrans(const char *cmd, int sockfd) {
    char msgbuff[2048];
    if (cmd[0] == 'h') {
        _writef(sockfd,
                "List current ongoing transcodings.\n"
                );
        return;
    }
    get_ongoing_transcodings(msgbuff,2048,cmd[2]=='l');
    if( strlen(msgbuff) == 0 ) {
        strcpy(msgbuff,"None.\n");
    }
    _writef(sockfd, msgbuff);
}

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
    char tmpbuff[512], tmpbuff2[512],msgbuff[2048];
    int i, left=2047;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "List ongoing recording for each video.\n"
                );
        return;
    }
    strcpy(msgbuff, "");
    for (i = 0; i < max_video; i++) {
        if (ongoing_recs[i]) {
            dumprecord(ongoing_recs[i], 0, tmpbuff, 511);
        } else {
            snprintf(tmpbuff, 511, "None.\n");
        }
        tmpbuff[511]=0;
        snprintf(tmpbuff2,511,"Video #%d: %s",i,tmpbuff);
        tmpbuff2[511] = '\0';
        strncat(msgbuff, tmpbuff2, left);
        left -= strlen(tmpbuff2);
        left = MAX(left,0);
        msgbuff[2048-1] = '\0';
    }
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
    char tmpbuff[512], msgbuff[1024], currtime[32];
    time_t ts_tmp;
    int sh, smin, ssec;
    time_t now;
    float avg1, avg5, avg15;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Print general server status information.\n"
                );
        return;
    }

    now = time(NULL);
    strncpy(currtime, ctime(&now),32);
    currtime[31] = '\0';

    ts_tmp = now - ts_serverstart;
    sh = ts_tmp / 3600;
    smin = (ts_tmp - sh * 3600) / 60;
    ssec = ts_tmp % 60;
    
    getsysload(&avg1, &avg5, &avg15);

    snprintf(msgbuff,511,
            "%15s: %s"
            "%15s: %s"
            "%15s: %02d:%02d:%02d\n"
            "%15s: %.1f %.1f %.1f\n",
            "Current time", currtime,
            "Started", ctime(&ts_serverstart),
            "Uptime", sh, smin, ssec,
            "Server load",avg1,avg5,avg15);
    msgbuff[511] = 0 ;

    _writef(sockfd, msgbuff);

    // Get IP from all current connected clients:
    char ctitle[16] = {"Clients"};
    for (int i = 0, clinbr = 1; i < max_clients; i++) {
        if (cli_threads[i]) {
            snprintf(msgbuff, 511,"%15s: %s%02d: %s, %s", ctitle, "#",  clinbr, client_ipadr[i], ctime(&client_tsconn[i]));
            tmpbuff[511] = 0 ;
            _writef(sockfd,msgbuff);
            clinbr++;
            *ctitle='\0'; // We only want the title on the first line
        }
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
    time_t ts_tmp;

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "Give current time on server.\n"
                );
        return;
    }

    ts_tmp = time(NULL);
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
            "%-24s: %s\n"
            "%-24s: %s\n"
            "%-24s: %s\n"
            "%-24s: %s\n"
            "%-24s: %d\n"
            "%-24s: %d\n"
            "%-24s: %d\n"
            "%-24s: %ds\n"
            "%-24s: %d\n"
            "%-24s: %ds\n"
            "%-24s: %d (%0.1fMB)\n"
            "%-24s: %02d:%02d (h:min)\n"
            "%-24s: %s\n"
            "%-24s: %s\n"
            ,
            "datadir",datadir,
            "logfile",logfile_name,
            "inifile_name",inifile,
            "xmldbfile_name",xmldbfile,
            "max_video",max_video,
            "max_entries",max_entries,
            "max_clients",max_clients,
            "client_idle_time",max_idle_time,
            "port",tcpip_port,
            "time_resolution",time_resolution,
            "video_buffer_size",video_bufsize,(float)video_bufsize/1024.0/1024.0,
            "default_recording_time",defaultDurationHour,defaultDurationMin,
            "xawtv_station file",xawtv_channel_file,
            "default_profile",default_transcoding_profile
            );
}

static void
_cmd_dump_tprofile(const char *cmd, int sockfd) {
    char msgbuff[1024];

    if (cmd[0] == 'h') {
        _writef(sockfd,
                "h @profile\nPrint all the settings of the specified profile.\n",
                xmldbfile
                );
        return;
    }
    char **field;
    int ret = matchcmd("^zp" _PR_S _PR_PROFN _PR_E, cmd, &field);
    if( ret == 2 ) {
        dump_transcoding_profile(&field[1][1], msgbuff, 1024);
		_writef(sockfd,msgbuff);
    } else {
        _cmd_undefined(cmd,sockfd);
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
                "Force an update of XML database (%s).\n",
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
        snprintf(msgbuff, 255,"Database successfully updated '%s'", xmldbfile);
        msgbuff[255] = 0 ;
        logmsg(LOG_INFO,msgbuff);
    }

    /*
    else {
    
        snprintf(msgbuff, 347,"Could not update database '%s', xmldbfile);
        msgbuff[347] = 0 ;
        logmsg(LOG_ERR,msgbuff);

    }
    */

    strcat(msgbuff,"\n");
    _writef(sockfd, msgbuff);
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
                "List the next recording for each video.\n"
                );
        return;
    }

    for (int video = 0; video < max_video; video++) {
        if (num_entries[video] > 0) {
            time_t ts_now = time(NULL);
            time_t until = recs[REC_IDX(video, 0)]->ts_start - ts_now;
            //int days = until / (24*3600);
            int hours = until/3600;
            int minutes = (until - hours*3600)/60;
            _writef(sockfd,"(%02d:%02d) : ",hours,minutes);
            dumprecord(recs[REC_IDX(video, 0)], cmd[1] == 'l' ? 2 : 0,  tmpbuff, 512);
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
                "Print server version.\n"
                );
        return;
    }
    _writef(sockfd, 
            "%s %s (%s)"
#ifdef _LARGEFILE64_SOURCE
                        "\nCompiled with Large File Support (files > 2GB)."
#endif
#ifdef DEBUG_SIMULATE
                        "\n *** DEBUG BUILD *** WILL NOT RECORD REAL VIDEO STREAMS. THIS iS ONLY A DEBUG BUILD.\n"
#endif

            "\n",server_program_name, server_version, server_build_date);
}

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
    strncpy(buff,"Statistics reset.\n",511);
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
    char **field;
    int ret = matchcmd("^vc" _PR_S _PR_VIDEO _PR_E, cmd, &field);
    if( ret == 2 ) {
        int video = atoi(field[1]);
        int fd = video_open(video);
        if( fd >= 0 ) {
            char *driver, *card, *version;
            unsigned int capflags;
            if( 0 == _vctrl_get_cardinfo(fd, &driver, &card, &version, &capflags) ) {
                video_close(fd);
                _writef(sockfd, "%s, driver=%s v%s\n",card,driver,version);
            }
        }
    } else {

        ret = matchcmd("^vc" _PR_E, cmd, &field);
        if( ret == 1 ) {
            // Print info for all available cards
            for(int video=0; video < max_video; video++) {
                int fd = video_open(video);
                if( fd >= 0 ) {
                    char *driver, *card, *version;
                    unsigned int capflags;
                    if( 0 == _vctrl_get_cardinfo(fd, &driver, &card, &version, &capflags) ) {
                        video_close(fd);
                        _writef(sockfd, "Card %02d: %s, driver=%s v%s\n",video,card,driver,version);
                    }
                }
            }
        } else {
            _cmd_undefined(cmd,sockfd);
        }
    }


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
        int video = atoi(field[1]);
        if( video >= 0 && video < max_video ) {
            if( ongoing_recs[video] ) {
                _writef(sockfd, "Cancelling recording to \"%s\" on video %d\n",
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
        _cmd_undefined(cmd,sockfd);
    }
    if( ret > 0 && field != (void *)NULL) {
        pcre_free_substring_list((const char **)field);
    }
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
                "q <ch> [<title>]\n"
                "q <ch> <duration> [<title>]\n"
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
    fromtimestamp(now+5,&sy,&sm,&sd,&sh,&smin,&ssec);
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
                if( ret < 0 ) {
                    _cmd_undefined(cmd,sockfd);
                } else {
                    if( ret > 2 ) {
                        profile=2;
                    }
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
            dh = atoi(field[2]);
            dmin = atoi(field[3]);
            if( ret > 4 ) {
                profile=4;
            }
        }
    }
    else {
        dh = atoi(field[2]);
        dmin = atoi(field[3]);
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
        _cmd_add(cmdbuff,sockfd);
    } else {
        _cmd_undefined(cmd, sockfd);
    }
    if (ret > 0 && field != (void *)NULL ) {
        pcre_free_substring_list((const char **)field);
    }
}

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

    if( cmd[2] == 'f' ) {
        int ret = matchcmd("^ktf" _PR_S "(y|n)"  _PR_E, cmd, &field);

        if( ret > 0 ) {
            dokilltranscodings = field[1][0] == 'y' ? 1 : 0 ;
            
            if (field != (void *)NULL ) {
                pcre_free_substring_list((const char **)field);
            }
            _writef(sockfd,"killflag=%c\n",dokilltranscodings ? 'y' : 'n' );
        } else {
            _writef(sockfd,"Syntax error.\n");
        }

    } else {
        kill_all_ongoing_transcodings();
        _writef(sockfd,"All ongoing transcodings killed.\n");
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
    cmdtable[CMD_LIST_CONTROLS]     = _cmd_list_controls;
    cmdtable[CMD_LIST_STATIONS]     = _cmd_list_stations;
    cmdtable[CMD_STATUS]            = _cmd_status;
    cmdtable[CMD_HELP]              = _cmd_help;
    cmdtable[CMD_TIME]              = _cmd_time;
    cmdtable[CMD_INFO]              = _cmd_info;
    cmdtable[CMD_UPDATEXMLFILE]     = _cmd_updatexmlfile;
    cmdtable[CMD_GETXML]            = _cmd_getxmlfile;
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
    static struct cmd_entry cmdfunc[] = {
        {"h",  CMD_HELP},
        {"lc", CMD_LIST_CONTROLS},
        {"ls", CMD_LIST_STATIONS},
        {"l",  CMD_LIST},
        {"i",  CMD_INFO},
        {"d",  CMD_DELETE},
        {"t",  CMD_TIME},
        {"x",  CMD_GETXML},
        {"u",  CMD_UPDATEXMLFILE},
        {"rp", CMD_REFRESHPROFILE},
        {"sp", CMD_SETPROFILE},
        {"st", CMD_STATISTICS},
        {"rst",CMD_RESETSTATS},
        {"s",  CMD_STATUS},
        {"a",  CMD_ADD},
        {"otl",CMD_ONGOINGTRANS},
        {"ot", CMD_ONGOINGTRANS},
        {"ktf",CMD_KILLTRANSCODING},
        {"kt", CMD_KILLTRANSCODING},
        {"o",  CMD_ONGOINGREC},
        {"n",  CMD_NEXTREC},
        {"vc", CMD_CARDINFO},
        {"v",  CMD_VERSION},
        {"q",  CMD_QUICKRECORDING},
        {"zp", CMD_PRINTPROFILE},
        {"z",  CMD_GETSETTINGS},
        {"!",  CMD_ABORT}
    };

    int cmdlen = sizeof (cmdfunc) / sizeof(struct cmd_entry);
    int i = 0;

    while (i < cmdlen && strncmp(cmd,cmdfunc[i].cmd_name,strlen(cmdfunc[i].cmd_name)) )
        i++;

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
cmdinterp(const char *cmd, int sockfd) {
    (_getCmdPtr(cmd))(cmd,sockfd);
}


