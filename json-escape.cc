//  Copyright (c) 2011 David Caldwell,  All Rights Reserved.

#include <string>
#include <stdlib.h>
#include "json-escape.h"
#include "strprintf.h"

using namespace std;

string json_escape(string s)
{
    string out;
    for (const char *c = s.c_str(); *c; c++)
        switch (*c) {
            case '\b': out += "\\b"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
          //case '/':  out += "\\/"; break; // Just because you can doesn't mean we will.
            default:
                if (isprint(*c))
                    out += *c;
                else
                    out += strprintf("\\u%04x", *c);
        };
    return out;
}

string json_unescape(string _s)
{
    string out;
    for (string::iterator s = _s.begin(); s < _s.end(); s++) {
        if (*s != '\\')
            out += *s;
        else {
            if (++s == _s.end()) return out + "!escape char missing";
            switch (*s) {
                case 'b':  out += "\b"; break;
                case 'n':  out += "\n"; break;
                case 'r':  out += "\r"; break;
                case 't':  out += "\t"; break;
                case '"':  out += "\""; break;
                case '\\': out += "\\"; break;
                case '/':  out += "/";  break;
                case 'u': {
                    if (s - _s.end() < 4) return out + "!escape \\u does not have 4 digits";
                    char hex[5] = {};
                    for (int hi=0; hi<4; hi++)
                        if (!isxdigit(hex[hi] = *s))
                            return out + strprintf("!escape \\u digit %d is %c and not a hex digit", hi+1, hex[hi]);
                    unsigned long ch = strtoul(hex, NULL, 16);
                    if (ch > 0xff) return out + "!can't deal with wide character " + hex;
                    out += (char)ch;
                    break;
                }
                default: return out + strprintf("!Error: bad escape: '%02x'", *s);
            };
        }
    }
    return out;
}
