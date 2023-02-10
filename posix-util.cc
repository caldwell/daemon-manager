//  Copyright (c) 2010-2023 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.

#include <sys/stat.h>
#include <unistd.h>
#include "posix-util.h"
#include "strprintf.h"

using namespace std;

bool exists(string path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

void mkdir_ug(string path, mode_t mode, int uid, int gid)
{
    if (!exists(path))
        mkdir(path.c_str(), mode)    == -1 && throw_strerr("mkdir %s failed", path.c_str());
    chown(path.c_str(), uid, gid)    == -1 && throw_strerr("Couldn't change %s to uid %d gid %d", path.c_str(), uid, gid);
}

// basename, but basename's annoying to call because c strings.
string parent(string path)
{
    if (path.length() > 0 && path.back() == '/') path.pop_back();
    size_t sep;
    if ((sep=path.find_last_of('/')) != string::npos)
        return path.substr(0,sep);
    return "/";
}

void mkdir_pug(string base, string subdirs, mode_t mode, int uid, int gid)
{
    if (subdirs == "/") return; // don't go past the base.
    if (!exists(base+subdirs))
        mkdir_pug(base, parent(subdirs), mode, uid, gid);
    mkdir_ug(base+subdirs, mode, uid, gid);
}
