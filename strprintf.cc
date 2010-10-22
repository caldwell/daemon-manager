//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

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
    throw vstrprintf(format, ap);
    return false;
}

bool throw_strerr(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    throw vstrprintf(format, ap) + ": " + strerror(errno);
    return false;
}
