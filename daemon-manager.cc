//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include "config.h"
#include "user.h"
#include "daemon.h"
#include "passwd.h"
#include "permissions.h"
#include "lengthof.h"
#include "strprintf.h"
#include "log.h"
#include "uniq.h"
#include "key-exists.h"

#include <vis.h>

using namespace std;

static void usage(char *me, int exit_code)
{
    printf("Usage:\n\t%s [-h | --help] [-c | --config=<config-file>] [-v | --verbose] [-f | --foreground] [-d | --debug]\n", me);
    exit(exit_code);
}

static void daemonize();
static vector<user*> user_list_from_config(struct master_config config);
static vector<class daemon*>empty_daemon_list(0);
static vector<class daemon*> load_daemons(vector<user*> user_list, vector<class daemon*>existing = empty_daemon_list);
static void autostart(vector<class daemon*> daemons);
static void select_loop(vector<user*> users, vector<class daemon*> daemons);
static vector<class daemon*> manageable_by_user(user *user, vector<class daemon*> daemons);
static string do_command(string command_line, user *user, vector<class daemon*> *daemons);
static void dump_config(struct master_config config);

int main(int argc, char **argv)
{
    string config_path("/etc/daemon-manager/daemon-manager.conf");
    int verbose = 0;
    bool foreground = false;
    bool debug = false;

    struct option opts[] = {
        { "help",       no_argument,        NULL, 'h' },
        { "config",     required_argument,  NULL, 'c' },
        { "verbose",    no_argument,        NULL, 'v' },
        { "foreground", no_argument,        NULL, 'f' },
        { "debug",      no_argument,        NULL, 'd' },
        {0,0,0,0}
    };

    char shortopts[100];
    size_t si=0;
    for (int i=0; opts[i].name; i++)
        if (si < sizeof(shortopts)-3 &&
            !opts[i].flag && isprint(opts[i].val)) {
            shortopts[si++] = opts[i].val;
            if (opts[i].has_arg == required_argument)
                shortopts[si++] = ':';
        }
    shortopts[si] = '\0';

    for (int ch; (ch = getopt_long(argc, argv, shortopts, opts, NULL)) != -1;) {
        switch (ch) {
            case 'h': usage(argv[0], EXIT_SUCCESS); break;
            case 'c': config_path = string(optarg); break;
            case 'v': verbose++; break;
            case 'f': foreground = true; break;
            case 'd': debug = foreground = true; break;
            default: usage(argv[0], 1); break;
        }
    }

    if (argc != optind) {
        fprintf(stderr, "Too many arguments.\n");
        usage(argv[0], EXIT_FAILURE);
    }

    init_log(!debug, min(LOG_DEBUG, LOG_NOTICE + verbose));

    struct master_config config;
    try {
        permissions::check(config_path, 0113, 0, 0);
        config = parse_master_config(config_path);
    } catch(std::exception &e) {
        log(LOG_ERR, "Couldn't load config file: %s\n", e.what());
        exit(EXIT_FAILURE);
    }

    if (!foreground)
        daemonize();

    vector<user*> users = user_list_from_config(config);

    vector<class daemon*> daemons = load_daemons(users);

    autostart(daemons);

    select_loop(users, daemons);

    return 0;
}

static void daemonize()
{
    int child = fork();
    if (child < 0) err(1, "Couldn't fork");
    if (child > 0) exit(EXIT_SUCCESS); // Parent exits

    // Child:
    int sid = setsid();
    if (sid < 0) {
        log(LOG_ERR, "setsid failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) {
        log(LOG_ERR, "What, / doesn't exist?? chdir: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
}

template<class T>
bool contains(vector<T> list, T value)
{
    return find(list.begin(), list.end(), value) != list.end();
}

static vector<user*> user_list_from_config(struct master_config config)
{
    vector<string> unique_users;
    for (config_it it = config.runs_as.begin(); it != config.runs_as.end(); it++)
        unique_users.push_back(it->first);
    for (config_it it = config.manages.begin(); it != config.manages.end(); it++)
        unique_users.push_back(it->first);
    unique_users.push_back(name_from_uid(0));

    unique_users = uniq(unique_users);

    dump_config(config);

    map<string,user*> users;
    vector<user*> user_list;

    for (vector<string>::iterator u = unique_users.begin(); u != unique_users.end(); u++) {
        try {
            user_list.push_back(users[*u] = new user(*u));
            users[*u]->create_dirs();
            users[*u]->open_server_socket();
        } catch (std::exception &e) {
            log(LOG_WARNING, "Ignoring %s: %s\n", u->c_str(), e.what());
        }
    }

    for (vector<user*>::iterator u = user_list.begin(); u != user_list.end(); u++) {
        (*u)->can_run_as_uid[(*u)->uid] = true; // You can always run as yourself, however ill-advised.
        if (config.runs_as.find((*u)->name) != config.runs_as.end()) {
            for (config_list_it name = config.runs_as[(*u)->name].begin(); name != config.runs_as[(*u)->name].end(); name++) {
                int uid = uid_from_name(*name);
                if (uid < 0)
                    log(LOG_ERR, "%s can't run as non-existant user \"%s\"\n", (*u)->name.c_str(), name->c_str());
                else
                    (*u)->can_run_as_uid[uid] = true;
            }
        }
        if (config.manages.find((*u)->name) != config.manages.end() || (*u)->uid == 0) {
            vector<string> *manage_list = (*u)->uid == 0 ? &unique_users // Root can manage all users.
                                                         : &config.manages[(*u)->name];
            for (config_list_it name = manage_list->begin(); name != manage_list->end(); name++) {
                if (!users[*name])
                    log(LOG_ERR, "%s can't manage non-existant user \"%s\"\n", (*u)->name.c_str(), name->c_str());
                else
                    (*u)->manages.push_back(users[*name]);
            }
            if (!contains((*u)->manages, *u)) // We can always manage ourselves.
                (*u)->manages.push_back(*u);
        }
    }
    return user_list;
}

static vector<class daemon*> load_daemons(vector<user*> user_list, vector<class daemon*>existing)
{
    sort(existing.begin(), existing.end(), daemon_compare);
    vector<class daemon*> daemons;
    // Now load up all the daemon config files for each user.
    for (vector<user*>::iterator u = user_list.begin(); u != user_list.end(); u++) {
        try {
            vector<string> confs = (*u)->config_files();
            for (vector<string>::iterator conf = confs.begin(); conf != confs.end(); conf++) {
                try {
                    class daemon *d = new class daemon(*conf, *u);
                    if (!binary_search(existing.begin(), existing.end(), d, daemon_compare)) {
                        daemons.push_back(d);
                        log(LOG_INFO, "Loaded daemon %s for %s\n", conf->c_str(), (*u)->name.c_str());
                    } else {
                        log(LOG_DEBUG, "Skipping daemon %s we've already seen\n", d->id.c_str());
                        delete d;
                    }
                } catch(std::exception &e) {
                    log(LOG_ERR, "Skipping %s's config file %s: %s\n", (*u)->name.c_str(), conf->c_str(), e.what());
                }
            }
        } catch (std::exception &e) {
            log(LOG_ERR, "Skipping %s's config files: %s\n", (*u)->name.c_str(), e.what());
        }
    }
    return daemons;
}

static void autostart(vector<class daemon*> daemons)
{
    // Now start all the daemons marked "autostart"
    for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
        if ((*d)->autostart)
            try { (*d)->start(); }
            catch(std::exception &e) { log(LOG_ERR, "Couldn't start %s: %s\n", (*d)->id.c_str(), e.what()); }
}

static void distribute_signal_to_children(int sig)
{
    log(LOG_DEBUG, "Signal: %s [%d]\n", strsignal(sig), sig);
    kill(0, sig);
    exit(EXIT_FAILURE);
}

static void handle_sig_child(int)
{
    log(LOG_DEBUG, "SIGCHLD\n");
}

static void select_loop(vector<user*> users, vector<class daemon*> daemons)
{
    signal(SIGCHLD, handle_sig_child);
    signal(SIGTERM, distribute_signal_to_children);
    signal(SIGINT,  distribute_signal_to_children);
    typedef map<int,user*> fd_map;
    typedef map<int,user*>::iterator fd_map_it;

    map<int,user*> listeners;
    map<int,user*> clients;

    for (vector<class user*>::iterator u = users.begin(); u != users.end(); u++)
        listeners[(*u)->command_socket] = *u;

    while (1) {
        struct pollfd fd[listeners.size() + clients.size()];
        int nfds = 0;

        for (fd_map_it lis = listeners.begin(); lis != listeners.end(); lis++, nfds++) {
            fd[nfds].fd = lis->first;
            fd[nfds].events = POLLIN;
        }
        for (fd_map_it cli = clients.begin(); cli != clients.end(); cli++, nfds++) {
            fd[nfds].fd = cli->first;
            fd[nfds].events = POLLIN;
        }

        time_t wait_time=-1; // infinite
        for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
            if ((*d)->state == coolingdown)
                wait_time = wait_time < 0 ? (*d)->cooldown_remaining() * 1000
                                          : min(wait_time, (*d)->cooldown_remaining() * 1000);

        int got = poll(fd, nfds, wait_time);

        // Cull daemons whose config files have been deleted
        for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end();)
            if (((*d)->state == stopped || (*d)->state == coolingdown) && !(*d)->exists()) {
                log(LOG_INFO, "Culling %s because %s has disappeared.\n", (*d)->id.c_str(), (*d)->config_file.c_str());
                delete *d;
                d = daemons.erase(d);
            } else
                d++;

        // Deal with input on the command sockets
        if (got > 0) {
            for (size_t i=0; i<lengthof(fd); i++) {
                if (fd[i].revents & POLLIN) {
                    if (key_exists(listeners, fd[i].fd)) {
                        struct sockaddr_un addr;
                        socklen_t addr_len;
                        int client = accept(fd[i].fd, (struct sockaddr*) &addr, &addr_len);
                        if (client == -1) {
                            log(LOG_WARNING, "accept() from socket %s failed: %s\n", listeners[fd[i].fd]->socket_path().c_str(), strerror(errno));
                            continue;
                        }
                        fcntl(client, F_SETFL, O_NONBLOCK);
                        clients[client] = listeners[fd[i].fd];
                    } else if (key_exists(clients, fd[i].fd)) {
                        char buf[1000];
                        int red = read(fd[i].fd, buf, sizeof(buf)-1);
                        if (red) {
                            if (buf[red-1] == '\n') red--;
                            buf[red] = '\0';
                            for (char *r = buf, *cmd; cmd = strsep(&r, "\n"); ) {
                                string resp = do_command(cmd, clients[fd[i].fd], &daemons);
                                int wrote = write(fd[i].fd, resp.c_str(), resp.length());
                                log(LOG_DEBUG, "Wrote %d bytes of response: %s\n", wrote, resp.c_str());
                            }
                        }
                    }
                }
                if (fd[i].revents & POLLHUP && key_exists(clients, fd[i].fd)) {
                    close(fd[i].fd);
                    clients.erase(fd[i].fd);
                }
            }
        }
        // Reap/respawn our children
        for (int kid; (kid = waitpid(-1, NULL, WNOHANG)) > 0;) {
            log(LOG_NOTICE, "Child %d exited\n", kid);
            for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
                if ((*d)->pid == kid) {
                    if ((*d)->state == running)
                        try { (*d)->respawn(); }
                        catch(std::exception &e) { log(LOG_ERR, "Couldn't respawn %s: %s\n", (*d)->id.c_str(), e.what()); }
                    else
                        (*d)->reap();
                    break;
                }
        }
        // Start up daemons that have cooled down
        for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
            if ((*d)->state == coolingdown && (*d)->cooldown_remaining() == 0) {
                log(LOG_INFO, "Cooldown time has arrived for %s\n", (*d)->id.c_str());
                try { (*d)->start(true); }
                catch(std::exception &e) { log(LOG_ERR, "Couldn't respawn %s: %s\n", (*d)->id.c_str(), e.what()); }
            }
    }
}

static vector<class daemon*> manageable_by_user(user *user, vector<class daemon*> daemons)
{
    vector<class daemon*> manageable;
    for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
        for (vector<class user*>::iterator u = user->manages.begin(); u != user->manages.end(); u++)
            if ((*d)->user == *u) {
                manageable.push_back(*d);
                break;
            }
    return manageable;
}

static string daemon_id_list(vector<class daemon*> daemons)
{
    string resp = "";
    for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
        resp += (resp.length() ? "," : "") + (*d)->id;
    return resp;
}

static string do_command(string command_line, user *user, vector<class daemon*> *daemons)
{
  try {
    vector<class daemon*> manageable = manageable_by_user(user, *daemons);
    size_t space = command_line.find_first_of(" ");
    string cmd = command_line.substr(0, space);
    string arg = space != command_line.npos ? command_line.substr(space+1, command_line.length()) : "";
    log(LOG_DEBUG, "line: \"%s\" cmd: \"%s\", arg: \"%s\"\n", command_line.c_str(), cmd.c_str(), arg.c_str());

    const string valid_commands[] = { "list", "status", "rescan", "start", "stop", "restart" };

    if (find(valid_commands, valid_commands + lengthof(valid_commands), cmd) == valid_commands + lengthof(valid_commands))
        throw_str("bad command \"%s\"", cmd.c_str());

    if (cmd == "list") {
        return "OK: " + daemon_id_list(manageable) + "\n";
    }

    if (cmd == "status") {
        string resp = strprintf("%-30s %-15s %6s %8s %8s %6s %6s\n", "daemon-id", "state", "pid", "respawns", "cooldown", "uptime", "total");
        for (vector<class daemon*>::iterator d = manageable.begin(); d != manageable.end(); d++)
          if (arg.empty() || arg == (*d)->id)
            resp += strprintf("%-30s %-15s %6d %8d %8d %6d %6d\n",
                              (*d)->id.c_str(),
                              (*d)->state_str().c_str(),
                              (*d)->pid,
                              (*d)->respawns,
                              (*d)->cooldown_remaining(),
                              (*d)->pid ? time(NULL) - (*d)->respawn_time : 0,
                              (*d)->pid ? time(NULL) - (*d)->start_time   : 0);
        return "OK: " + resp;
    }

    if (cmd == "rescan") {
        vector<class daemon*> new_daemons = load_daemons(user->manages, *daemons);
        if (new_daemons.size() == 0)
            return "OK: No new daemons found.\n";
        daemons->insert(daemons->end(), new_daemons.begin(), new_daemons.end());
        autostart(new_daemons);
        return "OK: New daemons scanned: " + daemon_id_list(new_daemons) + "\n";
    }

    vector<class daemon*>::iterator d = find_if(manageable.begin(), manageable.end(), daemon_id_match(arg));
    if (d == manageable.end()) throw_str("unknown id \"%s\"", arg.c_str());
    class daemon *daemon = *d;

    if      (cmd == "start")   if (daemon->pid) throw_str("Already running \"%s\"", daemon->id.c_str());
                               else daemon->start();
    else if (cmd == "stop")    daemon->stop();
    else if (cmd == "restart") { daemon->stop(); daemon->start(); }
    else throw_str("bad command \"%s\"", cmd.c_str());
  } catch (std::exception &e) {
      return string("ERR: ") + e.what() + "\n";
  }
  return "OK\n";
}

static void dump_config(struct master_config config)
{
    log(LOG_DEBUG, "Config:\n");
    log(LOG_DEBUG, " Runs as:\n");
    for (config_it it = config.runs_as.begin(); it != config.runs_as.end(); it++) {
        string s = "  "+it->first+": ";
        for (config_list_it lit = it->second.begin(); lit != it->second.end(); lit++) {
            s += " \""+*lit+"\"";
        }
        log(LOG_DEBUG, "%s\n", s.c_str());
    }

    log(LOG_DEBUG," Manages:\n");
    for (config_it it = config.manages.begin(); it != config.manages.end(); it++) {
        string s = "  "+it->first+": ";
        for (config_list_it lit = it->second.begin(); lit != it->second.end(); lit++) {
            s += " \""+*lit+"\"";
        }
        log(LOG_DEBUG, "%s\n", s.c_str());
    }
}

/*

=head1 NAME

daemon-manager - Allow users to setup and control daemons

=head1 SYNOPSIS

  daemon-manager [-h | --help] [-c | --config=<config-file>] [-v | --verbose] [-f | --foreground] [-d | --debug]

=head1 DESCRIPTION

B<daemon-manager> allows users to create and control their own background
processes (daemons). It allows these daemons to run as different users as
specifed by L<daemon.conf(5)> and permitted by L<daemon-manager.conf(5)>.

Once a daemon is running, B<daemon-manager> will respawn it if it ever
quits. If the daemon is quitting and respawning too quickly then
B<daemon-manager> will start delaying before respawning it. This delay is
called the cooldown time and is used to prevent heavy CPU usage when daemons
with severe problems quit the instant they are started.

=head1 CREATING DAEMONS

When it starts up, daemon-manager looks in "~/.daemon-manager/daemons" for
*.conf files that describe each daemon. For more information on the format
of the daemon config files please see L<daemon.conf(5)>. Once you have
created a daemon you can issue the C<rescan> command to L<dmctl(1)> which
will cause daemon-manager to rescan all the daemon config directories,
looking for new .conf files.

=head1 CONTROLLING DAEMONS

To start, stop, and inspect daemons, use the L<dmctl(1)> program.

=head1 DEBUGGING PROBLEMS

The daemon-manager process by default logs to syslog using the "daemon"
facility. It only logs messages of "Notice" priority or higher unless the
B<-v> option has been specified (see below).

If the daemons have been configured with the I<output> option set to C<log>
then their stdout and stderr will be redirected to a log file in
"~/.daemon-manager/logs/<daemon>.conf" where <daemon> is the basename of the
.conf file.

=head1 COMMAND LINE OPTIONS

This section describes the command line options for daemon-manager
master process itself.

=over

=item I<-h>

=item I<--help>

This option causes daemon-manager to print a quick usage line and then quit.

=item I<-c <config-file> >

=item I<--config=<config-file> >

Use this option to specify a non-standard path for the
L<daemon-manager.conf(5)> file, instead of the default
"/etc/daemon-manager/daemon-manager.conf" location.

Regardless of where it is, the file must be owned by root and it must not be
world readable.

=item I<-v>

=item I<--verbose>

Increase the logging verbosity. This option can be specified 2 times to
produce even more verbose output. The first time it will start outputting
syslog "Info" priority messages and the second time will add "Debug"
priority messages.

=item I<-f>

=item I<--foreground>

By default daemon-manager will detach and run in the background, returning
control to the shell when launched. Use this option to disable this feature
and keep daemon-manager running in the foreground.

=item I<-d>

=item I<--debug>

This option causes daemon-manager to log to stderr instead of syslog. As a
side effect, daemons with their I<output> option set to C<discard> will also
have their stderr and stdout streams output.

This option implies I<--foreground>.

=back

=head1 SEE ALSO

L<dmctl(1)>, L<daemon-manager.conf(5)>, L<daemon.conf(5)>

=cut

*/
