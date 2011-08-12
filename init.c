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

#define _BSD_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <linux/reboot.h>

#define SF(_o, _f, _b, _a) \
do { \
    (_o) = (_f) _a; \
    if ((_o) == (_b)) { \
        perror(#_f); \
        crash(); \
    } \
} while(0)

void mkdirP(char *dir);
void handleMount(char **saveptr);
void handleHostMount(char **saveptr);
void handleRun(char **saveptr);
void handleTimeout(char **saveptr);
void handleInput(char **saveptr);
void handleOutput(char **saveptr);
void crash();

unsigned int timeout = 0;
int childI = 0, childO = 1;
uid_t childUID = 0, childGID = 0;

int main(int argc, char **argv)
{
    int tmpi, i, o;
    int ubda;
    char *buf, *line, *word, *lsaveptr, *wsaveptr;
    size_t bufsz, bufused;
    ssize_t rd;
    struct termios termios_p;

    srandom(time(NULL));

    /* figure out the child's UID and GID */
    if (argc > 1)
        childUID = atoi(argv[1]);
    if (childUID == 0)
        childUID = random() % 995000 + 5000;
    if (argc > 2)
        childGID = atoi(argv[2]);
    if (childGID == 0)
        childGID = random() % 995000 + 5000;

    /* try to get console */
    mknod("/console", 0644 | S_IFCHR, makedev(5, 1));
    i = open("/console", O_RDONLY);
    o = open("/console", O_WRONLY);
    if (i != 0) dup2(i, 0);
    if (o != 1) dup2(o, 1);
    if (o != 2) dup2(o, 2);

    /* and tty1 */
    mknod("/tty1", 0644 | S_IFCHR, makedev(4, 1));
    childI = open("/tty1", O_RDONLY);
    childO = open("/tty1", O_WRONLY);

    /* make the TTY raw if it's supposed to be (argv[3] == isatty) */
    if (argc > 3 && !strcmp(argv[3], "0")) {
        SF(tmpi, tcgetattr, -1, (childO, &termios_p));
        cfmakeraw(&termios_p);
        SF(tmpi, tcsetattr, -1, (childO, TCSANOW, &termios_p));
    }

    printf("\n----------\nUMLBox starting.\n----------\n\n");

    /* first make sure we can access ubda */
    SF(tmpi, mknod, -1, ("/ubda", 0644 | S_IFBLK, makedev(98, 0)));

    /* and a root */
    SF(tmpi, mkdir, -1, ("/root", 0777));

    /* read our configuration from it */
    SF(ubda, open, -1, ("/ubda", O_RDONLY));
    bufsz = 1024;
    bufused = 0;
    SF(buf, malloc, NULL, (bufsz));
    while (1) {
        rd = read(ubda, buf + bufused, bufsz - bufused);
        if (rd > 0) {
            bufused += rd;
            if (bufused == bufsz) {
                bufsz *= 2;
                SF(buf, realloc, NULL, (buf, bufsz));
            }
        } else {
            break;
        }
    }
    buf[bufused] = '\0';
    close(ubda);

    fprintf(stderr, "\n----------\nRead configuration:\n----------\n%s----------\n\n", buf);

    /* now perform the commands */
    lsaveptr = NULL;
    while (line = strtok_r(lsaveptr ? NULL : buf, "\n", &lsaveptr)) {
        fprintf(stderr, "$ %s\n", line);
        word = strtok_r(line, " ", &wsaveptr);
        if (word == NULL || word[0] == '#') continue;
        if (!strcmp(word, "mount")) {
            handleMount(&wsaveptr);
        } else if (!strcmp(word, "hostmount")) {
            handleHostMount(&wsaveptr);
        } else if (!strcmp(word, "run")) {
            handleRun(&wsaveptr);
        } else if (!strcmp(word, "timeout")) {
            handleTimeout(&wsaveptr);
        } else if (!strcmp(word, "input")) {
            handleInput(&wsaveptr);
        } else if (!strcmp(word, "output")) {
            handleOutput(&wsaveptr);
        } else {
            fprintf(stderr, "Unrecognized command %s\n", word);
            crash();
        }
    }

    fprintf(stderr, "\n----------\nUMLBox is terminating.\n----------\n\n");

    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);
}

void mkdirP(char *dir)
{
    int tmpi;
    char *tdir, *elem, *saveptr;

    /* start at the root */
    SF(tmpi, chdir, -1, ("/"));

    /* then work through the dir */
    SF(tdir, strdup, NULL, (dir));
    saveptr = NULL;
    while (elem = strtok_r(saveptr ? NULL : tdir, "/", &saveptr)) {
        if (elem[0]) {
            /* best effort mkdir */
            mkdir(elem, 0777);

            /* then must-succeed chdir */
            SF(tmpi, chdir, -1, (elem));
        }
    }
    free(tdir);
    SF(tmpi, chdir, -1, ("/"));
}

void handleMount(char **saveptr)
{
    int tmpi;
    char *source, *target, *rtarget, *type, *data;

    /* get our options */
    SF(source, strtok_r, NULL, (NULL, " ", saveptr));
    SF(target, strtok_r, NULL, (NULL, " ", saveptr));
    SF(type, strtok_r, NULL, (NULL, " ", saveptr));

    /* data is optional */
    data = strtok_r(NULL, " ", saveptr);

    /* make the target directory */
    SF(rtarget, malloc, NULL, (strlen(target) + 7));
    sprintf(rtarget, "/root/%s", target);
    mkdirP(rtarget);

    /* then mount it */
    SF(tmpi, mount, -1, (source, rtarget, type, 0, data));
    free(rtarget);
}

void handleHostMount(char **saveptr)
{
    int tmpi;
    char *rrw, *host, *guest, *rguest;
    unsigned long flags;

    /* get our options */
    SF(rrw, strtok_r, NULL, (NULL, " ", saveptr));
    SF(host, strtok_r, NULL, (NULL, " ", saveptr));
    SF(guest, strtok_r, NULL, (NULL, " ", saveptr));

    /* figure out the flags */
    flags = 0;
    if (!strcmp(rrw, "r")) {
        flags |= MS_RDONLY;
    } else if (!strcmp(rrw, "rw")) {
        /* rd/rw, default */
    } else {
        fprintf(stderr, "Unrecognized hostmount flag %s\n", rrw);
        exit(1);
    }

    /* make the guest directory */
    SF(rguest, malloc, NULL, (strlen(guest) + 7));
    sprintf(rguest, "/root/%s", guest);
    mkdirP(rguest);

    /* then mount it */
    SF(tmpi, mount, -1, ("none", rguest, "hostfs", flags, host));
    free(rguest);
}

void handleRun(char **saveptr)
{
    char *dir, *cmd;
    pid_t pid, spid;

    /* read the dir */
    SF(dir, strtok_r, NULL, (NULL, " ", saveptr));

    /* read the command (remainder of the buffer) */
    SF(cmd, strtok_r, NULL, (NULL, "\n", saveptr));

    /* and run it, chrooted */
    srandom(random());
    SF(pid, fork, -1, ());
    if (pid == 0) {
        int tmpi;

        /* I/O redirection */
        if (childI != 0) dup2(childI, 0);
        if (childO != 1) {
            dup2(childO, 1);
            dup2(childO, 2);
        }

        /* chroot */
        chdir("/root");
        chroot("/root");
        chdir(dir);

        /* randomize GID/UID */
        SF(tmpi, setgid, -1, (childGID));
        SF(tmpi, setuid, -1, (childUID));

        /* and run */
        SF(tmpi, system, -1, (cmd));
        if (WEXITSTATUS(tmpi) == 127)
            fprintf(stderr, "/bin/sh could not be executed\n");
        exit(0);
        while (1) sleep(60*60*24);
    }

    /* as well as a pid to do the timeout */
    if (timeout != 0) {
        SF(spid, fork, -1, ());
        if (spid == 0) {
            sleep(timeout);
            exit(0);
            while (1) sleep(60*60*24);
        }

        if (wait(NULL) == spid) {
            /* kill it */
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }

    } else {
        waitpid(pid, NULL, 0);

    }
}

void handleTimeout(char **saveptr)
{
    char *timeouts;

    /* get the timeout */
    SF(timeouts, strtok_r, NULL, (NULL, "\n", saveptr));
    timeout = atoi(timeouts);
}

void handleInput(char **saveptr)
{
    char *file, *rfile;
    SF(file, strtok_r, NULL, (NULL, "\n", saveptr));

    SF(rfile, malloc, NULL, (strlen(file) + 7));
    sprintf(rfile, "/root/%s", file);

    if (childI != 0) close(childI);
    SF(childI, open, -1, (rfile, O_RDONLY));
    free(rfile);
}

void handleOutput(char **saveptr)
{
    char *file, *rfile;
    SF(file, strtok_r, NULL, (NULL, "\n", saveptr));

    SF(rfile, malloc, NULL, (strlen(file) + 7));
    sprintf(rfile, "/root/%s", file);

    if (childO != 1) close(childO);
    SF(childO, open, -1, (rfile, O_WRONLY|O_CREAT, 0666));
    free(rfile);
}

void crash()
{
    fprintf(stderr, "\n----------\nUMLBox is crashing!\n----------\n\n");
    exit(1);
}
