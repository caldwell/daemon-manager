//  Copyright (c) 2010 David Caldwell <david@porkrind.org>

#include <sys/stat.h>
#include <unistd.h>
#include "posix-util.h"
#include "strprintf.h"

using namespace std;

void mkdir_ug(string path, mode_t mode, int uid, int gid)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
        mkdir(path.c_str(), mode)    == -1 && throw_strerr("mkdir %s failed", path.c_str());
    chown(path.c_str(), uid, gid)    == -1 && throw_strerr("Couldn't change %s to uid %d gid %d", path.c_str(), uid, gid);
}

