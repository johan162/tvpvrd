/* =========================================================================
 * File:        WAKELAN.C
 * Description: Routines to wake up a server which have network support
 *              for LAN wakeup
 *
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Note:        parse_mac() based on code taken from a newsgroups so long ago 
 *              I cannot remember where. 
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

// Standard socket and IP address handling
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "wakelan.h"
#include "../utils.h"
#include "../xstr.h"
#include "../tvplog.h"

/**
 * Parse a MAC address
 * @param mac The parsed MAC address
 * @param str The string to be parsed
 * @return 1 if the string was succesfully parsed
 */
int
parse_mac(unsigned char *mac, char *str) {
    int count;
    unsigned char c;
    unsigned char val;
    int colon_ok = 1;

    if( strlen(str) < 17 )
        return 0;

    for (int i = 0; i < 6; i++) {
        mac[i] = 0;
    }

    for (int i = 0; i < 6; i++) {
        count = 0;
        val = 0;
        do {
            c = (unsigned char)toupper(*str++);
            if (c >= '0' && c <= '9') {
                val = (unsigned char)((val * 16) + (c - '0'));
            } else if (c >= 'A' && c <= 'F') {
                val = (unsigned char)((val * 16) + (c - 'A') + 10);
            } else if (c == ':') {
                if (colon_ok || count-- != 0)
                    break;
            } else if (c == '\0') {
                str--;
                break;
            } else {
                return 0;
            }
            colon_ok = 1;
        } while (++count < 2);
        colon_ok = (count < 2);
        *mac++ = val;
    }
    if (*str)
        return 0;
    return 1;
}


/**
 * Send a wakeup network package to the specified network card in order
 * to power on the server. Obviously the network card must support wake-on-lan
 * @param mac
 * @param target
 * @param str_bport
 * @return
 */
int
wakelan(char *mac, char *target, uint16_t target_bport) {

    struct in_addr inaddr;
    unsigned char macaddr[6];

    if (!parse_mac(macaddr, mac)) {
        logmsg(LOG_ERR,"Illegal MAC address ( %s )",mac);
        return -1;
    }

    if ( !inet_aton(target, &inaddr) ) {
        struct hostent *he = gethostbyname(target);
        if( he ) {
            inaddr = *(struct in_addr *)he->h_addr_list[0];
        } else {
            logmsg(LOG_ERR,"Illegal target host name ( %d  %s )",errno,strerror(errno));
            return -1;
        }
    }

    size_t msglen=0;
    unsigned char msg[1024];
    for (size_t i = 0; i < 6; i++) {
        msg[msglen++] = (unsigned char)0xff;
    }

    for (size_t i = 0; i < 16; i++) {
        for (size_t j = 0; j < sizeof(macaddr); j++) {
            msg[msglen++] = macaddr[j];
        }
    }

    uint16_t bport = 0;
    if( target_bport ) {
        bport = htons(target_bport);
    }

    struct sockaddr_in bcast;
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family      = AF_INET;
    bcast.sin_addr.s_addr = inaddr.s_addr;
    bcast.sin_port        = bport;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logmsg(LOG_ERR,"Cannot allocate socket for wakeup datagram ( %d : %s)",errno, strerror(errno));
        return -1;
    }

    int optval = 1;
    ssize_t rc = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
    if ( rc < 0) {
        logmsg(LOG_ERR, "Can'tset  socket option SO_BROADCAST: rc = %d, ( %d : %s )\n",
               rc,  errno, strerror(errno));
        close(sock);
        return -1;
    }
    
    rc = sendto(sock, &msg, msglen, 0, (struct sockaddr *)&bcast, sizeof(bcast));
    if ( rc < 0) {
        logmsg(LOG_ERR, "Cant send wakeup message on socket: rc = %d, ( %d : %s )\n",
               rc,  errno, strerror(errno));
        close(sock);
        return -1;
    } 
    close(sock);    
    return 0;
}

