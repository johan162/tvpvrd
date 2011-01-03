/* =========================================================================
 * File:        MAILCLIENTLIB.C
 * Description: A simple library to provide interface to a SMTP server
 *              and make it easy to send mails. The library supports both
 *              handling of attachment, plain and HTML mail as well as
 *              inline images within the HTML code.
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2010 Johan Persson
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

// Standard UNIX includes
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <syslog.h>

#include "mailclientlib.h"
#include "base64ed.h"
#include "quotprinted.h"

// Clear variable section in memory
#define CLEAR(x) memset (&(x), 0, sizeof(x))
#define CHARSET "UTF-8"

/**
 * A subset of the possible 250 return strings from EHLO command
 * used to determine what features the SMTP server supports.
 */
static char *
smtp_features[] = {
    "PIPELINING",
    "8BITMIME",
    "AUTH PLAIN LOGIN",
    "VRFY",
    "ETRN",
    "ENHANCEDSTATUSCODES",
    "DSN"
};
static const size_t NumFeat = sizeof(smtp_features) / sizeof(char *);

static char *
attach_mime_types[] = {
    "text/plain",
    "text/html",
    "image/png",
    "image/jpg",
    "image/gif",
    "application/octet-stream",
    "application/pdf",
} ;
static const size_t NumMIME = sizeof(attach_mime_types) / sizeof(char *);

static char *
transfer_encoding[] = {
    "8bit",
    "base64",
    "quoted-printable"
};
static const size_t NumTransEnc = sizeof(transfer_encoding) / sizeof(char *);

/**
 * Trim a string inplace by removing beginning and ending spaces
 * @param str
 */
static void
xstrtrim(char *str) {
    char *tmp = strdup(str),*startptr=tmp;
    int n=strlen(str);
    char *endptr = tmp+n-1;

    while( *startptr == ' ') {
        startptr++;
    }

    while( n>0 && *endptr == ' ') {
        --n;
        --endptr;
    }

    while( startptr <= endptr ) {
        *str++ = *startptr++;
    }

    *str = '\0';

    free(tmp);
}


/**
 * Insert '\r\n' pair after specified number of characters. If the input contains
 * a single '\n' it is translated to a '\r\n' pair
 * @param in
 * @param out
 * @param maxlen
 * @param width
 * @return 0 on success, -1 on failure
 */
static int
split_in_rows(char * const in, char * const out, size_t maxlen, size_t width) {
    char *pin = in;
    char *pout = out;
    size_t w=0;
    const size_t n = strlen(in);
    if( n+(n/width)+1 >= maxlen ) {
        return -1;
    }
    while( *pin ) {
        if( *pin == '\r' && *(pin+1) == '\n' ) {
            *pout++ = *pin++;
            *pout++ = *pin++;
            w=0;
        } else if( *pin == '\n' ) {
            *pout++ = '\r';
            *pout++ = *pin++;
            w=0;
        } else {
            *pout++ = *pin++;
            w++;
        }
        if( w == width ) {
            *pout++ = '\r' ;
            *pout++ = '\n' ;
            w=0;
        }
    }
    return 0;
}

/**
 * Free all memory associated with reply list
 * @param reply_list
 * @param maxlen
 */
static void
_free_smtplist(struct smtp_reply *reply_list[], size_t maxlen) {
    for (size_t i = 0; i < maxlen && reply_list[i]; i++) {
        free(reply_list[i]->str);
        free(reply_list[i]);
        reply_list[i] = NULL;
    }
}

/**
 * Call system free() for all non NULL pointers
 * @param ptr
 */
static inline void
_smtp_chkfree(void *ptr) {
    if( ptr ) free(ptr);
}

/**
 * Free memory allocated to each attachment
 * @param handle
 */
static void
_free_smtp_attachment(struct smtp_attachment **handle) {
    if( *handle == NULL ) return;
    
    _smtp_chkfree((*handle)->contentdisposition);
    _smtp_chkfree((*handle)->contenttransferencoding);
    _smtp_chkfree((*handle)->contenttype);
    _smtp_chkfree((*handle)->data);
    _smtp_chkfree((*handle)->filename);
    _smtp_chkfree((*handle)->name);
    _smtp_chkfree((*handle)->cid);
    free(*handle);
    *handle = NULL;
}

/**
 * Free all memory associated with the SMTP handle
 * @param handle
 */
static void
_free_smtp_handle(struct smtp_handle **handle) {
    if( *handle == NULL ) return;

    _free_smtplist((*handle)->cap, 64);
    _smtp_chkfree((*handle)->from);
    _smtp_chkfree((*handle)->returnpath);
    _smtp_chkfree((*handle)->subject);
    _smtp_chkfree((*handle)->mimeversion);
    _smtp_chkfree((*handle)->useragent);
    _smtp_chkfree((*handle)->contenttype);
    _smtp_chkfree((*handle)->contenttransferencoding);
    _smtp_chkfree((*handle)->html);
    _smtp_chkfree((*handle)->plain);

    _smtp_chkfree((*handle)->databuff);
    
    for (size_t i = 0; i < (*handle)->toidx; ++i) {
        _smtp_chkfree((*handle)->to[i]);
    }
    _smtp_chkfree((*handle)->to_concatenated);
    for (size_t i = 0; i < (*handle)->ccidx; ++i) {
        _smtp_chkfree((*handle)->cc[i]);
    }
    _smtp_chkfree((*handle)->cc_concatenated);
    for (size_t i = 0; i < (*handle)->bccidx; ++i) {
        _smtp_chkfree((*handle)->bcc[i]);
    }
    _smtp_chkfree((*handle)->bcc_concatenated);
    for (size_t i = 0; i < (*handle)->attachmentidx; ++i) {
        _free_smtp_attachment( & (*handle)->attachment[i] );
    }

    free(*handle);
    *handle = NULL;
}


/**
 * Convert a reply from the SMTP server into separate status codes and strings
 * and put each reply in the array structure.
 * @param buffer
 * @param reply_list
 * @param maxlen
 * @return number of lines in reply, -1 on failure
 */
static ssize_t
_smtp_split_reply(char *buffer, struct smtp_reply *reply_list[], size_t maxlen) {

    size_t const N = 255;
    char *pbuff = buffer;
    unsigned status;
    char reply[N];
    unsigned lastline = 0;
    size_t idx = 0;
    struct smtp_reply *entry;

    for (size_t i = 0; i < maxlen; ++i) {
        reply_list[i] = NULL;
    }

    while (!lastline && idx < maxlen) {

        if (isdigit(*pbuff) && isdigit(*(pbuff + 1)) && isdigit(*(pbuff + 2))) {
            // Read status code
            status = (*pbuff - '0')*100 + (*(pbuff + 1) - '0')*10 + (*(pbuff + 2) - '0');
            pbuff += 3;
        } else {
            return -1;
        }
        lastline = *pbuff++ == ' ';

        size_t n = N;
        char *preply = reply;
        while (n > 0 && *pbuff != '\r' && *pbuff != '\n') {
            *preply++ = *pbuff++;
            n--;
        }
        if (n < 1) return -1;
        *preply = '\0';

        entry = calloc(1, sizeof (struct smtp_reply));
        entry->status = status;
        entry->str = strdup(reply);
        reply_list[idx++] = entry;

        if (*pbuff == '\r' && *(pbuff + 1) == '\n') {
            pbuff += 2;
        } else {
            return -1;
        }

    }

    if (!lastline && idx >= maxlen) {
        return -1;
    }

    return idx;
}

static struct smtp_handle *
_smtp_connect(char *server_ip, char *service) {
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo; // will point to the results
    size_t const N = 128;
    struct smtp_reply * reply_list[N];

    CLEAR(hints);
    hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

    if ((status = getaddrinfo(server_ip, service, &hints, &servinfo))) {
        //logmsg(LOG_ERR, "getaddrinfo: %s\n", gai_strerror(status));
        return NULL;
    }
    int sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sock < 0) {
        return NULL;
    }
    if (-1 == connect(sock, servinfo->ai_addr, servinfo->ai_addrlen)) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return NULL;
    }
    freeaddrinfo(servinfo);

    ssize_t n;
    char *buffer = calloc(1, 2048);
    if ((n = recv(sock, buffer, 1023, 0)) == -1) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
        return NULL;
    }
    buffer[n] = '\0';

    ssize_t num = _smtp_split_reply(buffer, reply_list, N);
    if (1 != num) {
        _free_smtplist(reply_list, N);
        return NULL;
    }

    struct smtp_handle *handle = calloc(1, sizeof (struct smtp_handle));
    handle->sfd = sock;
    handle->cap[0] = reply_list[0];
    handle->capidx++;

    return handle;
}

/**
 * Send a specifed command to the SMTP server and record the servers possible multiline reply
 * in the supplied reply list
 * @param handle
 * @param cmd Command
 * @param arg Optional argument to command
 * @param reply_list Reply list
 * @param N Maximum number of replies in reply_list
 * @return number of lines in reply, -1 on failure
 */
static ssize_t
_smtp_send_command(struct smtp_handle *handle, char *cmd, char *arg, struct smtp_reply *reply_list[], size_t N) {
    char tmpbuff[255];
    if (arg == NULL || *arg == '\0') {
        snprintf(tmpbuff, 255, "%s\r\n", cmd);
    } else {
        snprintf(tmpbuff, 255, "%s%s\r\n", cmd, arg);
    }
    ssize_t nw = send(handle->sfd, tmpbuff, strlen(tmpbuff), 0);

    if (nw != (ssize_t) strlen(tmpbuff)) {
        printf("Error writing to socket (%s)", strerror(errno));
    } else {
        ssize_t n;
        char *buffer = calloc(1, 2048);
        if ((n = recv(handle->sfd, buffer, 2048, 0)) == -1) {
            return -1;
        }
        buffer[n] = '\0';

        ssize_t num = _smtp_split_reply(buffer, reply_list, N);

        if (num > 0) {
            if (reply_list[0]->status == 502) {
                // Command not recognized
                _free_smtplist(reply_list, N);
                return -1;
            }
            return num;
        }
    }
    return -1;
}

/**
 * Split and normlize a conformant email address into its human readable
 * name and the actual mail adress. This will also do some cleaning and
 * some minimum sanity check (with emphasis on minimum)
 *
 * @param mailaddr Original mail address
 * @param name Extrade human readable part
 * @param maxnlen Maximum buffer length for name
 * @param addr Extracted mail adress
 * @param maxalen Maximum buffer length for address
 * @return 0 on success, -1 on failure (illegal mail address)
 */
static int
_smtp_normalize_mailaddr(char *mailaddr, char *name, size_t maxnlen, char *addr, size_t maxalen) {
    char *left, *right;
    if( NULL == (left = strchr(mailaddr,'<')) ||
        NULL == (right = strchr(mailaddr,'>'))) {
        char *tmp = strdup(mailaddr);
        xstrtrim(tmp);
        if( NULL == strchr(tmp,' ') && strchr(tmp,'@') ) {
            char *tmp2=calloc(1,strlen(tmp)+3);
            sprintf(tmp2,"<%s>",tmp);
            if( maxalen < strlen(tmp2)+1 ) {
                free(tmp);
                free(tmp2);
                return -1;
            }
            strcpy(addr,tmp2);
            free(tmp);
            free(tmp2);
            return 0;
        } else {
            free(tmp);
            return -1;
        }
    }
    size_t nlen = left-mailaddr;
    size_t alen = strlen(left);
    if( maxnlen < nlen || maxalen < alen ) {
        return -1;
    }
    strncpy(name,mailaddr,nlen);
    name[nlen] = '\0';
    xstrtrim(name);
    nlen = strlen(name);
    if( *name != '"' && *(name+nlen-1) != '"') {
        char *tmp=calloc(1,strlen(name)+3);
        sprintf(tmp,"\"%s\"",name);
        if( maxnlen < strlen(tmp)+1 ) {
            return -1;
        }
        strcpy(name,tmp);
        free(tmp);
    } else if( (*name == '"' || *(name+nlen-1) == '"') &&
              !(*name == '"' && *(name+nlen-1) == '"') ) {
        return -1;
    }

    strncpy(addr,left,maxalen);
    addr[alen] = '\0';
    xstrtrim(addr);

    return 0;
}

/**
 * Send command to SMTP server and check thta it responds with the expected
 * stytaus code
 * @param handle
 * @param cmd
 * @param arg
 * @param expected
 * @return 0 if the server responded as expected, -1 otherwise
 */
static int
_sendchk(struct smtp_handle *handle,char *cmd, char *arg, int expected) {
    struct smtp_reply * r[1];
    int num = _smtp_send_command(handle, cmd, arg, r, 1);
    if ( num <= 0 || (num > 0 && r[0]->status != expected) ) {
        _free_smtplist(r, 1);
        return -1;
    }
    _free_smtplist(r, 1);
    return 0;
}

/**
 * Send a data stream to the SMTP server. This means all the data that
 * are sent after the DATA command.
 * @param handle
 * @param cmd
 * @param arg
 * @return 0 if all the data could be written, -1 otherwise
 */
static int
_senddata(struct smtp_handle *handle,char *cmd, char *arg) {
    const size_t N = strlen(cmd)+strlen(arg)+3;
    char *buff = calloc(1,N);
    snprintf(buff,N,"%s%s\r\n",cmd,arg);
    ssize_t num = send(handle->sfd,buff,strlen(buff),0);
    int rc = num == (ssize_t)strlen(buff) ? 0 : -1;
    free(buff);
    return rc;
}

/**
 * Return a pointer to a static string to a text representing the selected mime type
 * @param type
 * @return
 */

static char *
_smtp_get_mime(size_t type) {
    if (type >= NumMIME) return NULL;
    return attach_mime_types[type];
}

/**
 * Return a pointer to a static string to a text representing the selected transfer encoding
 * @param type
 * @return
 */
static char *
_smtp_get_transfer_encoding(size_t type) {
    if (type >= NumTransEnc) return NULL;
    return transfer_encoding[type];
}

/**
 * Debug print routines
 * @param reply_list
 * @param N
 */

static void
_print_reply(struct smtp_reply *reply_list[], size_t N) {
    for (size_t i = 0; i < N && reply_list[i]; i++) {
        printf("%02d: [%03d, \"%s\"]\n", i, reply_list[i]->status, reply_list[i]->str);
    }
}


/**
 * Debug print routine. Dump all information about an SMTP session
 * @param handle
 */
void
smtp_dump_handle(struct smtp_handle * handle, FILE *fp) {
    fprintf(fp,"Handle:\n");
    _print_reply(handle->cap, 64);
    fprintf(fp,"Subject: %s\n", handle->subject);
    fprintf(fp,"From: %s\n", handle->from);
    fprintf(fp,"DATA:\n%s\n", handle->databuff);
}

/**
 * Add a receipent of the mail. Use this to add either To, Cc or Bcc
 * @param handle
 * @param type
 * @param rcpt
 * @return 0 on success, -1 on failure
 */
int
smtp_add_rcpt(struct smtp_handle *handle, unsigned type, char *rcpt) {
    char email_namepart[256];
    char email_addrpart[256];
    char email_full[512];

    if( rcpt == NULL || *rcpt == '\0') {
        return -1;
    }

    if( -1 == _smtp_normalize_mailaddr(rcpt,email_namepart,256,email_addrpart,256) ) {
        return -1;
    }
    snprintf(email_full,512,"%s %s",email_namepart,email_addrpart);
    switch (type) {
        case SMTP_RCPT_TO:
            if (handle->toidx >= MAX_RCPT)
                return -1;
            if( handle->to_concatenated == NULL ) {
                handle->to_concatenated = calloc(1,MAX_HEADER_ADDR_SIZE);
            }
            handle->to[handle->toidx++] = strdup(email_full);
            if( *handle->to_concatenated ) {
                strcat(handle->to_concatenated,",");
            }
            strcat(handle->to_concatenated,email_full);
            break;
        case SMTP_RCPT_CC:
            if (handle->ccidx >= MAX_RCPT)
                return -1;
            if( handle->cc_concatenated == NULL ) {
                handle->cc_concatenated = calloc(1,MAX_HEADER_ADDR_SIZE);
            }
            handle->cc[handle->ccidx++] = strdup(email_full);
            if( *handle->cc_concatenated ) {
                strcat(handle->cc_concatenated,",");
            }
            strcat(handle->cc_concatenated,email_full);

            break;
        case SMTP_RCPT_BCC:
            if (handle->bccidx >= MAX_RCPT)
                return -1;
            if( handle->bcc_concatenated == NULL ) {
                handle->bcc_concatenated = calloc(1,MAX_HEADER_ADDR_SIZE);
            }
            handle->bcc[handle->bccidx++] = strdup(email_full);
            if( *handle->bcc_concatenated ) {
                strcat(handle->bcc_concatenated,",");
            }
            strcat(handle->bcc_concatenated,email_full);
            break;
    }
    return 0;
}

/**
 * Add plain message text to the mail. If this is used then smtp_add_html()
 * cannot be used. Only one of them can be used per mail.
 * @param handle
 * @param buffer
 * @return 0 on success, -1 on failure
 */
int
smtp_add_plain(struct smtp_handle *handle, char *buffer) {

    if (handle->plain || handle->html) {
        return -1;
    }
    handle->plain = strdup(buffer);

    return 0;
}

/**
 * Add HTML message to the mail. If this is used then smtp_add_plain()
 * cannot be used. Only one of them can be used per mail. Note that in the
 * mail you should add both the HTML version and also the alternative
 * version which is displayed for those clients that cannot handle HTML
 * mails.
 * @param handle
 * @param buffer HTML text
 * @param altbuffer Optional plai version of HTML text
 * @return 0 on success, -1 on failure
 */
int
smtp_add_html(struct smtp_handle *handle, char *buffer, char *altbuffer) {
    if (handle->plain || handle->html) {
        return -1;
    }
    if( buffer ) {
        handle->html = strdup(buffer);
    } else {
        return -1;
    }
    if( altbuffer ) {
        // Optional
        handle->plain = strdup(altbuffer);
    }

    return 0;
}

/**
 * Add attachment to mail and use specified filename and name. The attachment will be encoded
 * according to the specified transfer encoding. The content type is the responsibility of the
 * caller.
 * @param handle
 * @param filename
 * @param name
 * @param data
 * @param len
 * @param contenttype
 * @param encoding
 * @return -1 on failure, 0 on success
 */
int
smtp_add_attachment(struct smtp_handle *handle, char *filename, char *name, char *data, size_t len,
                    unsigned contenttype, unsigned encoding) {

    struct smtp_attachment *attach = calloc(1, sizeof (struct smtp_attachment));
    attach->filename = strdup(filename);
    attach->name = strdup(name);

    if( encoding == SMTP_CONTENT_TRANSFER_ENCODING_BASE64 ) {
        size_t const N1=2*len;
        char *bdata = calloc(1,N1);
        if( -1 == base64encode(data,len,bdata,N1) ) {
            free(bdata);
            return -1;
        }
        size_t const width=76;
        size_t const N2 = N1 +(len/width)*2+3;
        char *bsdata = calloc(1,N2);
        if( -1 == split_in_rows(bdata, bsdata, N2, width) ) {
            free(bdata);
            free(bsdata);
            return -1;
        }
        attach->data = strdup(bsdata);
        free(bdata);
        free(bsdata);
    } else if( encoding == SMTP_CONTENT_TRANSFER_ENCODING_QUOTEDPRINT ) {
        size_t const N1=3*len; // 
        char *bdata = calloc(1,N1);
        if( -1 == qprint_encode(data,bdata,N1) ) {
            free(bdata);
            return -1;
        }
        attach->data = strdup(bdata);
        free(bdata);
    } else if( encoding == SMTP_CONTENT_TRANSFER_ENCODING_8BIT ) {
        attach->data = strdup(data);
    } else {
        return -1;
    }

    char buff[255];
    switch( contenttype ) {
        case SMTP_ATTACH_CONTENT_TYPE_PLAIN:
        case SMTP_ATTACH_CONTENT_TYPE_HTML:
            snprintf(buff, 255, "%s; charset=\"%s\"",_smtp_get_mime(contenttype),CHARSET);
            break;
        default:
            snprintf(buff, 255, "%s; name=\"%s\"",_smtp_get_mime(contenttype),name);
            break;
    }
    attach->contenttype = strdup(buff);

    snprintf(buff, 255, "attachment");
    attach->contentdisposition = strdup(buff);

    snprintf(buff, 255, "%s",_smtp_get_transfer_encoding(encoding));
    attach->contenttransferencoding = strdup(buff);

    handle->attachment[handle->attachmentidx++] = attach;
    return 0;
}

/**
 * Add attachment from a fully qualified file name.
 * @param handle
 * @param filename
 * @return 0 on success, -1 on failuer
 */
int
smtp_add_attachment_fromfile(struct smtp_handle *handle, char *filename, unsigned contenttype, unsigned encoding) {

    FILE *fp;
    if( (fp=fopen(filename,"rb"))==NULL ) {
        return -1;
    }
    int fd = fileno(fp);
    struct stat buf;
    fstat(fd, &buf);
    char *buffer = calloc(1,buf.st_size);
    size_t readsize = fread(buffer,sizeof(char), buf.st_size,fp);
    if( readsize != (size_t)buf.st_size ) {
        fclose(fp);
        free(buffer);
        return -1;
    }
    fclose(fp);
    int rc = smtp_add_attachment(handle,basename(filename),basename(filename),buffer,buf.st_size,contenttype,encoding);
    free(buffer);
    return rc;
}


/**
 * Adds an attachment in the mail that holds one inline image that is referencd i the HTML
 * section of the mail as <img href="cid:XX"> where XX is the cid specified as the last argument
 * to the function. This should be aunique ID within the mail.
 * @param handle
 * @param filename
 * @param contenttype
 * @param cid
 * @return
 */
int
smtp_add_attachment_inlineimage(struct smtp_handle *handle, char *filename, char *cid) {

    // Determine contenttype based on file name extension
    size_t n=strlen(filename)-1,cnt=0;
    char ebuff[5];
    while( cnt < 5 && n > 0 && filename[n] != '.' ) {
        n--; cnt++;
    }
    if( cnt < 5 && n > 0 && filename[n] == '.') {
        strncpy(ebuff,&filename[n+1],5);
    } else {
        return -1;
    }

    unsigned contenttype ;
    if( strcasecmp(ebuff,"jpg") == 0 || strcasecmp(ebuff,"jpeg") == 0 ) {
        contenttype = SMTP_ATTACH_CONTENT_TYPE_JPG;
    } else if( strcasecmp(ebuff,"png") == 0 ) {
        contenttype = SMTP_ATTACH_CONTENT_TYPE_PNG;
    } else if( strcasecmp(ebuff,"gif") == 0 ) {
        contenttype = SMTP_ATTACH_CONTENT_TYPE_GIF;
    } else {
        return -1;
    }

    if( -1 == smtp_add_attachment_fromfile(handle, filename, contenttype, SMTP_CONTENT_TRANSFER_ENCODING_BASE64) ) {
        return -1;
    }

    // This is the content ID that is used as reference in the HTML
    // like <img src="cid:ID"> where ID could be the unique string suplpied here
    handle->attachment[handle->attachmentidx-1]->cid = strdup(cid);

    return 0;
}

/**
 * Send the mail
 * @param handle
 * @param from
 * @param subject
 * @return 0 on success, -1 on failure
 */
int
smtp_sendmail(struct smtp_handle *handle, char *from, char *subject) {

    handle->from = strdup(from);
    handle->returnpath = strdup(from);

    char *buff = calloc(1, 1024);
    qprint_encode_word(subject, buff, 1024);
    handle->subject = strdup(buff);

    // Use hostname as part of the unique boundary
    char hname[255];
    gethostname(hname,255);

    // Find out how we need to encode this mail. We support three ways
    // 1) The mail contains only plain text. In that case we only
    // set encoding to 8bit and type to text/plain and include it as payload
    // 2) The mail contains HTML. In that case we add two mime parts, both the
    // HTML version but als the text in the plain version. Normally it is a good
    // idea to include a plain version of the text as well.
    // 3) The same as 1) and 2) but with the addition of attachments

    if( handle->attachmentidx == 0) {

        // Mail have no attachments

        if (handle->plain && handle->html == NULL ) {
            // A plain text mail
            handle->contenttransferencoding = strdup("Content-Transfer-Encoding: 8bit");
            char buff[128];
            snprintf(buff,128,"Content-Type: text/plain; charset=\"%s\"",CHARSET);
            handle->contenttype = strdup(buff);
            size_t const databufflen = strlen(handle->plain) + 512;
            handle->databuff = calloc(1, databufflen);
            snprintf(handle->databuff, databufflen, "%s", handle->plain);

        } else if (handle->html) {

            char boundary[255];
            snprintf(boundary, 255, "_%x%x%x%x%s_", rand(), rand(), rand(), rand(), hname);
            snprintf(buff, 1024, "Content-Type: multipart/alternative; boundary=\"%s\"", boundary);
            handle->contenttype = strdup(buff);

            size_t const databufflen = strlen(handle->html) + strlen(handle->plain) + 512;
            handle->databuff = calloc(1, databufflen);
            snprintf(handle->databuff, databufflen,
                    "--%s\r\n"
                    "Content-Transfer-Encoding: 8bit\r\n"
                    "Content-Type: text/plain; charset=\"%s\"\r\n\r\n"
                    "%s\r\n"
                    "--%s\r\n"
                    "Content-Transfer-Encoding: 8bit\r\n"
                    "Content-Type: text/html; charset=\"%s\"\r\n\r\n"
                    "%s\r\n"
                    "--%s--\r\n",
                    boundary, CHARSET, handle->plain, boundary, CHARSET, handle->html, boundary);
        } else {
            free(buff);
            return -1;
        }

    } else {

        char boundary[255];
        snprintf(boundary, 255, "_%x%x%x%x%s_", rand(), rand(), rand(), rand(), hname);
        snprintf(buff, 1024, "Content-Type: multipart/mixed; boundary=\"%s\"", boundary);
        handle->contenttype = strdup(buff);

        size_t adbsize = 0;
        for (size_t i = 0; i < handle->attachmentidx; ++i) {
            adbsize += strlen(handle->attachment[i]->data) + 256;
        }

        if (handle->plain && handle->html == NULL) {
            // A plain text mail
            size_t const databufflen = strlen(handle->plain) + adbsize + 512;
            handle->databuff = calloc(1, databufflen);
            snprintf(handle->databuff, databufflen,
                    "--%s\r\n"
                    "Content-Transfer-Encoding: 8bit\r\n"
                    "Content-Type: text/plain; charset=\"%s\"\r\n"
                    "\r\n"
                    "%s\r\n",
                    boundary, CHARSET, handle->plain);
        } else {
            char boundary2[255];
            snprintf(boundary2, 255, "_%x%x%x%x%s_", rand(), rand(), rand(), rand(), hname);

            size_t const databufflen = strlen(handle->html) + strlen(handle->plain) + adbsize + 512;
            handle->databuff = calloc(1, databufflen);
            snprintf(handle->databuff, databufflen,
                    "--%s\r\n"
                    "Content-Type: multipart/alternative; boundary=\"%s\"\r\n"
                    "\r\n"
                    "--%s\r\n"
                    "Content-Transfer-Encoding: 8bit\r\n"
                    "Content-Type: text/plain; charset=\"%s\"\r\n\r\n"
                    "%s\r\n"
                    "--%s\r\n"
                    "Content-Transfer-Encoding: 8bit\r\n"
                    "Content-Type: text/html; charset=\"%s\"\r\n\r\n"
                    "%s\r\n"
                    "--%s--\r\n",
                    boundary, boundary2, boundary2, CHARSET, handle->plain, boundary2, CHARSET, handle->html, boundary2);
        }

        for (size_t i = 0; i < handle->attachmentidx; ++i) {

            size_t const abufflen = strlen(handle->attachment[i]->data) + 1024;
            char *tmpbuff = calloc(1, abufflen);
            if( handle->attachment[i]->cid ) {

                // If any of the attachments have an ID it means that is related to the
                // HTML part and in that case the overall MIME type for the mail must be set
                // to related.
                free(handle->contenttype);
                snprintf(buff, 1024, "Content-Type: multipart/related; boundary=\"%s\"", boundary);
                handle->contenttype = strdup(buff);

                // Inline image
                snprintf(tmpbuff, abufflen,
                    "--%s\r\n"
                    "Content-Transfer-Encoding: %s\r\n"
                    "Content-Type: %s\r\n"
                    "Content-ID: <%s>\r\n"
                    "X-Attachment-Id: %s\r\n"
                    "\r\n"
                    "%s\r\n",
                    boundary,
                    handle->attachment[i]->contenttransferencoding,
                    handle->attachment[i]->contenttype,
                    handle->attachment[i]->cid,handle->attachment[i]->cid,
                    handle->attachment[i]->data
                    );

            } else {
                // Normal attachment
                snprintf(tmpbuff, abufflen,
                    "--%s\r\n"
                    "Content-Transfer-Encoding: %s\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Disposition: %s; filename=\"%s\"\r\n"
                    "\r\n"
                    "%s\r\n",
                    boundary,
                    handle->attachment[i]->contenttransferencoding,
                    handle->attachment[i]->contenttype,
                    handle->attachment[i]->contentdisposition, handle->attachment[i]->filename,
                    handle->attachment[i]->data
                    );
            }
            strcat(handle->databuff, tmpbuff);
            free(tmpbuff);
        }
        char boundbuff[512];
        snprintf(boundbuff, 512, "--%s--\r\n", boundary);
        strcat(handle->databuff, boundbuff);

    }

    free(buff);

    char email_namepart[256];
    char email_addrpart[256];

    if( -1 == _smtp_normalize_mailaddr(from,email_namepart,256,email_addrpart,256) ||
        -1 == _sendchk(handle,"MAIL FROM: ", email_addrpart, 250) ) {
        return -1;
    }
    for( size_t i=0; i < handle->toidx; i++) {
        if( -1 == _smtp_normalize_mailaddr(handle->to[i],email_namepart,256,email_addrpart,256) ||
            -1 == _sendchk(handle,"RCPT TO: ", email_addrpart, 250) ) {
            return -1;
        }
    }
    for( size_t i=0; i < handle->ccidx; i++) {
        if( -1 == _smtp_normalize_mailaddr(handle->cc[i],email_namepart,256,email_addrpart,256) ||
            -1 == _sendchk(handle,"RCPT TO: ", email_addrpart, 250) ) {
            return -1;
        }
    }
    for( size_t i=0; i < handle->bccidx; i++) {
        if( -1 == _smtp_normalize_mailaddr(handle->bcc[i],email_namepart,256,email_addrpart,256) ||
            -1 == _sendchk(handle,"RCPT TO: ", email_addrpart, 250) ) {
            return -1;
        }
    }

    if( -1 == _sendchk(handle,"DATA", "", 354) ||
        -1 == _senddata(handle,"From: ", from) ||
        -1 == _senddata(handle,"To: ", handle->to_concatenated) ||
        -1 == _senddata(handle,"Cc: ", handle->cc_concatenated) ||
        -1 == _senddata(handle,"Subject: ", handle->subject) ||
        -1 == _senddata(handle,"MIME-Version: ", handle->mimeversion) ||
        -1 == _senddata(handle,handle->contenttype,"") ) {
        return -1;
    }

    if( handle->contenttransferencoding ) {
        if( -1 == _senddata(handle,handle->contenttransferencoding,"") ) {
            return -1;
        }
    }

    if( -1 == _senddata(handle,"\r\n", "") ||
        -1 == _senddata(handle,handle->databuff, "") ) {
        return -1;
    }

    if( -1 == _sendchk(handle,".", "", 250) ||
        -1 == _sendchk(handle,"QUIT", "", 221) ) {
        return -1;
    }

    return 0;
}

/**
 * Check what feature the SMTP server supports
 * @param handle
 * @param feature to check for
 * @return 1 if feature is supoprted, 0 if not and -1 if feature is an invalid argument
 */
int
smtp_server_support(struct smtp_handle *handle, size_t feature) {

    if( feature > NumFeat ) {
        return -1;
    }
    for( size_t i=0 ; i < handle->capidx; i++ ) {
        if( 0 == strncmp(smtp_features[feature],handle->cap[i]->str,strlen(smtp_features[feature]))) {
            return 1;
        }
    }
    return 0;
}

/**
 * Initialize a new connecttion to the SMTP server. The code assumed that the
 * SMTP server can use plain text login if it requires login
 * @param server_ip Server IP och fully qualified name
 * @param user User name to login to with server
 * @param pwd Password
 * @return -1 on failure , 0 on success
 */
struct smtp_handle *
smtp_setup(char *server_ip, char *user, char *pwd) {

    srand(time(NULL));

    struct smtp_handle *handle = _smtp_connect(server_ip, "smtp");

    if (handle != NULL) {
        char hname[255];
        int rc = gethostname(hname, 255);
        if (-1 == rc) {
            _free_smtp_handle(&handle);
            return NULL;
        }
        ssize_t num = _smtp_send_command(handle, "EHLO ", hname, &handle->cap[1], 63);

        if (num > 0) {
            handle->capidx += num;
            handle->useragent = strdup(SMTP_USER_AGENT);
            handle->mimeversion = strdup("1.0");

            if( strlen(user)>0 && strlen(pwd)>0 ) {
                char b64user[255];
                char b64pwd[255];
                base64encode(user, strlen(user), b64user, 255);
                base64encode(pwd, strlen(pwd), b64pwd, 255);
                // Now login
                char *login = "auth login";
                struct smtp_reply * r[1];
                num = _smtp_send_command(handle, login, "", r, 1);
                if (num > 0 && r[0]->status == 334) {
                    _free_smtplist(r, 1);
                    num = _smtp_send_command(handle, b64user, "", r, 1);
                    if (num > 0 && r[0]->status == 334) {
                        _free_smtplist(r, 1);
                        num = _smtp_send_command(handle, b64pwd, "", r, 1);
                        if (num > 0 && r[0]->status != 235) {
                            _free_smtplist(r, 1);
                            _free_smtp_handle(&handle);
                            return NULL;
                        }
                        _free_smtplist(r, 1);
                    }
                }
            }

        } else {
            smtp_cleanup(&handle);
        }
    }
    return handle;
}


/**
 * This is the opposite to smtp_setup. This will cleanup all used memrot by the
 * amtp session
 * @param handle
 */
void
smtp_cleanup(struct smtp_handle **handle) {

    shutdown((*handle)->sfd, SHUT_RDWR);
    close((*handle)->sfd);
    _free_smtp_handle(handle);
    *handle = NULL;
}

/**
 * Utility function to send basic HTML or plain message through the specified SMTP server
 * @param server
 * @param user
 * @param pwd
 * @param subject
 * @param from
 * @param to
 * @param cc
 * @param message
 * @param isHTML
 * @return 0 on success, -1 on failure
 */
int
smtp_simple_sendmail(char *server, char *user, char *pwd,
                     char * subject, 
                     char * from, char *to, char *cc,
                     char *message, unsigned isHTML) {
    struct smtp_handle *handle;
    if ((handle = smtp_setup(server, user, pwd))) {
        int rc = -1;
        if (-1 != smtp_add_rcpt(handle, SMTP_RCPT_TO, to)) {
            if( cc && *cc ) {
                if (-1 != smtp_add_rcpt(handle, SMTP_RCPT_CC, cc)) {
                    smtp_cleanup(&handle);
                    return -1;
                }
            }
            if (isHTML) {
                smtp_add_html(handle, message, NULL);
            } else {
                smtp_add_plain(handle, message);
            }
            rc = smtp_sendmail(handle, from, subject);
        }
        smtp_cleanup(&handle);
        return 0;
    } else {
        return -1;
    }

}