//  Copyright (c) 2010 David Caldwell,  All Rights Reserved. -*- c++ -*-
#ifndef __STRPRINTF_H__
#define __STRPRINTF_H__

#include <string>

std::string strprintf(const char *format, ...);

// Shorthand for "throw strprintf(...)". It has a return type so you can do:
//     mkdir() == -1 && throwstr("mkdir: %s", strerror(errno));
bool throw_str(const char *format, ...);

// Same plus an implied +": "+strerror(errno)
bool throw_strerr(const char *format, ...);

#endif /* __STRPRINTF_H__ */

