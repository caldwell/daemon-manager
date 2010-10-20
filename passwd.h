//  Copyright (c) 2010 David Caldwell,  All Rights Reserved. -*- c++ -*-
#ifndef __PASSWD_H__
#define __PASSWD_H__

#include <string>

int uid_from_name(std::string name);
std::string name_from_uid(int uid);
std::string name_from_gid(int gid);

#endif /* __PASSWD_H__ */

