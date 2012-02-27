/* =========================================================================
 * File:        MAILCLIENTLIB.H
 * Description: A simple library to provide interface to a SMTP server
 *              and make it easy to send mails. The library supports both
 *              handling of attachment, plain and HTML mail as well as
 *              inline images within the HTML code.
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

#ifndef MAILCLIENTLIB_H
#define	MAILCLIENTLIB_H

#ifdef	__cplusplus
extern "C" {
#endif

/* User agent string used */
#define SMTP_USER_AGENT "tvpvrd mailer 1.0"

/* Maximum number of recepients in one mail */
#define MAX_RCPT 100

/* Maximum number of attachments in one mail*/
#define MAX_ATTACHMENTS 50

/* Possible features of the server */
#define SMTP_SERVER_FEATURE_PIPELINING 0
#define SMTP_SERVER_FEATURE_8BITMIME 1
#define SMTP_SERVER_FEATURE_AUTH_PLAIN_LOGIN 2
#define SMTP_SERVER_FEATURE_VERIFY 3
#define SMTP_SERVER_FEATURE_ETRN 4
#define SMTP_SERVER_FEATURE_ENHANCEDSTATUS 5
#define SMTP_SERVER_FEATURE_DSN 6

/* Supported types fo attachments */
#define SMTP_ATTACH_CONTENT_TYPE_PLAIN 0
#define SMTP_ATTACH_CONTENT_TYPE_HTML 1
#define SMTP_ATTACH_CONTENT_TYPE_PNG 2
#define SMTP_ATTACH_CONTENT_TYPE_JPG 3
#define SMTP_ATTACH_CONTENT_TYPE_GIF 4
#define SMTP_ATTACH_CONTENT_TYPE_OCTET 5
#define SMTP_ATTACH_CONTENT_TYPE_PDF 6

#define SMTP_CONTENT_TRANSFER_ENCODING_8BIT 0
#define SMTP_CONTENT_TRANSFER_ENCODING_BASE64 1
#define SMTP_CONTENT_TRANSFER_ENCODING_QUOTEDPRINT 2

/* Type of recepients of mail */
#define SMTP_RCPT_TO 1
#define SMTP_RCPT_CC 2
#define SMTP_RCPT_BCC 3

/* Maximum size fo each recipient line */
#define MAX_HEADER_ADDR_SIZE 2048

/*
 * Structure to hold the reply from SMTP server for a specific command
 */
struct smtp_reply {
    int status;
    char *str;
};

/**
 * Hold data about one single attachment
 */
struct smtp_attachment {
    char *data;
    char *contenttype;
    char *contenttransferencoding;
    char *contentdisposition;
    char *filename;
    char *name;
    char *cid;
};


/**
 * Structures to hold the state of the SMTP connection as well as storing vital information
 * and data that will be used when formatting the mail to be sent.
 */
struct smtp_handle {
    int sfd;                                             // Socked fledescriptor for connection to server
    size_t capidx;                                       // Capability index into cap array
    struct smtp_reply * cap[64];                         // Capabilityies of the server
    char *subject;                                       // Subject of mail
    char *to[MAX_RCPT];                                  // All "To:" recipients
    char *to_concatenated;                               // All "To:" recipients as one string
    size_t toidx;                                        // Index into to array
    char *cc[MAX_RCPT];
    char *cc_concatenated;
    size_t ccidx;
    char *bcc[MAX_RCPT];
    char *bcc_concatenated;
    size_t bccidx;
    char *from;                                           // What Envelope From address to use
    char *returnpath; // Return-Path:                     // Explicit return path
    char *mimeversion; // MIME-Version:                   // MIME Version used (always 1.0)
    struct smtp_attachment * attachment[MAX_ATTACHMENTS]; // Array to hold all attachments in mail
    size_t attachmentidx;                                 // Index into attachment array
    char *html;                                           // HTML data for mail
    char *plain;                                          // Plain text data for mail
    char *useragent; // User-Agent:                       // User agent string used in mail
    char *contenttype;                                    // Content type header (for envelope)
    char *contenttransferencoding;                        // Encoding header (for envelope)
    char *databuff;                                       // The overall data buffer that will hold a complete
                                                          // data sectin ofthe mail all texts, all atatchment etc.
                                                          // This is what is actually sent to the SMTP server as
                                                          // the payload of the mail.
};



/**
 * Add a receipent of the mail. Use this to add either To, Cc or Bcc
 * @param handle
 * @param type
 * @param rcpt
 * @return 0 on success, -1 on failure
 */
int
smtp_add_rcpt(struct smtp_handle *handle, unsigned type, char *rcpt);

/**
 * Add plain message text to the mail. If this is used then smtp_add_html()
 * cannot be used. Only one of them can be used per mail.
 * @param handle
 * @param buffer
 * @return 0 on success, -1 on failure
 */
int
smtp_add_plain(struct smtp_handle *handle, char *buffer);

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
smtp_add_html(struct smtp_handle *handle, char *buffer, char *altbuffer);

/**
 * Add attachment to mail and use specified filename and name. The attachment type and
 * encoding is the responsibility of the caller.
 * @param handle
 * @param filename
 * @param name
 * @param data
 * @param len
 * @param contenttype
 * @param encoding
 * @return
 */
int
smtp_add_attachment(struct smtp_handle *handle, char *filename, char *name, char *data, size_t len,
                    unsigned contenttype, unsigned encoding);

/**
 * Add attachment from a fully qualified file name
 * @param handle
 * @param filename
 * @return 0 on success, -1 on failure
 */
int
smtp_add_attachment_fromfile(struct smtp_handle *handle, char *filename, unsigned contenttype, unsigned encoding);

/**
 * Adds an attachment in the mail that holds one inline image that is referencd in the HTML
 * section of the mail as <img src="cid:XX"> where XX is the cid specified as the last argument
 * to the function. This should be a unique ID within the mail. The contentype will be set
 * according to the file name extension (e.g. "*.png" => image/png and so on)
 * @param handle
 * @param filename
 * @param cid
 * @return 0 on success, -1 on failure
 */
int
smtp_add_attachment_inlineimage(struct smtp_handle *handle, char *filename, char *cid);

/**
 * Send the mail
 * @param handle
 * @param from
 * @param subject
 * @return 0 on success, -1 on failure
 */
int
smtp_sendmail(struct smtp_handle *handle, char *from, char *subject);

/**
 * Check what feature the SMTP server supports
 * @param handle
 * @param feature to check for
 * @return 1 if feature is supoprted, 0 if not
 */
int
smtp_server_support(struct smtp_handle *handle, size_t feature);

/**
 * Initialize a new connection to the SMTP server. The code assumed that the
 * SMTP server requres login
 * @param server_ip Server IP och fully qualified name
 * @param user User name to login to with server
 * @param pwd Password
 * @return NULL on failure, handle to mail object if success
 */
struct smtp_handle *
smtp_setup(char *server_ip, char *user, char *pwd);

/**
 * This is the opposite to smtp_setup. This will cleanup all used memrot by the
 * amtp session
 * @param handle
 */
void
smtp_cleanup(struct smtp_handle **handle);

/**
 * Debug print routine. Dump all information about an SMTP session
 * @param handle
 */
void
smtp_dump_handle(struct smtp_handle * handle, FILE *fp);

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
                     char *message, unsigned isHTML);

/**
 * Utility function to send basic HTML or plain message with an attachment from a file 
 * through the specified SMTP server
 * @param server
 * @param user
 * @param pwd
 * @param subject
 * @param from
 * @param to
 * @param cc
 * @param message
 * @param isHTML
 * @param filename
 * @param contenttype if speciried as -1 then assume plain type
 * @param encoding if specified as -1 then assume quoted printables
 * @return 
 */
int
smtp_simple_sendmail_with_fileattachment(char *server, char *user, char *pwd,
                     char * subject,
                     char * from, char *to, char *cc,
                     char *message, unsigned isHTML,
                     char *filename, int contenttype, int encoding);

#ifdef	__cplusplus
}
#endif

#endif	/* MAILCLIENTLIB_H */

