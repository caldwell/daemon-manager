//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __POSIX_UTIL_H__
#define __POSIX_UTIL_H__

#include <string>

bool exists(std::string path);
void mkdir_ug(std::string path, mode_t mode, int uid=-1, int gid=-1);
void mkdir_pug(std::string base, std::string subdirs, mode_t mode, int uid=-1, int gid=-1);

#endif /* __POSIX_UTIL_H__ */

