/* =========================================================================
 * File:        RKEY.H
 * Description: Functions to do key/val pair substitution in buffers
 * Author:      Johan Persson (johan162@gmail.com)
 * SVN:         $Id$
 *
 * Copyright (C) 2009,2010,2011 Johan Persson
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


#ifndef RKEY_H
#define	RKEY_H

#ifdef	__cplusplus
extern "C" {
#endif

#define MAX_KEYPAIR_VAL_SIZE 1024

/*
 * Holds each keypair to be replaced in the buffer/file
 */
struct keypairs {
    char *key;
    char *val;
};

/**
 * Replace all ocurrences of each key sorrounded by "[]" with its value in
 * the buffer pointed to by buffer.
 * @param buffer
 * @param maxlen
 * @param keys
 * @return 0 on success, -1 on failure
 */
int
replace_keywords(char *buffer, size_t maxlen, struct keypairs keys[], size_t nkeys);

/**
 * Read a template from a file and replace all keywords with the keyvalues and store
 * the result in the buffer pointed to by buffer. It is the callers responsibility
 * to free the buffer after usage.
 * @param filename
 * @param buffer
 * @param keys
 * @param nkeys
 * @return 0 on success, -1 on failure
 */
int
replace_keywords_in_file(char *filename,char **buffer, struct keypairs keys[], size_t nkeys);

/**
 * Cretae a new keypair list to use when replacing template keys in mail
 * @param maxsize
 * @return
 */
struct keypairs *
new_keypairlist(size_t maxsize);

/**
 * Add a key/value pair ot a pre-created keypair list
 * @param keys
 * @param maxkeys
 * @param key
 * @param val
 * @param idx
 * @return -1 on failure, 0 on success
 */
int
add_keypair(struct keypairs *keys, size_t maxkeys, char *key, char *val, size_t *idx);

/**
 * Free a keypair list previously created with new_keypairlist()
 * @param keys
 * @param maxkeys
 * @return
 */
int
free_keypairlist(struct keypairs *keys, size_t maxkeys);


#ifdef	__cplusplus
}
#endif

#endif	/* RKEY_H */

