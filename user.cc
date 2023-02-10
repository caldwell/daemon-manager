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

user::user(string name, string daemondir, string logdir)
{
    struct passwd *p = getpwnam(name.c_str());
    if (!p) throw_str("No user named \"%s\"", name.c_str());
    init(p, daemondir, logdir);
}

void user::init(struct passwd *p, string daemondir, string logdir)
{
    name = string(p->pw_name);
    uid = p->pw_uid;
    gid = p->pw_gid;
    homedir = string(p->pw_dir);
    this->daemondir = replace_dir_patterns(daemondir);
    this->logdir = replace_dir_patterns(logdir);
}

string user::replace_dir_patterns(string pattern)
{
    if (pattern.substr(0,2) == "~/")
        pattern.replace(0, 1, homedir);
    size_t start;
    if ((start = pattern.find("%username%")) != string::npos)
        pattern.replace(start, 10, name);
    return pattern;
}

void user::create_dirs()
{
    // Only create hierarchy in home directories, since that's the only one that's guessable.
    if (daemondir.substr(0,2) == "~/")
        mkdir_pug(homedir, daemondir.substr(2,daemondir.length()), 0750, uid, gid);
    if (logdir.substr(0,2) == "~/")
        mkdir_pug(homedir, logdir.substr(2,logdir.length()), 0750, uid, gid);
}

string user::config_path()
{
    return daemondir;
}

string user::log_dir()
{
    return logdir;
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

