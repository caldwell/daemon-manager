//  Copyright (c) 2010-2023 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
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
#include <signal.h>
#include "command-sock.h"
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
           "\t%s <daemon-id> log|tail\n"
           "\t%s <daemon-id> edit\n"
           "\t%s <daemon-id> kill -SIGNAL\n", me, me, me, me, me, me);
    exit(exit_code);
}

static string do_command(string command, int command_socket_fd);
static string canonify(string id, int command_socket_fd);
static void do_log(string id, int command_socket_fd);
static void do_tail(string id, int command_socket_fd);
static void do_edit(string id, int command_socket_fd);
static void do_kill(options o, string id, int command_socket_fd);

int main(int argc, char **argv)
{
    options o(argc, argv, argc > 2 && strcmp(argv[2], "kill") == 0 ? 2 : -1);
    if (o.get("version"))   { printf("dmctl version " VERSION "\n"); exit(EXIT_SUCCESS); }
    if (o.get("help", 'h')) usage(argv[0], EXIT_SUCCESS);
    unsigned max_args = o.args.size() > 2 && o.args[1] == "kill" ? 3 : 2;
    if (o.bad_args() || o.args.size() > max_args) usage(argv[0], EXIT_FAILURE);

    signal(SIGPIPE, SIG_IGN);

    int command_socket;
    try {
        struct sockaddr_un addr = command_sock_addr();
        command_socket = socket(PF_LOCAL, SOCK_STREAM /*SOCK_DGRAM*/, 0);
        if (command_socket < 0) throw_strerr("socket() failed");
        connect(command_socket, (struct sockaddr*) &addr, sizeof(sa_family_t) + strlen(addr.sun_path) + 1)
                                                                        == 0 || throw_strerr("Connect to %s failed", addr.sun_path);
        fcntl(command_socket, F_SETFL, O_NONBLOCK)                      == 0 || throw_strerr("Couldn't set O_NONBLOCK on %s", addr.sun_path);
    } catch (std::exception &e) {
        errx(1, "daemon-manager does not appear to be running.");
    }

    string command = o.args.size() == 0 ? "status"  :
                     o.args.size() == 1 ? o.args[0] :
                                          o.args[1];
    string id = o.args.size() > 1 ? canonify(o.args[0], command_socket) : "";

    try {
        if      (command == "log")
            do_log(id, command_socket);
        else if (command == "tail")
            do_tail(id, command_socket);
        else if (command == "edit")
            do_edit(id, command_socket);
        else if (command == "kill")
            do_kill(o, id, command_socket);
        else {
            string resp = do_command(command + string(" ") + id, command_socket); /* The daemon still takes args the old way ("start <daemon-id>"). */
            printf("%s", resp.c_str());
        }
        exit(EXIT_SUCCESS);
    } catch(std::exception &e) {
        fprintf(stderr, "%s%s", e.what(), *string(e.what()).rbegin() == '\n' ? "" : "\n");
        exit(EXIT_FAILURE);
    }
}

static bool wait_response(int command_socket_fd, bool block)
{
    struct pollfd fd[1];
    fd[0].fd = command_socket_fd;
    fd[0].events = POLLIN;
    int got = poll(fd, 1, block ? -1 : 0);
    if (got < 0)  err(1, "Poll failed");
    return got != 0;
}

static string do_command(string command, int command_socket_fd)
{
    int wrote = write(command_socket_fd, command.c_str(), command.length());
    if (!wrote)   errx(1, "Write to command fifo failed.");
    if (wrote < 0) {
        // If daemon-manager is fast it could have already spewed an error at us and closed the connection, causing our write to fail.
        // To detect this we see if there's something for us to read on the socket. If not, then the error was legit--report it.
        int saved_errno = errno;
        if (!wait_response(command_socket_fd, false)) {
            errno = saved_errno;
            err(1, "Write to command fifo failed");
        }
    }

    if (!wait_response(command_socket_fd, true))
        err(1, "Poll timed out.");

    string out;
    while (1) {
        char buf[256];
        int red = read(command_socket_fd, buf, sizeof(buf)-1);
        if (red == 0 || red < 0 && errno == EAGAIN) break; // done.
        if (red < 0 && errno == ECONNRESET && !out.empty()) break; // Don't whine if they sent a message but our next tentative read got closed down.
        if (red < 0)   err(1, "No response from daemon-manager");
        out.append(buf, red);
    }
    if (out.empty()) errx(1, "No response from daemon-manager.");

    if (out == "OK\n")              return string("");
    if (out.substr(0, 4) == "OK: ") return out.substr(4);
    throw std::runtime_error(out);
}

static string canonify(string id, int command_socket_fd)
{
    if (id.find('/') != id.npos) // already fully qualified
        return id;

    // Ask daemon-manager for the list of ids we can manage.
    string id_list;
    try {
        id_list = chomp(do_command("list", command_socket_fd));
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

static string find_log_file(string id, int command_socket_fd)
{
    string log_file;
    log_file = chomp(do_command("logfile "+id, command_socket_fd));

    struct stat st;
    stat(log_file.c_str(), &st) == 0 || throw_str("\"%s\" does not exist. Perhaps \"output=log\" is not enabled in %s's config file?",
                                                  log_file.c_str(), id.c_str());
    return log_file;
}

static void do_log(string id, int command_socket_fd)
{
    if (id == "") throw_str("\"log\" needs an argument");
    string log_file = find_log_file(id, command_socket_fd);

    if (isatty(STDOUT_FILENO)) {
        getenv("PAGER") && execl(getenv("PAGER"), getenv("PAGER"), log_file.c_str(), NULL);
        execlp("less", "less", log_file.c_str(), NULL);
        execlp("more", "more", log_file.c_str(), NULL);
    }
    execlp("cat",      "cat",      log_file.c_str(), NULL);
    throw_str("Couldn't run $PAGER, less, more, or cat on %s", log_file.c_str());
}

static void do_tail(string id, int command_socket_fd)
{
    if (id == "") throw_str("\"tail\" needs an argument");
    string log_file = find_log_file(id, command_socket_fd);

    execlp("tail", "tail", "-f", log_file.c_str(), NULL);
    throw_str("Couldn't run \"tail -f\" on %s", log_file.c_str());
}

static void do_edit(string id, int command_socket_fd)
{
    if (id == "") throw_str("\"edit\" needs an argument");
    string config_file = chomp(do_command("configfile "+id, command_socket_fd));

    const char *editor = getenv("VISUAL");
    if (!editor) editor = getenv("EDITOR");
    if (!editor) editor = "vi";
    execlp("/bin/sh", "/bin/sh", "-c", (string(editor) + " \"$1\"").c_str(), "--", config_file.c_str(), NULL);
    throw_str("Couldn't run \"%s\" on %s", editor, config_file.c_str());
}

static void do_kill(options o, string id, int command_socket_fd)
{
    if (id == "") throw_str("\"kill\" needs an argument");
    std::map<string, int> signals = {{"HUP",  SIGHUP} , {"INT",  SIGINT} , {"QUIT", SIGQUIT}, {"KILL", SIGKILL}, {"TERM", SIGTERM},
                                     {"STOP", SIGSTOP}, {"CONT", SIGCONT}, {"USR1", SIGUSR1}, {"USR2", SIGUSR2},
                                     #ifdef SIGINFO
                                     {"INFO", SIGINFO} /* Darwin has and uses this */
                                     #endif
    };
    int signum=0;
    try {
        size_t l = o.args[2].length();
        if (!signum && l > 0) signum = signals[o.args[2]];               // kill HUP
        if (!signum && l > 2) signum = signals[o.args[2].substr(3)];     // kill SIGHUP
        if (!signum && l > 1) signum = signals[o.args[2].substr(1)];     // kill -HUP
        if (!signum && l > 4) signum = signals[o.args[2].substr(4)];     // kill -SIGHUP
        if (!signum && l > 1) signum = stoul(o.args[2].substr(1), NULL); // kill -3
        if (signum <= 0) throw_str("Bad signal %d", signum);
    } catch(std::exception &e) {
        throw_str("Unsupported signal \"%s\" (%s)", o.args[2].c_str(), e.what());
    }
    do_command(strprintf("kill-%d %s", signum, id.c_str()), command_socket_fd);
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
  dmctl <daemon-id> kill -SIGNAL

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
  +
  If this command is part of a pipeline, then 'cat' will always be used.

*'<daemon-id>' tail*::

  This runs "tail -f" on the log file of the daemon identified by
  <daemon-id>.

*'<daemon-id>' edit*::

  This launches your editor on the config file identified by <daemon-id> (using
  the `VISUAL` environment variable, or if that isn't set the `EDITOR`
  environment variable, or, if _that_ isn't set, 'vi').

*'<daemon-id>' kill '-<signal>'*::

  This sends signal specified by <signal> to the daemon identified by
  <daemon-id>. The signal can be numeric or a string in the same form that
  'kill(1)' takes. For example, the following are all equivalent:

    dmctl mydaemon kill -9
    dmctl mydaemon kill -KILL
    dmctl mydaemon kill -SIGKILL
    dmctl mydaemon kill KILL
    dmctl mydaemon kill SIGKILL

  [NOTE]: This just sends a signal directly to the daemon's pid. If the signal
  causes the process to exit then 'daemon-manager(1)' will restart the daemon as
  usual (assuming the `autostart=yes` setting is enabled).

SEE ALSO
--------
'daemon-manager(1)', 'daemon.conf(5)'

//*/
