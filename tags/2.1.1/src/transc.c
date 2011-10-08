/* =========================================================================
 * File:        TRANSC.C
 * Description: Module to handle transcoding profiles
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010 Johan Persson
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
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <math.h>

#include <sys/stat.h>
#include <sys/time.h>

#include <libgen.h> // Needed for dirname

// Needed to get symbolic constants for v4l2 controls
#include <linux/videodev2.h>

#include "tvpvrd.h"
#include "transc.h"
#include "utils.h"
#include "stats.h"
#include "config.h"


struct ongoing_transcoding *ongoing_transcodings[3] ;
const int max_ongoing_transcoding = 3;
struct waiting_transcoding_t wtrans[MAX_WAITING_TRANSCODINGS] ;


// We store all the details about a specific transcoding profile in an array
// as well. In theory there is no limit to how many profiles a user may define
static struct transcoding_profile_entry *profiles[MAX_TRANS_PROFILES];
static unsigned num_transcoding_profiles=0;

/**
 * Check if ffmpeg binaries can be found at the specified location
 * @return -1 on failure, 0 on success
 */
int
check_ffmpeg_bin(void) {
    struct stat bstat;
    if( stat(ffmpeg_bin,&bstat) == 0 ) {
        return 0;
    } else {
        logmsg(LOG_ERR,"Can not find '%s' executable. Transcoding is not available.",ffmpeg_bin);
        return -1;
    }
}

/**
 * All ongoing transcodings are stored in a list. This function adds a new
 * transcoding to this list.
 * @param workingdir
 * @param short_filename
 * @param cmd_ffmpeg
 * @return
 */
int
record_ongoingtranscoding(char *workingdir,char *short_filename,char *cmd_ffmpeg, 
                          struct transcoding_profile_entry *profile, pid_t pid) {
    int i;
    for(i=0; i < max_ongoing_transcoding && ongoing_transcodings[i]; i++)
       ;
    if( i>= max_ongoing_transcoding ) {
        logmsg(LOG_ERR,"Can only record at most %d ongoing transcodings.",max_ongoing_transcoding);
        return -1;
    }
    struct ongoing_transcoding *entry = calloc(1,sizeof(struct ongoing_transcoding));
    if( entry == NULL ) {
        logmsg(LOG_ERR,"FATAL: Out of memory in record_ongoingtranscoding()");
        return -1;
    }
    entry->filename = short_filename;
    entry->start_ts = time(NULL);
    entry->workingdir = workingdir;
    entry->cmd = cmd_ffmpeg;
    entry->profile = profile;
    entry->pid = pid;
    ongoing_transcodings[i] = entry;
    return i;
}

/**
 * Forget the specified transcoding. This is called when a transcoding have finished
 * either sucessfully or with an error
 * @param idx The index of the transcoding to remove from the watch list
 */
void
forget_ongoingtranscoding(int idx) {
    if( idx >= 0 && idx < max_ongoing_transcoding ) {
        struct ongoing_transcoding *entry = ongoing_transcodings[idx];
        if( entry ) {
            (void)free(entry);
            ongoing_transcodings[idx] = (struct ongoing_transcoding *)NULL;
        } else {
            logmsg(LOG_ERR,"forget_ongoingtranscoding() : Internal error. 'Trying to remove non-existing record' idx=%d",idx);
        }
    } else {
        logmsg(LOG_ERR,"forget_ongoingtranscoding() : Internal error. 'Index out of bounds' idx=%d. Please report this problem.",idx);
    }
}

int
get_num_ongoing_transcodings(void) {
    int num=0;
    for (int i = 0; i < max_ongoing_transcoding; i++) {
        num += ongoing_transcodings[i] ? 1 : 0;
    }
    return num;
}

/**
 * Fill a buffer with information (text) on all the ongoing recordings
 * @param obuff
 * @param size
 * @return number of currently ongoing transcodings
 */
int
list_ongoing_transcodings(char *obuff, size_t size, int show_ffmpegcmd) {
    char tmpbuff[512];
    int y, m, d, h, min, sec;
    time_t now = time(NULL);

    *obuff = '\0';
    int num=get_num_ongoing_transcodings();

    if( num == 0 ) {
        strncpy(obuff,"None.\n",size-1);
        return 0;
    }

    for (int i = 0; i < max_ongoing_transcoding; i++) {
        if (ongoing_transcodings[i]) {
            int rtime = now-ongoing_transcodings[i]->start_ts;
            int rh = rtime/3600;
            int rmin = (rtime - rh*3600)/60;
            fromtimestamp(ongoing_transcodings[i]->start_ts, &y, &m, &d, &h, &min, &sec);
            if( show_ffmpegcmd ) {
                snprintf(tmpbuff, 511, "[#%02d|%02d:%02d|(%02d:%02d)|%-35.35s|@%s]\n(cmd: %s)\n",
                         i,
                         h, min,
                         rh,rmin,						 
                         ongoing_transcodings[i]->filename,
                         ongoing_transcodings[i]->profile->name,
                         ongoing_transcodings[i]->cmd);
            } else {
                snprintf(tmpbuff, 511, "[#%02d|%02d:%02d|(%02d:%02d)|%-35.35s|@%s]\n",
                         i,
                         h, min,
                         rh,rmin,
                         ongoing_transcodings[i]->filename,
                         ongoing_transcodings[i]->profile->name);               
            }
            tmpbuff[511] = '\0';
            if (strlen(tmpbuff) < size) {
                strncat(obuff, tmpbuff,size-1);
                obuff[size-1] = '\0';
                size -= strlen(tmpbuff);
            } else if( size > strlen("... (out of %02d)\n") ) {
                snprintf(tmpbuff,255,"... (out of %02d)\n",num);
                strncat(obuff,tmpbuff,size-1);
                obuff[size-1] = '\0';
            } else if( size > strlen("...\n") ) {
                strncat(obuff,"...\n",size-1);
            }
        }
    }
    obuff[size - 1] = '\0';
    return num;
}

/**
 * Store the start og the array of transcoding profiles in the supplied
 * parameter.
 * @param start
 * @return The total number of defined profiles
 */
unsigned
get_transcoding_profile_list(struct transcoding_profile_entry **start[]) {
    *start = profiles;
    return num_transcoding_profiles;
}

/**
 * Check if a profile with the specified name exists
 * @param name
 * @return 1 if the name exists 0 otherwise
 */
int
transcoding_profile_exist(char *name) {
    if( strlen(name) == 0 ) 
        return 0;
    for(unsigned i=0; i < num_transcoding_profiles; i++ ) {
        if( strncmp(name,profiles[i]->name,31) == 0 ) {
            return 1;
        }
    }
    return 0;
}

/**
 * Read all transcoding profile from the specified file and store it as profile number
 * 'idx'
 * @param filename
 * @param idx
 * @return
 */
int
_read_transcoding_profile(char *filename,unsigned idx) {
    // Find all the sections named ffmpeg_profile_<profile-name>
    // Loop through all sections to find the corresponding channel

    dictionary *profile = iniparser_load(filename);

    if( profile == NULL ) {
        logmsg(LOG_ERR,"Cannot read transcode profile '%s'",filename);
        return -1;
    }

    char profname[256];
    const unsigned bufsize=256;
    char buffer[bufsize],sname[128];

    strncpy(profname,basename(filename),255);
    profname[255] = '\0';
    int k=(int)strnlen(profname,255);
    while( k>0 && profname[k] != '.' )
        k--;
    if( k <= 0 ) {
        logmsg(LOG_ERR,"Invalid profile name: %s",profname);
        return -1;
    }
    profname[k] = '\0';

    struct transcoding_profile_entry *entry = calloc(1,sizeof(struct transcoding_profile_entry));

    if( entry == NULL ) {
        logmsg(LOG_ERR,"FATAL: Out of memory when reading profile.");
        return -1;
    }

    strncpy(entry->name,profname,31);
    entry->name[31] = '\0';
    
    (void)read_profile_stats(profname);
    
    /*--------------------------------------------------------------------------
    * ENCODER Section
    *--------------------------------------------------------------------------
    */
    strcpy(sname,"encoder");
    
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":keep_mp2file",bufsize-1);
    buffer[bufsize-1] = '\0';     
    entry->encoder_keep_mp2file = (unsigned)iniparser_getboolean(profile, buffer, KEEP_MP2FILE);

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0'; 
    entry->encoder_video_bitrate = (unsigned)validate(500000,8000000,"video_bitrate",
                                            iniparser_getint(profile, buffer, DEFAULT_VIDEO_BITRATE));
                            
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_peak_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';  
    entry->encoder_video_peak_bitrate = (unsigned)validate(500000,8000000,"video_peak_bitrate",
                                                 iniparser_getint(profile, buffer, DEFAULT_VIDEO_PEAK_BITRATE));
                         
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';                                   
    entry->encoder_audio_bitrate = (unsigned)validate(9,13,"audio_bitrate",
                                            iniparser_getint(profile, buffer, DEFAULT_AUDIO_BITRATE));
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_sampling",bufsize-1);
    buffer[bufsize-1] = '\0';                                      
    entry->encoder_audio_sampling = (unsigned)validate(0,2,"audio_sampling",
                                             iniparser_getint(profile, buffer, DEFAULT_AUDIO_SAMPLING));
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_aspect",bufsize-1);
    buffer[bufsize-1] = '\0';                                    
    entry->encoder_video_aspect = (unsigned)validate(0,3,"video_aspect",
                                           iniparser_getint(profile, buffer, DEFAULT_VIDEO_ASPECT));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_frame_size",bufsize-1);
    buffer[bufsize-1] = '\0';  
    strncpy(entry->encoder_video_frame_size_name,
            iniparser_getstring(profile, buffer, DEFAULT_VIDEO_FRAME_SIZE),
    15);
    entry->encoder_video_frame_size_name[15] = '\0';
    
 /*--------------------------------------------------------------------------
     * FFMPEG Section
     *--------------------------------------------------------------------------
     */
    strcpy(sname,"ffmpeg");

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":use_transcoding",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->use_transcoding = (unsigned)iniparser_getboolean(profile, buffer, DEFAULT_USE_TRANSCODING);
    
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->video_bitrate = (unsigned)validate(100,1500,"ffmpeg_video_bitrate",
                                    iniparser_getint(profile, buffer,DEFAULT_PROFILE_VIDEO_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_peak_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->video_peak_bitrate = (unsigned)validate(100,1800,"ffmpeg_video_peak_bitrate",
                                         iniparser_getint(profile, buffer,DEFAULT_PROFILE_VIDEO_PEAK_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->audio_bitrate = (unsigned)validate(32,320,"ffmpeg_audio_bitrate",
                                    iniparser_getint(profile, buffer,DEFAULT_PROFILE_AUDIO_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":pass",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->pass = (unsigned)validate(1,2,"ffmpeg_pass",
                           iniparser_getint(profile, buffer,DEFAULT_PROFILE_PASS));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_top",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_top = (unsigned)validate(0,160,"ffmpeg_crop_top",
                               iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_TOP));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_bottom",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_bottom = (unsigned)validate(0,160,"ffmpeg_crop_bottom",
                                  iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_BOTTOM));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_left",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_left = (unsigned)validate(0,160,"ffmpeg_crop_left",
                                iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_LEFT));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_right",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_right = (unsigned)validate(0,160,"ffmpeg_crop_right",
                                 iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_RIGHT));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_size",bufsize-1);
    buffer[bufsize-1] = '\0';
    strncpy(entry->size,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VIDEO_SIZE),
            31);
    entry->size[31] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":vpre",bufsize-1);
    buffer[bufsize-1] = '\0';
    strncpy(entry->vpre,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VPRE),
            31);
    entry->size[31] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":acodec",bufsize-1);
    buffer[bufsize-1] = '\0';
    strncpy(entry->acodec,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VCODEC),
            31);
    entry->acodec[31] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":vcodec",bufsize-1);
    buffer[bufsize-1] = '\0';
    strncpy(entry->vcodec,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VCODEC),
            31);
    entry->vcodec[31] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":file_extension",bufsize-1);
    buffer[bufsize-1] = '\0';
    strncpy(entry->file_extension,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_FILE_EXTENSION),
            7);
    entry->file_extension[7] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":extra_ffmpeg_options",bufsize-1);
    buffer[bufsize-1] = '\0';
    strncpy(entry->extra_ffmpeg_options,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_EXTRA_FFMPEG_OPTIONS),
            255);
    entry->extra_ffmpeg_options[255] = '\0';
    
    strncpy(entry->filename,filename,255);
    entry->filename[255] = '\0';

    profiles[idx] = entry;
    logmsg(LOG_NOTICE,"  -- read profile '%s'", entry->name);

    iniparser_freedict(profile);
    return 0;
}

/**
 * Read all defined transcoding profiles in the profile directory
 * @return 0 on sucess, -1 on failure
 */
int
read_transcoding_profiles(void) {
    char dirbuff[256];
    struct stat fstat;

    // Search for the profile directory in
    // 1) <CONFDIR>/tvpvrd/profiles
    // 2) <cwd>/profiles
    snprintf(dirbuff,255,"%s/tvpvrd/profiles",CONFDIR);
    logmsg(LOG_DEBUG,"Profile directory: %s",dirbuff);

    if( -1 == stat(dirbuff,&fstat) ) {
        logmsg(LOG_ERR,"Cannot find transcoding profiles in '%s' ( %d : %s )",dirbuff,
               errno,strerror(errno));
        return -1;
    }

    // Now loop through all profile files in 'dirbuff' directory
    DIR *dp;
    struct dirent *dirp;
    char tmpbuff[512];

    dp = opendir(dirbuff);
    if (dp == NULL) {
        logmsg(LOG_ERR,"Cannot open directory with profiles (%d : %s)",errno,strerror(errno));
        return -1;
    }

    while ((dirp = readdir(dp)) != NULL) {
        if ((strcmp(dirp->d_name, ".") != 0) && (strcmp(dirp->d_name, "..") != 0)) {

            // Only read files with suffix ".profile"
            unsigned len = strnlen(dirp->d_name,512);
            if( len > 8 && (strncmp(".profile",dirp->d_name+len-8,9) == 0) ) {

                snprintf(tmpbuff, 512, "%s/%s", dirbuff, dirp->d_name);
                lstat(tmpbuff, &fstat);

                if (S_ISREG(fstat.st_mode) || S_ISLNK(fstat.st_mode)) {

                    if( num_transcoding_profiles >= MAX_TRANS_PROFILES ) {
                        logmsg(LOG_ERR,"Maximum number of transcoding profiles (%d) exceeded.",
                               MAX_TRANS_PROFILES);
                        closedir(dp);
                        return -1;
                    }
                    logmsg(LOG_INFO,"Reading transcoding profile file '%s'",tmpbuff);

                    (void)_read_transcoding_profile(tmpbuff,num_transcoding_profiles++);

                }
                
            } else {
                logmsg(LOG_ERR,"Ignoring non profile file '%s' in profile directory.",dirp->d_name);
            }

        }
    }
    closedir(dp);
    return 0;
}

/**
 * Re-read all transcoding profiles from disk
 */
void
refresh_transcoding_profiles(void) {
    // Re-read allexisting profiles
    for(unsigned i=0; i < num_transcoding_profiles; i++) {
        (void)_read_transcoding_profile(profiles[i]->filename,i);
    }
}

/**
 * Dump a textual representation of the supplied transcoding profile to the
 * buffer given as the second argument of specified maxlen.
 * @param profile A pointer to the profile to be displayed
 * @param buff Buffer to hold the text
 * @param size Max length of the supplied buffer
 */
void
_dump_transcoding_profile(struct transcoding_profile_entry *profile, char *buff, size_t size) {
    double sampling[] = {44.1, 48.0, 32.0};
    int abps[] = {192, 224, 256, 320, 384};
    char *aspect[] = {"1x1","4x3","16x9","221x100"};

    snprintf(buff,size-1,
    "%-22s: %s\n"           /* name */
    "ENCODER:\n"
    "%-22s: %d\n"           /* video_bitrate */
    "%-22s: %d\n"           /* video_peak_bitrate */
    "%-22s: %.1f\n"         /* audio sampling */
    "%-22s: %d\n"           /* audio_bitrate*/
    "%-22s: %s\n"           /* aspect */
    "%-22s: %s\n"           /* frame_size */
    "FFMPEG:\n"
    "%-22s: %d\n"
    "%-22s: %d\n"
    "%-22s: %d\n"
    "%-22s: %s\n"
    "%-22s: %s\n"
    "%-22s: %d\n"
    "%-22s: %s\n"
    "%-22s: %d\n"
    "%-22s: %s\n"
    "%-22s: l=%d, r=%d, t=%d, b=%d\n"
    "%-22s: %s\n"
    "%-22s: %s\n",
    "name",profile->name,

    "video_bitrate", profile->encoder_video_bitrate,
    "video_peak_bitrate", profile->encoder_video_peak_bitrate,
    "audio_sampling", sampling[profile->encoder_audio_sampling],
    "audio_bitrate", abps[profile->encoder_audio_bitrate-9],
    "aspect", aspect[profile->encoder_video_aspect],
    "size",profile->encoder_video_frame_size_name,
    
    /* FFMPEG Settings */
    "use_transcoding", profile->use_transcoding,
    "video_bitrate", profile->video_bitrate,
    "video_peak_bitrate", profile->video_peak_bitrate,
    "vcodec",profile->vcodec,
    "vpre",profile->vpre,
    "pass",profile->pass,
    "acodec", profile->acodec,
    "audio_bitrate",profile->audio_bitrate,
    "video_size",profile->size,
    "crop",profile->crop_left,profile->crop_right,profile->crop_top,profile->crop_bottom,
    "ffmpeg_extra_options",profile->extra_ffmpeg_options,
    "file_extension",profile->file_extension
    );
}

/**
 * Dump the named transcoding profile as atextual representation to the specified buffer
 * @param name
 * @param buff
 * @param size
 * @return
 */
int 
dump_transcoding_profile(char *name, char *buff, size_t size) {
    unsigned i;
    for(i=0; i < num_transcoding_profiles && strcmp(name,profiles[i]->name); i++)
        ;
    if( i < num_transcoding_profiles ) {
        _dump_transcoding_profile(profiles[i],buff,size);
        return 0;
    } else {
        return -1;
    }
}

/**
 * Get the details of the named transcoding profile
 * @param name
 * @param entry
 */
void
get_transcoding_profile(char *name, struct transcoding_profile_entry **entry) {

    unsigned i=0;
    while( i < num_transcoding_profiles && strcmp(name,profiles[i]->name) )
        i++;
    if( i >= num_transcoding_profiles ) {
        logmsg(LOG_ERR,"Cannot find requested transcoding profile '%s' falling back on default profile '%s'",
                name,DEFAULT_TRANSCODING_PROFILE);
        i=0;
        while( i < num_transcoding_profiles && strcmp(profiles[i]->name,DEFAULT_TRANSCODING_PROFILE) )
            i++;
        if( i >= num_transcoding_profiles ) {
            logmsg(LOG_ERR,"Default transcoding profile '%s' does not exist. Falling back on the first profile '%s'",
                   DEFAULT_TRANSCODING_PROFILE,profiles[0]->name);
            i=0;
        }
    }

    logmsg(LOG_DEBUG,"get_transcoding_profile() : Found transcoding profil '%s' at index=%d",name,i);

    *entry = profiles[i];
}

/**
 * Generate a textual list of all currently specified transcoding profiles
 * @param buff
 * @param maxlen
 */
int
list_profile_names(char *buff,size_t maxlen) {
    unsigned idx=0;
    char tmpbuff[255];
    *buff = '\0';
    while( idx < num_transcoding_profiles ) {
        snprintf(tmpbuff,254,"#%02d : %s\n",idx+1,profiles[idx]->name);
        if( maxlen > strlen(tmpbuff) ) {
            strcat(buff,tmpbuff);
            maxlen -= strlen(tmpbuff);
        } else {
            logmsg(LOG_ERR,"supplied buffer size in list_profile_names() too small to hold all profiles");
            return -1;
        }
        ++idx;
    }
    return 0;
}

unsigned
get_profile_names(const char *list[],size_t maxlen) {
    unsigned n = MIN(maxlen,num_transcoding_profiles);
    for( unsigned i=0; i < n; ++i ) {
        list[i] = profiles[i]->name;
    }
    return n;
}

/*
 * Wait until the load on the server is below the acceptable limit to
 * start a new transcoding process. Return the number of seconds
 * @return 0 if Ok to start transcode, -1 otherwise
 */
int
wait_to_transcode(char *filename) {
    // We will not start transcoding until the 5 min load average is below max_load_for_transcoding
    unsigned waiting_time = 0;
    unsigned backoff_time = 7*60;
    unsigned logcnt=0;
    float avg1 = 0, avg5 = 0, avg15 = 0;
    getsysload(&avg1, &avg5, &avg15);

    if( avg5 > max_load_for_transcoding ) {
        logmsg(LOG_INFO, "Waiting to transcode '%s'. Current load %.2f. Must be < %d.",
               filename, avg5, max_load_for_transcoding);
    }

    while (avg5 > max_load_for_transcoding && 
           (max_waiting_time_to_transcode==0 || waiting_time < (unsigned)max_waiting_time_to_transcode)) {
        sleep(backoff_time);
        waiting_time += backoff_time;
        getsysload(&avg1, &avg5, &avg15);

        // Only write the waiting message to the logs every 4:th cycle to avoid
        // too much noise in the logflie
        if( ++logcnt > 3 && avg5 > max_load_for_transcoding ) {
            logmsg(LOG_NOTICE, "Still waiting to to transcode '%s'. Current load %.2f > %d. Total waiting time: %d min",
                   filename, avg5, max_load_for_transcoding,waiting_time/60);
            logcnt=0;
        }
        
    }
    return waiting_time < (unsigned)max_waiting_time_to_transcode || max_waiting_time_to_transcode==0 ? 0 : -1;
}

/*
 * Remember each transcoding that are waiting to start when the server load is
 * below the treshold
 */
int
remember_waiting_transcoding(char *short_filename,char *profile_name) {

    // Find the first empty slot
    int idx=0;
    while( idx < MAX_WAITING_TRANSCODINGS && *wtrans[idx].filename )
        ++idx;

    if( idx >= MAX_WAITING_TRANSCODINGS ) {
        logmsg(LOG_ERR,"Can only record a maximum of %d waiting transcoding", MAX_WAITING_TRANSCODINGS);
        return -1;
    }

    strncpy(wtrans[idx].filename,short_filename,254);
    wtrans[idx].filename[254] = '\0';
    strncpy(wtrans[idx].profilename,profile_name,254);
    wtrans[idx].profilename[254] = '\0';
    wtrans[idx].timestamp = time(NULL);

    return idx;
}

/*
 * Remove a queued transcoding once it has started
 */
int
forget_waiting_transcoding(int idx) {
    if( idx >= 0 && idx < MAX_WAITING_TRANSCODINGS ) {
        *wtrans[idx].filename = '\0';
        return 0;
    } else {
        logmsg(LOG_ERR,"Internal error. Illegal index for forget_waiting_transcoding()");
        return -1;
    }
}

/*
 * Return a list of all transcoding waiting in queue
 */
int
list_waiting_transcodings(char *buffer,size_t maxlen) {
    int num=0;
    int idx=0;
    char tmpbuff[1024];
    *buffer = '\0';
    while( idx < MAX_WAITING_TRANSCODINGS ) {
        if( *wtrans[idx].filename ) {
            ++num;
            time_t waiting_time = time(NULL)-wtrans[idx].timestamp;
            int whours = waiting_time/3600;
            waiting_time -= whours*3600;
            int wmin = waiting_time/60;
            int y,m,d,h,smin,ssec;
            fromtimestamp(wtrans[idx].timestamp,&y,&m,&d,&h,&smin,&ssec);
            snprintf(tmpbuff,1023,"[#%02d|%02d:%02d|(%02d:%02d)|%-35.35s|@%s]\n",
                    num,h,smin,
                    whours,wmin,
                    wtrans[idx].filename,wtrans[idx].profilename);
            if( maxlen > strlen(tmpbuff) ) {
                strcat(buffer,tmpbuff);
                maxlen -= strlen(tmpbuff);
            } else {
                logmsg(LOG_ERR,"Buffer to use to store waiting transcodings is too small.");
                return -1;
            }
        }
        ++idx;
    }

    if( num == 0 ) {
        strncpy(buffer,"None.\n",maxlen-1);
    }

    return 0;
}

/**
 * Construct the command line for ffmpeg
 * @param file
 * @param profile
 * @param destfile
 * @param destsize
 * @param cmd
 * @param size
 * @return
 */
int
create_ffmpeg_cmdline(char *filename, struct transcoding_profile_entry *profile, char *destfile, size_t destsize, char *cmd, size_t size) {

    // Build command line for ffmpeg
    strcpy(destfile, filename);
    int l = (int)strlen(filename)-1;
    while(l >= 0 && destfile[l] != '.') {
        l--;
    }
    if( l <= 0 ) {
        logmsg(LOG_ERR,"Cannot create ffmpeg command string. Invalid filename (no file extension found on source file '%s')",
               filename);
        return -1;
    }

    destfile[l] = '\0';
    strncat(destfile, profile->file_extension,destsize);

    if (profile->pass == 1) {
        if( strlen(profile->size) > 0 ) {
#ifdef OLDER_FFMPEG
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -vcodec %s -vpre %s -b %dk -bt %dk "
                    " -croptop %d -cropbottom %d -cropleft %d -cropright %d "
                    " -acodec %s -ab %dk "
                    " -s %s"
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->crop_top, profile->crop_bottom, profile->crop_left, profile->crop_right,
                    profile->acodec, profile->audio_bitrate,
                    profile->size,
                    profile->extra_ffmpeg_options, 
                    destfile);
#else
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -vcodec %s -vpre %s -b %dk -bt %dk "
                    " -acodec %s -ab %dk "
                    " -s %s"
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->acodec, profile->audio_bitrate,
                    profile->size,
                    profile->extra_ffmpeg_options,
                    destfile);

#endif
        } else {
#ifdef OLDER_FFMPEG
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -vcodec %s -vpre %s -b %dk -bt %dk "
                    " -croptop %d -cropbottom %d -cropleft %d -cropright %d "
                    " -acodec %s -ab %dk "
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->crop_top, profile->crop_bottom, profile->crop_left, profile->crop_right,
                    profile->acodec, profile->audio_bitrate,
                    profile->extra_ffmpeg_options, 
                    destfile);
#else
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -vcodec %s -vpre %s -b %dk -bt %dk "
                    " -acodec %s -ab %dk "
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->acodec, profile->audio_bitrate,
                    profile->extra_ffmpeg_options,
                    destfile);
#endif
        }
        
    } else {
        if( strlen(profile->size) > 0 ) {
#ifdef OLDER_FFMPEG
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -pass 1 -vcodec %s -vpre fastfirstpass -b %dk -bt %dk "
                    " -croptop %d -cropbottom %d -cropleft %d -cropright %d "
                    " -an "
                    " -s %s "
                    " -f rawvideo -y %s "
                    "/dev/null > /dev/null 2>&1; "
                    "%s -v 0 -i %s -threads 0 -pass 2 -vcodec %s -vpre %s -b %dk -bt %dk "
                    " -croptop %d -cropbottom %d -cropleft %d -cropright %d "
                    "-acodec %s -ab %dk "
                    " -s %s "
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->crop_top, profile->crop_bottom, profile->crop_left, profile->crop_right,
                    profile->size,
                    profile->extra_ffmpeg_options,
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->crop_top, profile->crop_bottom, profile->crop_left, profile->crop_right,
                    profile->acodec, profile->audio_bitrate,
                    profile->size,
                    profile->extra_ffmpeg_options,
                    destfile);
#else
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -pass 1 -vcodec %s -vpre fast_firstpass -b %dk -bt %dk "
                    " -an "
                    " -s %s "
                    " -f rawvideo -y %s "
                    "/dev/null > /dev/null 2>&1; "
                    "%s -v 0 -i %s -threads 0 -pass 2 -vcodec %s -vpre %s -b %dk -bt %dk "
                    "-acodec %s -ab %dk "
                    " -s %s "
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->size,
                    profile->extra_ffmpeg_options,
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->acodec, profile->audio_bitrate,
                    profile->size,
                    profile->extra_ffmpeg_options,
                    destfile);
#endif
        } else {
#ifdef OLDER_FFMPEG
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -pass 1 -vcodec %s -vpre fastfirstpass -b %dk -bt %dk "
                    " -croptop %d -cropbottom %d -cropleft %d -cropright %d "
                    " -an "
                    " -f rawvideo -y %s "
                    "/dev/null > /dev/null 2>&1; "
                    "%s -v 0 -i %s -threads 0 -pass 2 -vcodec %s -vpre %s -b %dk -bt %dk "
                    " -croptop %d -cropbottom %d -cropleft %d -cropright %d "
                    "-acodec %s -ab %dk "
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->crop_top, profile->crop_bottom, profile->crop_left, profile->crop_right,
                    profile->extra_ffmpeg_options,
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->crop_top, profile->crop_bottom, profile->crop_left, profile->crop_right,
                    profile->acodec, profile->audio_bitrate,
                    profile->extra_ffmpeg_options,
                    destfile);
#else
            snprintf(cmd, size,
                    "%s -v 0 -i %s -threads 0 -pass 1 -vcodec %s -vpre fast_firstpass -b %dk -bt %dk "
                    " -an "
                    " -f rawvideo -y %s "
                    "/dev/null > /dev/null 2>&1; "
                    "%s -v 0 -i %s -threads 0 -pass 2 -vcodec %s -vpre %s -b %dk -bt %dk "
                    "-acodec %s -ab %dk "
                    " -y %s %s > /dev/null 2>&1",
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->extra_ffmpeg_options,
                    ffmpeg_bin, filename,
                    profile->vcodec, profile->vpre, profile->video_bitrate, profile->video_peak_bitrate,
                    profile->acodec, profile->audio_bitrate,
                    profile->extra_ffmpeg_options,
                    destfile);
#endif
            
        }
    }
#ifdef OLDER_FFMPEG
    logmsg(LOG_NOTICE, "[Using old style] ffpmeg command: %s", cmd);
#else
    logmsg(LOG_NOTICE, "ffpmeg command: %s", cmd);
#endif

    return 0;

}

/**
 * Kille ongoing transcoding with index idx
 * @param idx
 */
int
kill_ongoing_transcoding(int idx) {

    if( idx >= 0 && idx < max_ongoing_transcoding ) {
        if (ongoing_transcodings[idx]) {
            logmsg(LOG_NOTICE,"Killing 'ffmpeg' process group %d",ongoing_transcodings[idx]->pid);
            (void)killpg(ongoing_transcodings[idx]->pid,SIGSTOP);
            usleep(50000);
            (void)killpg(ongoing_transcodings[idx]->pid,SIGKILL);
        }
    } else {
        logmsg(LOG_ERR,"No ongoing transcoding with index=%d",idx);
        return -1;
    }
    return 0;

}

/**
 * Kill all ongoing transcoding processes
 */
void
kill_all_ongoing_transcodings(void) {

    // Send the Kill signal to the process group of all ongoing transcodings
    for (int i = 0; i < max_ongoing_transcoding; i++) {
        kill_ongoing_transcoding(i);
    }

}

// Number of ongoing file transcoding threads
int nfiletransc_threads = 0;

// Mutex to protect the transcoding thread array and index variables
pthread_mutex_t filetransc_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure used to pass arguments to the transcoding process
struct transc_param {
    char *filename;
    char *profilename;
};

/**
 * Force a transcode of the specified file using the named profile.
 * If profile is the empty string then the default profile will be used.
 * The transcoding will start immediately regardless of the server load
 * if the argument wait is = 0
 *
 * This function is run in its own thread.
 *
 * @param filename
 * @param profile
 * @param wait
 * @return
 */
void *
_transcode_file(void *arg) {
    struct transcoding_profile_entry *profile;
    char cmdbuff[1024], cmd_ffmpeg[512], destfile[128];
    int runningtime = 0;
    char filename[512];
    char profilename[128];
    struct transc_param *param = (struct transc_param *) arg;

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for exah created thread
    pthread_detach(pthread_self());

    strncpy(filename,param->filename,511);
    strncpy(profilename, param->profilename,128);

    logmsg(LOG_DEBUG,"_transcode_file() : profilename='%s'",profilename);

    free(param->filename);
    free(param->profilename);
    free(param);

    if (-1 == wait_to_transcode(filename)) {
        logmsg(LOG_ERR, "Can not start transcoding of '%s'. Server too busy.", filename);
        pthread_mutex_lock(&filetransc_mutex);
        nfiletransc_threads--;
        pthread_mutex_unlock(&filetransc_mutex);

        pthread_exit(NULL);
        return (void *) 0;
    }

    // Create a temporary directory which we will use as working directory during
    // the transcoding process. This is necessary since ffmpeg always uses the same
    // name for some property/log files which means that two or more transcodings
    // can never run in the same directory.
    char wdirname[128];
    char suffix[10];
    strncpy(wdirname, basename(filename), 127);
    wdirname[127] = '\0';
    strip_filesuffix(wdirname,suffix,10);

    char wdirbuff[256];
    snprintf(wdirbuff,255,"vtmp/%s",wdirname);

    char workingdir[256];
    snprintf(workingdir,255, "%s/%s",datadir,wdirbuff);
    workingdir[255] = '\0';

    struct stat fstat;
    if( 0 == stat(workingdir,&fstat) ) {
        // Directory already exists. We play safe an bail out
        logmsg(LOG_ERR,"Directory '%s' already exists. Cannot transcode. Please remove directory manually.");
        pthread_mutex_lock(&filetransc_mutex);
        nfiletransc_threads--;
        pthread_mutex_unlock(&filetransc_mutex);

        pthread_exit(NULL);
        return (void *) 0;
    } else {
        logmsg(LOG_NOTICE,"Failed stat() on '%s' (%d : %s)",workingdir,errno,strerror(errno));
        chkcreatedir(datadir,wdirbuff);
    }

    // Link the file to transcode into this new working directory
    snprintf(wdirbuff,255,"%s/%s",workingdir,basename(filename));
    if( -1 == symlink(filename,wdirbuff) ) {
        logmsg(LOG_ERR,"Cannot symlink file '%s' to transcode into working directory '%s' ( %d : %s )",
                filename,wdirbuff,errno,strerror(errno));
        pthread_mutex_lock(&filetransc_mutex);
        nfiletransc_threads--;
        pthread_mutex_unlock(&filetransc_mutex);

        pthread_exit(NULL);
        return (void *) 0;
    }
    logmsg(LOG_INFO,"Linked file '%s' into temporary directory '%s' ",filename,wdirbuff);

    int transcoding_done = 0;
    get_transcoding_profile(profilename, &profile);
    logmsg(LOG_INFO, "Using profile '%s' for transcoding of '%s'", profile->name, filename);

    create_ffmpeg_cmdline(basename(filename), profile, destfile, 128, cmd_ffmpeg, 512);
    snprintf(cmdbuff, 1023, "cd %s;%s", workingdir, cmd_ffmpeg);
    cmdbuff[1023] = '\0';

    pid_t pid;
    if ((pid = fork()) == 0) {
        // In the child process
        // Make absolutely sure everything is cleaned up except the standard
        // descriptors
        for (int i = getdtablesize(); i > 2; --i) {
            (void) close(i);
        }

        // Since the ffmpeg command is run as a child process (via the sh comamnd)
        // we need to make sure all of this is in the same process group. This is
        // done in order so that we can kill the ffmpeg command if the server
        // is stopped by the user. The pid returned by the fork() will not be
        // the same process as is running the 'ffmpeg' command !
        setpgid(getpid(), 0); // This sets the PGID to be the same as the PID
        if (-1 == nice(20)) {
            logmsg(LOG_ERR, "Error when calling 'nice()' : ( %d : %s )", errno, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        if ( -1 == execl("/bin/sh", "sh", "-c", cmdbuff, (char *) 0) ) {
            logmsg(LOG_ERR, "Error when calling execl() '/bin/sh/%s' : ( %d : %s )", cmdbuff,errno, strerror(errno));
            _exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        logmsg(LOG_ERR, "Fatal. Can not create process to do transcoding for file '%s' (%d : %s)",
               basename(filename), errno, strerror(errno));

        pthread_mutex_lock(&filetransc_mutex);
        nfiletransc_threads--;
        pthread_mutex_unlock(&filetransc_mutex);

        pthread_exit(NULL);
        return (void *) 0;
        
    } else {

        // In parent which will be watching the ffmpeg command execution
        logmsg(LOG_INFO, "Successfully started process pid=%d for transcoding '%s'.", pid, basename(filename));

        pthread_mutex_lock(&recs_mutex);
        int tidx = record_ongoingtranscoding(workingdir, basename(filename), cmd_ffmpeg, profile, pid);
        pthread_mutex_unlock(&recs_mutex);

        if (tidx != -1) {

            // Now wait for the transcoding to finish and print the status of the
            // transcoding to the log. We do this in a busy waiting loop for simplicity.
            // since we can have such a long sleep period without really affecting the
            // performance.
            pid_t rpid;

            // We only allow one transcoding to run for a maximum of 49 h
            // This will easily allow (even on a weak CPU) the transcoding of 4h
            // videos in high quality which could take up to 4-5 hours per hour recorded
            // for a single running transcoding. This means that two simulataneous running
            // jobs would require ~8 hour per recorded hour to complete. This means that
            // it could take up to 48h to encode two 4h videos in high quality simultaneous.
            const int watchdog = 49 * 3600;
            int ret;
            struct rusage usage;
            do {
                // Transcoding usually takes hours so we don't bother waking up and check
                // if we are done more often than once every couple of seconds. This
                // might still seem like a short time but keep in mind that in the case the
                // user termintaes the transcoding process the server will not notice this
                // until this check is made. Therefore we want this to be reasonable
                // short as well. Ideally the wait4() should have a timeout argument in
                // which case we would have been notified sooner.

                // Why exactly 6s wait? Well, in case the user terminates the process it
                // means that on average it will take 3s before the structures are updated
                // with the removed transcoding which is the longest time a user ever should
                // have to waut for feedback.
                sleep(6);
                runningtime += 6;
                rpid = wait4(pid, &ret, WCONTINUED | WNOHANG | WUNTRACED, &usage);

            } while (pid != rpid && runningtime < watchdog);

            pthread_mutex_lock(&recs_mutex);
            forget_ongoingtranscoding(tidx);
            pthread_mutex_unlock(&recs_mutex);

            int rh = runningtime / 3600;
            int rm = (runningtime - rh*3600)/60;
            int rs = runningtime % 60;

            if (runningtime >= watchdog) {
                // Something is terrible wrong if the transcoding haven't
                // finished after the watchdog timeout
                logmsg(LOG_ERR, "Transcoding process for file '%s' seems hung. Running time %02d:%02d:%02d h. Process %d killed",
                        basename(filename), rh,rm,rs,pid);
                (void) kill(pid, SIGKILL);
            } else {
                if (WIFEXITED(ret)) {
                    transcoding_done = (WEXITSTATUS(ret) == 0);
                    if (transcoding_done) {
                        if (runningtime < 30) {
                             logmsg(LOG_NOTICE, "Transcoding process finished in less than 30s for file '%s'. This most likely indicates a problem",
                                        basename(filename));

                        } else {
                            logmsg(LOG_INFO, "Transcoding process %d for file '%s' finished normally after %02d:%02d:%02d h. (utime=%d s, stime=%d s))",
                                   pid,basename(filename), rh, rm, rs, usage.ru_utime.tv_sec, usage.ru_stime.tv_sec);

                        }
                    } else {
                       logmsg(LOG_INFO, "Error in transcoding process for file '%s', exit status=%d after %02d:%02d h",
                                        basename(filename),WEXITSTATUS(ret),rh,rm);
                    }
                } else {
                    if (WIFSIGNALED(ret)) {
                        logmsg(LOG_NOTICE, "Transcoding process %d for file '%s' was terminated by signal=%d (possibly by user) after %02d:%02d:%02d",
                                pid,basename(filename), WTERMSIG(ret),rh,rm,rs);
                    } else {
                        // Child must have been stopped. If so we have no choice than to kill it
                        logmsg(LOG_NOTICE, "Transcoding process %d for file '%s' was unexpectedly stopped by signal=%d after %02d:%02d:%02d h",
                                pid, basename(filename), WSTOPSIG(ret),rh,rm,rs);
                        (void) kill(pid, SIGKILL);
                    }
                }

                if( transcoding_done ) {
                    char newname[256], tmpbuff2[256], tmpbuff[256];

                    // Move MP4 file
                    if( use_profiledirectories ) {
                        snprintf(tmpbuff, 255, "%s/mp4/%s/%s", datadir, profile->name, destfile);
                    } else {
                        snprintf(tmpbuff, 255, "%s/mp4/%s", datadir, destfile);
                    }

                    tmpbuff[255] = '\0';
                    snprintf(tmpbuff2, 255, "%s/%s", workingdir, destfile);
                    tmpbuff2[255] = '\0';
                    int ret = mv_and_rename(tmpbuff2, tmpbuff, newname, 256);
                    if (ret) {
                        logmsg(LOG_ERR, "Could not move '%s' to '%s'", tmpbuff2, newname);
                    } else {
                        logmsg(LOG_INFO, "Moved '%s' to '%s'", tmpbuff2, newname);
                    }

                    // Remove temporary directory
                    if (removedir(workingdir)) {
                        logmsg(LOG_ERR, "Could not delete working directory '%s'.", workingdir);
                    } else {
                        logmsg(LOG_INFO, "Deleted working directory '%s'.", workingdir);

                        // The complete transcoding and file relocation has been successful. Now check
                        // if we should send a mail with this happy news!
                        if( send_mail_on_transcode_end ) {
                            char mailbuff[1024];
                            snprintf(mailbuff,1023,
                                    "Transcoding of '%s/%s' using profile '%s' finished.\n"
                                    "Total time: %02d:%02d:%02d h\n",
                                     workingdir,destfile,profile->name,
                                     rh,rm,rs);
                            mailbuff[1023] = '\0';
                            char subjectbuff[80];
                            snprintf(subjectbuff,79,"[tvpvrd] Transcoding of '%s' finished",destfile);
                            subjectbuff[79] = '\0';
                            send_mail(subjectbuff,send_mailaddress,mailbuff);
                        }
                    }

                } else {
                    logmsg(LOG_NOTICE,"Transcoding error. Working directory '%s' not removed.",workingdir);
                }

            }
        }
    }

    pthread_mutex_lock(&filetransc_mutex);
    nfiletransc_threads--;
    pthread_mutex_unlock(&filetransc_mutex);
    
    pthread_exit(NULL);
    return (void *) 0;
}


/**
 * Create a new thread and start a new transcoding of the named file
 * @param filename
 * @param profilename
 * @param wait
 * @return
 */
int
transcode_file(char *filename, char *profilename) {

    struct transc_param *param = calloc(1,sizeof(struct transc_param));

    // We must make our own local copy of the arguments since there is no
    // guarantee that the thread will start before this function returns and the
    // parent deletes the argument space
    param->filename = strdup(filename);
    param->profilename = strdup(profilename);

    // Start the thread that will actually do the recording to the file system
/*
    if( nfiletransc_threads >= MAX_FILETRANSC_THREADS ) {
        logmsg(LOG_ERR, "Only %d number of concurrent transcodings are permitted. Transcoding not started.");
        return -1;
    }
*/

    pthread_mutex_lock(&filetransc_mutex);

    pthread_t thread_id;
    int ret = pthread_create(&thread_id, NULL, _transcode_file, (void *) param);

    nfiletransc_threads++;
    pthread_mutex_unlock(&filetransc_mutex);

    if (ret != 0) {
        logmsg(LOG_ERR, "Could not create thread for transcoding of file '%s' using profile @%s",filename,profilename);
        pthread_mutex_lock(&filetransc_mutex);
        nfiletransc_threads--;
        pthread_mutex_unlock(&filetransc_mutex);
        return -1;
    } else {
        logmsg(LOG_INFO, "Created thread for transcoding of file '%s' using profile @%s",filename,profilename);
    }
    return 0;
}

// Protection of the filelist queue
pthread_mutex_t filelist_mutex = PTHREAD_MUTEX_INITIALIZER;

// Each filelist can contain at most this many video files
#define MAX_FILELIST_ENTRIES 200
int nfilelisttransc_threads = 0;

// The parameter structure which is passed to the transcode filelist working thread
struct transc_filelistparam {
    char *dirpath;
    char **filelist;
    char *profilename;
};

// The maximun number of simultaneous ongoing list transcodings
#define MAX_FILELISTS 5
struct filelist_queue {
    struct transc_filelistparam *filelist_param;
    int idx;
    int nentries;
    time_t start;
};
struct filelist_queue *ongoing_filelist_transcodings[MAX_FILELISTS] = {NULL};
static int num_filelists=0;

/**
 * Enqueu a gives list of files to transcode. All the transcoding file lists
 * are kept in an array.
 * @param filelist
 * @return 0 on success, -1 on failure
 */
int
enqueue_filelist(struct transc_filelistparam *filelist) {
    int i;

    pthread_mutex_lock(&filelist_mutex);
    // Find the first empty slot
    for(i=0; i<MAX_FILELISTS; ++i) {
        if( ongoing_filelist_transcodings[i] == NULL ) {
            ongoing_filelist_transcodings[i] = calloc(1,sizeof(struct filelist_queue));
            ongoing_filelist_transcodings[i]->filelist_param = filelist;
            ongoing_filelist_transcodings[i]->idx = 0;
            ongoing_filelist_transcodings[i]->start = time(NULL);
            num_filelists++;

            logmsg(LOG_NOTICE,"Enqueued filelist in slot [%d]",i);

            int count=0;
            while( count < MAX_FILELIST_ENTRIES && *filelist->filelist[count] ) {
                ++count;
            }
            ongoing_filelist_transcodings[i]->nentries = count;
            break;
        }
    }
    pthread_mutex_unlock(&filelist_mutex);

    if( i >= MAX_FILELISTS ) {
        logmsg(LOG_ERR,"No free filelist slots. Can only add a maximum of %d filelists.", MAX_FILELISTS);
        return -1;
    }

    return 0;
}

/**
 * Dequeue a list of files to transcode
 * @param filelist
 * @return
 */
int
dequeue_filelist(struct transc_filelistparam *filelist) {
    int i;

    pthread_mutex_lock(&filelist_mutex);
    // Find the slot where this is stored
    for(i=0; i<MAX_FILELISTS; ++i) {
        if( ongoing_filelist_transcodings[i] && ongoing_filelist_transcodings[i]->filelist_param == filelist ) {
            free(ongoing_filelist_transcodings[i]);
            ongoing_filelist_transcodings[i] = NULL;
            num_filelists--;

            logmsg(LOG_NOTICE,"Dequeued filelist in slot [%d]",i);
            break;
        }
    }
    pthread_mutex_unlock(&filelist_mutex);

    if( i >= MAX_FILELISTS ) {
        logmsg(LOG_ERR,"Internal error. Can not dequeue non existing filelist.");
        return -1;
    }

    return 0;
}

/**
 * Update the count where in a filelist we are, i.e. how many files have been processed
 * @param filelist
 * @return 0 on success, -1 on failure
 */
int
incidx_fillist(struct transc_filelistparam *filelist) {
    int i;

    pthread_mutex_lock(&filelist_mutex);
    // Find the slot where this is stored
    for(i=0; i<MAX_FILELISTS; ++i) {
        if( ongoing_filelist_transcodings[i] && ongoing_filelist_transcodings[i]->filelist_param == filelist ) {
            ongoing_filelist_transcodings[i]->idx++;
            logmsg(LOG_NOTICE,"Updated filelist in slot [%d] to idx=%d",i,ongoing_filelist_transcodings[i]->idx);
            if( ongoing_filelist_transcodings[i]->idx > ongoing_filelist_transcodings[i]->nentries ) {
                logmsg(LOG_ERR,"Internal error. Current filelist index is larger than the total number of entries in the list.");
                pthread_mutex_unlock(&filelist_mutex);
                return -1;
            }
            break;
        }
    }

    pthread_mutex_unlock(&filelist_mutex);

    if( i >= MAX_FILELISTS ) {
        logmsg(LOG_ERR,"Internal error. Can not increase index on non existing filelist.");
        return -1;
    }
    return 0;
}

/**
 * Fill a buffer with textural information on the current filelists
 * @param num
 * @param buffer
 * @param len
 * @param incfiles
 * @return
 */
int
get_queued_transc_filelists_info(int num,char *buffer,size_t len,int incfiles) {
    char tmpbuff[512] = {'\0'};
    // Return information on transcoding filelist with ordinal number 'num'
    // in the supplied buffer as a string
    if( len < 200 || num == 0 ) {
        logmsg(LOG_DEBUG,"get_queued_transc_filelists_info() : Illegal argument len=%d, num=%d",len,num);
        return -1;
    }

    // Locate the 'num' fillist
    int idx=0;
    int nfound=0;
    while( idx < MAX_FILELISTS ) {
        if( ongoing_filelist_transcodings[idx] ) {
            nfound++;
        }
        if( nfound==num )
            break;
        idx++;
    }

    if( idx >= MAX_FILELISTS ) {
        logmsg(LOG_DEBUG,"get_queued_transc_filelists_info() : idx=%d doesn't exist",idx);
        return -1;
    }

    if( ongoing_filelist_transcodings[idx] == NULL ) {
        logmsg(LOG_DEBUG,"get_queued_transc_filelists_info() : NULL pointer at idx=%d",idx);
        return -1;
    }

    time_t ts_tmp = time(NULL) - ongoing_filelist_transcodings[idx]->start;
    int sday = ts_tmp / (24*3600) ;
    int sh = (ts_tmp - sday*24*3600)/ 3600;
    int smin = (ts_tmp - sday*24*3600 - sh*3600 ) / 60;

    logmsg(LOG_NOTICE,"Filelist transcoding has been running for %d day(s) %02d:%02d (%d s)",sday,sh,smin,ts_tmp);

    // Try to estimate the remaining time (very, very roughly)
    double ts_left = 0.0;
    int lh=-1;
    int lday = -1;
    if( ongoing_filelist_transcodings[idx]->idx > 2 ) {

        // Number of files left to process
        int nleft = ongoing_filelist_transcodings[idx]->nentries - ongoing_filelist_transcodings[idx]->idx -1;

        // ts_left = (time for one file) * (number of files left)
        ts_left = ts_tmp / (ongoing_filelist_transcodings[idx]->idx) * 1.0;
        ts_left *= nleft;

        lday = (int)round(ts_left / (24*3600)) ;
        lh = (int)round((ts_left-lday*24*3600)/3600+1);
    }


    *buffer=0;
    snprintf(tmpbuff,511,
        "%15s: #%02d\n"
        "%15s: %02d videos in list\n"
        "%15s: %02d (%d%%) files\n"
        "%15s: %s"
        "%15s: %02d days %02d:%02d h\n"
        "%15s: %02d days %02d h (approx.)\n",

        "Filelist",num,
        "Total",ongoing_filelist_transcodings[idx]->nentries,
        "Processed",ongoing_filelist_transcodings[idx]->idx, (100*ongoing_filelist_transcodings[idx]->idx)/ongoing_filelist_transcodings[idx]->nentries,
        "Job started",ctime(&ongoing_filelist_transcodings[idx]->start),
        "Running time",sday,sh,smin,
        "Est. time left",lday,lh);

    strncpy(buffer,tmpbuff,len-1);
    buffer[len-1]='\0';
    if( incfiles ) {
       snprintf(tmpbuff,511,"\nProcessed:\n");
       strncat(buffer,tmpbuff,len-1);
       buffer[len-1]='\0';
       for(int i=0; i < ongoing_filelist_transcodings[idx]->idx; i++ ) {
           snprintf(tmpbuff,511,"  * %s\n",ongoing_filelist_transcodings[idx]->filelist_param->filelist[i]);
           strncat(buffer,tmpbuff,len-1);
           buffer[len-1]='\0';
       }

       snprintf(tmpbuff,511,"\nNot processed:\n");
       strncat(buffer,tmpbuff,len-1);
       buffer[len-1]='\0';
       for(int i=ongoing_filelist_transcodings[idx]->idx; i < ongoing_filelist_transcodings[idx]->nentries; i++ ) {
           snprintf(tmpbuff,511,"  + %s\n",ongoing_filelist_transcodings[idx]->filelist_param->filelist[i]);
           strncat(buffer,tmpbuff,len-1);
           buffer[len-1]='\0';
       }
    }

    return 0;
}

/**
 * The main thread function which runs for each filelist and keeps track of the
 * transcoding of each file in the list
 * @param arg
 * @return
 */
void *
_transcode_filelist(void *arg) {   
    struct transc_filelistparam *param = (struct transc_filelistparam *) arg;
    int idx=0;
    char buffer[512] = {'\0'};

    // To avoid reserving ~8MB after the thread terminates we
    // detach it. Without doing this the pthreads library would keep
    // the exit status until the thread is joined (or detached) which would mean
    // loosing 8MB for exah created thread
    pthread_detach(pthread_self());

    if( strnlen(param->dirpath,256) >= 256 ) {
        logmsg(LOG_ERR,"Dirpath in specified filelist is too long > 256 characters. Aborting transcoding of filelist.");
        pthread_exit(NULL);
        return (void *) 0;
    }

    enqueue_filelist(param);

    if( strnlen(param->dirpath,256) > 0 ) {
        strncpy(buffer,param->dirpath,256);
        strncat(buffer,"/",4);
    } else {
        *buffer = '\0';
    }

    if( *param->filelist[idx] ) {
        strncat(buffer,param->filelist[idx++],511);
    } else {
        *buffer = '\0';
    }

    buffer[511] = '\0';
    
    // Loop through all the filenames and transcode them one, by one
    while( *buffer != '\0' ) {
        logmsg(LOG_INFO, "Submitting '%s' for transcoding using @%s",buffer,param->profilename);
        wait_to_transcode(buffer);
        if( -1 == transcode_file(buffer, param->profilename) ) {
            logmsg(LOG_ERR,"Unable to start transcoding of file '%s'. Aborting filelist.",buffer);
            break;
        }

        incidx_fillist(param);

        // Always take a minimum of 4 min break between trying to submit new files.
        // This is done in order to make sure that the CPU load (to be tested in wait_to_transcode)
        // will have a chance to build up. If we didn't do this it would be possible to submit
        // 100's of videos for encoding on an idle server since it takes some time for the load
        // to accurately reflect the servers work
        sleep(4*60);

        if( strnlen(param->dirpath,256) > 0 ) {
            strncpy(buffer,param->dirpath,256);
            strncat(buffer,"/",4);
        } else {
            *buffer = '\0';
        }
        if( *param->filelist[idx] ) {
            strncat(buffer,param->filelist[idx++],511);
        } else {
            *buffer = '\0';
        }

        buffer[511] = '\0';
    }

    dequeue_filelist(param);
    
    idx=0;
    while(idx < MAX_FILELIST_ENTRIES && *param->filelist[idx]) {
        free(param->filelist[idx++]);
    }

    // Free the final "0" string that is used as sentinel
    if( idx < MAX_FILELIST_ENTRIES ) {
        free(param->filelist[idx]);
    }
    free(param->filelist);
    free(param->dirpath);
    free(param->profilename);
    free(param);

    pthread_mutex_lock(&filetransc_mutex);
    nfilelisttransc_threads++;
    pthread_mutex_unlock(&filetransc_mutex);

    pthread_exit(NULL);
    return (void *) 0;
}


/**
 * Handle transcoding of a list of files. This is the wrapper functiin that just
 * creates the thread that actually does all the work ni order to be responsive to the
 * user right away
 * @param dirpath Optional path that will be prepended to file names in the list
 * @param filelist A NULL terminated array of string pointers to the file names to be
 *        encoded
 * @return 0 on sucess, -1 on failure
 */
int
transcode_filelist(char *dirpath,char *filelist[],char *profilename) {
    
    struct transc_filelistparam *param = calloc(1,sizeof(struct transc_filelistparam));

    // Since these are used ina thread that will exist longer than this and parent
    // function we must maker sure we have our own copies of the dirpath and profilename
    // since they might be stack variables that goes out of scope.
    // The filelist is not a problem since that is allocated dynamically.
    param->dirpath = strdup(dirpath); // Released in _transcode_file
    param->profilename = strdup(profilename); // Released in _transcode_file
    param->filelist = filelist;

    // Check for degenerate cases
    if( filelist == NULL || filelist[0] == NULL || *filelist[0] == '\0' ) {
        logmsg(LOG_ERR, "Internal error: Empty list submitted to transcode_filelist()");
        return -1;
    }

    if( profilename == NULL || *profilename == '\0' ) {
        logmsg(LOG_ERR, "Internal error: No profile specified in call to transcode_filelist()");
        return -1;
    }

    // Start the thread that will actually do the recording to the file system
/*
    if( nfilelisttransc_threads >= MAX_FILELISTTRANSC_THREADS ) {
        logmsg(LOG_ERR, "Only %d number of concurrent list transcodings are permitted. Transcoding not started.");
        return -1;
    }
*/

    pthread_mutex_lock(&filetransc_mutex);
    pthread_t thread_id;
    int ret = pthread_create(&thread_id, NULL, _transcode_filelist, (void *) param);
    nfilelisttransc_threads++;
    pthread_mutex_unlock(&filetransc_mutex);

    if (ret != 0) {
        logmsg(LOG_ERR, "Could not create thread for transcoding of file list");
        pthread_mutex_lock(&filetransc_mutex);
        nfilelisttransc_threads--;
        pthread_mutex_unlock(&filetransc_mutex);
        return -1;
    } else {
        logmsg(LOG_INFO, "Created thread for transcoding of file list");
    }
    
    return 0;
}

/**
 * Read a list of filenames from a file
 * @param filename
 * @return
 */
int
read_filenamelist(char *filename, char *filenamelist[], int maxlen) {
    struct stat statbuffer;
    char linebuffer[512];

    if( -1 == stat(filename,&statbuffer) ) {
        logmsg(LOG_ERR, "File %s with list of movies to encode does not exist.",filename);
        return -1;
    }

    FILE *fp = fopen(filename, "r");
    if( NULL == fp ) {
        logmsg(LOG_ERR, "Cannot open file %s (%d : %s).",filename,errno,strerror(errno));
        return -1;
    }

    int i=0;
    char *ptr;
    char dirpath[256];
    char filenamebuffer[512];

    while( fgets(linebuffer,512,fp) && i<maxlen ) {

        // Get rid of trailing newline
        linebuffer[strlen(linebuffer)-1] = '\0';

        if( strlen(linebuffer) < 6 ) {
            logmsg(LOG_NOTICE,"Invalid file name on row=%d in filelist. Skipping.",i+1);
            continue;
        }

        // Is this a dirpath to use for the following files
        if( *linebuffer == ':' ) {
            strncpy(dirpath,linebuffer+1,255);

            // remove any possible trailing '/'
            if( dirpath[strlen(dirpath)-1] == '/' ) {
                dirpath[strlen(dirpath)-1] = '\0';
            }
            dirpath[255] = '\0';
            continue;
        }

        strncpy(filenamebuffer,dirpath,256);
        strncat(filenamebuffer,"/",511);
        strncat(filenamebuffer,linebuffer,511);
        ptr = strdup(filenamebuffer);

        logmsg(LOG_DEBUG, "Filename '%s' constructed.",ptr);

        if( ptr == NULL ) {
            logmsg(LOG_ERR, "Cannot allocate memory to store filename in filelist. (%d : %s)",errno,strerror(errno));
            for(int k=0; k < i; i++)
                free(filenamelist[k]);
            return -1;
        }
        if( -1 == stat(ptr,&statbuffer) ) {
            logmsg(LOG_ERR, "File '%s' in filelist does not exist. Aborting.",ptr);
            for(int k=0; k < i; i++)
                free(filenamelist[k]);
            free(ptr);
            return -1;
        }
        filenamelist[i++] = ptr;
    }

    // End of list marker;
    filenamelist[i] = strdup("\0");

    fclose(fp);
    return 0;
}

/**
 * Read video files to transcode from the list stored in the specified file
 * @param filename Name of file with list of all videos to transcode
 * @param profilename Profile to use for the transcoding. All videos will use the same transcoding
 * @return 0 on sucess, -1 on failure
 */
int
read_transcode_filelist(char *filename, char *profilename) {

    char dirpath[256] = {'\0'};
    char **filenamelist = calloc(MAX_FILELIST_ENTRIES,sizeof(char *));

    if( filenamelist == NULL ) {
        logmsg(LOG_ERR, "Cannot allocate memory for filelist. (%d : %s)",errno,strerror(errno));
        return -1;
    }

    if( -1 == read_filenamelist(filename,filenamelist,MAX_FILELIST_ENTRIES) ) {
        return -1;
    }
    
    if( -1 == transcode_filelist(dirpath,filenamelist,profilename) ) {
        return -1;
    }   
    
    logmsg(LOG_INFO,"Videos from list file '%s' queued to transcoding.",filename);
    return 0;
}

/**
 * Transcode all files in the given directory
 * @param dirpath
 * @param profilename
 * @return
 */
#define MAX_FILES_IN_DIR_TO_TRANSCODE 1024
int
transcode_whole_directory(char *dirpath, char *profilename) {
    DIR *dp;
    struct dirent *dirp;
    struct stat statbuf;
    char **filelist = calloc(MAX_FILES_IN_DIR_TO_TRANSCODE+1,sizeof(char *));
    int idx=0;

    // First check that this is really a directory that was specified
    stat(dirpath, &statbuf);
    if ( ! S_ISDIR(statbuf.st_mode) ) {
        logmsg(LOG_ERR,"Specified path '%s' is not a directory.",dirpath);
        return -1;
    }

    dp = opendir(dirpath);
    if (dp == NULL) {
        logmsg(LOG_ERR,"Cannot open directory. (%d : %s)",errno,strerror(errno));
        return -1;
    }

    // Loop through the directory (ignoring '.' and '..' and subdirectories)
    while ((dirp = readdir(dp)) != NULL) {

        if ((strcmp(dirp->d_name, ".") != 0) && (strcmp(dirp->d_name, "..") != 0)) {

            if (S_ISREG(statbuf.st_mode) || S_ISLNK(statbuf.st_mode)) {

                // Check that the file ends in a recognized video format suffix
                // .mpg, .mpeg, .mp2, .mp4, .rm, .avi, .flv,
                size_t k = strlen(dirp->d_name)-1;
                while( k >  0 && dirp->d_name[k] != '.' ) {
                    k--;
                }
                if( dirp->d_name[k] == '.' ) {
                    char *ptr = &dirp->d_name[k+1];
                    if( strcmp(ptr,".mpg") && strcmp(ptr,".mpeg") && strcmp(ptr,".mp2") &&
                        strcmp(ptr,".mp4") && strcmp(ptr,".rm") && strcmp(ptr,".avi") &&
                        strcmp(ptr,".flv") ) {
                        logmsg(LOG_NOTICE,"Ignoring file with unknown suffix '%s' when building filelist",dirp->d_name);
                    } else {
                        filelist[idx++] = strdup(dirp->d_name);
                        logmsg(LOG_NOTICE,"Adding video file '%s' to transcoding list",dirp->d_name);
                        if( idx >= MAX_FILES_IN_DIR_TO_TRANSCODE ) {
                            logmsg(LOG_ERR,"Filelist truncated after %d video file was read from directory '%s'",idx,dirpath);
                            break;
                        }
                    }
                }

            } else if (S_ISDIR(statbuf.st_mode)) {
                logmsg(LOG_NOTICE,"Ignoring subdirectory '%s' when building transcoding list",dirp->d_name);
            }
            else {
                logmsg(LOG_ERR,"Ignoring unknown file type '%s' when building transcoding list",dirp->d_name);
            }
        }
    }

    filelist[idx] = strdup("\0");

    if( -1 == transcode_filelist(dirpath,filelist,profilename) ) {
        return -1;
    }

    logmsg(LOG_INFO,"All %d video files from directory '%s' queued for transcoding.",idx,dirpath);
    return 0;
}