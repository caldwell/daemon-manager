//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "daemon.h"
#include "permissions.h"
#include "config.h"
#include "passwd.h"
#include "strprintf.h"
#include "log.h"
#include "posix-util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
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
    name = string(stem, ext ? ext - stem : strlen(stem));
    id = user->name + "/" + name;

    load_config();
}

void daemon::load_config()
{
    struct stat st = permissions::check(config_file, 0113, user->uid);
    if (st.st_mtime == config_file_stamp) return;

    map<string,string> cfg = parse_daemon_config(config_file);

    config.working_dir = cfg.count("dir") ? cfg["dir"] : "/";
    pwent pw = pwent(cfg.count("user") ? cfg["user"] : user->name);
    if (!pw.valid) throw_str("%s is not allowed to run as unknown user %s in %s\n", user->name.c_str(), cfg["user"].c_str(), config_file.c_str());
    if (!user->can_run_as_uid.count(pw.uid)) throw_str("%s is not allowed to run as %s[%u] in %s\n", user->name.c_str(), cfg["user"].c_str(), pw.uid, config_file.c_str());
    config.run_as = pw;
    if (!cfg.count("start")) throw_str("Missing \"start\" in %s\n", config_file.c_str());
    config.start_command = cfg["start"];
    config.autostart = !cfg.count("autostart") || strchr("YyTt1Oo", cfg["autostart"].c_str()[0]);
    config.log_output = cfg.count("output") && cfg["output"] == "log";
    config.want_sockfile = cfg.count("sockfile") && strchr("YyTt1Oo", cfg["sockfile"].c_str()[0]);

    config_file_stamp = st.st_mtime;
}

bool daemon::exists()
{
    struct stat st;
    return stat(config_file.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

string daemon::sock_file()
{
    return "/var/run/daemon-manager/" + config.run_as.name + "/" + id + ".socket";
}

void daemon::start(bool respawn)
{
    log(LOG_INFO, "Starting %s\n", id.c_str());

    load_config(); // Make sure we are up to date.

    int fd[2];
    if (pipe(fd) <0) throw_strerr("Couldn't pipe");
    fcntl(fd[0], F_SETFD, 1);
    fcntl(fd[1], F_SETFD, 1);
    int child = fork();
    if (child == -1) throw_strerr("Fork failed\n");
    if (child) {
        close(fd[1]);
        char err[1000]="";
        int red = read(fd[0], &err, sizeof(err));
        close(fd[0]);
        if(red > 0)
            throw runtime_error(string(err));
        current.pid = child; // Parent
        log(LOG_INFO, "Started %s. pid=%d\n", id.c_str(), current.pid);
        current.respawn_time = time(NULL);
        if (respawn)
            current.respawns++;
        else
            current.start_time = time(NULL);
        current.state = running;
        return;
    }

    // Child
    try {
        close(fd[0]);
        if (config.want_sockfile) {
            mkdir_ug("/var/run/daemon-manager/", 0755);
            mkdir_ug("/var/run/daemon-manager/" + config.run_as.name + "/", 0755, config.run_as.uid);
            mkdir_ug("/var/run/daemon-manager/" + config.run_as.name + "/" + user->name + "/", 0770, config.run_as.uid, user->gid);
        }
        if (config.log_output) {
            mkdir_ug(user->log_dir().c_str(), 0770, user->uid, user->gid);
            close(1);
            close(2);
            string logfile=user->log_dir() + name + ".log";
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
                    config.start_command.c_str());
        }
        initgroups(config.run_as.name.c_str(), config.run_as.gid)
                                          == -1 && throw_strerr("Couldn't init groups for %s", config.run_as.name.c_str());
        setgid(config.run_as.gid)         == -1 && throw_strerr("Couldn't set gid to %d\n", config.run_as.gid);
        setuid(config.run_as.uid)         == -1 && throw_strerr("Couldn't set uid to %d (%s)", config.run_as.uid, user->name.c_str());
        chdir(config.working_dir.c_str()) == -1 && throw_strerr("Couldn't change to directory %s", config.working_dir.c_str());

        vector<string> elist;
        elist.push_back("HOME=" + user->homedir);
        elist.push_back("LOGNAME=" + user->name);
        elist.push_back("SHELL=/bin/sh");
        elist.push_back("PATH=/usr/bin:/bin");
        if (config.want_sockfile)
            elist.push_back("SOCK_FILE=" + sock_file());
        const char *env[elist.size()+1];
        for (size_t i=0; i<elist.size(); i++)
            env[i] = elist[i].c_str();
        env[elist.size()] = NULL;
        execle("/bin/sh", "/bin/sh", "-c", config.start_command.c_str(), (char*)NULL, env);
        throw_strerr("Couldn't exec");
    } catch (std::exception &e) {
        write(fd[1], e.what(), strlen(e.what())+1/*NULL*/);
        close(fd[1]);
        exit(0);
    }
}

void daemon::stop()
{
    if (current.pid) {
        log(LOG_INFO, "Stopping [%d] %s\n", current.pid, id.c_str());
        kill(current.pid, SIGTERM);
        current.state = stopping;
    }
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
