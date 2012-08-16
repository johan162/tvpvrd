/* =========================================================================
 * File:        TRANSCPROFILE.C
 * Description: Module to handle transcoding profiles
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

// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h> // Needed to get MIN()/MAX()
#include <time.h>

// Needed to get symbolic constants for v4l2 controls
#include <linux/videodev2.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "futils.h"
#include "stats.h"
#include "tvplog.h"
#include "transcprofile.h"

// We store all the details about a specific transcoding profile in an array
// as well. In theory there is no limit to how many profiles a user may define
static struct transcoding_profile_entry *profiles[MAX_TRANS_PROFILES];
static size_t num_transcoding_profiles=0;

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
    strncat(buffer,":use_transcoding",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    entry->use_transcoding = (unsigned)iniparser_getboolean(profile, buffer, DEFAULT_USE_TRANSCODING);
    
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_bitrate",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    entry->video_bitrate = (unsigned)validate(100,3000,"ffmpeg_video_bitrate",
                                    iniparser_getint(profile, buffer,DEFAULT_PROFILE_VIDEO_BITRATE));
    
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":audio_bitrate",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    entry->audio_bitrate = (unsigned)validate(32,320,"ffmpeg_audio_bitrate",
                                    iniparser_getint(profile, buffer,DEFAULT_PROFILE_AUDIO_BITRATE));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":pass",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    entry->pass = (unsigned)validate(1,2,"ffmpeg_pass",
                           iniparser_getint(profile, buffer,DEFAULT_PROFILE_PASS));

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":video_size",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    strncpy(entry->size,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VIDEO_SIZE),
            31);
    entry->size[31] = '\0';
    
    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":acodec",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    strncpy(entry->acodec,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VCODEC),
            31);
    entry->acodec[31] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":vcodec",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    strncpy(entry->vcodec,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_VCODEC),
            31);
    entry->vcodec[31] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":file_extension",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    strncpy(entry->file_extension,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_FILE_EXTENSION),
            7);
    entry->file_extension[7] = '\0';

    strncpy(buffer,sname,bufsize-1);
    strncat(buffer,":extra_options",bufsize-1-strlen(buffer));
    buffer[bufsize-1] = '\0';
    strncpy(entry->extra_ffmpeg_options,
            iniparser_getstring(profile, buffer,(char *)DEFAULT_PROFILE_EXTRA_FFMPEG_OPTIONS),
            255);
    entry->extra_ffmpeg_options[255] = '\0';
    logmsg(LOG_DEBUG,"    [%s] ffmpeg_extra_options: %s",entry->name,entry->extra_ffmpeg_options);  
    
    strncpy(entry->filename,filename,255);
    entry->filename[255] = '\0';

    profiles[idx] = entry;
    logmsg(LOG_NOTICE,"  -- read profile '%s'", entry->name);

    iniparser_freedict(profile);
    return 0;
}


/**
 * Read all defined transcoding profiles in the profile directory
 * @return 0 on success, -1 on failure
 */
int
read_transcoding_profiles(void) {
    char dirbuff[256];
    snprintf(dirbuff,255,"%s/tvpvrd/profiles",CONFDIR);
    logmsg(LOG_DEBUG,"Profile directory: %s",dirbuff);  
    return process_files(dirbuff, ".profile", MAX_TRANS_PROFILES, 
                         &num_transcoding_profiles, _read_transcoding_profile);
}

/**
 * Re-read all transcoding profiles from disk
 */
void
refresh_transcoding_profiles(void) {
    // Re-read all existing profiles
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
    "%-22s: %d\n"           /* use_transcodings */
    "%-22s: %d\n"           /* video_bitrate */
    "%-22s: %s\n"           /* vcodec */
    "%-22s: %d\n"           /* pass */
    "%-22s: %s\n"           /* acodec */
    "%-22s: %d\n"           /* audio_bitrate */
    "%-22s: %s\n"           /* video_size */
    "%-22s: %s\n"           /* ffmmpeg_extra_options */
    "%-22s: %s\n",          /* file_extension */
            
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
    "vcodec",profile->vcodec,
    "pass",profile->pass,
    "acodec", profile->acodec,
    "audio_bitrate",profile->audio_bitrate,
    "video_size",profile->size,
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
    size_t const bufflen = 255;
    char tmpbuff[bufflen];
    *buff = '\0';
    while( idx < num_transcoding_profiles ) {
        snprintf(tmpbuff,bufflen-1,"#%02d : %s\n",idx+1,profiles[idx]->name);
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

/**
 * Generate a textual list of all currently specified transcoding profiles
 * @param buff
 * @param maxlen
 */
int
list_profile_names_htmllinks(char *buff,size_t maxlen) {
    unsigned idx=0;
    size_t const bufflen = 255;
    char tmpbuff[bufflen];
    *buff = '\0';
    while( idx < num_transcoding_profiles ) {
        snprintf(tmpbuff,bufflen-1,"#%02d : <a href=\"?c=dp @%s\">%s</a>\n",idx+1,profiles[idx]->name,profiles[idx]->name);
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


/**
 * Store the start of the array of transcoding profiles in the supplied
 * parameter.
 * @param start
 * @return The total number of defined profiles
 */
unsigned
get_transcoding_profile_list(struct transcoding_profile_entry **start[]) {
    *start = profiles;
    return num_transcoding_profiles;
}
