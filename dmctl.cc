//  Copyright (c) 2010 David Caldwell <david@porkrind.org>
////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include "user.h"
#include "stringutil.h"
#include "strprintf.h"
#include "options.h"

using namespace std;

static void usage(char *me, int exit_code)
{
    printf("Usage:\n"
           "\t%s list|rescan\n"
           "\t%s [<daemon-id>] status\n"
           "\t%s <daemon-id> start|stop|restart\n"
           "\t%s <daemon-id> log|tail\n", me, me, me, me);
    exit(exit_code);
}

static string do_command(string command, user *me);
static string canonify(string id, user *u);
static void do_log(string id, user *me);
static void do_tail(string id, user *me);

int main(int argc, char **argv)
{
    options o(argc, argv);
    if (o.get("version"))   { printf("dmctl version " VERSION "\n"); exit(EXIT_SUCCESS); }
    if (o.get("help", 'h')) usage(argv[0], EXIT_SUCCESS);
    if (o.bad_args() || o.args.size() > 2) usage(argv[0], EXIT_FAILURE);

    user *me;
    try {
        me = new user(getuid());
    } catch (std::exception &e) {
        errx(1, "Error: %s\n", e.what());
    }
    try {
        me->open_client_socket();
    } catch (std::exception &e) {
        errx(1, "daemon-manager does not appear to be running or you are not in the daemon-manager.conf file.");
    }

    string command = o.args.size() == 0 ? "status"  :
                     o.args.size() == 1 ? o.args[0] :
                                          o.args[1];
    string id = o.args.size() == 2 ? canonify(o.args[0], me) : "";

    try {
        if      (command == "log")
            do_log(id, me);
        else if (command == "tail")
            do_tail(id, me);
        else {
            string resp = do_command(command + string(" ") + id, me); /* The daemon still takes args the old way ("start <daemon-id>"). */
            printf("%s", resp.c_str());
        }
        exit(EXIT_SUCCESS);
    } catch(std::exception &e) {
        fprintf(stderr, "%s%s", e.what(), *string(e.what()).rbegin() == '\n' ? "" : "\n");
        exit(EXIT_FAILURE);
    }
}

static string do_command(string command, user *me)
{
    int wrote = write(me->command_socket, command.c_str(), command.length());
    if (wrote < 0) err(1, "Write to command fifo failed");
    if (!wrote)   errx(1, "Write to command fifo failed.");

    // Wait for our response:
    struct pollfd fd[1];
    fd[0].fd = me->command_socket;
    fd[0].events = POLLIN;
    int got = poll(fd, 1, -1);
    if (got < 0)  err(1, "Poll failed");
    if (got == 0) err(1, "Poll timed out.");

    string out;
    while (1) {
        char buf[256];
        int red = read(me->command_socket, buf, sizeof(buf)-1);
        if (red == 0 || red < 0 && errno == EAGAIN) break; // done.
        if (red < 0)   err(1, "No response from daemon-manager");
        out.append(buf, red);
    }
    if (out.empty()) errx(1, "No response from daemon-manager.");

    if (out == "OK\n")              return string("");
    if (out.substr(0, 4) == "OK: ") return out.substr(4);
    throw std::runtime_error(out);
}

static string canonify(string id, user *u)
{
    if (id.find('/') != id.npos) // already fully qualified
        return id;

    // Ask daemon-manager for the list of ids we can manage.
    string id_list;
    try {
        id_list = chomp(do_command("list", u));
    } catch(std::exception &e) {
        errx(1, "'list' failed: %s", e.what());
    }

    // Look for ids that match our unqualified id.
    // "this" will match "david/this" and "bob/this" but not "david/that"
    vector<string> candidates;
    vector<string> ids;
    split(ids, id_list, ",");
    for (vector<string>::iterator i=ids.begin(); i != ids.end(); i++) {
        vector<string> components;
        split(components, *i, "/");
        if (components.size() == 2 && components[1] == id)
            candidates.push_back(*i);
    }

    if (candidates.size() == 1)
        return candidates[0];

    if (candidates.size() == 0)
        errx(EXIT_FAILURE, "id \"%s\" not found", id.c_str());

    fprintf(stderr, "id \"%s\" is ambiguous. Which did you mean?\n", id.c_str());
    for (vector<string>::iterator c=candidates.begin(); c != candidates.end(); c++)
        fprintf(stderr, "\t%s\n", c->c_str());
    exit(EXIT_FAILURE);
}

static string find_log_file(string id, user *u)
{
    string log_file;
    log_file = chomp(do_command("logfile "+id, u));

    struct stat st;
    stat(log_file.c_str(), &st) == 0 || throw_str("\"%s\" does not exist. Perhaps \"output=log\" is not enabled in %s's config file?",
                                                  log_file.c_str(), id.c_str());
    return log_file;
}

static void do_log(string id, user *u)
{
    if (id == "") throw_str("\"log\" needs an argument");
    string log_file = find_log_file(id, u);

    if (isatty(STDOUT_FILENO)) {
        getenv("PAGER") && execl(getenv("PAGER"), getenv("PAGER"), log_file.c_str(), NULL);
        execlp("less", "less", log_file.c_str(), NULL);
        execlp("more", "more", log_file.c_str(), NULL);
    }
    execlp("cat",      "cat",      log_file.c_str(), NULL);
    throw_str("Couldn't run $PAGER, less, more, or cat on %s", log_file.c_str());
}

static void do_tail(string id, user *u)
{
    if (id == "") throw_str("\"tail\" needs an argument");
    string log_file = find_log_file(id, u);

    execlp("tail", "tail", "-f", log_file.c_str(), NULL);
    throw_str("Couldn't run \"tail -f\" on %s", log_file.c_str());
}

/* Magic quoting to transition to asciidoc mode
////

dmctl(1)
========
David Caldwell <david@porkrind.org>

NAME
----
dmctl - Daemon Manager control

SYNOPSIS
--------
  dmctl list|rescan
  dmctl [<daemon-id>] status
  dmctl <daemon-id> start|stop|restart
  dmctl <daemon-id> log|tail

DESCRIPTION
-----------
*dmctl* allows the user to communicate and interact with the
L<daemon-manager(1)> daemon. If no command is given, "*status*" is assumed.

COMMANDS
--------
*list*::

  This command will list all the daemon ids that the user is allowed to control.

*['<daemon-id>'] status*::

  This command will print a human readable list of the daemons-ids and their
  current stats. If the optional parameter '<daemon-id>' is specified then only
  the status for the daemon with that id will be shown.
  +
  The following pieces of data are shown per daemon:

    'daemon-id';;

      The daemon id.

    'state';;

      One of "`stopped`", "`running`", "`stopping`", "`coolingdown`".
      +
      The cooling down state happens when the daemon starts respawning too quickly. In
      order to prevent too much resource utilization, 'daemon-manager(1)' will require
      the daemon to cool off for some period of time before respawning it.

    'pid';;

      The process id of the running daemon, or zero if it is not running.

    'respawns';;

      The number of times the daemon has been respawned by daemon-manager(1) due to it
      quitting unexpectadly.

    'cooldown';;

      The number of seconds left in the cooldown period.

    'uptime';;

      The number of seconds the daemon has been running for since the last respawn.

    'total';;

      The total number of seconds the daemon has been running for since the last
      start.

*rescan*::

  Upon receiving this command, 'daemon-manager(1)' will look through the user's
  home directory (and the home directories of the other users managed by the user)
  for new daemon config files. Any new files will be loaded (and started if their
  'autostart' option is set).
  +
  It is not necessary to issue the 'rescan' command if a config file has been
  edited or deleted. 'start' and 'stop' will catch those 2 cases respectively.

*'<daemon-id>' start*::

  This will start the daemon identified by '<daemon-id>' if it hasn't already
  been started.
  +
  If the daemon is cooling down then 'start' will attempt to start it
  immediately. This is useful if you are debugging a daemon that is not launching
  properly and don't want to wait for the cooldown period.

*'<daemon-id>' stop*::

  This will stop the daemon identified by '<daemon-id>' if it isn't already
  stopped.

*'<daemon-id>' restart*::

  Currently this is just a short hand for a 'stop' command followed by a 'start'
  command.

*'<daemon-id>' log*::

  This lets you view the daemon identified by <daemon-id>'s log file. If the
  'PAGER' environment variable is set, then it will be used to view the
  file. Otherwise 'less' will be used and if it is not found then 'more'
  will be used and, failing that, 'cat'.

  If this command is part of a pipeline, then 'cat' will always be used.

*'<daemon-id>' tail*::

  This runs "tail -f" on the log file of the daemon identified by
  <daemon-id>.


SEE ALSO
--------
'daemon-manager(1)', 'daemon.conf(5)'

//*/
