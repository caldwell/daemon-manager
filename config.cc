//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "config.h"
#include "strprintf.h"
#include "uniq.h"
#include <iostream>
#include <fstream>
#include <string.h>

using namespace std;

static char *trim(char *s)
{
    while (*s && isspace(*s)) // Front
        s++;
    for (int i=strlen(s)-1; i>=0; i--) // Back
        if (!isspace(s[i])) break;
        else s[i] = '\0';
    return s;
}

struct master_config parse_master_config(string path)
{
    ifstream in(path.c_str(), ifstream::in);

    struct master_config config;
    map<string,vector<string> > *section = NULL;

    int n=0;
    char line[1000];
    while (in.good()) {
        in.getline(line, sizeof(line));
        n++;

        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';

        char *l = trim(line);
        if (*l == '\0') continue;

        if (*l == '[') {
            char *sect = l+1;
            char *end = strchr(sect, ']');
            if (!end) throw_str("Bad config file %s:%d missing ']' from section header", path.c_str(), n);
            *end = '\0';
            l = end+1;
            while (*l && isspace(*l)) l++;
            if (*l != '\0') throw_str("Bad config file %s:%d Junk at end of section header line", path.c_str(), n);

            if      (strcmp(sect, "runs_as") == 0) section = &config.runs_as;
            else if (strcmp(sect, "manages") == 0) section = &config.manages;
            else throw_str("Bad config file %s:%d Illegal section \"%s\"", path.c_str(), n, sect);
            continue;
        }

        if (!section) throw_str("Bad config file %s:%d Line before section header", path.c_str(), n);

        char *key = strsep(&l, "=");
        key = trim(key);

        vector<string> list;
        for (char *val; val = strsep(&l, ",");)
            if (trim(val)[0] != '\0') // Allow "user = " type lines.
                list.push_back(string(trim(val)));

        (*section)[string(key)] = uniq(list);
    }

    return config;
}

map<string,string> parse_daemon_config(string path)
{
    ifstream in(path.c_str(), ifstream::in);
    map<string,string> config;

    int n=0;
    char line[1000];
    while (in.good()) {
        in.getline(line, sizeof(line));
        n++;

        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';

        char *l = trim(line);
        if (*l == '\0') continue;

        char *key = strsep(&l, "=");
        if (!l) throw_str("Bad config file %s:%d Missing '=' after key", path.c_str(), n);

        config[trim(key)] = trim(l);
    }
    return config;
}
