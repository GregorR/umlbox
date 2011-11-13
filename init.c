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

#define _BSD_SOURCE /* for random, environment stuff, mknod, etc */
#define _POSIX_SOURCE /* for kill */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <linux/reboot.h>
#include <sys/reboot.h>

#define SF(_o, _f, _b, _a) \
do { \
    (_o) = (_f) _a; \
    if ((_o) == (_b)) { \
        perror(#_f #_a); \
        crash(); \
    } \
} while(0)

void mkdirP(char *dir);
void handleMount(char **saveptr);
void handleHostMount(char **saveptr);
void handleRun(int daemon, char **saveptr);
void handleTimeout(char **saveptr);
void handleInput(char **saveptr);
void handleOutput(char **saveptr);
void handleError(char **saveptr);
void handleSetID(int u, char **saveptr);
void handleTTYRaw(char **saveptr);
void handleEnv(char **saveptr);
void crash();

unsigned int timeout = 0;
int childI = 0, childO = 1, childE = 2;
uid_t childUID = 0;
gid_t childGID = 0;

int main(int argc, char **argv)
{
    int tmpi, i, o;
    int ubda;
    char *buf, *line, *word, *lsaveptr, *wsaveptr;
    size_t bufsz, bufused;
    ssize_t rd;

    srandom(time(NULL));

    /* drop our environment */
    if (clearenv() != 0) {
        perror("clearenv");
        exit(1);
    }
    setenv("PATH", "/usr/local/bin:/bin:/usr/bin", 1);
    setenv("TERM", "linux", 1);
    setenv("HOME", "/tmp", 1);

    /* try to get console */
    mknod("/console", 0644 | S_IFCHR, makedev(5, 1));
    i = open("/console", O_RDONLY);
    o = open("/console", O_WRONLY);
    if (i != 0) dup2(i, 0);
    if (o != 1) dup2(o, 1);
    if (o != 2) dup2(o, 2);

    /* and tty1-15 */
    SF(buf, malloc, NULL, (8));
    for (i = 1; i < 16; i++) {
        sprintf(buf, "/tty%d", i);
        mknod(buf, 0644 | S_IFCHR, makedev(4, i));
    }
    free(buf);

    printf("\n----------\nUMLBox starting.\n----------\n\n");

    /* first make sure we can access ubda */
    SF(tmpi, mknod, -1, ("/ubda", 0644 | S_IFBLK, makedev(98, 0)));

    /* and a root */
    SF(tmpi, mkdir, -1, ("/host", 0777));

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
    while ((line = strtok_r(lsaveptr ? NULL : buf, "\n", &lsaveptr))) {
        fprintf(stderr, "$ %s\n", line);
        word = strtok_r(line, " ", &wsaveptr);
        if (word == NULL || word[0] == '#') continue;
#define CMD(x) if (!strcmp(word, #x))
        CMD(mount) {
            handleMount(&wsaveptr);
        } else CMD(hostmount) {
            handleHostMount(&wsaveptr);
        } else CMD(run) {
            handleRun(0, &wsaveptr);
        } else CMD(daemon) {
            handleRun(1, &wsaveptr);
        } else CMD(timeout) {
            handleTimeout(&wsaveptr);
        } else CMD(input) {
            handleInput(&wsaveptr);
        } else CMD(output) {
            handleOutput(&wsaveptr);
        } else CMD(error) {
            handleError(&wsaveptr);
        } else CMD(setuid) {
            handleSetID(1, &wsaveptr);
        } else CMD(setgid) {
            handleSetID(0, &wsaveptr);
        } else CMD(ttyraw) {
            handleTTYRaw(&wsaveptr);
        } else CMD(env) {
            handleEnv(&wsaveptr);
        } else {
            fprintf(stderr, "Unrecognized command %s\n", word);
            crash();
        }
#undef CMD
    }

    fprintf(stderr, "\n----------\nUMLBox is terminating.\n----------\n\n");

    sync();
    reboot(LINUX_REBOOT_CMD_POWER_OFF);

    return 0;
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
    while ((elem = strtok_r(saveptr ? NULL : tdir, "/", &saveptr))) {
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
    sprintf(rtarget, "/host/%s", target);
    mkdirP(rtarget);

    /* then mount it */
    SF(tmpi, mount, -1, (source, rtarget, type, 0, data));
    free(rtarget);
}

void handleHostMount(char **saveptr)
{
    int tmpi;
    char *rrw, *host, *rhost, *guest, *rguest;
    unsigned long flags;

    /* get our options */
    SF(rrw, strtok_r, NULL, (NULL, " ", saveptr));
    SF(host, strtok_r, NULL, (NULL, " ", saveptr));
    SF(guest, strtok_r, NULL, (NULL, " ", saveptr));

    /* figure out the flags */
    flags = MS_NOSUID;
    if (!strcmp(rrw, "r")) {
        flags |= MS_RDONLY;
    } else if (!strcmp(rrw, "rw")) {
        /* rd/rw, default */
    } else {
        fprintf(stderr, "Unrecognized hostmount flag %s\n", rrw);
        exit(1);
    }

    /* make the host directory */
    SF(rhost, malloc, NULL, (strlen(host) + 2));
    sprintf(rhost, "%s/", host);

    /* make the guest directory */
    SF(rguest, malloc, NULL, (strlen(guest) + 6));
    sprintf(rguest, "/host%s", guest);
    mkdirP(rguest);

    /* then mount it */
    SF(tmpi, mount, -1, ("none", rguest, "hostfs", flags, rhost));
    free(rguest);
    free(rhost);
}

void handleRun(int daemon, char **saveptr)
{
    char *ru, *dir, *cmd;
    pid_t pid, spid;
    int user;

    /* root or user? */
    SF(ru, strtok_r, NULL, (NULL, " ", saveptr));
    if (!strcmp(ru, "root")) {
        user = 0;
    } else if (!strcmp(ru, "user")) {
        user = 1;
    } else {
        fprintf(stderr, "Use: run <root|user> <dir> <cmd>\n");
        exit(1);
    }

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
        if (childO != 1) dup2(childO, 1);
        if (childE != 2) dup2(childE, 2);

        /* chroot */
        SF(tmpi, chdir, -1, ("/host"));
        SF(tmpi, chroot, -1, ("/host"));
        tmpi = chdir(dir);
        (void) tmpi;

        /* randomize GID/UID */
        if (user) {
            if (childUID == 0)
                childUID = random() % 995000 + 5000;
            if (childGID == 0)
                childGID = random() % 995000 + 5000;
            SF(tmpi, setgid, -1, (childGID));
            SF(tmpi, setuid, -1, (childUID));
        }

        /* and run */
        SF(tmpi, system, -1, (cmd));
        if (WEXITSTATUS(tmpi) == 127) {
            fprintf(stderr, "/bin/sh could not be executed\n");
            close(0);

            /* attempt to give some debugging output */
            close(0);
            tmpi = execl("/bin/sh", "/bin/sh", NULL);
            perror("/bin/sh");
        }
        exit(0);
        while (1) sleep(60*60*24);
    }

    if (!daemon) {
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
    sprintf(rfile, "/host/%s", file);

    if (childI != 0) close(childI);
    SF(childI, open, -1, (rfile, O_RDONLY));
    free(rfile);
}

void handleOutput(char **saveptr)
{
    char *file, *rfile;
    SF(file, strtok_r, NULL, (NULL, "\n", saveptr));

    SF(rfile, malloc, NULL, (strlen(file) + 7));
    sprintf(rfile, "/host/%s", file);

    if (childO != 1) close(childO);
    if (childE != 2) close(childE);
    SF(childO, open, -1, (rfile, O_WRONLY|O_CREAT, 0666));
    free(rfile);
    SF(childE, dup, -1, (childO));
}

void handleError(char **saveptr)
{
    char *file, *rfile;
    SF(file, strtok_r, NULL, (NULL, "\n", saveptr));

    SF(rfile, malloc, NULL, (strlen(file) + 7));
    sprintf(rfile, "/host/%s", file);

    if (childE != 2) close(childE);
    SF(childE, open, -1, (rfile, O_WRONLY|O_CREAT, 0666));
    free(rfile);
}

void handleSetID(int u, char **saveptr)
{
    char *ids;
    uid_t id;
    SF(ids, strtok_r, NULL, (NULL, "\n", saveptr));
    id = atoi(ids);

    if (u) {
        childUID = id;
    } else {
        childGID = (gid_t) id;
    }
}

void handleTTYRaw(char **saveptr)
{
    struct termios termios_p;
    int tmpi;

    /* input ... */
    SF(tmpi, tcgetattr, -1, (childI, &termios_p));
    cfmakeraw(&termios_p);
    SF(tmpi, tcsetattr, -1, (childI, TCSANOW, &termios_p));

    /* output ... */
    SF(tmpi, tcgetattr, -1, (childO, &termios_p));
    cfmakeraw(&termios_p);
    SF(tmpi, tcsetattr, -1, (childO, TCSANOW, &termios_p));
}

void handleEnv(char **saveptr)
{
    char *var, *val;

    SF(var, strtok_r, NULL, (NULL, " ", saveptr));
    SF(val, strtok_r, NULL, (NULL, "\n", saveptr));
    setenv(var, val, 1);
}

void crash()
{
    fprintf(stderr, "\n----------\nUMLBox is crashing!\n----------\n\n");
    exit(1);
}
