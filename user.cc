//  Copyright (c) 2010-2023 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.

#include "user.h"
#include "strprintf.h"
#include "permissions.h"
#include "posix-util.h"
#include "passwd.h"
#include <string>
#include <glob.h>

using namespace std;

map<string,user*> users;

user::user(string name, string daemondir, string logdir)
{
    pwent pw = pwent(name);
    if (!pw.valid) throw_str("No user named \"%s\"", name.c_str());
    init(pw, daemondir, logdir);
}

void user::init(const pwent &pw, string daemondir, string logdir)
{
    name = pw.name;
    uid = pw.uid;
    gid = pw.gid;
    homedir = string(pw.dir);
    if (daemondir.size() && daemondir.back() == '/') daemondir.pop_back();
    if (logdir.size()    && logdir.back()    == '/') logdir.pop_back();
    this->daemondir = daemondir;
    this->logdir = logdir;
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
    return replace_dir_patterns(daemondir);
}

string user::log_dir()
{
    return replace_dir_patterns(logdir);
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

