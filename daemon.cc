//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "daemon.h"
#include "permissions.h"
#include "config.h"
#include "passwd.h"
#include "strprintf.h"
#include "log.h"
#include "posix-util.h"
#include "foreach.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <algorithm>
#include <stdexcept>
#include <libgen.h>
#include <grp.h>

using namespace std;

daemon::daemon(string config_file, class user *user)
        : config_file(config_file), config_file_stamp(-1), user(user)
{
    current = (struct current) { 0,stopped,0,0,0,0,0 };
    const char *stem = basename((char*)config_file.c_str());
    const char *ext = strstr(stem, ".conf");
    name = string(stem, ext ? (size_t)(ext - stem) : strlen(stem));
    id = user->name + "/" + name;

    load_config();
}

string daemon::get_and_clear_whines()
{
    string fine_whines;
    if (!whine_list.empty()) {
        foreach(string whine, whine_list)
            fine_whines += whine;
        whine_list.clear();
    }
    return fine_whines;
}

void daemon::load_config()
{
    struct stat st = permissions::check(config_file, 0113, user->uid);
    if (st.st_mtime == config_file_stamp) return;

    map<string,string> cfg = parse_daemon_config(config_file);

    // Look up all the keys and warn if we don't recognize them. Helps find typos in .conf files.
    const char *valid_keys[] = { "dir", "user", "start", "autostart", "output", "shell" };
    typedef pair<string,string> str_pair;
    foreach(str_pair c, cfg) {
        foreach(const char *key, valid_keys)
            if (c.first == key) goto found;
        { string warning = strprintf("Unknown key \"%s\" in config file \"%s\"\n", c.first.c_str(), config_file.c_str());
          log(LOG_WARNING, "%s", warning.c_str());
          whine_list.push_back("Warning: "+warning);
        } // C++ doesn't like "warning" being instantiated midsteram unless it goes out of scope before the goto label.
      found:;
    }


    config.working_dir = cfg.count("dir") ? cfg["dir"] : "/";
    pwent pw = pwent(cfg.count("user") ? cfg["user"] : user->name);
    if (!pw.valid) throw_str("%s is not allowed to run as unknown user %s in %s\n", user->name.c_str(), cfg["user"].c_str(), config_file.c_str());
    if (!user->can_run_as_uid.count(pw.uid)) throw_str("%s is not allowed to run as %s[%u] in %s\n", user->name.c_str(), cfg["user"].c_str(), pw.uid, config_file.c_str());
    config.run_as = pw;
    if (!cfg.count("start")) throw_str("Missing \"start\" in %s\n", config_file.c_str());
    config.start_command = cfg["start"];
    config.autostart = !cfg.count("autostart") || strchr("YyTt1Oo", cfg["autostart"].c_str()[0]);
    config.log_output = cfg.count("output") && cfg["output"] == "log";

    config_file_stamp = st.st_mtime;
}

bool daemon::exists()
{
    struct stat st;
    return stat(config_file.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

string daemon::log_file()
{
    return user->log_dir() + name + ".log";
}

#include <pwd.h>
// Just like initgroups() but actually check for errors ಠ_ಠ
int _initgroups(const char *user, gid_t user_gid)
{
    if (!user || !*user) throw_str("Bad username"); // getpwnam() returns something for ""!!
    struct passwd *pw = getpwnam(user);
    if (!pw) throw_str("User %s not found", user);

    return initgroups(user, user_gid);
}

void daemon::start(bool respawn)
{
    log(LOG_INFO, "Starting %s\n", id.c_str());

    load_config(); // Make sure we are up to date.

    current.pid = fork_setuid_exec(config.start_command);
    log(LOG_INFO, "Started %s. pid=%d\n", id.c_str(), current.pid);
    current.respawn_time = time(NULL);
    if (respawn)
        current.respawns++;
    else
        current.start_time = time(NULL);
    current.state = running;
}

int daemon::fork_setuid_exec(string command, map<string,string> env_in)
{
    log(LOG_INFO, "Launching %s\n", command.c_str());

    int fd[2];
    if (pipe(fd) <0) throw_strerr("Couldn't pipe");
    fcntl(fd[0], F_SETFD, FD_CLOEXEC);
    fcntl(fd[1], F_SETFD, FD_CLOEXEC);
    int child = fork();
    if (child == -1) throw_strerr("Fork failed\n");
    if (child) {
        // Parent
        close(fd[1]);
        char err[1000]="";
        int red = read(fd[0], &err, sizeof(err)-1);
        close(fd[0]);
        if(red > 0) {
            this->reap();
            throw runtime_error(string(err));
        }
        return child;
    }

    // Child
    try {
        close(fd[0]);
        if (config.log_output) {
            mkdir_ug(user->log_dir().c_str(), 0770, user->uid, user->gid);
            close(1);
            close(2);
            string logfile = log_file();
            open(logfile.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0750) ==  1 || throw_strerr("Couldn't open log file %s", logfile.c_str());
            dup2(1,2)                                                  == -1 && throw_strerr("Couldn't dup stdout to stderr");
            chown(logfile.c_str(), user->uid, user->gid)               == -1 && throw_strerr("Couldn't change %s to uid %d gid %d", logfile.c_str(), user->uid, user->gid);
            time_t t = time(NULL);
            const char *dashes="----------------------------------------------------------------------------------------------------";
            int dash_length = (int)id.size() + 24+9+2+2+4;
            fprintf(stderr,
                    "\n+%.*s+\n"
                    "| %.24s Starting \"%s\"... |\n"
                    "+%.*s+\n"
                    "> %s\n", dash_length, dashes,
                    ctime(&t), id.c_str(),
                    dash_length, dashes,
                    command.c_str());
        }
        _initgroups(config.run_as.name.c_str(), config.run_as.gid)
                                          == -1 && throw_strerr("Couldn't init groups for %s", config.run_as.name.c_str());
        setgid(config.run_as.gid)         == -1 && throw_strerr("Couldn't set gid to %d\n", config.run_as.gid);
        setuid(config.run_as.uid)         == -1 && throw_strerr("Couldn't set uid to %d (%s)", config.run_as.uid, user->name.c_str());
        chdir(config.working_dir.c_str()) == -1 && throw_strerr("Couldn't change to directory %s", config.working_dir.c_str());

        map<string,string> ENV = env_in;
        ENV.size(); // Work around clang bug (map<>::size doesn't get pulled in even though it appears in the below array declaration.
        ENV["HOME"]    = user->homedir;
        ENV["LOGNAME"] = user->name;
        ENV["PATH"]    = "/usr/bin:/bin";
        list<string> envs;                         // Storage for the full env c++ strings.
        const char *env[ENV.size()+1], **e = env;  // c-string pointers into envs[]
        typedef pair<string,string> pss;
        foreach(pss ep, ENV) {
            envs.push_front(ep.first + "=" + ep.second);
            *e++ = envs.front().c_str();
        }
        *e = NULL;
        execle("/bin/sh", "/bin/sh", "-c", command.c_str(), (char*)NULL, env);
        throw_strerr("Couldn't exec");
    } catch (std::exception &e) {
        write(fd[1], e.what(), strlen(e.what())+1/*NULL*/);
        close(fd[1]);
        exit(0);
    }
    return -1; // Can't happen--just here to shut gcc up.
}

void daemon::stop()
{
    if (current.pid) {
        log(LOG_INFO, "Stopping [%d] %s\n", current.pid, id.c_str());
        kill(current.pid, SIGTERM);
        current.state = stopping;
    }
    if (current.state == coolingdown)
        current.state = stopped;
    current.respawns = 0;
}


void daemon::respawn()
{
    reap();
    time_t now = time(NULL);
    time_t uptime = now - current.respawn_time;
    if (uptime < 60) current.cooldown = min((time_t)60, current.cooldown + 10); // back off if it's dying too often
    if (uptime > 60) current.cooldown = 0; // Clear cooldown on good behavior
    if (current.cooldown) {
        log(LOG_NOTICE, "%s is respawning too quickly, backing off. Cooldown time is %d seconds\n", id.c_str(), (int)current.cooldown);
        current.cooldown_start = now;
        current.state = coolingdown;
    } else
        start(true);
}

void daemon::reap()
{
    current.pid = 0;
    current.state = stopped;
}

time_t daemon::cooldown_remaining()
{
    return max((time_t)0, current.cooldown - (time(NULL) - current.cooldown_start));
}

bool daemon_compare(class daemon *a, class daemon *b)
{
    return a->config_file < b->config_file;
}

map<string,string> daemon::to_map()
{
    map <string,string> data;
    data["id"]                     = id;
    data["name"]                   = name;
    data["config_file"]            = config_file;
    data["user"]                   = user->name;
    data["current.pid"]            = strprintf("%d", current.pid);
    data["current.state"]          = _state_str[current.state];
    data["current.cooldown"]       = strprintf("%lld", (long long)current.cooldown);
    data["current.cooldown_start"] = strprintf("%lld", (long long)current.cooldown_start);
    data["current.respawns"]       = strprintf("%zd", current.respawns);
    data["current.start_time"]     = strprintf("%lld", (long long)current.start_time);
    data["current.respawn_time"]   = strprintf("%lld", (long long)current.respawn_time);
    return data;
}

#include "lengthof.h"
void daemon::from_map(map<string,string> data)
{
    if (id          != data["id"])          throw_str("ids do not match: \"%s\" vs \"%s\"!", id.c_str(), data["id"].c_str());
    if (name        != data["name"])        throw_str("names do not match: \"%s\" vs \"%s\"!", name.c_str(), data["name"].c_str());
    if (config_file != data["config_file"]) throw_str("config_files do not match: \"%s\" vs \"%s\"!", config_file.c_str(), data["config_file"].c_str());
    if (user->name  != data["user"])        throw_str("users do not match: \"%s\" vs \"%s\"!", user->name.c_str(), data["user"].c_str());

    for (size_t current_state = 0; current_state < lengthof(_state_str); current_state++)
        if (_state_str[current_state] == data["current.state"]) {
            current.state = (run_state) current_state;
            goto found;
        }
    throw_str("current.state \"%s\" is invalid!", data["current.state"].c_str());
  found:

    current.pid            = strtoul(data["current.pid"].c_str(), NULL, 10);
    current.cooldown       = strtoull(data["current.cooldown"].c_str(), NULL, 10);
    current.cooldown_start = strtoull(data["current.cooldown_start"].c_str(), NULL, 10);
    current.respawns       = strtoul(data["current.respawns"].c_str(), NULL, 10);
    current.start_time     = strtoull(data["current.start_time"].c_str(), NULL, 10);
    current.respawn_time   = strtoull(data["current.respawn_time"].c_str(), NULL, 10);
}
