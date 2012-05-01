/* =========================================================================
 * File:        FREQMAP.H
 * Description: This module is responsible for mapping between
 *              frequency and frequency table with there named channels.
 *              Please note that named channels have nothng to do with the
 *              broadcasting names. The channels names are standardized names
 *              for frequences. The names changes depending on geographic
 *              location and hence each geographic location uses its own
 *              frequency map. By default the "west-euprope" map will be used.
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
#ifndef _FREQMAP_H
#define	_FREQMAP_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Currently supported frequency maps
 */
typedef enum freqm {FREQMAP_EUROPEWEST=0, FREQMAP_EUROPEEAST, FREQMAP_FRANCE,
                    FREQMAP_IRELAND, FREQMAP_ITALY, FREQMAP_AUSTRALIA,
                    FREQMAP_NEWZEALAND, FREQMAP_USBCAST, FREQMAP_USCABLE,
                    FREQMAP_USCABLE_HRC, FREQMAP_USCABLE_IRC,
                    FREQMAP_JAPAN_BCAST, FREQMAP_JAPAN_CABLE,
                    FREQMAP_CHINA_BCAST, FREQMAP_SOUTHAFRICA,
                    FREQMAP_ARGENTINA, FREQMAP_AUSTRALIA_OPTUS } freqm_t;

/**
 * A frequency and its corresponding name. This is an element in the
 * frequency channel map. Do not confuse the channel name with the station
 * name (e.g. 'BBC1'). The frequency map to use is defined in the configuration
 * file at startup.
 */
struct freqch {
    unsigned freq;
    char ch[5];
};

/**
 * Specifies a particular frequency map by name, the size of the map and
 * a pointer to the actual frequency table. This is initialized at startup
 * and is statically defined.
 */
struct freqmap {
    char name[MAX_FMAPNAME_LENGTH];
    unsigned size;
    struct freqch *tbl;
};

/**
 * Initialize the frequency table. Must be called in the beginning of the
 * program
 */
void
initfreqtable(void);

/**
 * Set the current frequency map
 * @param name frequency map name
 * @return -1 on failure, idx >= 0 on sucess
 */
int
set_current_freqmap(const char *name);

/**
 * Get the current frequency map, both name and index
 * @param name Buffer set to the name of the current map
 * @param size Maximum size in bytes of buffer
 * @return The current map index
 */
int
get_current_freqmap(char *name, size_t size);

/**
 * Get the corresponding frequency map index from a map name
 * @param name
 * @return -1 on failure, the positive index otherwise
 */
int
getfmapidx(const char *name);

/**
 * Translate a frequency to the standard channel name in the frequency map.
 * Note: it isthe callers responsibility that the parameter fmap is a valid
 * map.
 * @param ch
 * @param freq
 * @return 0 on successs, -1 otherwise
 */
int
getchfromfreq(char **ch, const unsigned int freq);

/**
 * Translate a channel name to a frequency using the specified frequency map
 * @param freq in Hertz
 * @param ch channel name
 * @return 0 on successs, -1 otherwise
 */
int
getfreqfromch(unsigned int *freq, const char *ch);

/**
 * Fill the given buffer with a list of pointers to the station names.
 * Note: The entries are pointed to static storages and cannot be modified!
 * @param stations
 * @param maxlen
 * @return number of stations
 */
int
get_stations(const char *stations[],size_t maxlen);

/*
 * Fill the supplied buffer (array of string pointers)
 * with pointer to statically alocated buffers with the name of
 * all the defined frequency maps.
 * @param names Array of defined frequency maps
 * @param size Maximum size of buffert
 * @return number of names read
 */
unsigned
getfmapnames(char *names[], size_t size);

/**
 * Read the station top channel mapping fil
 * @param name Name of station file
 * @return -1 on failure , 0 otherwise
 */
int
read_xawtvfile(const char *name);

/**
 * Write a list of all defined stations to the specified file descriptor
 * @param fd File descriptor to write to
 */
void
list_stations(int fd);

/**
 * Translate a station name to a channel name in the cuurent frequency map
 * @param station Name of station
 * @param chbuffer Buffer to hold the channel name
 * @param size The maximum size of buffer
 * @return -1 on failure , 0 otherwise
 */
int
get_chfromstation(const char *station, char *chbuffer, size_t size);

/**
 * Translate a symbolic name to a frequency. The string can be given as
 * either a station or channel name
 * @param freq
 * @param name
 * @return 0 on success, -1 on failure
 */
int
getfreqfromstr(unsigned int *freq,const char *name);

/**
 * Store a list of "maxnum" elements length with pointers to all names of the
 * predefined frequency tables
 * 
 * @param list - list of array pointers that are filled with pointers to statically
 * allocated areas that MUST NOT be touched by the calling function.
 * @param maxnum
 * @return 0 on success, -1 on failure
 */
int
getfreqmaplist(char *list[], size_t maxnum);


#ifdef	__cplusplus
}
#endif

#endif	/* _FREQMAP_H */

