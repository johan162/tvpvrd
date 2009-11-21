/* =========================================================================
 * File:        TVCMD.H
 * Description: This module manage the command interface and is responsible
 *              to interpret and execute the commands given to the server.
 *              The implementation is based on a fairly standard command
 *              function jump table. Each command is parsed using
 *              pcre_grep() family of functions (Perl Compatible regex).
 *              Note that the regex is made to support UTF-8/16 characters.
 * Author:      Johan Persson (johanp@aditus.nu)
 * SVN:         $Id: tvcmd.h 144 2009-11-21 10:48:04Z ljp $
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

/*
 " *\n"\
 " * datadir\n"\
 " * ├── xmldb\n"\
 " * ├── mp2\n"\
 " * ├── mp4\n"\
 " * │   ├── high\n"\
 " * │   ├── mobile\n"\
 " * │   └── normal\n"\
 " * └── vtmp\n"\
 " *     ├── vid0\n"\
 " *     ├── vid1\n"\
 " *     ├── ...\n"\
 " *\n"\
 " * Legend:\n"\
 " * ------------------------------------------------------------------\n"\
 " * mp2   - The original mp2 encoded files from the video cards\n"\
 " * vtmp  - Temporary files created during recoding and transcoding\n"\
 " * mp4   - The transcoded mp4 files per defined profile\n"\
 " * xmldb - Teh XML DB file (+backups) are stored in this directory\n"\
 " * ------------------------------------------------------------------\n"\


*/


#ifndef _TVCMD_H
#define	_TVCMD_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Command interpretator. Givena  command string and
 * socket to communnicate on the function will execute
 * the parsed command and write the output back to the given
 * descriptor.
 * @param cmd
 * @param sockfd
 */
void cmdinterp(const char *cmd, int sockfd);

/*
 * Initialize the command interpretator. Must be called at
 * program startup.
 */
void cmdinit(void);

/*
 * Clean up the command interpretator
 */
void cmdfree(void);


/*
 * Update the database with all recordings. This must be made public outside
 * since this is called in the shutdown seqeunce.
 */
void _cmd_updatexmlfile(const char *cmd, int sockfd);

/**
 * Macro for issuing the update command
 */
#define UPDATE_DB() (void)_cmd_updatexmlfile("u", -1)

#ifdef	__cplusplus
}
#endif

#endif	/* _TVCMD_H */

