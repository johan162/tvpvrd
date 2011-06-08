/* =========================================================================
 * File:        TVHISTORY.C
 * Description: Module to handle history recording of made recordings
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2011 Johan Persson
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
#include <sys/wait.h>
#include <math.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h> // Needed to get MIN()/MAX()
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

// XML2 lib headers
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>
#include <libxml2/libxml/xmlreader.h>

#include "tvhistory.h"
#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "xstr.h"
#include "tvplog.h"

/**
 * Record for array of history records
 */
struct histrec {
    char *title;
    char *filepath;
    time_t ts_start;
    time_t ts_end;
};
static struct histrec history[HISTORY_LENGTH];
static size_t nrecs = 0;

/*
Example history file
 * 
 <tvrechistory version="1">
 <recording>
 <title></title>
 <start_ts></start_ts>
 <end_ts></end_ts>
 <filepath></filepath>
 </recording>
 </tvrechistory>
 
 */

#define XMLHISTDB_VERSIONNUM "1"

static const xmlChar *xmldb_version = (xmlChar *) XMLHISTDB_VERSIONNUM;
static const xmlChar *xmldb_nameText = (xmlChar *) "text";
static const xmlChar *xmldb_nameVersion = (xmlChar *) "version";
static const xmlChar *xmldb_root = (xmlChar *) "tvrechistory";
static const xmlChar *xmldb_nameRecording = (xmlChar *) "recording";
static const xmlChar *xmldb_nameTitle = (xmlChar *) "title";
static const xmlChar *xmldb_nameStart = (xmlChar *) "start_ts";
static const xmlChar *xmldb_nameEnd = (xmlChar *) "end_ts";
static const xmlChar *xmldb_nameFilepath = (xmlChar *) "filepath";

static void
processRecord(xmlNodePtr node) {
    xmlNodePtr childnode;
    nrecs = 0;

    while (node != NULL && nrecs < HISTORY_LENGTH) {
        // Ignore text in between proper child tags
        if (xmlStrcmp(node->name, xmldb_nameText)) {
            childnode = node->xmlChildrenNode;
            if (xmlStrcmp(node->name, xmldb_nameStart) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].ts_start = xatol((char *) childnode->content);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameEnd) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].ts_end = xatol((char *) childnode->content);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameTitle) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].title = strdup((char *) childnode->content);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameFilepath) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].filepath = strdup((char *) childnode->content);
                }
            } else {
                logmsg(LOG_ERR, "Unknown XML node name in history DB: %s", node->name);
            }
            ++nrecs;
        }
        node = node->next;
    }
}

/**
 * Read old history file from file
 * @return 
 */
static int
tvhist_read(void) {
    char xmlhistfile[256];

    snprintf(xmlhistfile, sizeof (xmlhistfile) - 1, "%s/xmldb/%s", datadir, HISTORYDB_FILENAME);
    xmlhistfile[sizeof (xmlhistfile) - 1] = '\0';

    xmlNodePtr node;
    xmlDocPtr doc;
    xmlChar *xmlver;

    /*
     * this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION

    // Parse the XML file
    doc = xmlParseFile(xmlhistfile);
    if (doc == NULL) {
        logmsg(LOG_ERR, "Unable to open XML history file: '%s' ( %d : %s )", xmlhistfile, errno, strerror(errno));
        return -1;
    }

    node = xmlDocGetRootElement(doc);

    if (xmlStrcmp(node->name, xmldb_root)) {
        logmsg(LOG_ERR, "XML file is not a proper history database file. Wrong root element. Found '%s' when expecting '%s'",
                node->name, xmldb_root);
        xmlFreeDoc(doc);
        return -1;
    }

    // Check that the version of the file is the expected
    xmlver = xmlGetProp(node, xmldb_nameVersion);
    if (xmlStrcmp(xmlver, xmldb_version)) {
        logmsg(LOG_NOTICE, "Expected XML history DB version '%s' but found version '%s'.", xmldb_version, xmlver);
        if (xatoi((char *) xmlver) > xatoi((char *) xmldb_version)) {
            logmsg(LOG_NOTICE, "Can not handle a newer history DB version. Please upgrade daaemon.");
            xmlFreeDoc(doc);
            return -1;
        }
    }
    xmlFree(xmlver);

    node = node->xmlChildrenNode;
    while (node != NULL) {
        if (xmlStrcmp(node->name, xmldb_nameRecording) == 0) {
            processRecord(node);
        }
        node = node->next;
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return 0;
}

/**
 * Dump data structure to file
 * @return 
 */
static int
tvhist_write(void) {
    char xmlhistfile[256];
    int fd;

    snprintf(xmlhistfile, sizeof (xmlhistfile) - 1, "%s/xmldb/%s", datadir, HISTORYDB_FILENAME);
    xmlhistfile[sizeof (xmlhistfile) - 1] = '\0';
    fd = open(xmlhistfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        logmsg(LOG_ERR, "Can't open '%s' XML history file for writing. (%d : %s) ",
                xmlhistfile, errno, strerror(errno));
        return -1;
    }

    time_t now = time(NULL);

    // Test if the first write was ok then we assume that the rest will
    // be ok as well. Not perfect but at least this way we will catch
    // wrong permissions

    if (-1 == _writef(fd, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n")) {
        return -1;
    }
    _writef(fd, "<!-- Created: %s -->\n", ctime(&now));
    _writef(fd, "<%s %s=\"%s\">\n", xmldb_root, xmldb_nameVersion, HISTORYDB_FILENAME);

    for (size_t i = 0; i < nrecs; ++i) {
        _writef(fd, "  <%s>\n", xmldb_nameRecording);
        _writef(fd, "    <%s>%s</%s>\n", xmldb_nameTitle, history[i].title, xmldb_nameTitle);
        _writef(fd, "    <%s>%l</%s>\n", xmldb_nameStart, history[i].ts_start, xmldb_nameStart);
        _writef(fd, "    <%s>%l</%s>\n", xmldb_nameEnd, history[i].ts_end, xmldb_nameEnd);
        _writef(fd, "    <%s>%s</%s>\n", xmldb_nameFilepath, history[i].filepath, xmldb_nameFilepath);
        _writef(fd, "  </%s>\n", xmldb_nameRecording);
    }
    _writef(fd, "</%s>\n", xmldb_root);
    close(fd);
    return 0;

}

static void
tvhist_free(void) {
    for (size_t idx = 0; idx < nrecs; ++idx) {
        if (history[idx].filepath) {
            free(history[idx].filepath);
            history[idx].filepath = (char *) NULL;
        }
        if (history[idx].title) {
            free(history[idx].title);
            history[idx].title = (char *) NULL;
        }
    }
    nrecs = 0;
}

void
hist_init(void) {
    tvhist_free();
    if (tvhist_read()) {
        logmsg(LOG_ERR, "Failed to read old history file. Will create an empty new history file.");
        tvhist_write();
    }
}

int
hist_update(char *title, const time_t ts_start, const time_t ts_end,char *fullPathFilename) {

    // Shift all records down one slot and loose the last record
    if (nrecs == HISTORY_LENGTH) {
        if (history[HISTORY_LENGTH - 1].filepath) {
            free(history[HISTORY_LENGTH - 1].filepath);
            history[HISTORY_LENGTH - 1].filepath = (char *) NULL;
        }
        if (history[HISTORY_LENGTH - 1].title) {
            free(history[HISTORY_LENGTH - 1].title);
            history[HISTORY_LENGTH - 1].title = (char *) NULL;
        }

    }
    for (size_t i = HISTORY_LENGTH - 1; i > 1; i--) {
        memcpy(&history[i - 1], &history[i], sizeof (struct histrec));
    }

    history[0].title = strdup(title);
    history[0].filepath = strdup(fullPathFilename);
    history[0].ts_start = ts_start;
    history[0].ts_end = ts_end;

    if( nrecs < HISTORY_LENGTH )
        nrecs++;
    
    tvhist_write();
    
    return 0;

}