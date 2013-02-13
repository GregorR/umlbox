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

#define _BSD_SOURCE /* for strdup */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "muxsocket.h"

#include "genfd.h"
#include "tcp4.h"
#include "unix.h"

void mapSet(struct Buffer_int *buf, int from, int to)
{
    while (buf->bufsz <= from) EXPAND_BUFFER(*buf);
    for (; buf->bufused <= from; buf->bufused++)
        buf->buf[buf->bufused] = -1;
    buf->buf[from] = to;
}

int main(int argc, char **argv)
{
    int preferredId, i, tmpi;
    struct Buffer_int readMap, writeMap; /* maps of fd -> id */
    fd_set readfds, writefds;
    int nfds, nsocks, r, w;
    Socket *sock;
    char ocbuf;

    if (argc < 2 || !argv[1][0] || argv[1][1]) {
        fprintf(stderr, "Use: umlbox-mudem {0|1} [sockets...]\n");
        return 1;
    }

    preferredId = atoi(argv[1]);

    /* initialize everything */
    initSockets(preferredId);
    initGenFD();
    initTCP4();
    initUNIX();

    /* perform our handshake (A->B->C) */
    if (preferredId == 1) {
        struct timeval timeout;

        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(0, &readfds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 4000; /* arbitrary */

        /* start by spamming 'A's */
        while (1) {
            write(1, "A", 1);
            SF(tmpi, select, -1, (1, &readfds, NULL, NULL, &timeout));
            if (tmpi) {
                ocbuf = '\0';
                read(0, &ocbuf, 1);
                if (ocbuf == 'B') break;
            }
        }

        /* then finalize with a 'C' */
        write(1, "C", 1);

    } else {
        /* start by waiting for an 'A' */
        while (1) {
            ocbuf = '\0';
            read(0, &ocbuf, 1);
            if (ocbuf == 'A') break;
        }

        /* then tell them we're here */
        write(1, "B", 1);

        /* and wait for the 'C' that ends the handshake */
        while (1) {
            ocbuf = '\0';
            read(0, &ocbuf, 1);
            if (ocbuf == 'C') break;
        }

    }

    /* now create every socket */
    for (i = 2; i < argc; i++) {
        Socket *sock;
        char *arg;

        SF(arg, strdup, NULL, (argv[i]));
        sock = socketByName(arg);

        if (sock == NULL) {
            fprintf(stderr, "Invalid socket %s.\n", argv[i]);
            exit(1);
        }

        registerSocket(sock, &i);
    }

    INIT_BUFFER(readMap);
    INIT_BUFFER(writeMap);

    /* and go into our select loop */
    while (1) {
        readMap.bufused = 0;
        writeMap.bufused = 0;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        nfds = 0;
        nsocks = socketCount();

        /* detect every socket */
        for (i = 0; i < nsocks; i++) {
            sock = socketById(i);
            if (sock && sock->vtbl->shouldSelect) {
                sock->vtbl->shouldSelect(sock, &r, &w);
                if (r >= 0) {
                    mapSet(&readMap, r, i);
                    if (r >= nfds) nfds = r + 1;
                    FD_SET(r, &readfds);
                }
                if (w >= 0) {
                    mapSet(&writeMap, w, i);
                    if (w >= nfds) nfds = w + 1;
                    FD_SET(w, &writefds);
                }
            }
        }

        /* then select them */
        SF(tmpi, select, -1, (nfds, &readfds, &writefds, NULL, NULL));

        /* now perform actions */
        for (i = 0; i < nfds; i++) {
            if (FD_ISSET(i, &readfds)) {
                sock = socketById(readMap.buf[i]);
                if (sock && sock->vtbl->selectedR(sock, i) != 0) {
                    freeSocket(sock);
                }
            }
            if (FD_ISSET(i, &writefds)) {
                sock = socketById(writeMap.buf[i]);
                if (sock && sock->vtbl->selectedW(sock, i) != 0) {
                    freeSocket(sock);
                }
            }
        }
    }

    return 0;
}
