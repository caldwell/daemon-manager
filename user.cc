//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "user.h"
//#include "except.h"
#include "strprintf.h"
#include "permissions.h"
#include <string>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>

using namespace std;

map<string,user*> users;

user::user(string name)
{
    struct passwd *p = getpwnam(name.c_str());
    if (!p) throw strprintf("No user named \"%s\"", name.c_str());
    init(p);
}

user::user(uid_t uid)
{
    struct passwd *p = getpwuid(uid);
    if (!p) throw strprintf("No user with uid %d", uid);
    init(p);
}

void user::init(struct passwd *p)
{
    name = string(p->pw_name);
    uid = p->pw_uid;
    gid = p->pw_gid;
    homedir = string(p->pw_dir);
}

void user::create_files()
{
    create_dirs();
    create_fifos();
    open_fifos();
}

void user::create_dirs()
{
    struct stat st;
    if (stat(fifo_dir().c_str(), &st) != 0) {
        mkdir(fifo_dir().c_str(), 0750);
        chown(fifo_dir().c_str(), uid, gid);
    }
    if (uid != 0 && // Don't create /etc/daemon-manager. Package manager should do that.
        stat(config_path().c_str(), &st) != 0) {
        mkdir(config_path().c_str(), 0750);
        chown(config_path().c_str(), uid, gid);
    }
}

static void create_fifo(string path_str, uid_t uid, gid_t gid)
{
    const char *path = path_str.c_str();
    struct stat st;
    if (stat(path, &st) == 0 && unlink(path)) throw strprintf("Couldn't remove old FIFO @ %s: %s", path, strerror(errno));
    if (mkfifo(path, 0700)) throw strprintf("mkfifo %s failed: %s", path, strerror(errno));
    chown(path, uid, gid);
}

void user::create_fifos()
{
     create_fifo(fifo_path(true),  uid, gid);
     create_fifo(fifo_path(false), uid, gid);
}


static int open_fifo(string path, int oflags)
{
    int fifo = open(path.c_str(), oflags);
    if (fifo < 0) throw strprintf("open %s (%s) failed: %s", path.c_str(), oflags & O_RDONLY ? "read" : "write", strerror(errno));
    fcntl(fifo, F_SETFD, 1);
    return fifo;
}

void user::open_fifos(bool client)
{
    // We don't use this end of the FIFO, we just hold it open because otherwise Linux craps its pants when the
    // number of writers go from 1 to 0 (select always returns the fd to tell us it's EOF and there's no way to
    // clear the condition short of closing the file and re-opening it which is gross). Not needed on Mac OS X.
    if (!client) fifo_req     = open_fifo(fifo_path(true),  O_RDONLY | O_NONBLOCK);
                 fifo_req_wr  = open_fifo(fifo_path(true),  O_WRONLY | O_NONBLOCK);
                 fifo_resp_rd = open_fifo(fifo_path(false), O_RDONLY | O_NONBLOCK);
    if (!client) fifo_resp    = open_fifo(fifo_path(false), O_WRONLY | O_NONBLOCK);
}

string user::fifo_dir()
{
    return uid == 0 ? "/var/run/daemon-manager/"
                    : homedir + "/.daemon-manager/";
}
string user::fifo_path(bool request)
{
    return fifo_dir() + (request ? "req" : "resp")+".fifo";
}

string user::config_path()
{
    return uid == 0 ? "/etc/daemon-manager/daemons"
                    : homedir + "/.daemon-manager/daemons";
}

string user::log_dir()
{
    return uid == 0 ? "/var/log/daemon-manager/"
                    : homedir + "/.daemon-manager/log/";
}

vector<string> user::config_files()
{
    permissions::check(config_path(), 0002, uid);
    // check permissions

    vector<string> files;
    glob_t gl;
    int status = glob((config_path() + "/*.conf").c_str(), 0, NULL, &gl);

    for (size_t i=0; i<gl.gl_pathc; i++)
        files.push_back(string(gl.gl_pathv[i]));

    globfree(&gl);
    if (status != GLOB_NOMATCH && status != 0)
        throw strprintf("Glob failed: %d/%s", status, strerror(errno));

    return files;
}

