/* =========================================================================
 * File:        VCTRL.H
 * Description: Collection of utility functions fo interface the video device
 *              For example setting the encoded bitrate for both audio and
 *              video.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
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


#ifndef _VCTRL_H
#define	_VCTRL_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Utility to initialize defined structures before passing
 * then on to the ioctl() method
 */
#define CLEAR(x) memset (&(x), 0, sizeof (x))
	
/*
 * The first argument ot a number of _vctrl functions and 
 * determines if the should set / or get specific parameters
 */	
#define VCTRL_GET 0
#define VCTRL_SET 1

/*
 * An input source can be sepcified in the add command as one of the
 * camer input on the cards as well. For example to select the
 * video input with index 1 on card 0 the station is specified
 * with this prefix followed by two digits, the first for the card
 * and the second for the input source index.
 */
#define INPUT_SOURCE_PREFIX "_inp"

/*
 * A menu item in a control
 */
struct vidcontrol_menu_item {
    unsigned index;
    char name[32];
};

/*
 * Type sof video controls
 */
enum vidcontrol_t {VCTRL_INTEGER=0, VCTRL_BOOLEAN, VCTRL_MENU, VCTRL_CLASS_TITLE};

/*
 * Store information about a specific video control
 */
struct vidcontrol {
    unsigned id;
    enum vidcontrol_t type;
    char name[32];
    int minval;
    int maxval;
    int step;
    int defval;
    int value;
    unsigned flags;
    struct vidcontrol_menu_item menu[12];
    unsigned num_menu;
};

/*
 * Store known frame sizes, e.g "qvga", "qqvga" etc.
 */
struct framesize {
    char name[8];
    int width;
    int height;
};

/**
 * Open and close video functions
 * @param video NUmerb of the video card to open
 */
int video_open(unsigned int video);

/**
 * Close the video card associated with filedescriptor 'fd'
 * @param fd
 * @return
 */
int video_close(int fd);

/*
 * Get/Set control functions-. The set of controls supported by the
 * cards can be a bit diffrent but the v4l2 driver will create
 * standardized names and indexes that we use to reference specific
 * controls
 */

/**
 * Set control value by naming the control
 * @param fd
 * @param name
 * @param val
 * @return
 */
int
video_set_controlbyname(int fd, char *name, int val);

/**
 * Set contro9l value by using the id for the control
 * @param fd
 * @param id
 * @param val
 * @return
 */
int
video_set_controlbyid(int fd, int id, int val);

/**
 * Get the current values of the specified control
 * @param fd
 * @param id
 * @param val
 * @return
 */
int
video_get_controlbyid(int fd, int id, int *val);

/**
 * set the wanted bitrate fro the MP2 HW encoder
 * Both the normal and peak values must be specified, The values are
 * specified in BITS and normally have a value between 1000000 - 8000000
 * As aguide the peak bitrate should be ~500000 higher for good quality
 * @param fd
 * @param bitrate
 * @param peak_bitrate
 * @return
 */
int
video_set_video_bitrate(int fd, unsigned bitrate, unsigned peak_bitrate);

/**
 * Get the current bitrate used by the card
 * @param fd
 * @param bitrate
 * @param peak_bitrate
 * @return -1 on error, 0 on success
 */
int
video_get_video_bitrate(int fd, unsigned *bitrate, unsigned *peak_bitrate);

/**
 * Specify the audio parameter used bu the MP2 HW encoder. Both the sampling
 * and bitrate must be specifed using one of the following symbolic defines
 * 
 * Sampling rates:
 * V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100 	44.1 kHz
 * V4L2_MPEG_AUDIO_SAMPLING_FREQ_48000 	48 kHz
 * V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000 	32 kHz
 * 
 * Bitrates:
 * V4L2_MPEG_AUDIO_L2_BITRATE_192K 	192 kbit/s
 * V4L2_MPEG_AUDIO_L2_BITRATE_224K 	224 kbit/s
 * V4L2_MPEG_AUDIO_L2_BITRATE_256K 	256 kbit/s
 * V4L2_MPEG_AUDIO_L2_BITRATE_320K 	320 kbit/s
 * V4L2_MPEG_AUDIO_L2_BITRATE_384K 	384 kbit/s
 * 
 * @param sampling
 * @param bitrate
 * @return -1 on error, 0 on success 
 */
int
video_set_audio_bitrate(int fd, unsigned sampling, unsigned bitrate);

/**
 * Return the current audio parameters used
 * @param fd
 * @param sampling
 * @param bitrate
 * @return -1 failure, 0 success
 */
int
video_get_audio_bitrate(int fd, unsigned *sampling, unsigned *bitrate);

/*
 * Set the video aspect for the M2 HW encoder. The aspect should be one of the
 * following defines
 * 
 * Aspect:
 * V4L2_MPEG_VIDEO_ASPECT_1x1
 * V4L2_MPEG_VIDEO_ASPECT_4x3
 * V4L2_MPEG_VIDEO_ASPECT_16x9
 * V4L2_MPEG_VIDEO_ASPECT_221x100 
 * 
 * @return -1 on fail, 0 on success
 */
int
video_set_video_aspect(int fd, unsigned aspect);

/**
 * Set the video channel
 * @param fd
 * @param ch
 * @return 0 on success, -1 on failure
 */
int
video_set_channel(const int fd, char *ch);

/**
 * Get the current channel from the TV card
 * @return 0 on suceess, -1 on failure
 * @param fd
 * @param ch
 * @param size
 * @return
 */
int
video_get_channel(const int fd, char *ch, size_t size);

/**
 * Set the output size ffrom the MP2 HW encoder using one
 * of the predefined sizes (See named_sizes)
 * 
 * Example:
 * set_named_size(fd,"qvga");
 * @param fd
 * @param name
 * @return -1 on failure, 0 otherwise
 */
int
video_set_named_size(int fd, const char *name);

/**
 * If the width height corresponds to a predefined size
 * set the buf to point to a statically allocated sting
 * with the predefined name.
 * @param width
 * @param height
 * @param buf
 * @return 0 On success, -1 on failure
 */
int
video_get_sizename(int width, int height, char **buf);

/**
 * Translate a named size to its WxH
 * @param width
 * @param height
 * @param name
 * @return 0 on success, -1 on fail
 */
int
video_get_wh_fromname(int *width, int *height, char *name);

/**
 * Get a list of textual names of all available input sources for the
 * specified video card. It is the calling routines responsibility to
 * make sure that the buffer supplied can hod 32 string pointers
 * @param fd
 * @param nbrinputs
 * @param buff
 * @return
 */
int
video_get_input_source_list(const int fd, int *nbrinputs, char *buff[]);

/**
 * Get the index number for the current video input
 * @param fd
 * @param index
 * @return 0 on success, -1 otherwise
 */
int
video_get_input_source(const int fd, int *index);

/**
 * Set the wanted input video source
 * @param fd
 * @param index
 * @return 0 on success, -1 otherwise
 */
int
video_set_input_source(const int fd, int index);

/**
 * Video Device Control: _vctrl_vidcontrol_tostr
 * Internal helper function. Implements a "toStr" method for a specific control and puts
 * a string representation of the control in the supplied buffr "buff" with maximum
 * length "size". in general the supplied buffer should be at least 128 bytes to get
 * all information from controls.
 */
char *
_vctrl_vidcontrol_tostr(struct vidcontrol *vctl, char *buff, int size, int longformat);

/**
 * Video Device Control: _vctrl_getcontrols
 * Read all the supported constrols from the card and store them in the
 * supplied array of maximum size "size"
 * @return The number of controls found, -1 on error
 */
int
_vctrl_getcontrols(int fd, struct vidcontrol vctl[], size_t size);

/**
 *
 * @param fd
 * @param frequnits
 * @param minfreq
 * @param maxfreq
 * @param analogue_tv
 * @param signal_strength
 * @return -1 on failure, 0 otherwise
 */
int
_vctrl_gettunerinfo(const int fd, double *frequnits,
                    int *minfreq, int *maxfreq,
                    int *analogue_tv, int *signal_strength);

/**
 * Get general card information. The pointers will be set to statically allocated
 * buffers and should never, ever be changed.
 * @param fd
 * @param driver
 * @param card
 * @param version
 * @param capflags
 * @return -1 on failure, 0 otherwise
 */
int
_vctrl_get_cardinfo(int fd, char **driver, char **card,
                    char **version, unsigned int *capflags);

/**
 * Determine the number of installed video cards by trying to open 
 * the first five, one by one
 * @return Number of video cards found
 */
int
_vctrl_getnumcards(void);


#ifdef	__cplusplus
}
#endif

#endif	/* _VCTRL_H */

