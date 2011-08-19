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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "muxsocket.h"
#include "muxstdio.h"

/* NULL vtbl */
static SocketVTbl nullVTbl = {
    NULL, NULL, NULL, NULL, NULL, NULL
};

/* array of all current sockets */
static struct Buffer_Socket sockets;

/* and nameables */
static NameableSocket *nameableSockets;

/* and major sockets */
Socket *stdinSocket, *stdoutSocket;

/* preferred ID offset */
static int socketPreferredId;

/* base constructor for all sockets */
Socket *newSocket(size_t sz)
{
    Socket *ret;
    SF(ret, malloc, NULL, (sz));
    ret->sz = sz;
    ret->vtbl = &nullVTbl;
    ret->id = -1;
    return ret;
}

/* super-constructor for writable sockets */
void newSocketWritable(SocketWritable *self, int fd)
{
    int flags, tmpi;

    /* fd needs to be non-blocking */
    SF(flags, fcntl, -1, (fd, F_GETFL, 0));
    SF(tmpi, fcntl, -1, (fd, F_SETFL, flags | O_NONBLOCK));

    self->fd = fd;
    INIT_BUFFER(self->wbuf);
}

/* generic destruct() for SocketWritable */
void socketWritableDestruct(Socket *self)
{
    close(((SocketWritable *) self)->fd);
    FREE_BUFFER(((SocketWritable *) self)->wbuf);
}

/* generic shouldSelect() for SocketWritable */
void socketWritableShouldSelect(Socket *self, int *r, int *w)
{
    SocketWritable *sockw = (SocketWritable *) self;

    *r = -1;
    if (sockw->wbuf.bufused > 0) {
        *w = ((SocketWritable *) self)->fd;
    } else {
        *w = -1;
    }
}

/* generic shouldSelect() for SocketWritables which are also readable */
void socketWritableShouldSelectR(Socket *self, int *r, int *w)
{
    socketWritableShouldSelect(self, r, w);
    *r = ((SocketWritable *) self)->fd;
}

/* generic selectedR() for any socket that uses simple FDs */
int socketSelectedR(Socket *self, int fd)
{
    char buf[1024];
    ssize_t rd;

    /* try to read */
    rd = read(fd, buf, 1024);

    if (rd <= 0) {
        /* BAD! */
        return 1;
    }

    /* say we read it */
    socketRead(self, buf, rd);

    return 0;
}

/* generic selectedW() for SocketWritable */
int socketWritableSelectedW(Socket *self, int fd)
{
    ssize_t wrote;
    SocketWritable *sockw = (SocketWritable *) self;

    /* write as much of the buffer as we can */
    wrote = write(fd, sockw->wbuf.buf, sockw->wbuf.bufused);

    if (wrote < 0) {
        /* BAD! */
        return 1;
    }

    /* move down the remainder */
    memmove(sockw->wbuf.buf, sockw->wbuf.buf + wrote, sockw->wbuf.bufused - wrote);
    sockw->wbuf.bufused -= wrote;

    return 0;
}

/* generic write() for SocketWritable */
void socketWritableWrite(Socket *self, const void *buf, size_t count)
{
    SocketWritable *sockw = (SocketWritable *) self;
    WRITE_BUFFER(sockw->wbuf, buf, count);
}

/* call this when a socket receives data */
void socketRead(Socket *self, const void *buf, size_t count)
{
    unsigned char szbuf[4];

    /* write our send command */
    muxCommand(stdoutSocket, 's', self->id);

    /* and the data */
    muxPrepareInt(szbuf, (int32_t) count);
    stdoutSocket->vtbl->write(stdoutSocket, szbuf, 4);
    stdoutSocket->vtbl->write(stdoutSocket, buf, count);
}

/* construct a socket by name */
Socket *socketByName(char *namePlus)
{
    char *name, *saveptr;
    NameableSocket *ns;

    /* get out the name part */
    name = strtok_r(namePlus, ":", &saveptr);

    /* try to find it */
    for (ns = nameableSockets; ns; ns = ns->next) {
        if (!strcmp(ns->name, name)) {
            /* got it! */
            return ns->construct(&saveptr);
        }
    }

    return NULL;
}

/* get a socket by ID */
Socket *socketById(int id)
{
    if (id < 0 || id >= sockets.bufused) return NULL;
    return sockets.buf[id];
}

/* get the maximum socket ID + 1 */
int socketCount()
{
    return sockets.bufused;
}

/* initialize the socket subsystem */
void initSockets(int preferredId)
{
    int forceId;
    INIT_BUFFER(sockets);

    socketPreferredId = preferredId;
    nameableSockets = NULL;

    /* register stdin and stdout */
    forceId = 0;
    registerSocket(stdinSocket = newStdinSocket(), &forceId);
    forceId = 1;
    registerSocket(stdoutSocket = newStdoutSocket(), &forceId);
}

/* register a new socket */
int registerSocket(Socket *socket, const int *forceId)
{
    int id;

    if (forceId) {
        id = *forceId;
    } else {
        /* try to find a free ID */
        for (id = socketPreferredId; id < sockets.bufused && sockets.buf[id]; id += 2);
    }

    /* make sure we have the space */
    while (id >= sockets.bufsz) EXPAND_BUFFER(sockets);
    for (; sockets.bufused <= id; sockets.bufused++) {
        sockets.buf[sockets.bufused] = NULL;
    }

    /* kill anything that's already there */
    if (sockets.buf[id])
        freeSocket(sockets.buf[id]);

    /* then take it */
    sockets.buf[id] = socket;
    socket->id = id;

    return id;
}

/* deregister and free a socket */
void freeSocket(Socket *socket)
{
    /* destroy */
    if (socket->vtbl->destruct)
        socket->vtbl->destruct(socket);
    sockets.buf[socket->id] = NULL;

    /* then tell the other side */
    muxCommand(stdoutSocket, 'd', socket->id);

    free(socket);
}

/* register a nameable socket */
void registerNameableSocket(NameableSocket *ns)
{
    ns->next = nameableSockets;
    nameableSockets = ns;
}
