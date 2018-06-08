//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <list>
#include <algorithm>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <pwd.h>
#include "config.h"
#include "user.h"
#include "daemon.h"
#include "command-sock.h"
#include "passwd.h"
#include "permissions.h"
#include "lengthof.h"
#include "strprintf.h"
#include "log.h"
#include "uniq.h"
#include "options.h"
#include "foreach.h"
#include "stringutil.h"
#include "json-escape.h"
#include "peercred.h"

using namespace std;

static void usage(char *me, int exit_code)
{
    printf("Usage:\n\t%s [-h | --help] [-c | --config=<config-file>] [-v | --verbose] [-f | --foreground] [-d | --debug]\n", me);
    exit(exit_code);
}

static void daemonize();
static void create_pidfile(string pidfile);
static vector<user*> user_list_from_config(struct master_config config);
static vector<class daemon*>empty_daemon_list(0);
static vector<class daemon*> load_daemons(vector<user*> user_list, vector<class daemon*>existing = empty_daemon_list);
static void import_daemons(vector<class daemon*> daemons, FILE *f);
static void export_daemons(vector<class daemon*> daemons, FILE *f);
static void autostart(vector<class daemon*> daemons);
static int open_server_socket();
static void select_loop(vector<user*> users, vector<class daemon*> daemons, int command_socket_fd);
static vector<class daemon*> manageable_by_user(user *user, vector<class daemon*> daemons);
static string do_command(string command_line, user *user, vector<class daemon*> *daemons);
static void dump_config(struct master_config config);

static char *daemon_manager_exe_path;
static char **daemon_manager_argv;
int main(int argc, char **argv)
{
    daemon_manager_exe_path = argv[0];
    daemon_manager_argv = argv;

    string config_path("/etc/daemon-manager/daemon-manager.conf");
    int verbose = 0;
    bool foreground = false;
    bool debug = false;
    string pidfile;

    options o(argc, argv);
    if (o.get("help",       'h'))               { usage(argv[0], EXIT_SUCCESS); }
    if (o.get("config",     'c', arg_required)) { config_path = string(o.arg); }
    if (o.get("verbose",    'v'))               { verbose = o.argm.size(); }
    if (o.get("pidfile",    'p', arg_required)) { pidfile = o.arg; }
    if (o.get("foreground", 'f'))               { foreground = true; }
    if (o.get("debug",      'd'))               { debug = foreground = true; }
    if (o.get("version"))                       { printf("daemon-manager version " VERSION "\n"); exit(EXIT_SUCCESS); }
    if (o.bad_args() ||
        o.args.size()) usage(argv[0], EXIT_FAILURE);


    init_log(!debug, min(LOG_DEBUG, LOG_NOTICE + verbose));

    struct master_config config;
    try {
        permissions::check(config_path, 0113, 0, 0);
        config = parse_master_config(config_path);
    } catch(std::exception &e) {
        log(LOG_ERR, "Couldn't load config file: %s\n", e.what());
        exit(EXIT_FAILURE);
    }

    bool reincarnating = !!getenv("dm_running_daemons_fd");

    if (!foreground && !reincarnating)
        daemonize();

    if (pidfile != "")
        create_pidfile(pidfile);

    int command_socket_fd;
    try { command_socket_fd = open_server_socket(); }
    catch(std::exception &e) {
        log(LOG_ERR, "Couldn't open command socket: %s\n", e.what());
        exit(EXIT_FAILURE);
    }

    vector<user*> users = user_list_from_config(config);

    vector<class daemon*> daemons = load_daemons(users);

    if (reincarnating) {
        FILE *import = fdopen(atoi(getenv("dm_running_daemons_fd")), "r+");
        try {
            if (!import) throw_strerr("fdopen() failed");
            unsetenv("dm_running_daemons_fd");
            import_daemons(daemons, import);
        } catch(std::exception &e) {
            log(LOG_ERR, "Couldn't import old state: %s\n", e.what());
            log(LOG_ERR, "Daemons have all been forgotten--kill the pids manually. Sorry.");
        }
        if (import)
            fclose(import);
    }

    autostart(daemons);

    select_loop(users, daemons, command_socket_fd);

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

static string pidfile;
static void unlink_pidfile() {
    unlink(pidfile.c_str());
}
static void create_pidfile(string _pidfile)
{
    pidfile = _pidfile;
    FILE *p = fopen(pidfile.c_str(), "w");
    if (!p) { log(LOG_ERR, "Couldn't open pid file \"%s\": %s\n", pidfile.c_str(), strerror(errno)); exit(1); }
    fprintf(p, "%d\n", getpid());
    fclose(p);
    atexit(unlink_pidfile);
}

template<class T>
bool contains(vector<T> list, T value)
{
    return find(list.begin(), list.end(), value) != list.end();
}

static vector<user*> user_list_from_config(struct master_config config)
{
    vector<string> unique_users;
    for (config_it it = config.can_run_as.begin(); it != config.can_run_as.end(); it++)
        unique_users.push_back(it->first);
    for (config_it it = config.manages.begin(); it != config.manages.end(); it++)
        unique_users.push_back(it->first);
    unique_users.push_back(name_from_uid(0));

    unique_users = uniq(unique_users);

    dump_config(config);

    map<string,user*> users;
    vector<user*> user_list;

    foreach(string name, unique_users) {
        class user *u=NULL;
        try {
            u = new user(name);
            u->create_dirs();
            user_list.push_back(users[name] = u);
        } catch (std::exception &e) {
            log(LOG_WARNING, "Ignoring %s: %s\n", name.c_str(), e.what());
            delete u;
        }
    }

    foreach(user *u, user_list) {
        u->can_run_as_uid[u->uid] = true; // You can always run as yourself, however ill-advised.
        if (config.can_run_as.find(u->name) != config.can_run_as.end()) {
            for (config_list_it name = config.can_run_as[u->name].begin(); name != config.can_run_as[u->name].end(); name++) {
                uid_t uid = uid_from_name(*name);
                if (uid == (uid_t)-1)
                    log(LOG_ERR, "%s can't run as non-existant user \"%s\"\n", u->name.c_str(), name->c_str());
                else
                    u->can_run_as_uid[uid] = true;
            }
        }
        if (config.manages.find(u->name) != config.manages.end() || u->uid == 0) {
            vector<string> *manage_list = u->uid == 0 ? &unique_users // Root can manage all users.
                                                      : &config.manages[u->name];
            for (config_list_it name = manage_list->begin(); name != manage_list->end(); name++) {
                if (!users[*name])
                    log(LOG_ERR, "%s can't manage non-existant user \"%s\"\n", u->name.c_str(), name->c_str());
                else
                    u->manages.push_back(users[*name]);
            }
        }
        if (!contains(u->manages, u)) // We can always manage ourselves.
            u->manages.push_back(u);
    }
    return user_list;
}

static vector<class daemon*> load_daemons(vector<user*> user_list, vector<class daemon*>existing)
{
    sort(existing.begin(), existing.end(), daemon_compare);
    vector<class daemon*> daemons;
    foreach(class user *u, user_list) {
        try {
            foreach(string conf, u->config_files()) {
                try {
                    class daemon *d = new class daemon(conf, u);
                    if (!binary_search(existing.begin(), existing.end(), d, daemon_compare)) {
                        daemons.push_back(d);
                        log(LOG_INFO, "Loaded daemon %s for %s\n", conf.c_str(), u->name.c_str());
                    } else {
                        log(LOG_DEBUG, "Skipping daemon %s we've already seen\n", d->id.c_str());
                        delete d;
                    }
                } catch(std::exception &e) {
                    log(LOG_ERR, "Skipping %s's config file %s: %s\n", u->name.c_str(), conf.c_str(), e.what());
                }
            }
        } catch (std::exception &e) {
            log(LOG_ERR, "Skipping %s's config files: %s\n", u->name.c_str(), e.what());
        }
    }
    return daemons;
}

static void autostart(vector<class daemon*> daemons)
{
    // Now start all the daemons marked "autostart"
    foreach(class daemon *d, daemons)
        if (d->config.autostart && d->current.state == stopped)
            try { d->start(); }
            catch(std::exception &e) { log(LOG_ERR, "Couldn't start %s: %s\n", d->id.c_str(), e.what()); }
}

static void kill_unimported(map<string,string> data)
{
    if (data.find("current.pid") != data.end() &&
        data.find("current.state") != data.end() &&
        data["current.state"] == "running") {
        unsigned int pid  = strtoul(data["current.pid"].c_str(), NULL, 10);
        log(LOG_NOTICE, "Killing PID %d from old daemon \"%s\": unimportable running daemon\n", pid, data["id"].c_str());
        kill(pid, SIGTERM);
    } else
        log(LOG_NOTICE, "Forgetting %s daemon \"%s\"\n", data["current.state"].c_str(), data["id"].c_str());
}



pair<string,string> parse_json_key_value(string line)
{
#define inc() if (++c == line.end()) throw_str("unterminated string");
    pair<string,string> p;
    string::const_iterator c = line.begin();
    for (int pi=0; pi<2; pi++) {
        while (*c != '\"')
            inc();
        inc();
        string::const_iterator start = c;
        char last=0;
        while (*c != '\"' || last == '\\') {
            last = *c;
            inc();
        }
        string::const_iterator end = c;
        inc();
        if (*c == ':')
            inc();

        if (pi == 0) p.first  = json_unescape(string(start, end));
        else         p.second = json_unescape(string(start, end));
    }
    //log(LOG_INFO, "Parsing JSON line: key={{%s}} value={{%s}}\n", p.first.c_str(), p.second.c_str());
    return p;
#undef inc
}

static void import_daemons(vector<class daemon*> daemons, FILE *f)
{
    map<string, class daemon*> daemon_by_config;
    foreach(class daemon *d, daemons)
        daemon_by_config[d->config_file] = d;

    char buffer[10000];
    int red = fread(buffer, 1, sizeof(buffer), f);
    fseek(f, 0, SEEK_SET);
    log(LOG_INFO, "Importing: %.*s\n----\n", red, buffer);


    int version;
    fscanf(f, "{ \"version\": %d\n", &version) == 1 || throw_str("Missing version");
    version == 1 || throw_str("Version == %d and not 1", version);
    fscanf(f, " \"daemons\": [\n");
    char line[10000];
    while (fgets(line, sizeof(line), f))
        if (line[strspn(line, " ")] == '{') {
            map<string,string> data;
            while (fgets(line, sizeof(line), f))
                if (line[strspn(line, " ")] != '}')
                    data.insert(parse_json_key_value(line));
                else {

                    if (daemon_by_config[data["config_file"]]) {
                        try {
                            daemon_by_config[data["config_file"]]->from_map(data);
                            log(LOG_INFO, "Reimport \"%s\" successfully.\n", daemon_by_config[data["config_file"]]->id.c_str());
                        } catch(std::exception &e) {
                            log(LOG_ERR, "Couldn't reimport \"%s\": %s\n", daemon_by_config[data["config_file"]]->id.c_str(), e.what());
                            kill_unimported(data);
                        }
                    } else
                        kill_unimported(data);

                    break;
                }
        }
}

static void export_daemons(vector<class daemon*> daemons, FILE *f)
{
    fprintf(f, "{\n"
               "    \"version\": 1,\n");
    fprintf(f, "    \"daemons\": [\n");
    list<string> daemonrep;
    foreach(class daemon *d, daemons) {
        map<string,string> dmap = d->to_map();
        list<string> keyval;
        typedef pair<string,string> str_pair;
        foreach(str_pair dr, dmap)
            keyval.push_back(strprintf("            \"%s\": \"%s\"", json_escape(dr.first).c_str(), json_escape(dr.second).c_str()));
        daemonrep.push_back(strprintf("        {\n"
                                      "%s\n"
                                      "        }", join(keyval, ",\n").c_str()));
    }
    fprintf(f, "%s\n", join(daemonrep, ",\n").c_str());
    fprintf(f, "    ]\n"
               "}\n");
}

static void reincarnate(vector<class daemon*> daemons)
{
    FILE *f = NULL;
    try {
        char Template[] = "/tmp/daemon-manager-running-daemons.XXXXXXXXXXXX";
        int fd = mkstemp(Template);
        if (fd == -1) throw_strerr("mkstemp() failed [%s]", Template);
        unlink(Template);
        FILE *f = fdopen(fd, "w+");
        export_daemons(daemons, f);
        fflush(f);
        lseek(fd, 0, SEEK_SET);
        setenv("dm_running_daemons_fd", strprintf("%d", fd).c_str(), 1) == 0 || throw_strerr("setenv() failed");
        log(LOG_INFO, "Re-execing ourselves...\n");
        execv(daemon_manager_exe_path, daemon_manager_argv);
        throw_strerr("exec() failed");
    } catch(std::exception &e) {
        if (f)
            fclose(f);
        log(LOG_ERR, "Couldn't re-exec ourselves: %s\n", e.what());
    }
}

static int open_server_socket()
{
    struct sockaddr_un addr = command_sock_addr();
    struct stat st;
    if(stat(addr.sun_path, &st) == 0)
        unlink(addr.sun_path)                                    == 0 || throw_strerr("Couldn't remove old socket @ %s", addr.sun_path);

    int command_socket = socket(PF_LOCAL, SOCK_STREAM /*SOCK_DGRAM*/, 0);
    if (command_socket < 0) throw_strerr("socket() failed");
    fcntl(command_socket, F_SETFD, FD_CLOEXEC)                  == -1 && throw_strerr("Couldn't set socket to close on exec");
    ::bind(command_socket, (struct sockaddr*) &addr, sizeof(sa_family_t) + strlen(addr.sun_path) + 1)
                                                                 == 0 || throw_strerr("Binding to socket %s failed", addr.sun_path);
    listen(command_socket, 1)                                    == 0 || throw_strerr("listen(%s) failed", addr.sun_path);

    // Needs to be world read/writable so that all users can connect (we authrorize them when they connect)
    chmod(addr.sun_path, 0777)                                   == 0 || throw_strerr("chmod %s, 0777", addr.sun_path);
    return command_socket;
}

int time_to_die;
static void handle_sig_term_or_int(int sig)
{
    time_to_die = sig;
}

int child_mortality;
static void handle_sig_child(int)
{
    child_mortality++;
}

bool hup_two_three_four;
static void handle_sig_hup(int)
{
    hup_two_three_four = true;
}

static void select_loop(vector<user*> users, vector<class daemon*> daemons, int command_socket_fd)
{
    signal(SIGCHLD, handle_sig_child);
    signal(SIGTERM, handle_sig_term_or_int);
    signal(SIGINT,  handle_sig_term_or_int);
    signal(SIGHUP,  handle_sig_hup);
    signal(SIGPIPE, SIG_IGN);
    typedef map<int,user*>::iterator fd_map_it;

    map<int,user*> clients;
    map<uid_t,user*> users_by_id;

    foreach(class user *u, users)
        users_by_id[u->uid] = u;

    while (1) {
        // Handle signals:
        if (hup_two_three_four) {
            log(LOG_DEBUG, "SIGHUP\n");
            hup_two_three_four = false;
            reincarnate(daemons);
        }
        if (child_mortality) {
            log(LOG_DEBUG, "SIGCHILD\n");
            child_mortality = 0;
        }
        if (time_to_die > 0) {
            time_to_die = -1; // only print this stuff once.
            log(LOG_INFO, "Shutting down due to %s.\n", time_to_die == SIGTERM ? "SIGTERM" : "SIGINT");

            log(LOG_INFO, "Stopping all running daemons...\n");
        }
        if (time_to_die) {
            // We re-stop running stuff every time through the main loop since dmctl may start something while
            // we're waiting for something else to stop.
            bool quiesced = true;
            foreach(class daemon *d, daemons) {
                if (d->current.state == running || d->current.state == coolingdown)
                    d->stop();
                if (d->current.state != stopped)
                    quiesced = false;
            }
            if (quiesced) {
                log(LOG_INFO, "Terminating.\n");
                exit(EXIT_SUCCESS);
            }
        }

        // Wait for something to happen.
        struct pollfd fd[1/*command_socket*/ + clients.size()];
        int nfds = 0;

        fd[nfds].fd = command_socket_fd;
        fd[nfds++].events = POLLIN;
        for (fd_map_it cli = clients.begin(); cli != clients.end(); cli++, nfds++) {
            fd[nfds].fd = cli->first;
            fd[nfds].events = POLLIN;
        }

        time_t wait_time=-1; // infinite
        foreach(class daemon *d, daemons)
            if (d->current.state == coolingdown)
                wait_time = wait_time < 0 ? d->cooldown_remaining() * 1000
                                          : min(wait_time, d->cooldown_remaining() * 1000);

        int got = poll(fd, nfds, wait_time);

        // Cull daemons whose config files have been deleted
        for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end();)
            if (((*d)->current.state == stopped || (*d)->current.state == coolingdown) && !(*d)->exists()) {
                log(LOG_INFO, "Culling %s because %s has disappeared.\n", (*d)->id.c_str(), (*d)->config_file.c_str());
                delete *d;
                d = daemons.erase(d);
            } else
                d++;

        // Deal with input on the command sockets
        if (got > 0) {
            for (size_t i=0; i<lengthof(fd); i++) {
                if (fd[i].revents & POLLIN) {
                    if (fd[i].fd == command_socket_fd) {
                        struct sockaddr_un addr;
                        socklen_t addr_len = sizeof(addr);
                        int client = accept(command_socket_fd, (struct sockaddr*) &addr, &addr_len);
                        if (client == -1) {
                            log(LOG_WARNING, "accept() from command socket failed: %s\n", strerror(errno));
                            continue;
                        }
                        fcntl(client, F_SETFD, FD_CLOEXEC);
                        fcntl(client, F_SETFL, O_NONBLOCK);
                        uid_t uid;
                        try {
                            uid = get_peer_uid(client);
                            if (!users_by_id.count(uid)) {
                                struct passwd *p = getpwuid(uid);
                                throw_str("Not authorized. \"%s\" (uid %d) is not in the daemon-manager.conf file", p ? p->pw_name : "unknown user", uid);
                            }
                        } catch(std::exception &e) {
                            log(LOG_WARNING, "Command socket: %s\n", e.what());
                            string resp = string("ERR: ")+e.what()+"\n";
                            ssize_t wrote = write(client, resp.c_str(), resp.length());
                            if (wrote < 0)                       log(LOG_WARNING, "Couldn't write err to client socket %d: %s\n", client, strerror(errno));
                            if (wrote != (ssize_t)resp.length()) log(LOG_WARNING, "Couldn't write err to client socket %d (%zd vs %lu)\n", client, wrote, resp.length());
                            close(client);
                            continue;
                        }
                        clients[client] = users_by_id[uid];
                    } else if (clients.count(fd[i].fd)) {
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
                if (fd[i].revents & POLLHUP && clients.count(fd[i].fd)) {
                    close(fd[i].fd);
                    clients.erase(fd[i].fd);
                }
            }
        }
        // Reap/respawn our children
        for (int kid; (kid = waitpid(-1, NULL, WNOHANG)) > 0;) {
            log(LOG_NOTICE, "Child %d exited\n", kid);
            foreach(class daemon *d, daemons)
                if (d->current.pid == kid) {
                    if (d->current.state == running)
                        try { d->respawn(); }
                        catch(std::exception &e) { log(LOG_ERR, "Couldn't respawn %s: %s\n", d->id.c_str(), e.what()); }
                    else
                        d->reap();
                    break;
                }
        }
        // Start up daemons that have cooled down
        foreach(class daemon *d, daemons)
            if (d->current.state == coolingdown && d->cooldown_remaining() == 0) {
                log(LOG_INFO, "Cooldown time has arrived for %s\n", d->id.c_str());
                try { d->start(true); }
                catch(std::exception &e) { log(LOG_ERR, "Couldn't respawn cooled-down %s: %s\n", d->id.c_str(), e.what()); }
            }
    }
}

static vector<class daemon*> manageable_by_user(user *user, vector<class daemon*> daemons)
{
    vector<class daemon*> manageable;
    foreach(class daemon *d, daemons)
        foreach(class user *u, user->manages)
            if (d->user == u) {
                manageable.push_back(d);
                break;
            }
    return manageable;
}

static string daemon_id_list(vector<class daemon*> daemons)
{
    string resp = "";
    foreach(class daemon *d, daemons)
        resp += (resp.length() ? "," : "") + d->id;
    return resp;
}

#include <list>
#include "stringutil.h"
static string elapsed(time_t seconds)
{
    std::list<string> s;
                 s.push_front(strprintf("%lds", seconds % 60)); seconds /= 60;
    if (seconds) s.push_front(strprintf("%ldm", seconds % 60)); seconds /= 60;
    if (seconds) s.push_front(strprintf("%ldh", seconds % 24)); seconds /= 24;
    if (seconds) s.push_front(strprintf("%ldd", seconds %  7)); seconds /=  7;
    if (seconds) s.push_front(strprintf("%ldw", seconds     ));

    s.resize(2);
    return join(s, "");
}

static string do_command(string command_line, user *user, vector<class daemon*> *daemons)
{
  try {
    vector<class daemon*> manageable = manageable_by_user(user, *daemons);
    size_t space = command_line.find_first_of(" ");
    string cmd = command_line.substr(0, space);
    string arg = space != command_line.npos ? command_line.substr(space+1, command_line.length()) : "";
    log(LOG_DEBUG, "line: \"%s\" cmd: \"%s\", arg: \"%s\"\n", command_line.c_str(), cmd.c_str(), arg.c_str());

    const string valid_commands[] = { "list", "status", "rescan", "start", "stop", "restart", "logfile", "pid" };

    if (find(valid_commands, valid_commands + lengthof(valid_commands), cmd) == valid_commands + lengthof(valid_commands))
        throw_str("bad command \"%s\"", cmd.c_str());

    if (cmd == "list") {
        return "OK: " + daemon_id_list(manageable) + "\n";
    }

    if (cmd == "status") {
        string resp = strprintf("%-30s %-15s %6s %8s %8s %8s %8s\n", "daemon-id", "state", "pid", "respawns", "cooldown", "uptime", "total");
        foreach(class daemon *d, manageable)
          if (arg.empty() || arg == d->id)
            resp += strprintf("%-30s %-15s %6d %8zd %8s %8s %8s\n",
                              d->id.c_str(),
                              d->state_str().c_str(),
                              d->current.pid,
                              d->current.respawns,
                              elapsed(d->cooldown_remaining()).c_str(),
                              elapsed(d->current.pid ? time(NULL) - d->current.respawn_time : 0).c_str(),
                              elapsed(d->current.pid ? time(NULL) - d->current.start_time   : 0).c_str())
                + d->get_and_clear_whines();
        return "OK: " + resp;
    }

    if (cmd == "rescan") {
        vector<class daemon*> new_daemons = load_daemons(user->manages, *daemons);
        if (new_daemons.size() == 0)
            return "OK: No new daemons found.\n";
        daemons->insert(daemons->end(), new_daemons.begin(), new_daemons.end());
        autostart(new_daemons);
        string fine_whines;
        foreach(class daemon *d, new_daemons)
            fine_whines += d->get_and_clear_whines();
        return "OK: New daemons scanned: " + daemon_id_list(new_daemons) + "\n" + fine_whines;
    }

    vector<class daemon*>::iterator d = find_if(manageable.begin(), manageable.end(), daemon_id_match(arg));
    if (d == manageable.end()) throw_str("unknown id \"%s\"", arg.c_str());
    class daemon *daemon = *d;

    if      (cmd == "start")   if (daemon->current.pid) throw_str("Already running \"%s\"", daemon->id.c_str());
                               else daemon->start();
    else if (cmd == "stop")    daemon->stop();
    else if (cmd == "restart") { daemon->stop(); daemon->start(); }
    else if (cmd == "logfile") return "OK: " + daemon->log_file();
    else if (cmd == "pid")     if (!daemon->current.pid) throw_str("\"%s\" isn't running", daemon->id.c_str());
                               else return strprintf("OK: %d\n", daemon->current.pid);
    else throw_str("bad command \"%s\"", cmd.c_str());
    if (!daemon->whine_list.empty())
        return "OK: " + daemon->get_and_clear_whines();
  } catch (std::exception &e) {
      return string("ERR: ") + e.what() + "\n";
  }
  return "OK\n";
}

static void dump_config(struct master_config config)
{
    log(LOG_DEBUG, "Config:\n");
    log(LOG_DEBUG, " Can run as:\n");
    for (config_it it = config.can_run_as.begin(); it != config.can_run_as.end(); it++) {
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
////

daemon-manager(1)
=================
David Caldwell <david@porkrind.org>

NAME
----
daemon-manager - Allow users to setup and control daemons


SYNOPSIS
--------
daemon-manager [*-h* | *--help*] [*-c* | *--config=<config-file>*] [*-v* | *--verbose*] [*-f* | *--foreground*] [*-d* | *--debug*]

DESCRIPTION
-----------
'*daemon-manager*' allows users to create and control their own background
processes (daemons). It allows these daemons to run as different users as
specifed by 'daemon.conf(5)' and permitted by 'daemon-manager.conf(5)'.

Once a daemon is running, 'daemon-manager' will respawn it if it ever
quits. If the daemon is quitting and respawning too quickly then
'daemon-manager' will start delaying before respawning it. This delay is
called the cooldown time and is used to prevent heavy CPU usage when daemons
with severe problems quit the instant they are started.

CREATING DAEMONS
----------------
When it starts up, 'daemon-manager' looks in "~/.daemon-manager/daemons" for
*.conf files that describe each daemon. For more information on the format of
the daemon config files please see 'daemon.conf(5)'. Once you have created a
daemon you can issue the `'rescan'' command to 'dmctl(1)' which will cause
'daemon-manager' to rescan all the daemon config directories looking for new
.conf files.

CONTROLLING DAEMONS
-------------------
To start, stop, and inspect daemons, use the 'dmctl(1)' program.

DEBUGGING PROBLEMS
------------------
The 'daemon-manager' process by default sends log messages to syslog using the
"daemon" facility. It only logs messages of "Notice" priority or higher unless
the *-v* option has been specified (see below).

If the daemons have been configured with the `'output'' option set to `'log''
then their stdout and stderr will be redirected to a log file in
"~/.daemon-manager/logs/'<daemon>'.log" where '<daemon>' is the basename of the
.conf file.

COMMAND LINE OPTIONS
--------------------
This section describes the command line options for daemon-manager
master process itself.

*-h*::
*--help*::

   This option causes 'daemon-manager' to print a quick usage line and then
   quit.

*-c* '<config-file>'::
*--config*='<config-file>'::

  Use this option to specify a non-standard path for the
  'daemon-manager.conf(5)' file, instead of the default
  "/etc/daemon-manager/daemon-manager.conf" location.
  +
  Regardless of where it is, the file must be owned by root and it must not be
  world writable.

*-v*::
*--verbose*::

  Increase the logging verbosity. This option can be specified 2 times to
  produce even more verbose output. The first time it will start outputting
  syslog "Info" priority messages and the second time will add "Debug"
  priority messages.

*-f*::
*--foreground*::

  By default 'daemon-manager' will detach and run in the background, returning
  control to the shell when launched. Use this option to disable this feature
  and keep 'daemon-manager' running in the foreground.

*-d*::
*--debug*::

  This option causes 'daemon-manager' to log to stderr instead of syslog. As a
  side effect, daemons with their 'output' option set to +discard+ will also
  have their stderr and stdout streams output.
  +
  This option implies *--foreground*.

SEE ALSO
--------
'dmctl(1)', 'daemon-manager.conf(5)', 'daemon.conf(5)'

//*/
