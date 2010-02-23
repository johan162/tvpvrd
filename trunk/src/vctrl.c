/* =========================================================================
 * File:        VCTRL.C
 * Description: Collection of functions fo interface the video device
 *              For example setting the encoded bitrate for both audio and
 *              video.
 *
 *              All functions named "_vctrl_" are functions that directly
 *              interfaces the video device via ioctl() calls. They can be 
 *              called directly by a client but there are better higher
 *              leve "user" fuinctions more suitable to be called directly.
 *              See documentation for v4l2 for details on the ioctl()
 *              parameters used.
 *              
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

// We want the full POSIX and C99 standard
#define _GNU_SOURCE

// And we need to have support for files over 2GB in size
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

// Standard UNIX headers
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <math.h>
#include <stropts.h>
#include <unistd.h>

// Needed for all the video stuff
#include <linux/videodev2.h>

// Local hader files
#include "tvpvrd.h"
#include "vctrl.h"
#include "utils.h"
#include "freqmap.h"

/*
 * Common name to standard video sizes. This is primariliy used to allow
 * the user to set a standard format of the original MP2 encoder from
 * the TV card.
 */
static struct framesize named_size[] = {
    {"pal",720,576},
    {"qvga",320,240},
    {"qqvga",160,120},
    {"vga",640,480},
    {"cif",352,288},
    {"3q",480,384},
    {"3qmp4",480,352},
    {"half",360,288}
};
/*
 * Size of named_size table
 */
static const int num_named_sizes = sizeof(named_size)/sizeof(struct framesize);

/*
 * A global vector that is used to store the name and values
 * if video constrols that we read from the card and present to
 * the user as information. Each constrol has a name, default value
 * actual value, min/max  range and type.
 */
static struct vidcontrol vidcontrols[32];


/**
 * Special version of ioctl() that can handle an interrup in the middle
 * of a call
 * @return 0 on successs, -1 otherwise 
 */
static int
xioctl(int fd, int request, void * arg) {
    int r;

    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && EINTR == errno);

    return r;
}

/*-------------------------------------------------------------------------------------------
 * The following section defines a number of "_vctrl_" functions. These are the lowest level
 * video interfacing functions which calls ioctl() diectly. They are not in general meant
 * to be used by a client. For this there are higher abstraction functions that doesn't
 * require that the structure of the V4L2 internal data structures is known to the caller.  
 * -------------------------------------------------------------------------------------------
 */

/**
 * Video Device Control: _vctrl_openvideo
 * Open the specified video. The video parameter is a small unsigned (normally in the range 0-4)
 * and speciies the number of the card which is normall accessed trhough the /dev/video0,
 * /dev/video1 etc. device
 * The function does a number of sanity checks to try to determine that the video device
 * really exists.
 * @return a video descriptor used to reference the device in all other control functions
 */
int
_vctrl_openvideo(unsigned int video) {
    struct stat st;
    char vdevice[64];

    // Sanity checks
    if (video > 5) {
        logmsg(LOG_ERR, "(_vctrl_openvideo) Video device must be in range 0-5");
        return -1;
    }
    sprintf(vdevice, "%s%d", "/dev/video", video);

    if (stat(vdevice, &st) == -1) {
        logmsg(LOG_ERR, "Cannot identify '%s': %d, %s", vdevice, errno, strerror(errno));
        return -1;
    }

    if (!S_ISCHR(st.st_mode)) {
        logmsg(LOG_ERR, "%s is no device\n", vdevice);
        return -1;
    }

    int fd = open(vdevice, O_RDONLY | O_NONBLOCK , 0);

    if (fd == -1) {
        logmsg(LOG_ERR, "Cannot open video stream '%s' ( %d : %s )",
               vdevice, errno, strerror(errno));
        return -1;
    }

    logmsg(LOG_NOTICE,"Opened video stream '%s' as fd=%d",vdevice,fd);

    return fd;
}

/**
 * Video Device Control: _vctrl_close
 * Close the specified video descriptor
 * 
 */
int
_vctrl_closevideo(int fd) {
    int ret = close(fd);
    if(ret == -1 ) {
       logmsg(LOG_ERR, "Cannot close video stream fd=%d ( %d : %s )",
		   fd, errno, strerror(errno));
	    return -1;
    }
    // Give the driver some "breathing" time so we cannot open it
    // too fast again.
    usleep(500);
    logmsg(LOG_NOTICE, "Closed video stream fd=%d.",fd);
    return 0;
}


/**
 * Video Device Control: _vctrl_size
 * Get/Set the output size from the video MP2 encoder. A number of common
 * sizes are specified in the named_sizes array.
 * Normamly by default the card will use (for PAL B/G/I) 720x576
 * 
 */
int
_vctrl_size(int set, int fd, int *width, int *height) {
    // Get/Set the size used for the MPEG capture on the card
    struct v4l2_format vfmt;
    CLEAR(vfmt);
    vfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_G_FMT, &vfmt) == -1) {
        logmsg(LOG_ERR, "(VIDIOC_G_FMT) Cannot get video format. %d : %s", errno, strerror(errno));
        return errno;
    } else {
        if (set) {
            vfmt.fmt.pix.width = *width;
            vfmt.fmt.pix.height = *height;
            if (xioctl(fd, VIDIOC_S_FMT, &vfmt) == -1) {
                logmsg(LOG_ERR, "(VIDIOC_S_FMT) Cannot set video format. %d : %s", errno, strerror(errno));
                return errno;
            }
        } else {
            *width = vfmt.fmt.pix.width;
            *height = vfmt.fmt.pix.height;
        }
    }
    return 0;
}

/**
 * Video Device Control: _vctrl_tuner
 * Return detailed information on the the specified tuner. This is really
 * only a helper function for _gettunerinfo
 */
int
_vctrl_tuner(const int set, const int fd,  struct v4l2_tuner *vtun) {
    if( set ) {
        logmsg(LOG_ERR, "_vctrl_tuner does not support VCTRL_SET.");
        return -1;
    }
    if (xioctl(fd, VIDIOC_G_TUNER, vtun) == -1) {
        logmsg(LOG_ERR, "(VIDIOC_G_TUNER) Cannot get information on video tuner. %d : %s", errno, strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Video Device Control: _vctrl_gettunerinfo
 * Return common information from the tuner that might interest a user
 */
int
_vctrl_gettunerinfo(const int fd, double *frequnits,
                      int *minfreq, int *maxfreq,
                      int *analogue_tv, int *signal_strength) {
    struct v4l2_tuner vtun;

    CLEAR(vtun);
    vtun.index = 0; // Assume first tuner

    if( _vctrl_tuner(VCTRL_GET, fd, &vtun) == 0 ) {
        if( frequnits != NULL )
            *frequnits = vtun.capability & V4L2_TUNER_CAP_LOW ? 62.5 : 62500;
        if(minfreq != NULL)
            *minfreq = floor(*frequnits * vtun.rangelow);
        if( maxfreq != NULL )
            *maxfreq = floor(*frequnits * vtun.rangehigh);
        if( analogue_tv != NULL )
            *analogue_tv = vtun.type & V4L2_TUNER_ANALOG_TV;
        if( signal_strength != NULL )
            *signal_strength = vtun.signal;

        return 0;
    }
    else
        return -1;
}

/**
 * Video Device Control: _vctrl_channel
 * Get/Set chanel of the tuner. The channel name must exist in the curren frequency map
 * When the function is called to set the channel the size paramter is not used. This
 * is only meant to give the maximum buffer size of the ch buffer to limit how long
 * string is written when the function is called to get the channel.
 * 
 * Example:
  * _vctrl_channel(VCTR_SET,fd,"SE8",0);
 * char ch[16];
 * _vctrl_channel(VCTR_GET,fd,ch,16);
 */
int
_vctrl_channel(const int set, const int fd, char *ch, int size) {
    unsigned int freq;
    struct v4l2_frequency vfreq;
    char *static_ch;
    double frequnits;

    if( ! set ) {
        CLEAR(vfreq);
        vfreq.tuner = 0;
        vfreq.type = V4L2_TUNER_ANALOG_TV;
        if (xioctl(fd, VIDIOC_G_FREQUENCY, &vfreq) == -1) {
            logmsg(LOG_ERR, "(VIDIOC_G_FREQUENCY) Cannot get video tuner frequency. %d : %s", errno, strerror(errno));
            return errno;
        } else {
            // Get the current unit 65.5 or 65000 Hz per frequence unit
            if( 0 == _vctrl_gettunerinfo(fd,&frequnits,NULL,NULL,NULL,NULL) ) {
                if( 0 == getchfromfreq(&static_ch, floor(vfreq.frequency * frequnits) ) ) {
                    strncpy(ch, static_ch, size);
                    ch[size-1] = '\0';
                } else {
                    snprintf(ch,size,"%d",(int)(vfreq.frequency * frequnits));
                    return -1;
                }
            }
            else {
                return -1;
            }
        }
    } else {
        CLEAR(vfreq);
        vfreq.tuner = 0;
        vfreq.type = 2; // Analog TV

        // Translate channel to frequency
        if (-1 == getfreqfromstr(&freq, ch)) {
            logmsg(LOG_ERR, "Cannot get frequency for channel %s", ch);
            return -1;
        }
        if( 0 == _vctrl_gettunerinfo(fd,&frequnits,NULL,NULL,NULL,NULL) ) {
            // Translate frequency to frequency units
            vfreq.frequency = floor(freq / frequnits);
            if (xioctl(fd, VIDIOC_S_FREQUENCY, &vfreq) == -1) {
                logmsg(LOG_ERR, "(VIDIOC_S_FREQUENCY) Cannot set video tuner frequency. %d : %s",
                       errno, strerror(errno));
                return errno;
            }
        } else {
            return -1;
        }

    }
    return 0;
}

/**
 * Video Device Control: _vctrl_get_cardinfo
 * Get basic information on the TV-Card used, like name , driver and version
 */
int
_vctrl_get_cardinfo(int fd, char **driver, char **card, char **version, unsigned int *capflags) {
    static struct v4l2_capability cap;
    static unsigned char version_string[12];

    CLEAR(cap);
    CLEAR(version_string);

    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
        logmsg(LOG_ERR, "Cannot query capabilities. %d : %s", errno, strerror(errno));
        return -1;
    } else {
        *driver = (char *)cap.driver;
        *card = (char *)cap.card;
        snprintf((char *)version_string,12,"%u.%u.%u",
                (cap.version >> 16) & 0xff,
                (cap.version >> 8) & 0xff,
                (cap.version & 0xff));
        *version = (char *)&version_string[0];
        if( capflags != NULL )
            *capflags = cap.capabilities;
    }
    return 0;
}

/**
 * Video Device Control: _vctrl_vidcontrol_tostr
 * Internal helper function. Implements a "toStr" method for a specific control and puts
 * a string representation of the control in the supplied buffr "buff" with maximum 
 * length "size". in general the supplied buffer should be at least 128 bytes to get
 * all information from controls.
 */
char *
_vctrl_vidcontrol_tostr(struct vidcontrol *vctl, char *buff, int size) {
    static char menu[256];

    if( vctl->type == VCTRL_CLASS_TITLE ) {
        snprintf(buff,size,"\n** %s **\n",vctl->name);
    } else {
        snprintf(buff,size,
                 "%30s (%8d) : val=%d, default=%d, [%d,%d], step=%d, %s\n",
                 vctl->name,vctl->id,
                 vctl->value,vctl->defval,vctl->minval,vctl->maxval,vctl->step,
                 vctl->type==2 ? "(menu)" : vctl->type==1 ? "(bool)" : "(int)");
        if( vctl->type == VCTRL_MENU ) {
            for(int i=0; i < vctl->num_menu; i++) {
                snprintf(menu,255,"%41s : %d = %s\n"," ",vctl->menu[i].index,vctl->menu[i].name);
                menu[255] = '\0';
                strncat(buff,menu,size);
            }
        }
    }
    buff[size-1] = '\0';
    return buff;
}

/**
 * Video Device Control: _vctrl_getcontrols
 * Read all the supported constrols from the card and store them in the
 * supplied array of maximum size "size"
 * @return The number of controls found, -1 on error
 */
int
_vctrl_getcontrols(int fd, struct vidcontrol vctl[], int size) {
    struct v4l2_queryctrl qctl;
    struct v4l2_control ctl;
    struct v4l2_querymenu qmenu;
    int vci;

    CLEAR(qctl);
    qctl.id = V4L2_CTRL_FLAG_NEXT_CTRL;

    // Loop through all controls
    for (vci=0; xioctl(fd, VIDIOC_QUERYCTRL, &qctl) == 0 && vci < size; ) {
        CLEAR(vctl[vci]);
        strncpy(vctl[vci].name,(char *)qctl.name,31);
        vctl[vci].name[31] = '\0';
        vctl[vci].id = qctl.id;

        if (qctl.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
            vctl[vci].type = VCTRL_CLASS_TITLE;
            vci++;
        } else if (!(V4L2_CTRL_FLAG_DISABLED & qctl.flags)) {
            CLEAR(ctl);
            ctl.id = qctl.id;
            if (0 == xioctl(fd, VIDIOC_G_CTRL, &ctl)) {
                vctl[vci].type = VCTRL_MENU;
                vctl[vci].minval = qctl.minimum;
                vctl[vci].maxval = qctl.maximum;
                vctl[vci].defval = qctl.default_value;
                vctl[vci].step = qctl.step;
                vctl[vci].value = ctl.value;
                vctl[vci].flags = qctl.flags;
                if (qctl.type == V4L2_CTRL_TYPE_MENU) {
                    vctl[vci].type = VCTRL_MENU;
                    CLEAR(qmenu);
                    qmenu.id = qctl.id;
                    int midx=0;
                    for (qmenu.index = qctl.minimum; qmenu.index <= qctl.maximum; qmenu.index++) {
                        if (0 == ioctl(fd, VIDIOC_QUERYMENU, &qmenu)) {
                            vctl[vci].menu[midx].index = qmenu.index;
                            strncpy(vctl[vci].menu[midx].name, (char *)qmenu.name,31);
                            vctl[vci].menu[midx].name[31] = '\0';
                            midx++;
                        }
                    }
                    vctl[vci].num_menu = midx;
                } else if(qctl.type == V4L2_CTRL_TYPE_INTEGER) {
                    vctl[vci].type = VCTRL_INTEGER;
                }
                else {
                    vctl[vci].type = VCTRL_BOOLEAN;
                }
            } else {
                logmsg(LOG_ERR, "(VIDIOC_G_CTRL) Cannot read value from control '%s' (id=%d) %d : %s",qctl.name, qctl.id, errno, strerror(errno));
                return -1;
            }
            vci++;
        }
        qctl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    return vci;
}


/**
 * Video Device Control: _vctrl_get_controlvaluebyname
 * Get the values of a control by specifying the contro by it's user friendly name
 * 
 */
int
_vctrl_get_controlvaluebyname(const char *name,int *val,int *type,struct vidcontrol controls[],int size) {
    int i;
    for( i=0; i < size && strcmp(name,controls[i].name); i++)
        ;
    if( i < size ) {
        *val = controls[i].value;
        *type = controls[i].type;
        return controls[i].id;
    }
    return -1;
}

/**
 * Video Device Control: _vctrl_get_controlvalue
 * Get the value of a control specified by its id
 */
int
_vctrl_get_controlvalue(int fd, unsigned id, int *val) {
    struct v4l2_control ctl;
    CLEAR(ctl);
    ctl.id = id;
    if( 0 == xioctl(fd, VIDIOC_G_CTRL, &ctl) ) {
        *val = ctl.value;
        return 0;
    } else {
        logmsg(LOG_ERR, "(VIDIOC_G_CTRL) Cannot get value for control (id=%d) %d : %s", ctl.id, errno, strerror(errno));
        return errno;
    }
}


/**
 * Video Device Control: _vctrl_set_controlvalue
 * Set the value of a control specified by its id
 * 
 */
int
_vctrl_set_controlvalue(int fd, unsigned id, int val) {
    struct v4l2_control ctl;
    CLEAR(ctl);
    ctl.id = id;
    ctl.value = val;
    if( 0 == xioctl(fd, VIDIOC_S_CTRL, &ctl) ) {
        return 0;
    } else {
        if( errno == EBUSY ) {
            // The control could be temporarily busy so we sleep for 1 second and try again
            sleep(1);
            CLEAR(ctl);
            ctl.id = id;
            ctl.value = val;
            if( 0 == xioctl(fd, VIDIOC_S_CTRL, &ctl) ) {
                return 0;
            } else {
                logmsg(LOG_ERR, "(VIDIOC_S_CTRL) Cannot set value (%d) to control (id=%d) %d : %s", val, ctl.id, errno, strerror(errno));
                return errno;
            }

        } else {
            logmsg(LOG_ERR, "(VIDIOC_S_CTRL) Cannot set value (%d) to control (id=%d) %d : %s", val, ctl.id, errno, strerror(errno));
            return errno;
        }
    }
}

/**
 * Video Device Control: _vctrl_get_cropcap
 * Get the crop capability structure
 */
int
_vctrl_get_cropcap(int fd, struct v4l2_cropcap *vcrop) {

    CLEAR(*vcrop);
    vcrop->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if( 0 == xioctl(fd, VIDIOC_CROPCAP, vcrop) ) {
        return 0;
    } else {
        logmsg(LOG_ERR, "(VIDIOC_CROPCAP) Cannot get crop capabilities. %d : %s", errno, strerror(errno));
        return errno;
    }
}

/* --------------------------------------------------------------------------------------------------------
 * The following set of set_/get_ functions are user level functions that can be called directly
 * to set/get the sepcified values for the video card. 
 * --------------------------------------------------------------------------------------------------------
 */

/* 
 * Open and close video functions
 */
int video_open(unsigned int video) {
    int fd = _vctrl_openvideo(video);

    // We need to set the FD_CLOEXEC flag to make
    // sure the video descriptor is closed in any
    // childs we fork. Otherwise the video will be
    // kept open when it is not in use.
    set_cloexec_flag(fd, 1);

    return fd;
}

int video_close(int fd) {
    return _vctrl_closevideo(fd);
}

/*
 * Get/Set control functions
 */
int
video_set_controlbyname(int fd, char *name,int val) {
    int oldval,type;
    int id =  _vctrl_get_controlvaluebyname(name,&oldval,&type,vidcontrols,32);
    if( id >= 0 ) {
        return _vctrl_set_controlvalue(fd, id, val);
    }
    return id;
}

int
video_set_controlbyid(int fd, int id, int val) {
    return _vctrl_set_controlvalue(fd, id, val);
}

int
video_get_controlbyid(int fd, int id, int *val) {
    return _vctrl_get_controlvalue(fd, id, val);
}


/*
 * set the wanted bitrate fro the MP2 HW encoder
 * Both the normal and peak values must be specified, The values are
 * specified in BITS and normally have a value between 1,000,000 - 8,000,000
 * As aguide the peak bitrate should be ~500,000 higher for good quality
 *
 * Note: The driver doesn't allow a bitrate to be set higher then the peak
 * bitrate nor does it allow the peak bit rate to be set lower than the current
 * bit rate. This means that we must read the current values and depending
 * on these either set the peak or the normal bitrate first to make sure
 * that the bitrate is never higher than than the peak bitrate. The rules
 * will then be:
 *
 * B = New bitrate, PB = New peak bitrate, PB > B
 * OB = Old bitrate , OPB Old peak bitrate, OPB > OB
 *
 * if( PB  < OB ) {
 *     Set B; Set PB;
 * } else {
 *     Set PB; Set B;
 * }
 */
int
video_set_video_bitrate(int fd, int bitrate, int peak_bitrate) {

    // First some sanity check on the parameters
    if (bitrate < 500000 || peak_bitrate < 500000) {
        logmsg(LOG_ERR, "Video bitrate or peak bitrate specifed is out of range < 500,000. values=(%d,%d)",bitrate,peak_bitrate);
        return -1;
    }
    if (bitrate > 8000000 || peak_bitrate > 8000000) {
        logmsg(LOG_ERR, "Video bitrate or peak bitrate specifed is out of range > 8,000,000. values=(%d,%d)",bitrate,peak_bitrate);
        return -1;
    }

    int old_bitrate, old_peakbitrate;
    int ret = video_get_video_bitrate(fd, &old_bitrate, &old_peakbitrate);
    if( ret != 0 ) {
        logmsg(LOG_ERR, "Can not read old video bitrate before setting new fd=%d ( %d : %s )", fd, errno, strerror(errno));
        return -1;
    }

    if( peak_bitrate < old_peakbitrate ) {
        ret = video_set_controlbyid(fd, V4L2_CID_MPEG_VIDEO_BITRATE, bitrate);
        if (ret != 0) {
            logmsg(LOG_ERR, "Can not set video bitrate fd=%d ( %d : %s )", fd, errno, strerror(errno));
            return -1;
        }
        ret = video_set_controlbyid(fd, V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, peak_bitrate);
        if (ret != 0) {
            logmsg(LOG_ERR, "Can not set video peak bitrate fd=%d ( %d : %s )", fd, errno, strerror(errno));
            return -1;
        }
    } else {
        ret = video_set_controlbyid(fd, V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, peak_bitrate);
        if (ret != 0) {
            logmsg(LOG_ERR, "Can not set video peak bitrate fd=%d ( %d : %s )", fd, errno, strerror(errno));
            return -1;
        }
        ret = video_set_controlbyid(fd, V4L2_CID_MPEG_VIDEO_BITRATE, bitrate);
        if (ret != 0) {
            logmsg(LOG_ERR, "Can not set video bitrate fd=%d ( %d : %s )", fd, errno, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/*
 * Get the current bitrate used by the card
 */
int
video_get_video_bitrate(int fd, int *bitrate, int *peak_bitrate) {
    int ret = video_get_controlbyid(fd,V4L2_CID_MPEG_VIDEO_BITRATE, bitrate);
    if( ret != 0 )
        return ret;
    return video_get_controlbyid(fd,V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, peak_bitrate);
}

/*
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
 * @return -1 on error, 0 on success 
 */
int
video_set_audio_bitrate(int fd, int sampling, int bitrate) {
    if( sampling < V4L2_MPEG_AUDIO_SAMPLING_FREQ_44100 || sampling > V4L2_MPEG_AUDIO_SAMPLING_FREQ_32000 )
        return -1;
    int ret = video_set_controlbyid(fd, V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ, sampling);
    if( ret != 0 ) {
        logmsg(LOG_ERR,"Can not set audio sampling rate fd=%d ( %d : %s )",fd,errno, strerror(errno));
        return -1;
    }
    if( bitrate < V4L2_MPEG_AUDIO_L2_BITRATE_192K || bitrate > V4L2_MPEG_AUDIO_L2_BITRATE_384K )
        return -1;
    ret = video_set_controlbyid(fd, V4L2_CID_MPEG_AUDIO_L2_BITRATE, bitrate);
    if( ret != 0 ) {
        logmsg(LOG_ERR,"Can not set audio bitrate fd=%d ( %d : %s )",fd,errno, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Return the current audio parameters used
 */
int
video_get_audio_bitrate(int fd, int *sampling, int *bitrate) {
    int ret = video_get_controlbyid(fd, V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ, sampling);
    if( ret != 0 )
        return ret;
    return video_get_controlbyid(fd, V4L2_CID_MPEG_AUDIO_L2_BITRATE, bitrate);
}

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
video_set_video_aspect(int fd, int aspect) {
    int ret = video_set_controlbyid(fd, V4L2_CID_MPEG_VIDEO_ASPECT, aspect);
    if( ret != 0 ) {
        logmsg(LOG_ERR,"Can not set video aspect fd=%d ( %d : %s )",fd,errno, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Set the video channel
 * @return 0 on success, -1 on failure
 */
int
video_set_channel(const int fd, char *ch) {
    int ret = _vctrl_channel(VCTRL_SET, fd, ch, strlen(ch)+1);
    if( ret != 0 ) {
        logmsg(LOG_ERR,"Can not video channel fd=%d ( %d : %s )",fd,errno, strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Get the current channel from the TV card
 * @return 0 on suceess, -1 on failure
 */
int
video_get_channel(const int fd, char *ch, int size) {
    return _vctrl_channel(VCTRL_GET, fd, ch, size);
}

/*
 * Set the output size ffrom the MP2 HW encoder using one
 * of the predefined sizes (See named_sizes)
 * 
 * Example:
 * set_named_size(fd,"qvga");
 */
int
video_set_named_size(int fd, const char *name) {
    int i;
    for(i=0; i < num_named_sizes && strcmp(name,named_size[i].name); i++)
        ;
    if( i>=num_named_sizes )
        return -1;
    int ret = _vctrl_size(VCTRL_SET, fd, &named_size[i].width, &named_size[i].height);
    if( ret != 0 ) {
        logmsg(LOG_ERR,"Can not set video named size fd=%d ( %d : %s )",fd,errno, strerror(errno));
        return -1;
    }
    return 0;

}

/*
 * If the width height corresponds to a predefined size
 * set the buf to point to a statically allocated sting
 * with the predefined name.
 * @return 0 On success, -1 on failure 
 */
int
video_get_sizename(int width,int height,char **buf) {
    int i;
    for(i=0; i < num_named_sizes; i++) {
        if( named_size[i].height == height && named_size[i].width == width ) {
            *buf = &named_size[i].name[0];
            return 0;
        }
    }
    return -1;
}

/**
 * Translate a named size to its WxH
 * @return 0 on success, -1 on fail
 */
int
video_get_wh_fromname(int *width, int *height, char *name) {
    int i;
    for(i=0; i < num_named_sizes && stricmp(name,named_size[i].name); i++)
        ;
    if( i>=num_named_sizes )
        return -1;
    *width = named_size[i].width;
    *height = named_size[i].height;
    return 0;
}


/* vctrl.c */