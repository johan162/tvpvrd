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
#include <libgen.h> // Needed for dirname()

// XML2 lib headers
#include <libxml2/libxml/parser.h>
#include <libxml2/libxml/tree.h>
#include <libxml2/libxml/xmlreader.h>

#include "tvconfig.h"
#include "tvhistory.h"
#include "tvpvrd.h"
#include "utils.h"
#include "xstr.h"
#include "tvplog.h"
#include "datetimeutil.h"
#include "mailutil.h"
#include "listhtml.h"

/**
 * Record for array of history records
 */
struct histrec {
    char *title;
    char *filepath;
    time_t ts_start;
    time_t ts_end;
    char *profile;
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

#define XMLHISTDB_VERSIONNUM "2"

static const xmlChar *xmldb_version = (xmlChar *) XMLHISTDB_VERSIONNUM;
static const xmlChar *xmldb_nameText = (xmlChar *) "text";
static const xmlChar *xmldb_nameVersion = (xmlChar *) "version";
static const xmlChar *xmldb_nameDir = (xmlChar *) "dir";
static const xmlChar *xmldb_root = (xmlChar *) "tvpvrdhistory";
static const xmlChar *xmldb_nameRecording = (xmlChar *) "recording";
static const xmlChar *xmldb_nameTitle = (xmlChar *) "title";
static const xmlChar *xmldb_nameStart = (xmlChar *) "timestampstart";
static const xmlChar *xmldb_nameEnd = (xmlChar *) "timestampend";
static const xmlChar *xmldb_nameStartDate = (xmlChar *) "datestart";
static const xmlChar *xmldb_nameEndDate = (xmlChar *) "dateend";
static const xmlChar *xmldb_nameStartTime = (xmlChar *) "timestart";
static const xmlChar *xmldb_nameEndTime = (xmlChar *) "timeend";
static const xmlChar *xmldb_nameFilepath = (xmlChar *) "filepath";
static const xmlChar *xmldb_nameProfile = (xmlChar *) "profile";

static void
processRecord(xmlNodePtr node) {
    xmlNodePtr childnode;
    xmlChar *xmldir;
    node = node->xmlChildrenNode;
    char fnamebuff[255];
    
    /* Note: We only read the timestamp for the start and end since the
     * human readable form can be inferred from the timestamps
     */
    while (node != NULL ) {
        // Ignore text in between proper child tags
        if ( xmlStrcmp(node->name, xmldb_nameText) ) {
            childnode = node->xmlChildrenNode;
            if (xmlStrcmp(node->name, xmldb_nameStart) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].ts_start = xatol((char *) childnode->content);
                } else {
                    logmsg(LOG_NOTICE, "Corrupted history file at node: %s", node->name);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameEnd) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].ts_end = xatol((char *) childnode->content);
                } else {
                    logmsg(LOG_NOTICE, "Corrupted history file at node: %s", node->name);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameTitle) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].title = strdup((char *) childnode->content);
                } else {
                    logmsg(LOG_NOTICE, "Corrupted history file at node: %s", node->name);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameStartDate) == 0 ||
                       xmlStrcmp(node->name, xmldb_nameEndDate) == 0 ||
                       xmlStrcmp(node->name, xmldb_nameStartTime) == 0 ||
                       xmlStrcmp(node->name, xmldb_nameEndTime) == 0 ) {
                
                // Do nothing, we only read the timestamp
                
            } else if (xmlStrcmp(node->name, xmldb_nameFilepath) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    *fnamebuff = '\0';
                    xmldir = xmlGetProp(node, xmldb_nameDir);
                    if( xmldir ) {
                        strncpy(fnamebuff,(char*)xmldir,sizeof(fnamebuff));
                        strncat(fnamebuff,"/",sizeof(fnamebuff)-strnlen(fnamebuff,sizeof(fnamebuff)));
                        strncat(fnamebuff,(char *)childnode->content,sizeof(fnamebuff)-strnlen(fnamebuff,sizeof(fnamebuff)));                    
                        xmlFree(xmldir);
                    } else {
                        // version=1 old history style file
                        strncpy(fnamebuff,(char *)childnode->content,sizeof(fnamebuff));
                    }
                    history[nrecs].filepath = strdup(fnamebuff);
                } else {
                    logmsg(LOG_NOTICE, "Corrupted history file at node: %s", node->name);
                }
            } else if (xmlStrcmp(node->name, xmldb_nameProfile) == 0) {
                if (childnode && xmlStrcmp(childnode->name, xmldb_nameText) == 0) {
                    history[nrecs].profile = strdup((char *) childnode->content);
                } else {
                    logmsg(LOG_NOTICE, "Corrupted history file at node: %s", node->name);
                }                
            } else {
                logmsg(LOG_NOTICE, "Unknown XML node name in history file: %s", node->name);
            }
        }
        node = node->next;
    }
}

/**
 * Read old history file from file
 * @return 0 on success , -1 on failure
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
        logmsg(LOG_NOTICE, "Unable to read XML history file: '%s' ( %d : %s )", xmlhistfile, errno, strerror(errno));
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
        logmsg(LOG_NOTICE, "Expected XML history DB version '%s' but found version '%s'. Will be converted to new version on save.", xmldb_version, xmlver);
        if (xatoi((char *) xmlver) > xatoi((char *) xmldb_version)) {
            logmsg(LOG_NOTICE, "Can not handle a newer history DB version. Please upgrade daemon.");
            xmlFreeDoc(doc);
            return -1;
        }
    }
    xmlFree(xmlver);
    
    nrecs=0;
    node = node->xmlChildrenNode;
    while (node != NULL) {
        if (xmlStrcmp(node->name, xmldb_nameRecording) == 0) {
            processRecord(node);
            ++nrecs;
            logmsg(LOG_DEBUG,"Read history title: %s",history[nrecs-1].title);
        }
        node = node->next;
    }
       
    
    xmlFreeDoc(doc);
    xmlCleanupParser();

    return 0;
}

/**
 * Write history list to file
 * @return 0 on success , -1 on failure
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

    if (-1 == _writef(fd, 
        "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
        "<?xml-stylesheet type=\"text/xsl\" href=\"%s/tvpvrd/xsl/%s\"?>\n",CONFDIR,HISTORY_XSL_FILENAME )) {
        return -1;
    }
    _writef(fd, "<!-- Created: %s -->\n", ctime(&now));
    _writef(fd, "<%s %s=\"%s\">\n", xmldb_root, xmldb_nameVersion, xmldb_version);
    
    // Convert timestamps to more human readable form
    int syear, smonth, sday, shour, smin, ssec,
        eyear, emonth, eday, ehour, emin, esec;
    

    char dirbuff[255];
    for (size_t i = 0; i < nrecs; ++i) {
        fromtimestamp(history[i].ts_start, &syear, &smonth, &sday, &shour, &smin, &ssec);
        fromtimestamp(history[i].ts_end, &eyear, &emonth, &eday, &ehour, &emin, &esec);
        _writef(fd, "  <%s>\n", xmldb_nameRecording);
        _writef(fd, "    <%s>%s</%s>\n", xmldb_nameTitle, history[i].title, xmldb_nameTitle);
        _writef(fd, "    <%s>%lld</%s>\n", xmldb_nameStart, (long long int)history[i].ts_start, xmldb_nameStart);
        _writef(fd, "    <%s>%lld</%s>\n", xmldb_nameEnd, (long long int)history[i].ts_end, xmldb_nameEnd);
        _writef(fd, "    <%s>%04d-%02d-%02d</%s>\n", xmldb_nameStartDate, syear, smonth, sday, xmldb_nameStartDate);
        _writef(fd, "    <%s>%02d:%02d</%s>\n", xmldb_nameStartTime, shour, smin, xmldb_nameStartTime);
        _writef(fd, "    <%s>%04d-%02d-%02d</%s>\n", xmldb_nameEndDate, eyear, emonth, eday, xmldb_nameEndDate);
        _writef(fd, "    <%s>%02d:%02d</%s>\n", xmldb_nameEndTime, ehour, emin, xmldb_nameEndTime);        
        
        // We must make a copy since we are not safe in assuming dirname() will not modify the buffer
        strncpy(dirbuff,history[i].filepath,sizeof(dirbuff));
        _writef(fd, "    <%s dir=\"%s\">%s</%s>\n", xmldb_nameFilepath, dirname(dirbuff), basename(history[i].filepath), xmldb_nameFilepath);
        
        _writef(fd, "    <%s>%s</%s>\n", xmldb_nameProfile, history[i].profile, xmldb_nameProfile);
        _writef(fd, "  </%s>\n", xmldb_nameRecording);
    }
    _writef(fd, "</%s>\n", xmldb_root);
    close(fd);
    return 0;

}

/**
 * Free entire history vector
 */
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
        if (history[idx].profile) {
            free(history[idx].profile);
            history[idx].profile = (char *) NULL;
        }        
    }
    nrecs = 0;
}

/**
 * Initialize history handling. The history is simply stored in a vector of history entries
 */
void
hist_init(void) {
    logmsg(LOG_DEBUG,"Calling hist_init()");
    tvhist_free();
    if (tvhist_read()) {
        logmsg(LOG_NOTICE, "Failed to read old history file. Will create an empty new history file.");
        tvhist_write();
    } else {
        logmsg(LOG_DEBUG, "Read history XML file.");
    }
}

/**
 * Add a recording to the history list
 * @param title
 * @param ts_start
 * @param ts_end
 * @param fullPathFilename
 * @param profile
 * @return 
 */
int
hist_addrec(char *title, const time_t ts_start, const time_t ts_end, char *fullPathFilename, char *profile) {

    logmsg(LOG_DEBUG,"Adding history for: title=%s",title);
    
    // Shift all records down one slot and free the last record
    if (nrecs == HISTORY_LENGTH) {
        if (history[HISTORY_LENGTH - 1].filepath) {
            free(history[HISTORY_LENGTH - 1].filepath);
            history[HISTORY_LENGTH - 1].filepath = (char *) NULL;
        }
        if (history[HISTORY_LENGTH - 1].title) {
            free(history[HISTORY_LENGTH - 1].title);
            history[HISTORY_LENGTH - 1].title = (char *) NULL;
        }
        if (history[HISTORY_LENGTH - 1].profile) {
            free(history[HISTORY_LENGTH - 1].profile);
            history[HISTORY_LENGTH - 1].profile = (char *) NULL;
        }        

    }
        
    for (size_t i = HISTORY_LENGTH - 1; i > 0; i--) {
        memcpy(&history[i], &history[i - 1], sizeof (struct histrec));
    }

    history[0].title = strdup(title);
    history[0].profile = strdup(profile);
    history[0].filepath = strdup(fullPathFilename);
    history[0].ts_start = ts_start;
    history[0].ts_end = ts_end;

    if( nrecs < HISTORY_LENGTH )
        nrecs++;
    
    if( 0 == tvhist_write() ) {
        logmsg(LOG_DEBUG, "Successfully updated history XML file. Size after update nrecs=%d",nrecs);
    } else {
        logmsg(LOG_ERR, "Could NOT write history XML file. Permission problems?");
    }
    
    return 0;

}

/**
 * Put a formatted version of the history list in the supplied buffer
 * @param buff Buffer to store history list in
 * @param maxlen Maximum length of buffer
 * @return 0 on success, -1 on failure
 */
int
hist_listbuff(char *buff, size_t maxlen) {
    struct tm result;
    int sy, sm, sd, sh, smi, ss;
    int ey, em, ed, eh, emi, es;    
    static char wday_name[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static char month_name[12][4] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    char line[256];
    *buff = '\0';
    
    if( 0 == nrecs ) {
        snprintf(buff,maxlen,"(no history)\n");
        return 0;
    }

    char filename[255];
    for (size_t i = 0; i < nrecs && maxlen > 0 ; ++i) {
        fromtimestamp(history[i].ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
        fromtimestamp(history[i].ts_end, &ey, &em, &ed, &eh, &emi, &es);        
        (void)localtime_r(&history[i].ts_start, &result);        

        strncpy(filename,history[i].filepath,sizeof(filename));
        filename[sizeof(filename)-1]='\0';
        snprintf(line,sizeof(line),
         "%02u "
         "%s %s %02d %02d:%02d "
         "%-43s"
         "%-40s"
         "%-10s\n",
         (int)(i+1),
         wday_name[result.tm_wday], month_name[sm-1], sd,
         sh, smi,
         history[i].title,
         basename(filename),
         history[i].profile);
        if( strnlen(line,256) > maxlen ) {
            return -1;
        }
        strncat(buff,line,maxlen);
        maxlen -= strnlen(line,256);
        *line='\0';
    }    
    if( maxlen > 0 )
        return 0;
    else
        return -1;
}

static void
_html_header(char *buffer, size_t maxlen, struct css_record_style *rs) {
    snprintf(buffer, maxlen,
            "<tr style=\"%s\">"
            "<th style=\"%s\">#</th>\n"
            "<th style=\"%s\">Date</th>\n"
            "<th style=\"%s\">Time</th>\n"
            "<th style=\"%s\">Title</th>\n"
            "<th style=\"%s\">File</th>\n"
            "</tr>\n",
            rs->tr,
            rs->td_l,
            rs->td_i,
            rs->td_i,
            rs->td_i,
            rs->td_r);
}

static void
_html_row(char *buffer, size_t maxlen, struct css_record_style *rs, struct histrec *hr, size_t idx) {
    struct tm result;
    int sy, sm, sd, sh, smi, ss;
    int ey, em, ed, eh, emi, es;
    static char wday_name[7][4] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static char month_name[12][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    fromtimestamp(hr->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
    fromtimestamp(hr->ts_end, &ey, &em, &ed, &eh, &emi, &es);
    (void) localtime_r(&hr->ts_start, &result);

    snprintf(buffer, maxlen,
            "<tr style=\"%s\">"
            "<td style=\"%s\">%02zu</td>\n"
            "<td style=\"%s\">%s %s %d</td>\n"
            "<td style=\"%s\">%02d:%02d - %02d:%02d</td>\n"
            "<td style=\"%s\"><strong>%s</strong></td>\n"
            "<td style=\"%s\">%s</td>\n"
            "</tr>\n",
            rs->tr,
            rs->td_l, idx,
            rs->td_i, wday_name[result.tm_wday], month_name[sm - 1], sd,
            rs->td_i, sh, smi, eh, emi,
            rs->td_i, hr->title,
            rs->td_r, basename(hr->filepath));
}

int
hist_listhtmlbuff(char *buffer, size_t maxlen, size_t style) {

    char tmpbuffer[1024];
    struct css_table_style ts;
    bzero(&ts, sizeof (struct css_table_style));
    set_listhtmlcss(&ts, style);
    int max = maxlen;
    time_t ts_tmp = time(NULL);

    snprintf(buffer, max,
            "<div style=\"%s\">Generated by: <strong>%s %s</strong>, %s</div>"
            "<table border=0 style=\"%s\" cellpadding=4 cellspacing=0>\n",
            ts.date, server_program_name, server_version, ctime(&ts_tmp), ts.table);
    max -= strlen(buffer);

    _html_header(tmpbuffer, sizeof (tmpbuffer), &ts.header_row);
    strncat(buffer, tmpbuffer, max - 1);
    max -= strlen(tmpbuffer);

    for (size_t i = 0; i < nrecs - 1 && max > 0; ++i) {
        *tmpbuffer = '\0';
        if (i % 2) {
            _html_row(tmpbuffer, max, &ts.odd_row, &history[i], i);
        } else {
            _html_row(tmpbuffer, max, &ts.even_row, &history[i], i);
        }
        strncat(buffer, tmpbuffer, max);
        max -= strnlen(tmpbuffer, sizeof (tmpbuffer));
    }

    // Now print the last row. This can either be a normal row with the formatting
    // for a last row or it could be the string "(No recordings)" to indicate an empty list

    if (0 == nrecs) {
        snprintf(tmpbuffer, sizeof (tmpbuffer),
                "<tr><td style=\"%s\">&nbsp;</td><td style=\"%s font-style:italic;text-align:center;\" colspan=3>(No history)</td><td style=\"%s\">&nbsp;</td></tr>\n",
                ts.last_even_row.td_l, ts.last_even_row.td_i, ts.last_even_row.td_r);
    } else {
        if (nrecs % 2) {
            _html_row(tmpbuffer, max, &ts.last_even_row, &history[nrecs - 1], nrecs);
        } else {
            _html_row(tmpbuffer, max, &ts.last_odd_row, &history[nrecs - 1], nrecs);
        }
    }
    
    strncat(buffer, tmpbuffer, max);
    max -= strnlen(tmpbuffer, sizeof (tmpbuffer));
    
    if (max > 0)
        return 0;
    else
        return -1;

}

int
hist_list(int fd) {
    size_t const maxlen=HISTORY_LENGTH*1024;
    
    char *buff = calloc(maxlen, sizeof(char));
    if( NULL == buff ) {
        return -1;
    }
    if( -1 == hist_listbuff(buff, maxlen) ) {
        return -1;
    }
    _writef(fd, buff);
    free(buff);
    return 0;
}

int
hist_mail(void) {    

    size_t const maxlen=HISTORY_LENGTH*1024;

    char *buffer_plain = calloc(maxlen, sizeof(char));
    char *buffer_html = calloc(maxlen, sizeof(char));

    if( buffer_plain == NULL || buffer_html == NULL ) {
        logmsg(LOG_ERR,"Out of memory when trying to allocate buffer! CRITICAL!");
        exit(1);
    }

    if( -1 == hist_listbuff(buffer_plain, maxlen-3) ) {
        free(buffer_plain);
        free(buffer_html);
        return -1;
    }

    hist_listhtmlbuff(buffer_html, maxlen, 0);
    strcat(buffer_plain,"\n\n");
 
    // snprintf(buffer_html,maxlen-1,"<div>Previous recordings:</div><pre>%s</pre>\n\n",buffer_plain);

    char subject[255];
    snprintf(subject,sizeof(subject),"Recording history");

    int ret = sendmail_helper(subject, buffer_plain, buffer_html);

    free(buffer_html);
    free(buffer_plain);    

    return ret;
    
}

/* EOF */
