//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <stdexcept>
#include "user.h"

using namespace std;

static void usage(char *me, int exit_code)
{
    printf("Usage:\n"
           "\t%s list|status|rescan\n"
           "\t%s start|stop|restart <daemon-id>\n", me, me);
    exit(exit_code);
}

static string do_command(string command, user *me);

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) usage(argv[0], 1);
    user *me;
    try {
        me = new user(getuid());
    } catch (std::exception &e) {
        errx(1, "Error: %s\n", e.what());
    }
    try {
        me->open_client_socket();
    } catch (std::exception &e) {
        errx(1, "daemon-manager does not appear to be running.");
    }

    string command = argv[1];
    if (argc == 3)
        command += string(" ") + argv[2];

    try {
        string resp = do_command(command, me);
        printf("%s", resp.c_str());
        exit(EXIT_SUCCESS);
    } catch(std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        exit(EXIT_FAILURE);
    }
}

static string do_command(string command, user *me)
{
    int wrote = write(me->command_socket, command.c_str(), command.length());
    if (wrote < 0) err(1, "Write to command fifo failed");
    if (!wrote)   errx(1, "Write to command fifo failed.");

    // Drain the FIFO
    char buf[1000];
    while (read(me->command_socket, buf, sizeof(buf)-1) > 0) {}

    // Wait for our response:
    struct pollfd fd[1];
    fd[0].fd = me->command_socket;
    fd[0].events = POLLIN;
    int got = poll(fd, 1, -1);
    if (got < 0)  err(1, "Poll failed");
    if (got == 0) err(1, "Poll timed out.");

    int red = read(me->command_socket, buf, sizeof(buf)-1);
    if (red < 0)   err(1, "No response from daemon-manager");
    if (red == 0) errx(1, "No response from daemon-manager.");

    buf[red] = '\0';
    if (strcmp(buf, "OK\n") == 0)     return string("");
    if (strncmp(buf, "OK: ", 4) == 0) return string(buf+4);
    throw std::runtime_error(buf);
}

/*

=head1 NAME

dmctl - Daemon Manager control

=head1 SYNOPSIS

  dmctl list|status|rescan
  dmctl start|stop|restart <daemon-id>

=head1 DESCRIPTION

B<dmctl> allows the user to communicate and interact with the
L<daemon-manager(1)> daemon.

=head1 COMMANDS

=over

=item I<list>

This command will list all the daemon ids that the user is allowed to control.

=item I<status>

This command will print a human readable list of the daemons-ids and their
current stats.

The following pieces of data are shown per daemon:

=over

=item I<daemon-id>

The daemon id.

=item I<state>

One of C<stopped>, C<running>, C<stopping>, C<coolingdown>.

The cooling down state happens when the daemon starts respawning too quickly. In
order to prevent too much resource utilization, daemon-manager(1) will require
the daemon to cool off for some period of time before respawning it.

=item I<pid>

The process id of the running daemon, or zero if it is not running.

=item I<respawns>

The number of times the daemon has been respawned by daemon-manager(1) due to it
quitting unexpectadly.

=item I<cooldown>

The number of seconds left in the cooldown period.

=item I<uptime>

The number of seconds the daemon has been running for since the last respawn.

=item I<total>

The total number of seconds the daemon has been running for since the last
start.

=back

=item I<rescan>

Upon receiving this command, L<daemon-manager(1)> will look through the user's
home directory (and the home directories of the other users managed by the user)
for new daemon config files. Any new files will be loaded (and started if their
I<autostart> option is set).

It is not necessary to issue the I<rescan> command if a config file has been
edited or deleted. I<start> and I<stop> will catch those 2 cases respectively.

=item I<start> <daemon-id>

This will start the daemon identified by I<< <daemon-id> >> if it hasn't already
been started.

If the daemon is cooling down then I<start> will attempt to start it
immediately. This is useful if you are debugging a daemon that is not launching
properly and don't want to wait for the cooldown period.

=item I<stop> <daemon-id>

This will stop the daemon identified by I<< <daemon-id> >> if it isn't already
stopped.

=item I<restart> <daemon-id>

Currently this is just a short hand for a I<stop> command followed by a I<start>
command.

=back

=head1 SEE ALSO

L<daemon-manager(1)>, L<daemon.conf(5)>

=cut

*/
