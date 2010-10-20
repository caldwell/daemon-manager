//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "passwd.h"
#include "strprintf.h"
#include <string>
#include <pwd.h>
#include <grp.h>

using namespace std;

int uid_from_name(string name)
{
    struct passwd *p = getpwnam(name.c_str());
    if (!p) return -1;
    return p->pw_uid;
}

string name_from_uid(int uid)
{
    struct passwd *p = getpwuid(uid);
    if (!p) return strprintf("%d", uid);
    return string(p->pw_name);
}

string name_from_gid(int gid)
{
    struct group *g = getgrgid(gid);
    if (!g) return strprintf("%d", gid);
    return string(g->gr_name);
}


