/* =========================================================================
 * File:        TRANSCPROFILE.H
 * Description: Module to handle transcoding profiles
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009-2012 Johan Persson
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
#ifndef TRANSCPROFILE_H
#define	TRANSCPROFILE_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * MAX_TRANS_PROFILES integer
 * The maximum number of allowed ffmpeg profiles
 */
#define MAX_TRANS_PROFILES 64
    


/*
 * DEFAULT_TRANSCODING_PROFILE string
 * Defailt transcoding profile that all recording will use if not otherwise
 * specified
 */
#define DEFAULT_TRANSCODING_PROFILE "normal"

/*----------------------------------------------------------------------------------
 * The following defines specfies the default values used for a transcoding
 * profile when the value is not specified in the profile fille.
 *----------------------------------------------------------------------------------
 */

/*
 * DEFAULT_USE_TRANSCODING boolean
 * Should we use transcoding at all
 */
#define DEFAULT_USE_TRANSCODING 1

/*
 * DEFAULT_VCODEC string
 * What video codec ffmpeg will use by default
 */
#define DEFAULT_PROFILE_VCODEC "libx264"

/*
 * DEFAULT_VPRE string
 * Default preset for the -vpre setting for ffmpeg. Use mepty string and let ffmpeg
 * decide.
 */
#define DEFAULT_PROFILE_VPRE ""

/*
 * DEFAULT_VPRE string
 * Default preset for the -vpre setting for ffmpeg. Use mepty string and let ffmpeg
 * decide.
 */
#define DEFAULT_PROFILE_VPRE1 ""

/*
 * DEFAULT_PROFILE_VIDEO_BITRATE integre
 * Default video bitrate in kbps
 */
#define DEFAULT_PROFILE_VIDEO_BITRATE 600

/*
 * DEFAULT_PROFILE_VIDEO_PEAK_BITRATE integer
 * Default video peak bitrate in kbps
 */
#define DEFAULT_PROFILE_VIDEO_PEAK_BITRATE  800

/*
 * DEFAULT_PROFILE_AUDIO_BITRATE integer
 * Default audio bitrate in kbps
 */
#define DEFAULT_PROFILE_AUDIO_BITRATE 192

/*
 * DEFAULT_PROFILE_PASS integer
 * Default number of passes 1 or 2
 */
#define DEFAULT_PROFILE_PASS 2

/*
 * DEFAULT_PROFILE_VIDEO_SIZE integer
 * Default video size
 * The empty string indicates that the size should be the same
 * as the input size (from the MP2 stream).
 * The size is otherwise given as a string "WxH". Shortcuts can also be
 * used, e.g. sqcif, qcif, cif, 4cif, qqvga, qvga, vga, svga etc.
 * See the man page for ffmeg for more details
 */
#define DEFAULT_PROFILE_VIDEO_SIZE ""

/*
 * DEFAULT_CROP_TOP integer
 * DEFAULT_CROP_BOTTOM integer
 * DEFAULT_CROP_LEFT integer
 * DEFAULT_CROP_RIGHT integer
 * Default cropping size for video if nothing else specified
 */
#define DEFAULT_PROFILE_CROP_TOP 0
#define DEFAULT_PROFILE_CROP_BOTTOM 0
#define DEFAULT_PROFILE_CROP_LEFT 0
#define DEFAULT_PROFILE_CROP_RIGHT 0

/*
 * DEFAULT_PROFILE_AUDIO_CODEC string
 * If left empty ffmpeg will default to using the aac encoder.
 * Any other installed libraries can be used. For example to use the
 * lame mp3 encoder use "libmp3lame" as value
 */
#define DEFAULT_PROFILE_ACODEC ""

/*
 * DEFAULT_PROFILE_FILE_EXTENSION string
 * The fiel extension to be used on the transcoded file
 */
#define DEFAULT_PROFILE_FILE_EXTENSION ".mp4"
/*
 * DEFAULT_PROFILE_EXTRA_FFMPEG_OPTIONS string
 * User selectable additoinal options to use for ffmpeg
 * This option string is added to the end of the ffmpeg command
 * just before the output file is specified
 */
#define DEFAULT_PROFILE_EXTRA_FFMPEG_OPTIONS ""

// The name of the sections in an ini file for a profile
#define FFMPEG_SECTION "ffmpeg"
#define ENCODER_SECTION "encoder"


/*
 * DEFAULT_VIDEO_BITRATE integer
 * DEFAULT_VIDEO_PEAK_BITRATE integer
 * Deafult video bitrates 3 Mbps , peak 3.5 Mbps
 * Values should be in range 500000 up to 8000000
 */
#define DEFAULT_VIDEO_BITRATE 3000000
#define DEFAULT_VIDEO_PEAK_BITRATE 3500000

/*
 * DEFAULT_VIDEO_FRAME_SIZE string
 * Default MP2 frame size 3-quarter
 * Supported format at present are
 *  "default = 720,576
 *  "qvga = 320,240
 *  "qqvga" = 160,120
 *  "vga" = 640,480
 *  "cif" = 352,288
 *  "3q" = 480,384
 *  "3qmp4" = 480,352
 *   "half" = 360,288
 */
#define DEFAULT_VIDEO_FRAME_SIZE "3qmp4"

/*
 * Deafult audio sampling freq
 */
#define DEFAULT_AUDIO_SAMPLING V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100

/*
 * Default audio bitrate for MPEG Layer 2
 */
#define DEFAULT_AUDIO_BITRATE V4L2_MPEG_AUDIO_L2_BITRATE_256K

/*
 * Default video aspect rate
 * A possible value would also be V4L2_MPEG_VIDEO_ASPECT_4x3
 */
#define DEFAULT_VIDEO_ASPECT V4L2_MPEG_VIDEO_ASPECT_16x9

/*
 * KEEP_MP2FILE bool
 * Keep original MP2 file from video card even after transcoding
 */
#define KEEP_MP2FILE 1


/**
  * Holds all the information on a specific transcoding profile. The profile
 * has two main sections.
 * 1) The setting for the HW encoder on the capture card
 * 2) The ffmpeg settings to use at the actual transcoding
 */
struct transcoding_profile_entry {
    
    /* profile name */
    char name[32];
    
    /* HW encoder entries */
    unsigned encoder_keep_mp2file;
    unsigned encoder_video_bitrate;
    unsigned encoder_video_peak_bitrate;
    unsigned encoder_audio_bitrate;
    unsigned encoder_audio_sampling;
    unsigned encoder_video_aspect;
    char encoder_video_frame_size_name[32];
    
    /* ffmpeg entries */
    unsigned use_transcoding;
    unsigned video_bitrate;
    unsigned pass;
    unsigned audio_bitrate;
    char size[32];
    char vpre[32];
    char vpre1[32];
    char vcodec[32];
    char acodec[32];
    char file_extension[16];
    unsigned crop_top,crop_bottom;
    unsigned crop_left,crop_right;
    char extra_ffmpeg_options[256];
    char filename[256];
};


/**
 * read all transcoding profiles from file
 * @return -1 on failure, 0 on success
 */
int
read_transcoding_profiles(void);

/**
 * Get the details fro the specified transcoding profile name
 * @param name profile name
 * @param entry a pointer to a static buffer with the transcoding information
 */
void
get_transcoding_profile(char *name, struct transcoding_profile_entry **entry);

/**
 * Get a list of all currently specified transcodings
 * @param start
 * @return number of entries in list
 */
unsigned
get_transcoding_profile_list(struct transcoding_profile_entry **start[]);

/**
 * Check if the named transcoding proifile exists ot not
 * @param name
 * @return Boolean, 1 exists , 0 doesn't
 */
int
transcoding_profile_exist(char *name);

/**
 * Re-read all transcoding profiles from file
 */
void
refresh_transcoding_profiles(void);

/**
 * Dump s atring representation of all transcoding profiles to the specified buffer
 * @param name
 * @param buff
 * @param size
 * @return -1 failure, 0 on success
 */
int 
dump_transcoding_profile(char *name, char *buff, size_t size);


/**
 * Return a list of all defined profiles
 * @param buff
 * @param maxlen
  * @return -1 on error 0 otherwise
 */
int
list_profile_names(char *buff, size_t maxlen);

/**
 * Return a list of all defined profiles with HTML links to display
 * the details of the profile (assumes the Web-interface)
 * @param buff
 * @param maxlen
 * @return -1 on error 0 otherwise
 */
int
list_profile_names_htmllinks(char *buff, size_t maxlen);

/**
 * Store a list of pointers to profile names in the supplied buffer
 * Note: The profile names cannot be modified in any way
 * @param list
 * @param maxlen
 * @return number of names in list
 */
unsigned
get_profile_names(const char *list[], size_t maxlen);


#ifdef	__cplusplus
}
#endif

#endif	/* TRANSCPROFILE_H */

