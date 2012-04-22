/* =========================================================================
 * File:        TVWEBCMD.C
 * Description: Handle the interface via a Web-browser
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

// Standard UNIX includes
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <locale.h>
#include <syslog.h>
#include <errno.h>
#include <sys/param.h>
#include <time.h>
#include <inttypes.h>
#include <ctype.h>

#include "config.h"
#include <pcre.h>

#include "tvpvrd.h"
#include "tvconfig.h"
#include "utils.h"
#include "tvwebui.h"
#include "tvcmd.h"
#include "freqmap.h"
#include "stats.h"
#include "transc.h"
#include "transcprofile.h"
#include "recs.h"
#include "tvhtml.h"
#include "datetimeutil.h"
#include "tvplog.h"
#include "vctrl.h"
#include "xstr.h"


static const char *min_list_start[] = {
    "00", "05", "10", "15", "20", "25", "30",
    "35", "40", "45", "50", "55"
};
static const char *min_list_end[] = {
    "00", "04", "09", "14", "19", "24", "29", "30",
    "39", "44", "49", "54", "59"
};
static const char *hour_list[] = {
    "17", "18", "19", "20", "21", "22", "23",
    "00", "01", "02", "03", "04", "05", "06",
    "07", "08", "09", "10", "11", "12","13", "14", "15", "16"
};
static const char *hourlength_list[] = {
    "0", "1", "2", "3"
};

void
_web_cmd_module_start(int sockd,char *legend) {
    _writef(sockd, "<div class=\"cmdmodule\">\n");
    _writef(sockd, "<fieldset><legend>%s</legend>\n",legend);
}

void
_web_cmd_module_end(int sockd) {
    _writef(sockd, "</fieldset>\n");    
    _writef(sockd, "</div> <!-- cmdmodule -->\n");
}

/**
 * Display the next recording area
 * @param sockd
 */
void
web_cmd_next(int sockd) {
    _web_cmd_module_start(sockd,"Next recording");
    _writef(sockd, "<div class=\"displayasled_on nofloat\" id=\"nextrec\">\n");
    list_recs(1,4,sockd); // Use style==4 , fancy
    _writef(sockd, "</div> <!-- displayasled -->\n");
    _web_cmd_module_end(sockd);
}

/**
 * Display the ongoing transaction area
 * @param sockd
 */
void
web_cmd_ongoingtransc(int sockd) {

    size_t num=get_num_ongoing_transcodings();

    if( 0 == num && web_autodisplay_transc )
        return;
    
    _web_cmd_module_start(sockd,"Ongoing transcoding");
    if( 0==num ) {
        
        _writef(sockd, "<div class=\"ongoing_transc_entry fullw\">\n");
        _writef(sockd, "<div class=\"displayasled_off\"><pre> - - -</pre></div>\n");
        _writef(sockd, "<div class=\"ongoing_transc_stop_disabled\">Stop</div>\n");
        _writef(sockd, "</div> <!-- ongoing_transc_entry -->\n");
        
    } else {
        int active_transc=0;
        for (size_t i = 0; i < max_ongoing_transcoding; i++) {
            if (ongoing_transcodings[i]) {                
                if (0 == active_transc % 2) {
                    _writef(sockd, "<div class=\"ongoing_transc_entry%s\">\n", num > 1 ? " halfw" : " fullw");
                } else {
                    _writef(sockd, "<div class=\"ongoing_transc_entry%s margleft\">\n", num > 1 ? " halfw" : " fullw");
                }
            
                time_t now = time(NULL);
                int rtime = now-ongoing_transcodings[i]->start_ts;
                int rh = rtime/3600;
                int rmin = (rtime - rh*3600)/60;

                _writef(sockd, "<div class=\"displayasled_on\"><pre>(%02d:%02d)\n%s</pre></div>\n",rh,rmin,ongoing_transcodings[i]->filename);
                _writef(sockd, "<div class=\"ongoing_transc_stop\"><a href=\"cmd?c=kt%%20%d\">Stop</a></div>\n",i);
                _writef(sockd, "</div> <!-- ongoing_transc_entry -->\n");
                active_transc++;
            }
            

        }
    }
        
    _web_cmd_module_end(sockd);
}

/**
 * Display the ongoing recordings area
 * @param sockd
 */
void
web_cmd_ongoing(int sockd) {

    _web_cmd_module_start(sockd,"Ongoing recordings");
    
    char buffer[255],caption_buffer[255];
    for (size_t i = 0; i < max_video; i++) {
        if (-1 == video_get_cardinfo(i, FALSE, buffer, sizeof (buffer))) {
            snprintf(caption_buffer, sizeof (caption_buffer), "Card %zu", i + 1);
        } else {
            snprintf(caption_buffer, sizeof (caption_buffer), "%s.", buffer);
        }
        if (0 == i % 2) {
            _writef(sockd, "<div class=\"ongoing_rec_entry%s\">\n", max_video > 1 ? " halfw" : " fullw");
        } else {
            _writef(sockd, "<div class=\"ongoing_rec_entry%s margleft\">\n", max_video > 1 ? " halfw" : " fullw");
        }
        if (ongoing_recs[i]) {
            int ey, em, ed, eh, emi, es;
            fromtimestamp(ongoing_recs[i]->ts_end, &ey, &em, &ed, &eh, &emi, &es);
            int sy, sm, sd, sh, smi, ss;
            fromtimestamp(ongoing_recs[i]->ts_start, &sy, &sm, &sd, &sh, &smi, &ss);
            _writef(sockd, "<div class=\"displayasled_on\"><pre>%s %02d:%02d-%02d:%02d\n%s</pre></div>\n", ongoing_recs[i]->channel, sh, smi, eh, emi, ongoing_recs[i]->title);
        } else {
            _writef(sockd, "<div class=\"displayasled_off\"><pre> - - -</pre></div>\n");
        }
        _writef(sockd, "<div class=\"caption\">\n");
        _writef(sockd, caption_buffer);
        _writef(sockd, "</div>\n");
        if (ongoing_recs[i]) {
            _writef(sockd, "<div class=\"ongoing_rec_stop\"><a href=\"killrec?rid=%d\">Stop</a></div>\n", i);
        } else {
            _writef(sockd, "<div class=\"ongoing_rec_stop_disabled\">Stop</div>\n");
        }

        _writef(sockd, "</div> <!-- ongoing rec entry -->\n");

    }

    _web_cmd_module_end(sockd);

}

/**
 * Display the quick add area
 * @param sockd
 */
void
web_cmd_qadd(int sockd) {
    
    if( !web_display_qadd )
        return;
    
    const char *station_list[128];
    const size_t n_stations = get_stations(station_list, 128);

    const char *profile_list[64];
    const size_t n_profile = get_profile_names(profile_list, 64);

    const size_t n_hourlength = sizeof (hourlength_list) / sizeof (char *);
    const size_t n_min_end = sizeof (min_list_end) / sizeof (char *);

    _web_cmd_module_start(sockd,"Quick recording");

    /*
     * Add quick recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"addqrec\">\n", "id_qadd_form");

    html_element_select(sockd, "Profile:", "profile", default_transcoding_profile, profile_list, n_profile, "id_qprofile");
    html_element_select(sockd, "Station:", "channel", NULL, station_list, n_stations, "id_qstation");

    html_element_select(sockd, "Len:", "length_hour", "00", hourlength_list, n_hourlength, "id_length_hour");
    html_element_select(sockd, "&nbsp;", "length_min", "45", min_list_end, n_min_end, "id_length_min");

    html_element_input_text(sockd, "Title:", "title", "id_qtitle");
    html_element_submit(sockd, "submit_qaddrec", "Start", "id_qaddrec");

    _writef(sockd, "</form>\n");

    _web_cmd_module_end(sockd);

}

/**
 * Display the add and delete recording area
 * @param sockd
 */
void
web_cmd_add(int sockd) {
    static const struct skeysval_t  day_list[] = {
        {"","Auto"},
        {"Mon","Mon"},
        {"Tue","Tue"},
        {"Wed","Wed"},
        {"Thu","Thu"},
        {"Fri","Fri"},
        {"Sat","Sat"},
        {"Sun","Sun"} 
    };

    const size_t n_day = 8;
    static const struct skeysval_t rpt_list[] = {
        {.key = "", .val = "(none)"},
        {.key = "w", .val = "Weekly"},
        {.key = "d", .val = "Daily"},
        {.key = "f", .val = "Mon-Fri"},
        {.key = "t", .val = "Mon-Thu"},
        {.key = "n", .val = "Tue-Fri"},
        {.key = "s", .val = "Sat-Sun"}
    };
    const size_t n_rpt = 7;
    static const char *rptcount_list[] = {
        " ", "02", "03", "04", "05", "06", "07", "08", "09", "10",
        "11", "12", "13", "14", "15", "16", "17", "18", "19",
        "20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
        "30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
        "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "50"
    };
    const size_t n_rptcount = 39;
    const char *station_list[128];
    const size_t n_stations = get_stations(station_list, 128);

    const char *profile_list[64];
    const size_t n_profile = get_profile_names(profile_list, 64);

    const size_t n_hour = sizeof (hour_list) / sizeof (char *);
    const size_t n_min_start = sizeof (min_list_start) / sizeof (char *);
    const size_t n_min_end = sizeof (min_list_end) / sizeof (char *);

    _web_cmd_module_start(sockd,"New recording");

    /*
     * Add recordings
     */
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"addrec\">\n", "addrecording");

    html_element_select(sockd, "Profile:", "profile", default_transcoding_profile, profile_list, n_profile, "id_profile");
    html_element_select(sockd, "Station:", "channel", NULL, station_list, n_stations, "id_station");
    html_element_select_code(sockd, "Repeat:", "repeat", NULL, rpt_list, n_rpt,"id_rpttype");
    html_element_select(sockd, "Count:", "repeatcount", NULL, rptcount_list, n_rptcount, "id_rptcount");
    
    html_element_select_code(sockd, "Day:", "start_day", NULL, day_list, n_day, "id_start");
    
    html_element_select(sockd, "Start:", "start_hour", "18", hour_list, n_hour, "id_starthour");
    html_element_select(sockd, "&nbsp;", "start_min", NULL, min_list_start, n_min_start, NULL);
    
/*
    _writef(sockd, "<div class=\"input_container\" id=\"be_hyphen\"><span class=\"be_hyphen\"> &rarr; </span></div>");
*/
    
    html_element_select(sockd, "End:", "end_hour", "18", hour_list, n_hour, "id_endhour");
    html_element_select(sockd, "&nbsp;", "end_min", "59", min_list_end, n_min_end, NULL);

    html_element_input_text(sockd, "Title:", "title", "id_title");
    html_element_submit(sockd, "submit_addrec", "Add", "id_addrec");

    _writef(sockd, "</form>\n");

    // Close container
    _web_cmd_module_end(sockd);
}


/**
 * Display the add and delete recording area
 * @param sockd
 */
void
web_cmd_del(int sockd) {
    static const char *yn_list[] = {
        "Yes", "No"
    };
    const size_t n_ynlist = 2;
    
    /*
     * Delete recordings
     */
    _web_cmd_module_start(sockd,"Delete recording");
    
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"delrec\"  onsubmit=\"return confirm('Really delete?')\">\n", "deleterecording");

    struct skeysval_t *listrec;
    size_t num = list_recskeyval(&listrec, 10); // style==10, simple format with no idx and no profile
    html_element_select_code(sockd, "Title:", "recid", NULL, listrec, num, "id_delselect");
    for (size_t i = 0; i < num; ++i) {
        free(listrec[i].key);
        free(listrec[i].val);
    }
    free(listrec);

    html_element_select(sockd, "Delete serie:", "delserie", "No", yn_list, n_ynlist, "id_seriesyn");
    html_element_submit(sockd, "submit_delrec", "Delete", "delrec");

    _writef(sockd, "</form>\n");

    // Close container
    _web_cmd_module_end(sockd);
}


/**
 * The full main page used when we are called from an ordinary browser, This is also
 * the place where we execute the WEb-command as a side effect to get the web output
 * @param sockd
 * @param wcmd
 * @param cookie_val
 * @param mobile
 */
void
web_main_page(int sockd, char *wcmd, char *cookie_val, int mobile) {
    // Initialize a new page

    if (mobile) {
        web_main_page_mobile(sockd, wcmd, cookie_val);
        return;
    }

    html_startpage(sockd, cookie_val, FALSE);
    html_windtitlebar(sockd,TRUE);

    // Left side : Command table
    _writef(sockd, "<div id=\"windowmenu\">\n");
    web_commandlist(sockd);
    _writef(sockd, "\n</div> <!-- windowmenu -->\n"); 

    // Right side : Output and recording management
    _writef(sockd, "<div id=\"windowcontent\">\n");
    html_cmd_output(sockd, wcmd);
    usleep(cmd_delay); // Give some time for the command to execute
    web_cmd_next(sockd);
    web_cmd_ongoing(sockd);
    web_cmd_add(sockd);    
    web_cmd_qadd(sockd);
    web_cmd_del(sockd);
    web_cmd_ongoingtransc(sockd);   
    _writef(sockd, "\n</div> <!-- windowcontent -->\n");

    html_statusbar(sockd);
    html_endpage(sockd);

}

/**
 * The modified (smaller) main page used when we are called from a mobile
 * browser.
 * @param sockd
 * @param wcmd
 * @param cookie_val
 */
void
web_main_page_mobile(int sockd, char *wcmd, char *cookie_val) {
    // Initialize a new page

    html_startpage(sockd, cookie_val, TRUE);
    html_windtitlebar(sockd,TRUE);

    _writef(sockd, "<div class=\"single_side\">\n");    
    web_cmd_add(sockd);
    web_commandlist_short(sockd);    
    html_cmd_output(sockd, wcmd);
    _writef(sockd, "\n</div> <!-- single_side -->\n");

    html_endpage(sockd);

}


/**
 * Display the login page
 * @param sockd
 * @param mobile
 */
void
web_login_page(int sockd, int mobile) {
    // Initialize a new page   
    // Give the special cookie value "logout" which will create a
    // header which replace the old cookie and sets it expire time
    // in the past so it is removed from the browser
    html_startpage(sockd, "logout", mobile);
    html_windtitlebar(sockd,FALSE);

    _writef(sockd, "<div id=\"login_window\">\n");
    _writef(sockd, "<div id=\"login_title\">Please login</div>\n");
    _writef(sockd, "<form name=\"%s\" method=\"get\" action=\"login\">\n", "tvlogin");
    html_element_input_text(sockd, "User:", "user", "id_loginuser");
    html_element_input_password(sockd, "Password:", "pwd", "id_loginpwd");
    html_element_submit(sockd, "submit_login", "Login", "id_submitlogin");

    _writef(sockd, "<form>\n");
    _writef(sockd, "</div>\n");

    html_endpage(sockd);
}


/**
 * =================================================================================
 * The following section is used to handle the command menu in the WEB interface
 * this is built up from a list structure to separate the content from the
 * actual layout. This way it is easy to adjust the output depending on whether
 * we are called from a mobile or from a stationary browser.
 *
 * We then have to sets of commands. One set to use for the mobile browser and one
 * full set to be used for the ordinary browser.
 *
 * We also have different commands allowed depending on if the server is running
 * in master or slave mode. (The slave does not support any recording commands)
 * =================================================================================
 */

// An entry for a single command. The display name and the actual command string
// to send back to the server
struct cmd_entry {
    char *cmd_name;
    char *cmd_desc;
};

// Logically we group a set of commnds in groups with logically similair commands
// to make it easier fro the user to navigate.
struct cmd_grp {
    char *grp_name;
    char *grp_desc;
    size_t cmd_num;
    struct cmd_entry *entry;
};



//------------------------------------------------------------------
// MASTER personality
//------------------------------------------------------------------
static struct cmd_entry cmdfunc_master_recs[] = {
    {"lh", "Combined"},
    {"lu", "Singles"},    
    {"lr", "Series"},
    {"rh", "History"},
};

static struct cmd_entry cmdfunc_master_mailrecs[] = {
    {"lm", "Combined"},
    {"lmr","Singles/Series"},
    {"rhm","History"}
};

static struct cmd_entry cmdfunc_master_transcoding[] = {
    {"wt", "Queue"},
    {"st", "Statistics"}
};

static struct cmd_entry cmdfunc_master_status[] = {
    {"s", "Status"},
    {"t", "Time"},
    {"df","Disk space"}
};

static struct cmd_entry cmdfunc_master_view[] = {
    {"ls", "Station list"},
    {"lph","Profiles"}    
};

/*
static struct cmd_entry cmdfunc_master_driver[] = {
    {"vc", "Cards"},
    {"li", "List inputs"}
    {"lc%200", "Settings #0"},
    {"lc%201", "Settings #1"}
};
*/

//------------------------------------------------------------------
// SLAVE personality
//------------------------------------------------------------------
static struct cmd_entry cmdfunc_slave_transcoding[] = {
    {"ot", "Ongoing transcodings"},
    {"wt", "Waiting transcodings"},
    {"st", "Statistics"},
    {"lph", "Profiles"}
};

static struct cmd_entry cmdfunc_slave_status[] = {
    {"s", "Status"},
    {"t", "Time"}
};

static struct cmd_entry cmdfunc_slave_view[] = {
    {"z", "Settings"},
    {"rh", "History"}
};

//------------------------------------------------------------------
// Mobile phone master personality
//------------------------------------------------------------------
static struct cmd_entry cmdfunc_master_menu_short[] = {
    {"s", "Status"},
    {"lh", "List rec"},
    {"o", "Ongoing"},
    {"rh", "History"}
};


//------------------------------------------------------------------
// Command groups for normal, slave and phone (short version)
//------------------------------------------------------------------

static struct cmd_grp cmd_grp_master[] = {
    {"Server", "Server information", sizeof (cmdfunc_master_status) / sizeof (struct cmd_entry), cmdfunc_master_status},
    {"Recordings", "Stored recordings", sizeof (cmdfunc_master_recs) / sizeof (struct cmd_entry), cmdfunc_master_recs},
    {"Mail recordings", "Mail recordings", sizeof (cmdfunc_master_mailrecs) / sizeof (struct cmd_entry), cmdfunc_master_mailrecs},    
    {"View", "View", sizeof (cmdfunc_master_view) / sizeof (struct cmd_entry), cmdfunc_master_view},
    {"Transcoding", "Transcoding info", sizeof (cmdfunc_master_transcoding) / sizeof (struct cmd_entry), cmdfunc_master_transcoding}    
    /*
    {"Capture cards", "Card information", sizeof (cmdfunc_master_driver) / sizeof (struct cmd_entry), cmdfunc_master_driver}
     */
};

static struct cmd_grp cmd_grp_slave[] = {
    {"Transcoding", "Transcoding info", sizeof (cmdfunc_slave_transcoding) / sizeof (struct cmd_entry), cmdfunc_slave_transcoding},
    {"Server", "Show status", sizeof (cmdfunc_slave_status) / sizeof (struct cmd_entry), cmdfunc_slave_status},
    {"View", "View", sizeof (cmdfunc_slave_view) / sizeof (struct cmd_entry), cmdfunc_slave_view}
};

static struct cmd_grp cmd_grp_master_menu_short[] = {
    {"Menu", "Server information", sizeof (cmdfunc_master_menu_short) / sizeof (struct cmd_entry), cmdfunc_master_menu_short}
};

static struct cmd_grp cmd_grp_slave_short[] = {
    {"Server", "Show status", sizeof (cmdfunc_slave_status) / sizeof (struct cmd_entry), cmdfunc_slave_status},
    {"Trans", "Transcoding info", sizeof (cmdfunc_slave_transcoding) / sizeof (struct cmd_entry), cmdfunc_slave_transcoding}
};

/**
 * Display the long command list (used for ordinary browsers)
 * @param sockd
 */
void
web_commandlist(int sockd) {

    static struct cmd_grp *cmdgrp;
    int cmdgrplen;

    if (is_master_server) {
        cmdgrp = cmd_grp_master;
        cmdgrplen = sizeof (cmd_grp_master) / sizeof (struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave;
        cmdgrplen = sizeof (cmd_grp_slave) / sizeof (struct cmd_grp);
    }

    for (int i = 0; i < cmdgrplen; ++i) {

        _writef(sockd, "<div class=\"mnugrp_title_row\">\n<span class=\"mnugrp_title\">%s</span>\n</div>\n",
                cmdgrp[i].grp_name, cmdgrp[i].grp_desc);

        _writef(sockd, "<div class=\"mnugrp_commands\">\n");
        for (size_t j = 0; j < cmdgrp[i].cmd_num; ++j) {
            _writef(sockd, "<a href=\"cmd?c=%s\">&#8718; %s</a><br />\n", cmdgrp[i].entry[j].cmd_name, cmdgrp[i].entry[j].cmd_desc);
        }
        _writef(sockd, "</div>\n");

    }
}

/**
 * Display the short version of the command list. Used for mobile browsers.
 * @param sockd
 */
void
web_commandlist_short(int sockd) {

    static struct cmd_grp *cmdgrp;
    size_t cmdgrplen;

    if (is_master_server) {
        cmdgrp = cmd_grp_master_menu_short;
        cmdgrplen = sizeof (cmd_grp_master_menu_short) / sizeof (struct cmd_grp);
    } else {
        cmdgrp = cmd_grp_slave_short;
        cmdgrplen = sizeof (cmd_grp_slave_short) / sizeof (struct cmd_grp);
    }

    _writef(sockd, "<div id=\"cmd_menu\">\n");
    for (size_t i = 0; i < cmdgrplen; ++i) {
        _writef(sockd, "<div class=\"cmd_grp\" id=\"cmdgrp%d\">",i+1);
        for (size_t j = 0; j < cmdgrp[i].cmd_num; ++j) {            
            _writef(sockd, 
                    "<div class=\"cmd_item\" id=\"cmditem%d%d\">"
                    "<a href=\"cmd?c=%s\">%s</a>"
                    "</div>\n",i+1,j+1, 
                    cmdgrp[i].entry[j].cmd_name, cmdgrp[i].entry[j].cmd_desc);
        }
        _writef(sockd, "</div>\n <!-- cmdgrp -->");
    }
    _writef(sockd, "</div> <!-- cmd_menu -->\n");

}
