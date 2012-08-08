/* =========================================================================
 * File:        TRANSC.H
 * Description: Module to handle transcoding 
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

#ifndef _TRANSC_H
#define	_TRANSC_H

#ifdef	__cplusplus
extern "C" {
#endif


// Wee keep all ongoing transcoding in an array so that we now what is going on.
// Each transcoding is run as a separate process and that process is
// monitored by a thread in the main server.
struct ongoing_transcoding {
    time_t start_ts;
    char *workingdir;
    char *filename;
    char *cmd;
    struct transcoding_profile_entry *profile;
    pid_t pid;
};
extern struct ongoing_transcoding *ongoing_transcodings[] ;
extern const size_t max_ongoing_transcoding;

// We keep track on all transcodings that are waiting to happen
#define MAX_WAITING_TRANSCODINGS 64
struct waiting_transcoding_t {
    char filename[255];
    char profilename[255];
    time_t timestamp;
};
extern struct waiting_transcoding_t wtrans[] ;


/**
 * Check if ffmpeg binaries can be found at the specified location
 * @return -1 on failure, 0 on success
 */
int
check_ffmpeg_bin(void);

/**
 * Remember an ongoing transcoding
 * @param workingdir
 * @param short_filename
 * @param cmd_ffmpeg
 * @param profile
 * @param pid
 * @return An index that uniqely identifies this ongoing transcoding
 */
int
record_ongoingtranscoding(char *workingdir,char *short_filename,char *cmd_ffmpeg,
                          struct transcoding_profile_entry *profile, pid_t pid);

/**
 * Forget an ongoing transcoding
 * @param idx The index of the transcoding to forget.
 */
void
forget_ongoingtranscoding(int idx);

/**
 * Store a string represenattion of all ongoing transcodings in the specified buffer
 * @param obuff Output buffer
 * @param size Maximum size of buffer
 * @param show_ffmpegcmd 1=Alos show the details on the ffmpeg command used
 * @return -1 failure, 0 on success
 */
int
list_ongoing_transcodings(char *obuff, size_t size, int show_ffmpegcmd);

/**
 * Wait until load on the server is low enough to start another transcoding process
 * @param filename
 * @return -1 failure, 0 on success
 */
int
wait_to_transcode(char *filename);

/**
 * Create the actual ffmpeg command line necessary according to the chosen profile
 * @param filename
 * @param profile
 * @param destfile
 * @param destsize
 * @param cmd
 * @param size
 * @return -1 failure, 0 on success
 */
int
create_ffmpeg_cmdline(char *filename, struct transcoding_profile_entry *profile,
                      char *destfile, size_t destsize, char *cmd, size_t size);



/**
 * Return number of ongoing transcodings
 * @return Number of currently ongoing transcodings
 */
size_t
get_num_ongoing_transcodings(void);


/**
 * Kill_ongoing transcoding processes.
 * Kill the specified ongoing transcoding
 * @param idx
 */
int
kill_ongoing_transcoding(int idx);

/**
 * Kill all ongoing transcoding processes. This can be called either
 * via the kill command from a client or it can be automatically called
 * when the server shuts down. This beahviour is constrolled via the
 * configuration file.
 *
 */
void
kill_all_ongoing_transcodings(void);

/**
 * Transcode the specified file using the optional profilename
 * If wait is 0 the transcoding will start immediately without checking
 * for server load.
 * @param filename
 * @param profilename
 * @return
 */
int
transcode_file(char *filename, char *profilename);

/**
 * Read a list of filenames to transcode. The list of files are read from the
 * named file. All files are encoded using the same profile.
 * @param filename File with list of 
 * @param profilename
 * @return
 */
int
read_transcode_filelist(char *filename, char *profilename);

/**
 * Transcode all videos in named directory
 * @param dirpath
 * @param profilename
 * @return
 */
int
transcode_whole_directory(char *dirpath, char *profilename);

/**
 * Return information on the currently queued files for transcoding from the chosen
 * file list
 * @param num The number of the filelist to show
 * @param buffer String buffer where the informatoin will be written
 * @param len Length of buffer
 * @param incfiles 1=include files names in information
 * @return -1 on error 0 otherwise
 */
int
get_queued_transc_filelists_info(int num, char *buffer, size_t len, int incfiles);


/**
 *
 * @param short_filename
 * @param profile_name
 * @return
 */
int
remember_waiting_transcoding(char *short_filename, char *profile_name);

/**
 *
 * @param idx
 * @return
 */
int
forget_waiting_transcoding(int idx);

/**
 *
 * @param buffer
 * @param maxlen
 * @return
 */
int
list_waiting_transcodings(char *buffer, size_t maxlen);

/**
 * Transcode a specified MP2 file into MP4 using the specified profile. After
 * transcoding is done move the file to the corresponding MP4 directory.
 * The values returned as the last three parameters will be used to update
 * the statistics by the calling function.
 * @param datadir
 * @param workingdir
 * @param short_filename
 * @param basename
 * @param profile
 * @param filesize
 * @param transcode_time
 * @param avg_5load
 * @param updatedfilename
 * @return
 */
int
transcode_and_move_file(char *datadir, char *workingdir, char *short_filename, char *recurrence_title,
                        struct transcoding_profile_entry *profile,
                        unsigned *filesize, struct timeall *transcode_time, float *avg_5load, char *updatedfilename);

#ifdef	__cplusplus
}
#endif

#endif	/* _TRANSC_H */

