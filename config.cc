//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include "config.h"
#include "strprintf.h"
#include "uniq.h"
#include "stringutil.h"
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

static string remove_comment(string in) { return trim(in.substr(0, min(in.length(), in.find('#')))); }

struct master_config parse_master_config(string path)
{
    ifstream in(path.c_str(), ifstream::in);

    struct master_config config;
    map<string,vector<string> > *section = NULL;

    int n=0;
    string line;
    while (in.good()) {
        getline(in, line);
        n++;

        line = remove_comment(line);
        if (line.empty()) continue;

        if (line[0] == '[') {
            size_t end = line.find(']');
            if (end == line.npos) throw_str("%s:%d missing ']' from section header", path.c_str(), n);
            string sect = line.substr(1, end-1);
            if (trim(line.substr(end+1)).length()) throw_str("%s:%d Junk at end of section header line", path.c_str(), n);

            if      (sect == "runs_as" || // The old name, here for backwards compatibility and not documented.
                     sect == "can_run_as") section = &config.can_run_as;
            else if (sect == "manages") section = &config.manages;
            else throw_str("%s:%d Illegal section \"%s\"", path.c_str(), n, sect.c_str());
            continue;
        }

        if (!section) throw_str("%s:%d Line before section header", path.c_str(), n);

        size_t sep;
        string key = trim(line.substr(0, sep=line.find(':')));

        vector<string> list;
        if (sep != line.npos)
            split(list, trim(line.substr(sep+1)), string(","));

        (*section)[key] = uniq(list);
    }

    return config;
}

map<string,string> parse_daemon_config(string path)
{
    ifstream in(path.c_str(), ifstream::in);
    map<string,string> config;

    int n=0;
    string line;
    while (in.good()) {
        getline(in, line);
        n++;

        line = remove_comment(line);
        if (line.empty()) continue;

        size_t sep;
        string key = trim(line.substr(0, sep=line.find('=')));
        if (sep == line.npos) throw_str("%s:%d Missing '=' after key", path.c_str(), n);

        config[key] = trim(line.substr(sep+1));
    }
    return config;
}
