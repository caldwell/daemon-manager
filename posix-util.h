//  Copyright (c) 2010 David Caldwell <david@porkrind.org> -*- c++ -*-
#ifndef __POSIX_UTIL_H__
#define __POSIX_UTIL_H__

#include <string>

void mkdir_ug(std::string path, mode_t mode, int uid=-1, int gid=-1);

#endif /* __POSIX_UTIL_H__ */

