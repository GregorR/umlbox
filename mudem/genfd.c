/*
 * Copyright (C) 2011 Gregor Richards
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define _POSIX_SOURCE /* for strtok_r */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "muxsocket.h"
#include "muxstdio.h"

/* types */
typedef struct _SocketGenFDC SocketGenFDC;
typedef struct _SocketGenFD SocketGenFD;

struct _SocketGenFDC {
    Socket ssuper;
    int infd, outfd;
};

struct _SocketGenFD {
    SocketWritable ssuper;
    int infd;
};

/* vtbl for GenFDC */
static Socket *genfdcConnect(Socket *self);

static SocketVTbl genfdcVTbl = {
    NULL, genfdcConnect, NULL, NULL, NULL, NULL
};

/* vtbl for GenFD */
void socketGenFDShouldSelect(Socket *self, int *r, int *w);

static SocketVTbl genfdVTbl = {
    socketWritableDestruct, NULL, socketGenFDShouldSelect, socketSelectedR,
    socketWritableSelectedW, socketWritableWrite
};

/* GenFDC nameable */
static Socket *newGenFDC(char **saveptr);
static NameableSocket genfdcN = {
    NULL, "genfd", newGenFDC
};


/* connection function for GenFDC */
static Socket *genfdcConnect(Socket *self)
{
    SocketGenFDC *sockc = (SocketGenFDC *) self;
    SocketGenFD *ret;
    int fd, tmpi;

    /* make the return */
    ret = (SocketGenFD *) newSocket(sizeof(SocketGenFD));
    ret->ssuper.ssuper.vtbl = &genfdVTbl;
    newSocketWritable(&ret->ssuper, sockc->outfd);
    ret->infd = sockc->infd;

    return (Socket *) ret;
}

/* create a new named GenFDC */
static Socket *newGenFDC(char **saveptr)
{
    SocketGenFDC *ret;
    char *ins, *outs;
    int infd, outfd;
    int tmpi;

    /* get the in and out FDs */
    ins = strtok_r(NULL, ":", saveptr);
    if (ins == NULL) return NULL;
    outs = strtok_r(NULL, "", saveptr);
    if (outs == NULL) return NULL;

    /* make the return */
    ret = (SocketGenFDC *) newSocket(sizeof(SocketGenFDC));
    ret->ssuper.vtbl = &genfdcVTbl;
    ret->infd = atoi(ins);
    ret->outfd = atoi(ins);

    return (Socket *) ret;
}

/* selection for genfds */
void socketGenFDShouldSelect(Socket *self, int *r, int *w)
{
    socketWritableShouldSelect(self, r, w);
    *r = ((SocketGenFD *) self)->infd;
}

/* initializer for this whole mess */
void initGenFD()
{
    registerNameableSocket(&genfdcN);
}
