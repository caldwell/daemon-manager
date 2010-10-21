//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "daemon.h"
#include "permissions.h"
#include "config.h"
#include "passwd.h"
#include "strprintf.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <algorithm>

using namespace std;

daemon::daemon(string config_file, class user *user) : config_file(config_file), config_file_stamp(-1), user(user), state(stopped), cooldown(0)
{
    load_config();
}

template<class T,class U>
bool exists(map<T,U> m, T k)
{
    return m.find(k) != m.end();
}

void daemon::load_config()
{
    struct stat st = permissions::check(config_file, 0113, user->uid);
    if (st.st_mtime == config_file_stamp) return;

    map<string,string> config = parse_daemon_config(config_file);

    working_dir = exists(config, string("dir")) ? config["dir"] : "/";
    int uid = exists(config, string("dir")) ? uid_from_name(config["user"]) : user->uid;
    if (uid < 0) throw strprintf("%s is not allowed to run as unknown user %s in %s\n", user->name.c_str(), config["user"].c_str(), config_file.c_str());
    if (!user->can_run_as_uid[uid]) throw strprintf("%s is not allowed to run as %s in %s\n", user->name.c_str(), config["user"].c_str(), config_file.c_str());
    run_as_uid = uid;
    if (!exists(config, string("start"))) throw strprintf("Missing \"start\" in %s\n", config_file.c_str());
    start_command = config["start"];
    autostart = !exists(config, string("autostart")) || strchr("YyTt1Oo", config["autostart"].c_str()[0]);


    config_file_stamp = st.st_mtime;
}

#include <libgen.h>
string daemon::id()
{
    const char *stem = basename((char*)config_file.c_str());
    const char *ext = strstr(stem, ".conf");
    return user->name + "/" + string(stem, ext ? ext - stem : strlen(stem));
}

string daemon::sock_file()
{
    return "/var/run/daemon-manager/" + name_from_uid(run_as_uid) + "/" + id() + ".socket";
}

static void mkdirs(string path, mode_t mode, int uid=-1, int gid=-1)
{
    mkdir(path.c_str(), mode);
    chown(path.c_str(), uid, gid);
}

void daemon::create_sock_dir()
{
    mkdirs("/var/run/daemon-manager/", 0755);
    mkdirs("/var/run/daemon-manager/" + name_from_uid(run_as_uid) + "/", 0770, run_as_uid);
    mkdirs("/var/run/daemon-manager/" + name_from_uid(run_as_uid) + "/" + user->name + "/", 0770, run_as_uid, user->gid);
}

void daemon::start(bool respawn)
{
    log(LOG_INFO, "Starting %s\n", id().c_str());

    int fd[2];
    if (pipe(fd) <0) throw strprintf("Couldn't pipe: %s", strerror(errno));
    fcntl(fd[0], F_SETFD, 1);
    fcntl(fd[1], F_SETFD, 1);
    int child = fork();
    if (child == -1) throw strprintf("Fork failed: %s\n", strerror(errno));
    if (child) {
        close(fd[1]);
        char err[1000]="";
        int red = read(fd[0], &err, sizeof(err));
        close(fd[0]);
        if(red > 0)
            throw string(err);
        pid = child; // Parent
        log(LOG_INFO, "Started %s. pid=%d\n", id().c_str(), pid);
        respawn_time = time(NULL);
        if (respawn)
            respawns++;
        else
            start_time = time(NULL);
        state = running;
        return;
    }

    // Child
    try {
        close(fd[0]);
        create_sock_dir();
        setuid(run_as_uid);
        seteuid(run_as_uid);
        setgid(user->gid);
        setegid(user->gid);
        if (chdir(working_dir.c_str())) throw strprintf("Couldn't change to directory %s: %s", working_dir.c_str(), strerror(errno));
        const char *const env[] = { (string("SOCK_FILE=")+sock_file()).c_str(), NULL };
        execle("/bin/sh", "/bin/sh", "-c", start_command.c_str(), (char*)NULL, env);
        throw strprintf("Couldn't exec: %s", strerror(errno));
    } catch (string e) {
        write(fd[1], e.c_str(), e.length()+1/*NULL*/);
        close(fd[1]);
        exit(0);
    }
}

void daemon::stop()
{
    if (pid) {
        log(LOG_INFO, "Stopping [%d] %s\n", pid, id().c_str());
        kill(pid, SIGTERM);
    }
    respawns = 0;
    state = stopped;
}


void daemon::respawn()
{
    time_t now = time(NULL);
    time_t uptime = now - respawn_time;
    if (uptime < 60) cooldown = min((time_t)60, cooldown + 10); // back off if it's dying too often
    if (uptime > 60) cooldown = 0; // Clear cooldown on good behavior
    if (cooldown) {
        log(LOG_NOTICE, "%s is respawning too quickly, backing off. Cooldown time is %d seconds\n", id().c_str(), cooldown);
        cooldown_start = now;
        state = coolingdown;
    } else
        start(true);
}

void daemon::reap()
{
    pid = 0;
}

time_t daemon::cooldown_remaining()
{
    return max((time_t)0, cooldown - (time(NULL) - cooldown_start));
}
