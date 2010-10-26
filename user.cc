//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "user.h"
//#include "except.h"
#include "strprintf.h"
#include "permissions.h"
#include "posix-util.h"
#include <string>
#include <sstream>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
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
    if (!p) throw_str("No user named \"%s\"", name.c_str());
    init(p);
}

user::user(uid_t uid)
{
    struct passwd *p = getpwuid(uid);
    if (!p) throw_str("No user with uid %d", uid);
    init(p);
}

void user::init(struct passwd *p)
{
    name = string(p->pw_name);
    uid = p->pw_uid;
    gid = p->pw_gid;
    homedir = string(p->pw_dir);
}

void user::create_dirs()
{
    mkdir_ug(socket_dir().c_str(), 0750, uid, gid);
    if (uid != 0) // Don't create /etc/daemon-manager. Package manager should do that.
        mkdir_ug(config_path().c_str(), 0750, uid, gid);
}

struct sockaddr_un user::sock_addr()
{
    struct sockaddr_un addr;
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, socket_path().c_str(), sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0';
    return addr;
}

void user::open_server_socket()
{
    struct sockaddr_un addr = sock_addr();
    struct stat st;
    if(stat(addr.sun_path, &st) == 0)
        unlink(addr.sun_path)                                    == 0 || throw_strerr("Couldn't remove old socket @ %s", addr.sun_path);

    command_socket = socket(PF_LOCAL, SOCK_STREAM /*SOCK_DGRAM*/, 0);
    if (command_socket < 0) throw_strerr("socket() failed");
    fcntl(command_socket, F_SETFD, 1)                           == -1 && throw_strerr("Couldn't set socket to close on exec");
    bind(command_socket, (struct sockaddr*) &addr, sizeof(sa_family_t) + strlen(addr.sun_path) + 1)
                                                                 == 0 || throw_strerr("Binding to socket %s failed", addr.sun_path);
    listen(command_socket, 1)                                    == 0 || throw_strerr("listen(%s) failed", addr.sun_path);

    chown(addr.sun_path, uid, gid)                               == 0 || throw_strerr("chown %s, uid:%d, gid%d failed", addr.sun_path, uid, gid);
    chmod(addr.sun_path, 0700)                                   == 0 || throw_strerr("chmod %s, 0700", addr.sun_path);
}

void user::open_client_socket()
{
    struct sockaddr_un addr = sock_addr();
    command_socket = socket(PF_LOCAL, SOCK_STREAM /*SOCK_DGRAM*/, 0);
    if (command_socket < 0) throw_strerr("socket() failed");
    connect(command_socket, (struct sockaddr*) &addr, sizeof(sa_family_t) + strlen(addr.sun_path) + 1)
                                                                    == 0 || throw_strerr("Connect to %s failed", addr.sun_path);
    fcntl(command_socket, F_SETFL, O_NONBLOCK)                      == 0 || throw_strerr("Couldn't set O_NONBLOCK on %s", addr.sun_path);
}

string user::socket_dir()
{
    return uid == 0 ? "/var/run/daemon-manager/"
                    : homedir + "/.daemon-manager/";
}
string user::socket_path()
{
    return socket_dir() + "command.sock";
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
        throw_strerr("Glob failed: %d", status);

    return files;
}

