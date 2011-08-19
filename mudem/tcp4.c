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

#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/ip.h>

#include "muxsocket.h"
#include "muxstdio.h"

/* types */
typedef struct _SocketTCP4L SocketTCP4L;
typedef struct _SocketTCP4C SocketTCP4C;
typedef SocketWritable SocketTCP4;

struct _SocketTCP4L {
    Socket ssuper;
    int fd;
};

struct _SocketTCP4C {
    Socket ssuper;
    struct sockaddr *addr;
    size_t addrlen;
};

/* vtbl for TCP4L */
static void tcp4lDestruct(Socket *self);
static void tcp4lShouldSelect(Socket *self, int *r, int *w);
static int tcp4lSelectedR(Socket *self, int fd);

static SocketVTbl tcp4lVTbl = {
    tcp4lDestruct, NULL, tcp4lShouldSelect, tcp4lSelectedR, NULL, NULL
};

/* vtbl for TCP4C */
static Socket *tcp4cConnect(Socket *self);

static SocketVTbl tcp4cVTbl = {
    NULL, tcp4cConnect, NULL, NULL, NULL, NULL
};

/* vtbl for TCP4 */
static SocketVTbl tcp4VTbl = {
    socketWritableDestruct, NULL, socketWritableShouldSelectR, socketSelectedR,
    socketWritableSelectedW, socketWritableWrite
};

/* TCP4L nameable */
static Socket *newTCP4L(char **saveptr);
static NameableSocket tcp4lN = {
    NULL, "tcp4-listen", newTCP4L
};

/* TCP4C nameable */
static Socket *newTCP4C(char **saveptr);
static NameableSocket tcp4cN = {
    NULL, "tcp4", newTCP4C
};


/* destructor for TCP4L/C */
static void tcp4lDestruct(Socket *self)
{
    close(((SocketTCP4L *) self)->fd);
}

/* select() for TCP4L */
static void tcp4lShouldSelect(Socket *self, int *r, int *w)
{
    *r = ((SocketTCP4L *) self)->fd;
    *w = -1;
}

/* accept a TCP4 connection */
static int tcp4lSelectedR(Socket *self, int fd)
{
    SocketTCP4 *tcp4;
    int newfd, id;
    unsigned char idbuf[4];

    /* accept it */
    newfd = accept(fd, NULL, NULL);
    if (newfd < 0) return 0;

    /* then make the return */
    tcp4 = (SocketTCP4 *) newSocket(sizeof(SocketTCP4));
    newSocketWritable(tcp4, newfd);
    tcp4->ssuper.vtbl = &tcp4VTbl;

    /* register it */
    id = registerSocket((Socket *) tcp4, NULL);

    /* then tell the other side */
    muxCommand(stdoutSocket, 'c', self->id);
    muxPrepareInt(idbuf, id);
    stdoutSocket->vtbl->write(stdoutSocket, idbuf, 4);

    return 0;
}

/* connection function for TCP4C */
static Socket *tcp4cConnect(Socket *self)
{
    SocketTCP4C *sockc = (SocketTCP4C *) self;
    SocketTCP4 *ret;
    int fd, tmpi;

    /* make the socket */
    SF(fd, socket, -1, (AF_INET, SOCK_STREAM, 0));

    tmpi = connect(fd, sockc->addr, sockc->addrlen);
    if (tmpi < 0) {
        close(fd);
        return NULL;
    }

    /* then make the return */
    ret = (SocketTCP4 *) newSocket(sizeof(SocketTCP4));
    newSocketWritable(ret, fd);
    ret->ssuper.vtbl = &tcp4VTbl;

    return (Socket *) ret;
}

/* create a new named TCP4L */
static Socket *newTCP4L(char **saveptr)
{
    SocketTCP4L *ret;
    char *ports;
    int port, fd, tmpi;
    struct sockaddr_in sin;

    /* get the port */
    ports = strtok_r(NULL, "", saveptr);
    if (ports == NULL) return NULL;
    port = atoi(ports);

    /* make the socket */
    SF(fd, socket, -1, (AF_INET, SOCK_STREAM, 0));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = 0;
    SF(tmpi, bind, -1, (fd, (struct sockaddr *) &sin, sizeof(sin)));

    /* and set it up to listen */
    SF(tmpi, listen, -1, (fd, 32));

    /* then make the return */
    ret = (SocketTCP4L *) newSocket(sizeof(SocketTCP4L));
    ret->ssuper.vtbl = &tcp4lVTbl;
    ret->fd = fd;

    return (Socket *) ret;
}

/* create a new named TCP4C */
static Socket *newTCP4C(char **saveptr)
{
    SocketTCP4C *ret;
    char *hosts, *ports;
    struct addrinfo hints, *ai;
    int tmpi;

    /* get the host and port */
    hosts = strtok_r(NULL, ":", saveptr);
    if (hosts == NULL) return NULL;
    ports = strtok_r(NULL, "", saveptr);
    if (ports == NULL) return NULL;

    /* make the return */
    ret = (SocketTCP4C *) newSocket(sizeof(SocketTCP4C));
    ret->ssuper.vtbl = &tcp4cVTbl;

    /* get the host */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    tmpi = getaddrinfo(hosts, ports, &hints, &ai);
    if (tmpi != 0)
        return NULL;
    ret->addr = ai->ai_addr;
    ret->addrlen = ai->ai_addrlen;

    return (Socket *) ret;
}

/* initializer for this whole mess */
void initTCP4()
{
    registerNameableSocket(&tcp4lN);
    registerNameableSocket(&tcp4cN);
}
