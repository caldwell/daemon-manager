//  Copyright (c) 2010 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.

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
    stat(path, &st) == 0 || throw_str("%s doesn't exist", path);
    if (st.st_mode & bad_modes & 0002)   throw_str("%s can't be world writable", path);
    if (st.st_mode & bad_modes & 0111)   throw_str("%s can't be executable", path);
    if (required_uid != (uid_t)-1 && st.st_uid != required_uid) throw_str("%s must be owned by \"%s\"", path, name_from_uid(required_uid).c_str());
    if (required_gid != (gid_t)-1 && st.st_gid != required_gid) throw_str("%s must have group \"%s\"",path, name_from_gid(required_gid).c_str());
    return st;
}
