//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "permissions.h"
#include "strprintf.h"
#include "passwd.h"
#include <sys/stat.h>
#include <string>

using namespace std;

struct stat permissions::check(string file, int bad_modes, uid_t required_uid, gid_t required_gid)
{
    const char *path = file.c_str();
    struct stat st;
    if (stat(path, &st)) throw strprintf("%s doesn't exist", path);
    if (st.st_mode & bad_modes & 0002)   throw strprintf("%s can't be world writable", path);
    if (st.st_mode & bad_modes & 0111)   throw strprintf("%s can't be executable", path);
    if (required_uid != (uid_t)-1 && st.st_uid != required_uid) throw strprintf("%s must be owned by \"%s\"", path, name_from_uid(required_uid).c_str());
    if (required_gid != (gid_t)-1 && st.st_gid != required_gid) throw strprintf("%s must have group \"%s\"",path, name_from_gid(required_gid).c_str());
    return st;
}
