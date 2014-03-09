//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.

#include "user.h"
#include "strprintf.h"
#include "permissions.h"
#include "posix-util.h"
#include <string>
#include <pwd.h>
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
    if (uid != 0) // Don't create /etc/daemon-manager. Package manager should do that.
        mkdir_ug(config_path().c_str(), 0750, uid, gid);
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

