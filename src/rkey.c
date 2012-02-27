/* =========================================================================
 * File:        RKEY.C
 * Description: Functions to do key/val pair substitution in buffers
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "rkey.h"

#define MIN(a,b) (a) < (b) ? (a) : (b)

/**
 * Replace all ocurrences of each key sorrounded by "[]" with its value in
 * the buffer pointed to by buffer.
 * @param buffer
 * @param maxlen
 * @param keys
 * @return 0 on success, -1 on failure
 */
int
replace_keywords(char *buffer, size_t maxlen, struct keypairs keys[], size_t nkeys) {

    size_t const N = strlen(buffer)+MAX_KEYPAIR_VAL_SIZE*nkeys;
    char match[255];
    char *wbuff = calloc(1,N);
    char *pwbuff = wbuff;
    char *pbuff = buffer;
    size_t n=0;

    while( n < N && *pbuff ) {
        if( *pbuff == '[') {
            char *ppbuff = pbuff;
            size_t nn = 0 ;
            ++ppbuff;
            while( nn < 255 && *ppbuff && *ppbuff != ']') {
                match[nn++] = *ppbuff++;
            }
            match[nn] = '\0';
            if( *ppbuff == ']' ) {

                size_t i=0;
                while( i < nkeys && strcasecmp(match,keys[i].key) ) {
                    i++;
                }
                if( i < nkeys ) {
                    // We got a match
                    nn = MIN(MAX_KEYPAIR_VAL_SIZE,strlen(keys[i].val));
                    size_t j=0;
                    while( nn > 0 ) {
                        *pwbuff++ = keys[i].val[j++];
                        nn--;
                    }
                    pbuff = ++ppbuff;
                } else {
                    // No match
                    *pwbuff++ = *pbuff++;
                    n++;
                }

            } else {
                *pwbuff++ = *pbuff++;
                n++;
            }
        } else {
            *pwbuff++ = *pbuff++;
            n++;
        }
    }

    if (maxlen > strlen(wbuff)) {
        strcpy(buffer, wbuff);
        free(wbuff);
        return 0;
    } else {
        free(wbuff);
        return -1;
    }
}

/**
 * Read a template from a file and replace all keywords with the keyvalues and store
 * the sult in the buffer pointed to by buffer
 * @param filename
 * @param buffer
 * @param keys
 * @param nkeys
 * @return 0 on success, -1 on failure
 */
int
replace_keywords_in_file(char *filename,char **buffer, struct keypairs keys[], size_t nkeys) {

    FILE *fp;
    *buffer = NULL;
    if( (fp=fopen(filename,"rb"))==NULL ) {
        return -1;
    }
    int fd = fileno(fp);
    struct stat buf;
    fstat(fd, &buf);
    size_t const N = buf.st_size + MAX_KEYPAIR_VAL_SIZE*nkeys + 1;
    *buffer = calloc(1,N);
    size_t readsize = fread(*buffer,sizeof(char), buf.st_size, fp);
    if( readsize != (size_t)buf.st_size ) {
        fclose(fp);
        free(*buffer);
        *buffer = NULL;
        return -1;
    }
    fclose(fp);
    return replace_keywords(*buffer, N, keys, nkeys);
}

struct keypairs *
new_keypairlist(size_t maxsize) {
    return calloc(maxsize,sizeof(struct keypairs));
}

int
add_keypair(struct keypairs *keys, size_t maxkeys, char *key, char *val, size_t *idx) {
    if( *idx >= maxkeys ) {
        return -1;
    } else {
        keys[*idx].key = strdup(key);
        keys[*idx].val = strdup(val);
        (*idx)++;
        return 0;
    }
}

int
free_keypairlist(struct keypairs *keys, size_t maxkeys) {
    if( keys == NULL )
        return -1;
    for( size_t i=0; i < maxkeys; ++i) {
        if( keys[i].key ) {
            free(keys[i].key);
        }
        if( keys[i].val ) {
            free(keys[i].val);
        }
    }
    free(keys);
    return 0;
}
