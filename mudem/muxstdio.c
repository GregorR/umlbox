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

#include <stdio.h>
#include <stdlib.h>

#include "muxstdio.h"

/* put an int into a char[4] */
void muxPrepareInt(unsigned char *buf, int32_t i)
{
    buf[0] = i >> 24;
    buf[1] = (i >> 16) & 0xFF;
    buf[2] = (i >> 8) & 0xFF;
    buf[3] = i & 0xFF;
}

/* helpers */
void muxCommand(Socket *sock, char command, int32_t i)
{
    unsigned char buf[5];

    buf[0] = command;
    muxPrepareInt(buf + 1, i);

    if (sock->vtbl->write)
        sock->vtbl->write(sock, buf, 5);
}

static ssize_t readAll(int fd, void *buf, size_t count)
{
    ssize_t rd, ird;
    rd = 0;
    while (rd < count) {
        ird = read(fd, (char *) buf + rd, count - rd);
        if (ird <= 0)
            return -1;
        rd += ird;
    }
    return rd;
}

static int32_t getint()
{
    int32_t val;
    unsigned char c;
    int i;

    val = 0;
    for (i = 0; i < 4; i++) {
        val <<= 8;
        readAll(0, &c, 1);
        val |= c;
    }

    return val;
}

/* vtbl for stdin: */
static void stdinShouldSelect(Socket *self, int *r, int *w);
static int stdinSelectedR(Socket *self, int fd);

static SocketVTbl stdinVTbl = {
    NULL, NULL, stdinShouldSelect, stdinSelectedR, NULL, NULL
};

/* vtbl for stdout: */
static SocketVTbl stdoutVTbl = {
    socketWritableDestruct, NULL, socketWritableShouldSelect, NULL,
    socketWritableSelectedW, socketWritableWrite
};

/* stdin */
Socket *newStdinSocket()
{
    Socket *ret = newSocket(sizeof(Socket));
    ret->vtbl = &stdinVTbl;
    return ret;
}

static void stdinShouldSelect(Socket *self, int *r, int *w)
{
    *r = 0;
    *w = -1;
}

static int stdinSelectedR(Socket *self, int fd)
{
    char command;
    int id, cid;
    ssize_t ct;
    Socket *sock, *csock;
    char *buf;

    if (readAll(0, &command, 1) != 1) {
        fprintf(stderr, "Critical error! Lost stdin!\n");
        return 1;
    }
    id = getint();
    sock = socketById(id);
    if (sock == NULL) return 0;

    switch (command) {
        case 'c':
            cid = getint();
            if (!sock->vtbl->connect) {
                fprintf(stderr, "Received a connection request to unconnectable socket %d!\n", id);
                return 0;
            }
            csock = sock->vtbl->connect(sock);
            if (!csock) {
                fprintf(stderr, "Failed to connect to socket %d.\n", id);
                muxCommand(stdoutSocket, 'd', cid);
                return 0;
            }
            registerSocket(csock, &cid);
            break;

        case 'd':
            freeSocket(sock);
            break;

        case 's':
            ct = (size_t) getint();
            SF(buf, malloc, NULL, (ct));
            /* read it in */
            if (readAll(0, buf, ct) != ct) {
                free(buf);
                muxCommand(stdoutSocket, 'd', id);
                fprintf(stderr, "Short send!\n");
                return 0;
            }
            if (!sock->vtbl->write) {
                free(buf);
                muxCommand(stdoutSocket, 'd', id);
                fprintf(stderr, "Send to unwritable socket %d!\n", id);
                return 0;
            }
            sock->vtbl->write(sock, buf, ct);
            free(buf);
            break;

        default:
            fprintf(stderr, "Critical error! Unrecognized command %d!\n", (int) command);
            return 1;
    }

    return 0;
}

/* stdout */
Socket *newStdoutSocket()
{
    Socket *ret;
    ret = newSocket(sizeof(SocketWritable));
    newSocketWritable((SocketWritable *) ret, 1);
    ret->vtbl = &stdoutVTbl;
    return ret;
}
