/* =========================================================================
 * File:        TVXMLDB.C
 * Description: Module for reading/writing the database of all recordings
 *              to and from file. The database itself is stored in
 *              XML format in plain text files.
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

// Standard headers
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include <pcre.h>

// XML2 lib headers
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlreader.h>

#include "tvxmldb.h"
#include "tvpvrd.h"
#include "utils.h"
#include "recs.h"
#include "transc.h"

/* ---------------------------------------------------------------------------
 * XML File processing functions
 * ---------------------------------------------------------------------------
 */

/*
 * Names of element in the XML file
 */
#define XMLDB_VERSIONNUM "1"

static const xmlChar *xmldb_version =           (xmlChar *) XMLDB_VERSIONNUM;
static const xmlChar *xmldb_root =              (xmlChar *) "tvrecdb";
static const xmlChar *xmldb_nameVersion =       (xmlChar *) "version";
static const xmlChar *xmldb_nameRecording =     (xmlChar *) "recording";
static const xmlChar *xmldb_nameText =          (xmlChar *) "text";
static const xmlChar *xmldb_nameStartdate =     (xmlChar *) "startdate";
static const xmlChar *xmldb_nameEnddate =       (xmlChar *) "enddate";
static const xmlChar *xmldb_nameEndtime =       (xmlChar *) "endtime";
static const xmlChar *xmldb_nameStarttime =     (xmlChar *) "starttime";
static const xmlChar *xmldb_nameChannel =       (xmlChar *) "channel";
static const xmlChar *xmldb_nameVideo =         (xmlChar *) "video";
static const xmlChar *xmldb_nameFilename =      (xmlChar *) "filename";
static const xmlChar *xmldb_nameTitle =         (xmlChar *) "title";
static const xmlChar *xmldb_nameRecurrence =    (xmlChar *) "repeat";
//static const xmlChar *xmldb_nameDir =           (xmlChar *) "directory";
static const xmlChar *xmldb_nameRecType =       (xmlChar *) "type";
static const xmlChar *xmldb_nameRecNbr =        (xmlChar *) "nbr";
static const xmlChar *xmldb_nameRecMangling =   (xmlChar *) "titlemangling";
static const xmlChar *xmldb_propnameRecPrefix = (xmlChar *) "prefix";
static const xmlChar *xmldb_nameTProfile =      (xmlChar *) "transcodeprofile";
static const xmlChar *xmldb_nameRecStartNumber= (xmlChar *) "startnumber";
/*
 * Process a <repeat> .. </repeat> block
 *
 */
static void processRepeatingRecording(xmlDocPtr doc, xmlNodePtr node, int *rectype, int *recnbr, 
                                      int *recmangling, char *recprefix, int *startnumber) {
    xmlNodePtr childnode;
    xmlChar *xmlval;

    node = node->xmlChildrenNode;
    while (node != NULL) {

        if (xmlStrcmp(node->name, xmldb_nameText)) {

            childnode = node->xmlChildrenNode;

            if (xmlStrcmp(node->name, xmldb_nameRecType) == 0) {
                if (xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    *rectype = atoi((const char *) childnode->content);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameRecNbr) == 0) {
                if (xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    *recnbr = atoi((const char *) childnode->content);
                }
            } else if(xmlStrcmp(node->name,xmldb_nameRecStartNumber) == 0 ) {
                if (xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    *startnumber = atoi((const char *) childnode->content);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameRecMangling) == 0) {
                if (xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    xmlval = xmlGetProp(node, xmldb_propnameRecPrefix);
                    if (xmlval != NULL) {
                        strncpy(recprefix, (const char *) xmlval, REC_MAX_NPREFIX - 1);
                        recprefix[REC_MAX_NPREFIX - 1] = 0;
                        xmlFree(xmlval);
                    } else {
                        strncpy(recprefix, DEFAULT_PREFIX, REC_MAX_NPREFIX - 1);
                        recprefix[REC_MAX_NPREFIX - 1] = 0;
                    }
                    *recmangling = atoi((const char *) childnode->content);
                }
            } else {
                logmsg(LOG_ERR, "Unknown XML node name in repeated recording: %s", node->name);
            }
        }
        node = node->next;
    }
}

/*
 * Parse a tim string given as nn:nn[:nn] and store the values in
 * the last three arguments. if seconds is not specified in the string
 * it is set to 0
 */
int
parseTime(const char *time, int *h, int *m, int *s) {
    int ret;
    char **field;

    // Setup regex for parsing of date
    ret = matchcmd("([0-2][0-9]):([0-5][0-9])(:([0-5][0-9]))?",time,&field);

    if (ret == 3 || ret == 5 ) {
        *h = atoi(field[1]);
        *m = atoi(field[2]);
        if( ret == 5) {
            *s = atoi(field[4]);
        } else {
            *s = 0;
        }
        pcre_free_substring_list((const char **)field);
        return 1;
    } else {
        *h = 0;
        *m = 0;
        *s = 0;
        return 0;
    }
}

/*
 * Parse a date string given as yyyy-mm-dd and store the values in
 * the last three arguments.
 */
int
parseDate(const char *date, int *y, int *m, int *d) {
    int ret;
    char **field;

    // Setup regex for parsing of date
    ret = matchcmd("(20[0-4][0-9])-([0-1]?[0-9])-([0-3]?[0-9])",date,&field);

    if (ret == 4) {
        *y = atoi(field[1]);
        *m = atoi(field[2]);
        *d = atoi(field[3]);
        pcre_free_substring_list((const char **)field);
        return 1;
    } else {
        *y = 0;
        *m = 0;
        *d = 0;
        return 0;
    }
}

/*
 * Parse a single recording in the XML file. Extract the necessary fields
 * and add this as a proper entry in te list of recordings.
 */
static void processRecording(xmlDocPtr doc, xmlNodePtr node) {
    char directory[512], tmpbuff[512], bname_buffer[512];
    char filename[REC_MAX_NFILENAME], title[REC_MAX_NTITLE], channel[REC_MAX_NCHANNEL];
    char recprefix[REC_MAX_NPREFIX], *profiles[REC_MAX_TPROFILES];
    xmlNodePtr childnode;
    int sy, sm, sd;
    int ey, em, ed;
    int sh, smin, ssec;
    int eh, emin, esec;
    time_t ts_start, ts_end;
//    char *bname, *dname;
    struct recording_entry *entry;

    node = node->xmlChildrenNode;
    
    int recurrence = 0;
    int rectype = 0;
    int recnbr = 0;
    int recmangling = 0;
    int startnumber=0;
    int video=-1;
    recprefix[0] = '\0';
    directory[0] = '\0';

    int num_profiles = 0;
    for(int i=0; i < REC_MAX_TPROFILES; i++) {
        profiles[i] = calloc(1,REC_MAX_TPROFILE_LEN);
    }


    while (node != NULL) {

        // Ignore text in between proper child
        if (xmlStrcmp(node->name, xmldb_nameText)) {
            childnode = node->xmlChildrenNode;
            if (xmlStrcmp(node->name, xmldb_nameStartdate) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    if (!parseDate((const char *) childnode->content, &sy, &sm, &sd)) {
                        logmsg(LOG_ERR, "Failed to parse start date in XML file.");
                    }
                }
            } else if (xmlStrcmp(node->name, xmldb_nameEnddate) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {

                    if (!parseDate((const char *) childnode->content, &ey, &em, &ed)) {
                        logmsg(LOG_ERR, "Failed to parse end date in XML file.");
                    }
                }
            } else if (xmlStrcmp(node->name, xmldb_nameStarttime) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    if (!parseTime((const char *) childnode->content, &sh, &smin, &ssec)) {
                        logmsg(LOG_ERR, "Failed to parse start time in XML file.");
                    }
                }
            } else if (xmlStrcmp(node->name, xmldb_nameEndtime) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    if (!parseTime((const char *) childnode->content, &eh, &emin, &esec)) {
                        logmsg(LOG_ERR, "Failed to parse end time in XML file.");
                    }
                }
            } else if (xmlStrcmp(node->name, xmldb_nameTitle) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    strncpy(title, (const char *) childnode->content, REC_MAX_NTITLE - 1);
                    title[REC_MAX_NTITLE - 1] = 0;
                }
            } else if (xmlStrcmp(node->name, xmldb_nameFilename) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    strncpy(filename, (const char *) childnode->content, REC_MAX_NFILENAME - 1);
                    filename[REC_MAX_NFILENAME - 1] = 0;
                }
            } else if (xmlStrcmp(node->name, xmldb_nameChannel) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    strncpy(channel, (const char *) childnode->content, REC_MAX_NCHANNEL - 1);
                    channel[REC_MAX_NCHANNEL - 1] = 0;
                }
            } else if (xmlStrcmp(node->name, xmldb_nameTProfile) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    strncpy(profiles[num_profiles], (const char *) childnode->content, REC_MAX_TPROFILE_LEN - 1);
                    profiles[num_profiles++][REC_MAX_TPROFILE_LEN - 1] = 0;
                }                
            } else if (xmlStrcmp(node->name, xmldb_nameVideo) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    video = atoi((const char *) childnode->content);
                    childnode = childnode->next;
                }
            } else if (xmlStrcmp(node->name, xmldb_nameRecurrence) == 0) {
                recurrence = 1;
                processRepeatingRecording(doc, node,
                        &rectype, &recnbr, &recmangling, recprefix, &startnumber);
            } else {
                logmsg(LOG_ERR, "Unknown XML node name: %s", node->name);
            }
        }
        node = node->next;
    }

    strncpy(bname_buffer, filename,511);
    bname_buffer[511] = '\0';
    strncpy(filename, basename(bname_buffer), REC_MAX_NFILENAME);

    if( video >= max_video || video < 0 ) {
        logmsg(LOG_ERR, "Cannot insert record: \"%s\" invalid video card specified (%d)", title,video);
    }
    else {
        // Create a new recording. This means that a recurrent recording is expanded with
        // a single record for all its occurrences
        ts_start = totimestamp(sy, sm, sd, sh, smin, ssec);
        ts_end = totimestamp(ey, em, ed, eh, emin, esec);

        if( num_profiles == 0 ) {
            logmsg(LOG_ERR,"    -- Warning: No profiles defined for recording: '%s'. Adding default profile '%s' ",
                    title,default_transcoding_profile);
            num_profiles=1;
            strncpy(profiles[0],default_transcoding_profile,REC_MAX_TPROFILE_LEN);
            profiles[0][REC_MAX_TPROFILE_LEN-1] = '\0';
        }
        for( int k=0; k < num_profiles; k++) {
            if(!transcoding_profile_exist(profiles[k]) ) {
                logmsg(LOG_NOTICE,"    -- Warning: Transcoding profile %s does not exist. Falling back on default profile.",profiles[k]);
                strncpy(profiles[k],default_transcoding_profile,REC_MAX_TPROFILE_LEN-1);
                profiles[0][REC_MAX_TPROFILE_LEN-1] = '\0';
            }
        }

        entry = newrec(title, filename,
                       ts_start, ts_end,
                       channel, recurrence,
                       rectype, recnbr, recmangling,
                       profiles);

        entry->recurrence_start_number = startnumber;
        
        for(int i=0; i < REC_MAX_TPROFILES; i++) {
            free(profiles[i]);
        }

        if (insertrec(video, entry) == 0) {
            snprintf(tmpbuff, 512, "Cannot insert record \"%s\" since it collides with existing recordings.", entry->title);
            tmpbuff[512-1] = '\0';
            logmsg(LOG_ERR, tmpbuff);
            freerec(entry,"processRecording()");
            entry = NULL;
        } else {
            logmsg(LOG_NOTICE, "  -- inserted record '%s'", title);
        }
    }
}

/**
 * processXMLFile
 * Parse the XML file with recordings and build the internal
 * memory structure
 */
int
readXMLFile(const char *filename) {
    xmlNodePtr node;
    xmlDocPtr doc;

    xmlChar *xmlver;
    /*
     * 1  == Element
     * 15 == EndElement
     * 8  == Comment
     * 2  == Attribute
     * 3  == Text field
     * 17 == XML declaration
     * 14 == Significant White Space
     */

    /*
     * this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION

    // Parse the XML file
    doc = xmlParseFile(filename);
    if (doc == NULL) {
        logmsg(LOG_ERR, "Unable to open XML Database file: \"%s\"", filename);
        return -1;
    }

    node = xmlDocGetRootElement(doc);

    if (xmlStrcmp(node->name, xmldb_root)) {
        logmsg(LOG_ERR, "XML file is not a proper recording database file. Wrong root element. Found \"%s\" when expecting \"%s\"",
                node->name,xmldb_root);
        xmlFreeDoc(doc);
        return -1;
    }

    // Check that the version of the file is the expected
    xmlver = xmlGetProp(node, xmldb_nameVersion);
    if (xmlStrcmp(xmlver, xmldb_version)) {
        logmsg(LOG_ERR, "XML file is not a proper recording database file. Wrong version. Found \"%s\" when expecting \"%s\"",
                xmlver,xmldb_version);
        xmlFreeDoc(doc);
        return -1;
    }
    xmlFree(xmlver);


    node = node->xmlChildrenNode;
    while (node != NULL) {
        if (xmlStrcmp(node->name, xmldb_nameRecording) == 0) {
            processRecording(doc, node);
        }
        node = node->next;
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return 0;
}

/*
 * Write an XML representation of the internal recording arrays to the file pointed to by
 * the specified descriptor
 */
int
_writeXMLFile(const int fd) {
    int j, nsaved_recrec;
    int y, m, d, h, min, sec;
    int saved_recrec[2 * MAX_ENTRIES];
    char tmpbuff[256];
    time_t now;

    nsaved_recrec = 0;
    now = time(NULL);

    // Test if the first write was ok then we assume that the rest will
    // be ok as well. Not perfect but at least this way we will catch
    // wrong permissions

    if( -1 == _writef(fd, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n") ) {
        return -1;
    }
    _writef(fd, "<!-- Created: %s -->\n", ctime(&now));
    _writef(fd, "<%s %s=\"1\">\n",xmldb_root,xmldb_nameVersion);

    for (int video = 0; video < max_video; video++) {

        for (int i = 0; i < num_entries[video]; i++) {

            if (recs[REC_IDX(video, i)]->recurrence == 0) {
                // Process a single recording
                _writef(fd, "  <%s>\n",xmldb_nameRecording);
                _writef(fd, "    <%s>%s</%s>\n", xmldb_nameTitle,recs[REC_IDX(video, i)]->title,xmldb_nameTitle);
                _writef(fd, "    <%s>%s</%s>\n", xmldb_nameChannel, recs[REC_IDX(video, i)]->channel,xmldb_nameChannel);
                _writef(fd, "    <%s>%d</%s>\n", xmldb_nameVideo,video,xmldb_nameVideo);
                fromtimestamp(recs[REC_IDX(video, i)]->ts_start, &y, &m, &d, &h, &min, &sec);
                _writef(fd, "    <%s>%02d-%02d-%02d</%s>\n",xmldb_nameStartdate, y, m, d,xmldb_nameStartdate);
                _writef(fd, "    <%s>%02d:%02d:%02d</%s>\n",xmldb_nameStarttime, h, min, sec,xmldb_nameStarttime);
                fromtimestamp(recs[REC_IDX(video, i)]->ts_end, &y, &m, &d, &h, &min, &sec);
                _writef(fd, "    <%s>%02d-%02d-%02d</%s>\n",xmldb_nameEnddate, y, m, d,xmldb_nameEnddate);
                _writef(fd, "    <%s>%02d:%02d:%02d</%s>\n",xmldb_nameEndtime, h, min, sec,xmldb_nameEndtime);               
                strncpy(tmpbuff, recs[REC_IDX(video, i)]->filename, 255);
                tmpbuff[255] = 0;
                _writef(fd, "    <%s>%s</%s>\n",xmldb_nameFilename, basename(tmpbuff),xmldb_nameFilename);
                for(int k=0; k < REC_MAX_TPROFILES && strlen(recs[REC_IDX(video, i)]->transcoding_profiles[k]) > 0; k++) {
                    _writef(fd, "    <%s>%s</%s>\n",xmldb_nameTProfile, recs[REC_IDX(video, i)]->transcoding_profiles[k],
                            xmldb_nameTProfile);
                }
                _writef(fd, "  </%s>\n",xmldb_nameRecording);
            } else {
                // Since we only store the master recurrence and not
                // the full expanded list of recurrences we must check
                // if we have already saved this recurrent recording.
                // We store all the already saved recordings in
                // the array "saved_recrec"
                j = 0;
                while (j < nsaved_recrec && (saved_recrec[j] != recs[REC_IDX(video, i)]->recurrence_id)) {
                    j++;
                }
                if (j == nsaved_recrec) {
                    // Not yet saved

                    // Start by finding the lowest start number in the sequence, this will be th start
                    // number that we save in the master record
                    int min_start_number = recs[REC_IDX(video, i)]->recurrence_start_number;
                    for(int k=i+1; k <  num_entries[video]; k++) {
                        if( recs[REC_IDX(video, k)]->recurrence &&
                            recs[REC_IDX(video, k)]->recurrence_id == recs[REC_IDX(video, i)]->recurrence_id ) {
                            min_start_number = MIN(recs[REC_IDX(video, k)]->recurrence_start_number,min_start_number);
                        }
                    }
                    saved_recrec[nsaved_recrec] = recs[REC_IDX(video, i)]->recurrence_id;
                    nsaved_recrec++;

                    _writef(fd, "  <%s>\n",xmldb_nameRecording);
                    _writef(fd, "    <%s>%s</%s>\n",xmldb_nameTitle, recs[REC_IDX(video, i)]->recurrence_title, xmldb_nameTitle);
                    _writef(fd, "    <%s>%s</%s>\n",xmldb_nameChannel, recs[REC_IDX(video, i)]->channel, xmldb_nameChannel);
                    _writef(fd, "    <%s>%d</%s>\n", xmldb_nameVideo,video,xmldb_nameVideo);

                    fromtimestamp(recs[REC_IDX(video, i)]->ts_start, &y, &m, &d, &h, &min, &sec);
                    _writef(fd, "    <%s>%02d-%02d-%02d</%s>\n",xmldb_nameStartdate, y, m, d,xmldb_nameStartdate);
                    _writef(fd, "    <%s>%02d:%02d:%02d</%s>\n",xmldb_nameStarttime, h, min, sec,xmldb_nameStarttime);
                    fromtimestamp(recs[REC_IDX(video, i)]->ts_end, &y, &m, &d, &h, &min, &sec);
                    _writef(fd, "    <%s>%02d-%02d-%02d</%s>\n",xmldb_nameEnddate, y, m, d,xmldb_nameEnddate);
                    _writef(fd, "    <%s>%02d:%02d:%02d</%s>\n",xmldb_nameEndtime, h, min, sec,xmldb_nameEndtime);

                    strncpy(tmpbuff, recs[REC_IDX(video, i)]->recurrence_filename, 255);
                    tmpbuff[255] = 0;
                    _writef(fd, "    <%s>%s</%s>\n",xmldb_nameFilename, basename(tmpbuff),xmldb_nameFilename);
                    // FIXME: Profile
                    _writef(fd, "    <%s>%s</%s>\n",xmldb_nameTProfile, recs[REC_IDX(video, i)]->transcoding_profiles[0], xmldb_nameTProfile);
                    _writef(fd, "    <%s>\n",xmldb_nameRecurrence);
                    _writef(fd, "      <%s>%d</%s>\n",xmldb_nameRecType, recs[REC_IDX(video, i)]->recurrence_type, xmldb_nameRecType);
                    _writef(fd, "      <%s>%d</%s>\n",xmldb_nameRecNbr, recs[REC_IDX(video, i)]->recurrence_num, xmldb_nameRecNbr);
                    _writef(fd, "      <%s prefix=\"%s\">%d</%s>\n",xmldb_nameRecMangling,
                            recs[REC_IDX(video, i)]->recurrence_mangling_prefix,
                            recs[REC_IDX(video, i)]->recurrence_mangling,
                            xmldb_nameRecMangling);
                    _writef(fd, "      <%s>%d</%s>\n",xmldb_nameRecStartNumber,min_start_number,xmldb_nameRecStartNumber);
                    _writef(fd, "    </%s>\n",xmldb_nameRecurrence);
                    _writef(fd, "  </%s>\n",xmldb_nameRecording);
                }
            }
        }
    }
    return _writef(fd, "</%s>\n",xmldb_root);
}

/*
 * writeXMLFile
 * Dump internal memory structure as an XML file.
 */
int
writeXMLFile(const char *filename) {
    int fd;

    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        logmsg(LOG_ERR, "Can't open '%s' XML data file for writing. (%d : %s) ",
                filename,errno,strerror(errno));
        return -1;
    }

    int ret = _writeXMLFile(fd);
    close(fd);
    if( -1 == ret ) {
        logmsg(LOG_ERR,"Failed to write XML data file '%s'. (%d : %s)",filename,errno,strerror(errno));
        return -1;
    }
    return 0;
}
