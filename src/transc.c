/* =========================================================================
 * File:        TRANSC.C
 * Description: Module to handle transcoding profiles
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id: transc.c 138 2009-11-19 11:34:29Z ljp $
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>

#include <sys/stat.h>

// Needed to get symbolic constants for v4l2 controls
#include <linux/videodev2.h>

#include "tvpvrd.h"
#include "transc.h"
#include "utils.h"
#include "stats.h"

struct ongoing_transcoding {
    time_t start_ts;
    char *workingdir;
    char *filename;
    char *cmd;
    struct transcoding_profile_entry *profile;
    pid_t pid;
};
static struct ongoing_transcoding *ongoing_transcodings[16] = { (struct ongoing_transcoding *)0 };
static const int max_ongoing_transcoding = 16;

static struct transcoding_profile_entry *profiles[MAX_TRANS_PROFILES];
static int num_transcoding_profiles=0;

/**
 * Check if ffmpeg bianries can be found at the specified location
 * @return -1 on failure, 0 on success
 */
int
check_ffmpeg_bin(void) {
    struct stat bstat;
    if( stat(FFMPEG_BIN,&bstat) == 0 ) {
        return 0;
    } else {
        logmsg(LOG_ERR,"Can not find '%s' executable. Transcoding is not available.",FFMPEG_BIN);
        return -1;
    }
}

/**
 * All ongoing transcodings are stored in a list. This functsion adds a new
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
        logmsg(LOG_ERR,"Can only record at most %d transcodings.",max_ongoing_transcoding);
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
 * Forget the specified transcoding
 * @param idx
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

/**
 * Fill a buffer with information on all the ongoing recordings
 * @param obuff
 * @param size
 * @return number of ongoing transcodings
 */
int
get_ongoing_transcodings(char *obuff, int size, int show_ffmpegcmd) {
    char tmpbuff[512];
    int y, m, d, h, min, sec;
    time_t now = time(NULL);

    *obuff = '\0';
    int num=0;
    for (int i = 0; i < max_ongoing_transcoding; i++) {
        num += ongoing_transcodings[i] ? 1 : 0;
    }
    for (int i = 0; i < max_ongoing_transcoding; i++) {
        if (ongoing_transcodings[i]) {
            int rtime = now-ongoing_transcodings[i]->start_ts;
            int rh = rtime/3600;
            int rmin = (rtime - rh*3600)/60;
            fromtimestamp(ongoing_transcodings[i]->start_ts, &y, &m, &d, &h, &min, &sec);
            if( show_ffmpegcmd ) {
                snprintf(tmpbuff, 511, "[#%02d|%02d:%02d|(%02d:%02d)|%-35.35s|%s]\n(cmd: %s)\n",
                         i,
                         h, min,
                         rh,rmin,						 
                         ongoing_transcodings[i]->filename,
                         ongoing_transcodings[i]->profile->name,
                         ongoing_transcodings[i]->cmd);
            } else {
                snprintf(tmpbuff, 511, "[#%02d|%02d:%02d|(%02d:%02d)|%-35.35s|%s]\n",
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

int
get_transcoding_profile_list(struct transcoding_profile_entry **start[]) {
    *start = profiles;
    return num_transcoding_profiles;
}

int
transcoding_profile_exist(char *name) {
    if( strlen(name) == 0 ) 
        return 0;
    for(int i=0; i < num_transcoding_profiles; i++ ) {
        if( strncmp(name,profiles[i]->name,31) == 0 ) {
            return 1;
        }
    }
    return 0;
}

int
_read_transcoding_profile(char *filename,int idx) {
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
    int k=strnlen(profname,255);
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
    entry->encoder_keep_mp2file = iniparser_getboolean(profile, buffer, KEEP_MP2FILE);

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';      
    entry->encoder_video_bitrate = validate(500000,8000000,"video_bitrate",
                                            iniparser_getint(profile, buffer, DEFAULT_VIDEO_BITRATE));
                            
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_peak_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';  
    entry->encoder_video_peak_bitrate  = validate(500000,8000000,"video_peak_bitrate",
                                                  iniparser_getint(profile, buffer, DEFAULT_VIDEO_PEAK_BITRATE));
                         
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';                                   
    entry->encoder_audio_bitrate = validate(9,13,"audio_bitrate",
                                            iniparser_getint(profile, buffer, DEFAULT_AUDIO_BITRATE));
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_sampling",bufsize-1);
    buffer[bufsize-1] = '\0';                                      
    entry->encoder_audio_sampling = validate(0,2,"audio_sampling",
                                             iniparser_getint(profile, buffer, DEFAULT_AUDIO_SAMPLING));
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_aspect",bufsize-1);
    buffer[bufsize-1] = '\0';                                    
    entry->encoder_video_aspect = validate(0,3,"video_aspect",
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
    strncat(buffer,":video_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->video_bitrate = validate(100,1500,"ffmpeg_video_bitrate",
                                    iniparser_getint(profile, buffer,DEFAULT_PROFILE_VIDEO_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_peak_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->video_peak_bitrate = validate(100,1800,"ffmpeg_video_peak_bitrate",
                                         iniparser_getint(profile, buffer,DEFAULT_PROFILE_VIDEO_PEAK_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_bitrate",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->audio_bitrate = validate(32,320,"ffmpeg_audio_bitrate",
                                    iniparser_getint(profile, buffer,DEFAULT_PROFILE_AUDIO_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":pass",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->pass = validate(1,2,"ffmpeg_pass",
                           iniparser_getint(profile, buffer,DEFAULT_PROFILE_PASS));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_top",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_top = validate(0,160,"ffmpeg_crop_top",
                               iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_TOP));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_bottom",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_bottom = validate(0,160,"ffmpeg_crop_bottom",
                                  iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_BOTTOM));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_left",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_left = validate(0,160,"ffmpeg_crop_left",
                                iniparser_getint(profile, buffer,DEFAULT_PROFILE_CROP_LEFT));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":crop_right",bufsize-1);
    buffer[bufsize-1] = '\0';
    entry->crop_right = validate(0,160,"ffmpeg_crop_right",
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
    logmsg(LOG_NOTICE,"  -- read profile \"%s\"", entry->name);

    iniparser_freedict(profile);
    return 0;
}

int
read_transcoding_profiles(void) {
    char dirbuff[256];
    struct stat fstat;

    // Search for the profile directory in
    // 1) /etc/tvpvrd/profiles
    // 2) <cwd>/profiles
    strcpy(dirbuff,"/etc/tvpvrd/profiles");
    if( -1 == stat(dirbuff,&fstat) ) {
        char cwd[256];
        // Try curent working directory
        char *ret = getcwd(cwd,256);
        if( ret != NULL ) {
            snprintf(dirbuff,255,"%s/%s",ret,"profiles");
            if( -1 == stat(dirbuff,&fstat) ) {
                logmsg(LOG_ERR,"Cannot find any transcoding profiles. Aborting.");
                return -1;
            }
        }
    }

    // Now loop through all files in 'dirbuff' directory
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
            snprintf(tmpbuff, 512, "%s/%s", dirbuff, dirp->d_name);
            lstat(tmpbuff, &fstat);

            if (S_ISREG(fstat.st_mode) || S_ISLNK(fstat.st_mode)) {

                if( num_transcoding_profiles >= MAX_TRANS_PROFILES ) {
                    logmsg(LOG_ERR,"Maximum number of transcoding profiles (%d) exceeded.",
                           MAX_TRANS_PROFILES);
                    return -1;
                }
               logmsg(LOG_INFO,"Reading transcoding profile file '%s'",tmpbuff);
                
                (void)_read_transcoding_profile(tmpbuff,num_transcoding_profiles++);

            }
        }
    }

    return 0;
}

void
refresh_transcoding_profiles(void) {
    // Re-read allexisting profiles
    for(int i=0; i < num_transcoding_profiles; i++) {
        (void)_read_transcoding_profile(profiles[i]->filename,i);
    }
}

void
_dump_transcoding_profile(struct transcoding_profile_entry *profile, char *buff, int size) {
    float sampling[] = {44.1, 48.0, 32.0};
    int abps[] = {192, 224, 256, 320, 384};
    char *aspect[] = {"1x1","4x3","16x9","221x100"};

    snprintf(buff,size-1,
    "%25s: %s\n"           /* name */
    "ENCODER:\n"
    "%25s: %d\n"           /* video_bitrate */
    "%25s: %d\n"           /* video_peak_bitrate */
    "%25s: %.1f\n"         /* audio sampling */
    "%25s: %d\n"           /* audio_bitrate*/
    "%25s: %s\n"           /* aspect */
    "%25s: %s\n"           /* frame_size */
    "FFMPEG:\n"
    "%25s: %d\n"
    "%25s: %d\n"
    "%25s: %s\n"
    "%25s: %s\n"
    "%25s: %d\n"
    "%25s: %s\n"
    "%25s: %d\n"
    "%25s: %s\n"
    "%25s: (l=%d, r=%d, t=%d, b=%d)\n"
    "%25s: %s\n"
    "%25s: %s\n",
    "name",profile->name,

    "video_bitrate", profile->encoder_video_bitrate,
    "video_peak_bitrate", profile->encoder_video_peak_bitrate,
    "audio_sampling", sampling[profile->encoder_audio_sampling],
    "audio_bitrate", abps[profile->encoder_audio_bitrate-9],
    "aspect", aspect[profile->encoder_video_aspect],
    "size",profile->encoder_video_frame_size_name,

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

int 
dump_transcoding_profile(char *name, char *buff, int size) {
    int i;
    for(i=0; i < num_transcoding_profiles && strcmp(name,profiles[i]->name); i++)
        ;
    if( i < num_transcoding_profiles ) {
        _dump_transcoding_profile(profiles[i],buff,size);
        return 0;
    } else {
        return -1;
    }
}

void
get_transcoding_profile(char *name, struct transcoding_profile_entry **entry) {

    int i=0;
    while( i < num_transcoding_profiles && strcmp(name,profiles[i]->name) )
        i++;
    if( i >= num_transcoding_profiles ) {
        logmsg(LOG_ERR,"Cannot find requested transcoding profile '%s' falling back on default profile '%s'",
                name,DEFAULT_TRANSCODING_PROFILE);
        i=0;
        while( i < num_transcoding_profiles && strcmp(name,DEFAULT_TRANSCODING_PROFILE) )
            i++;
        if( i >= num_transcoding_profiles ) {
            logmsg(LOG_ERR,"FATAL: Default transcoding profile '%s' does not exist. Falling back on the first profile '%s'",
                   DEFAULT_TRANSCODING_PROFILE,profiles[0]->name);
            i=0;
        }
    }
    *entry = profiles[i];
}

/*
 * Wait until the load on the server is below the acceptable limit to
 * start a new transcoding process. Return the number of seconds
 * @return 0 if Ok to start transcode, -1 otherwise
 */
int
wait_to_transcode(char *filename) {
    // We will not start transcoding until the 15 min load average is below max_load_for_transcoding
    int waiting_time = 0;
    const int backoff_time = 5*60;
    float avg1 = 0, avg5 = 0, avg15 = 0;
    getsysload(&avg1, &avg5, &avg15);

    if( avg5 > max_load_for_transcoding ) {
        logmsg(LOG_INFO, "Waiting to transcode '%s'. Current load %.2f. Must be < %d.",
               filename, avg5, max_load_for_transcoding);
    }
    while (avg5 > max_load_for_transcoding && waiting_time < max_waiting_time_to_transcode) {
        sleep(backoff_time);
        waiting_time += backoff_time;
        getsysload(&avg1, &avg5, &avg15);
        if( avg5 > max_load_for_transcoding ) {
            logmsg(LOG_NOTICE, "Still waiting to to transcode '%s'. Current load %.2f > %d. Total waiting time: %d min",
                   filename, avg5, max_load_for_transcoding,waiting_time/60);
        }
    }
    return waiting_time < max_waiting_time_to_transcode ? 0 : -1;
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
create_ffmpeg_cmdline(char *filename, struct transcoding_profile_entry *profile, char *destfile, int destsize, char *cmd, int size) {

    // Build command line for ffmpeg
    strcpy(destfile, filename);
    int l = strlen(filename)-1;
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
        } else {
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
        }
        
    } else {
        if( strlen(profile->size) > 0 ) {
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
        } else {
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
            
        }
    }

    logmsg(LOG_NOTICE, "ffpmeg command: %s", cmd);

    return 0;

}

void
kill_all_ongoing_transcodings(void) {

    // First ask nicely
    for (int i = 0; i < max_ongoing_transcoding; i++) {
        if (ongoing_transcodings[i]) {
            logmsg(LOG_NOTICE,"Stopping ffmpeg process %d",ongoing_transcodings[i]->pid);
            (void)killpg(ongoing_transcodings[i]->pid,SIGSTOP);
        }
    }

    // Wait a bit
    usleep(600);

    // Then not so nicely, kill every process
    for (int i = 0; i < max_ongoing_transcoding; i++) {
        if (ongoing_transcodings[i]) {
            logmsg(LOG_NOTICE,"Killing 'ffmpeg' process %d",ongoing_transcodings[i]->pid);
            (void)killpg(ongoing_transcodings[i]->pid,SIGKILL);
            free(ongoing_transcodings[i]);
            ongoing_transcodings[i] = (struct ongoing_transcoding *)NULL;
        }
    }

}
