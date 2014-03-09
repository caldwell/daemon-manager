//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.

#include <stdexcept>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include "strprintf.h"

using std::string;

static string vstrprintf(const char *format, va_list ap)
{
    char *buf;
    vasprintf(&buf, format, ap);
    if (!buf)
        return string("");
    string s(buf);
    free(buf);
    return s;
}

string strprintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    return vstrprintf(format, ap);
}

bool throw_str(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    throw std::runtime_error(vstrprintf(format, ap));
    return false;
}

bool throw_strerr(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    throw std::runtime_error(vstrprintf(format, ap) + ": " + strerror(errno));
    return false;
}
