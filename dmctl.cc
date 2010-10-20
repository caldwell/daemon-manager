//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include "user.h"

using namespace std;

static void usage(char *me, int exit_code)
{
    printf("Usage:\n"
           "\t%s list|status\n"
           "\t%s start|stop|restart <daemon-id>\n", me, me);
    exit(exit_code);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) usage(argv[0], 1);
    user *me;
    try {
        me = new user(getuid());
        me->open_fifos(true);
    } catch (string e) {
        errc(1, 0, "Error: %s\n", e.c_str());
    }

    string command = argv[1];
    if (argc == 3)
        command += string(" ") + argv[2];
    int wrote = write(me->fifo_req_wr, command.c_str(), command.length());
    if (wrote < 0) err(1,  "daemon-manager does not appear to be running.");
    if (!wrote) errc(1, 0, "daemon-manager does not appear to be running.");

    // Drain the FIFO
    char buf[1000];
    while (read(me->fifo_resp_rd, buf, sizeof(buf)-1) > 0) {}

    // Wait for our response:
    struct pollfd fd[1];
    fd[0].fd = me->fifo_resp_rd;
    fd[0].events = POLLIN;
    int got = poll(fd, 1, -1);
    if (got < 0) err(1, "Poll failed.");
    if (got == 0) err(1, "Poll timed out.");

    int red = read(me->fifo_resp_rd, buf, sizeof(buf)-1);
    if (red < 0) err(1, "no response from daemon-manager");
    if (red == 0) errc(1, 0, "no response from daemon-manager.");

    buf[red] = '\0';
    if (strcmp(buf, "OK\n") == 0) exit(0);
    if (strncmp(buf, "OK: ", 4) == 0) {
        printf("%s", buf+4);
        exit(0);
    }
    printf("%s", buf);
    exit(1);
}

