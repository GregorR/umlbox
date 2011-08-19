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

#ifndef MUXSOCKET_H
#define MUXSOCKET_H

#include <unistd.h>

#include "buffer.h"

typedef struct _SocketVTbl SocketVTbl;
typedef struct _Socket Socket;
typedef struct _SocketWritable SocketWritable;
typedef struct _NameableSocket NameableSocket;

BUFFER(Socket, Socket *);

/* vtbl for sockets */
struct _SocketVTbl {
    /* disconnect/destructor */
    void (*destruct)(Socket *self);

    /* connect to a connectable socket; returns a new socket ID */
    Socket *(*connect)(Socket *self);

    /* called to determine whether to select() over this socket */
    void (*shouldSelect)(Socket *self, int *r, int *w);

    /* called when this socket is selected for read (must be set only if
     * shouldSelect says so) */
    int (*selectedR)(Socket *self, int fd);

    /* called when this socket is selected for write (must be set only if
     * shouldSelect says so) */
    int (*selectedW)(Socket *self, int fd);

    /* write into buffer */
    void (*write)(Socket *self, const void *buf, size_t count);
};

/* base type for all sockets */
struct _Socket {
    size_t sz;
    SocketVTbl *vtbl;
    int id;
};

/* base type for buffered writable sockets */
struct _SocketWritable {
    Socket ssuper;
    int fd;
    struct Buffer_char wbuf;
};

/* a nameable socket type, for arg-specified sockets */
struct _NameableSocket {
    NameableSocket *next;
    const char *name;
    Socket *(*construct)(char **saveptr);
};

/* base constructor for all sockets */
Socket *newSocket(size_t sz);

/* super-constructor for writable sockets */
void newSocketWritable(SocketWritable *self, int fd);

/* generic destruct() for SocketWritable */
void socketWritableDestruct(Socket *self);

/* generic shouldSelect() for SocketWritable */
void socketWritableShouldSelect(Socket *self, int *r, int *w);

/* generic shouldSelect() for SocketWritables which are also readable */
void socketWritableShouldSelectR(Socket *self, int *r, int *w);

/* generic selectedR() for any socket that uses simple FDs */
int socketSelectedR(Socket *self, int fd);

/* generic selectedW() for SocketWritable */
int socketWritableSelectedW(Socket *self, int fd);

/* generic write() for SocketWritable */
void socketWritableWrite(Socket *self, const void *buf, size_t count);

/* call this when a socket receives data */
void socketRead(Socket *self, const void *buf, size_t count);

/* construct a socket by name */
Socket *socketByName(char *name);

/* get a socket by ID */
Socket *socketById(int id);

/* get the maximum socket ID + 1 */
int socketCount();

/* initialize the socket subsystem */
void initSockets(int preferredId);

/* register a new socket */
int registerSocket(Socket *socket, const int *forceId);

/* deregister and free a socket */
void freeSocket(Socket *socket);

/* register a nameable socket */
void registerNameableSocket(NameableSocket *ns);

/* major sockets */
extern Socket *stdinSocket, *stdoutSocket;

#endif
