//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "strprintf.h"

using std::string;

string strprintf(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char *buf;
    vasprintf(&buf, format, ap);
    if (!buf)
        return string("");
    string s(buf);
    free(buf);
    return s;
}
