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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "muxsocket.h"
#include "muxstdio.h"

/* types */
typedef struct _SocketUNIXL SocketUNIXL;
typedef struct _SocketUNIXC SocketUNIXC;
typedef SocketWritable SocketUNIX;

struct _SocketUNIXL {
    Socket ssuper;
    int fd;
};

struct _SocketUNIXC {
    Socket ssuper;
    struct sockaddr *addr;
    size_t addrlen;
};

/* vtbl for UNIXL */
static void unixlDestruct(Socket *self);
static void unixlShouldSelect(Socket *self, int *r, int *w);
static int unixlSelectedR(Socket *self, int fd);

static SocketVTbl unixlVTbl = {
    unixlDestruct, NULL, unixlShouldSelect, unixlSelectedR, NULL, NULL
};

/* vtbl for UNIXC */
static Socket *unixcConnect(Socket *self);

static SocketVTbl unixcVTbl = {
    NULL, unixcConnect, NULL, NULL, NULL, NULL
};

/* vtbl for UNIX */
static SocketVTbl unixVTbl = {
    socketWritableDestruct, NULL, socketWritableShouldSelectR, socketSelectedR,
    socketWritableSelectedW, socketWritableWrite
};

/* UNIXL nameable */
static Socket *newUNIXL(char **saveptr);
static NameableSocket unixlN = {
    NULL, "unix-listen", newUNIXL
};

/* UNIXC nameable */
static Socket *newUNIXC(char **saveptr);
static NameableSocket unixcN = {
    NULL, "unix", newUNIXC
};


/* destructor for UNIXL/C */
static void unixlDestruct(Socket *self)
{
    close(((SocketUNIXL *) self)->fd);
}

/* select() for UNIXL */
static void unixlShouldSelect(Socket *self, int *r, int *w)
{
    *r = ((SocketUNIXL *) self)->fd;
    *w = -1;
}

/* accept a UNIX connection */
static int unixlSelectedR(Socket *self, int fd)
{
    SocketUNIX *sock;
    int newfd, id;
    unsigned char idbuf[4];

    /* accept it */
    newfd = accept(fd, NULL, NULL);
    if (newfd < 0) return 0;

    /* then make the return */
    sock = (SocketUNIX *) newSocket(sizeof(SocketUNIX));
    newSocketWritable(sock, newfd);
    sock->ssuper.vtbl = &unixVTbl;

    /* register it */
    id = registerSocket((Socket *) sock, NULL);

    /* then tell the other side */
    muxCommand(stdoutSocket, 'c', self->id);
    muxPrepareInt(idbuf, id);
    stdoutSocket->vtbl->write(stdoutSocket, idbuf, 4);

    return 0;
}

/* connection function for UNIXC */
static Socket *unixcConnect(Socket *self)
{
    SocketUNIXC *sockc = (SocketUNIXC *) self;
    SocketUNIX *ret;
    int fd, tmpi;

    /* make the socket */
    SF(fd, socket, -1, (AF_UNIX, SOCK_STREAM, 0));

    tmpi = connect(fd, sockc->addr, sockc->addrlen);
    if (tmpi < 0) {
        close(fd);
        return NULL;
    }

    /* then make the return */
    ret = (SocketUNIX *) newSocket(sizeof(SocketUNIX));
    newSocketWritable(ret, fd);
    ret->ssuper.vtbl = &unixVTbl;

    return (Socket *) ret;
}

/* create a new named UNIXL */
static Socket *newUNIXL(char **saveptr)
{
    SocketUNIXL *ret;
    char *path;
    int fd, tmpi;
    struct sockaddr_un sun;

    /* get the path */
    path = strtok_r(NULL, "", saveptr);

    /* make the socket */
    SF(fd, socket, -1, (AF_UNIX, SOCK_STREAM, 0));
    sun.sun_family = AF_UNIX;
    strncpy(sun.sun_path, path, sizeof(sun.sun_path));
    SF(tmpi, bind, -1, (fd, (struct sockaddr *) &sun, sizeof(sun)));

    /* and set it up to listen */
    SF(tmpi, listen, -1, (fd, 32));

    /* then make the return */
    ret = (SocketUNIXL *) newSocket(sizeof(SocketUNIXL));
    ret->ssuper.vtbl = &unixlVTbl;
    ret->fd = fd;

    return (Socket *) ret;
}

/* create a new named UNIXC */
static Socket *newUNIXC(char **saveptr)
{
    SocketUNIXC *ret;
    char *path;
    struct sockaddr_un *sun;

    /* get the path */
    path = strtok_r(NULL, "", saveptr);

    /* make the return */
    ret = (SocketUNIXC *) newSocket(sizeof(SocketUNIXC));
    ret->ssuper.vtbl = &unixcVTbl;

    SF(sun, malloc, NULL, (sizeof(struct sockaddr_un)));
    sun->sun_family = AF_UNIX;
    strncpy(sun->sun_path, path, sizeof(sun->sun_path));
    ret->addr = (struct sockaddr *) sun;
    ret->addrlen = sizeof(*sun);

    return (Socket *) ret;
}

/* initializer for this whole mess */
void initUNIX()
{
    registerNameableSocket(&unixlN);
    registerNameableSocket(&unixcN);
}
