//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.

#include "config.h"
#include "strprintf.h"
#include "uniq.h"
#include "stringutil.h"
#include "log.h"
#include <grp.h>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;

static string remove_comment(string in) { return trim(in.substr(0, min(in.length(), in.find('#')))); }

template<class T>
std::vector<T> concat(std::vector<T> a, std::vector<T> b)
{
    std::vector<T> list = a;
    list.insert(list.end(), b.begin(), b.end());
    return list;
}

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

            if (sect == "runs_as") {
                sect = "can_run_as";
                log(LOG_WARNING, "%s:%d [runs_as] is now called [can_run_as]. Please fix this, it will break eventually.\n", path.c_str(), n);
            }

            if      (sect == "can_run_as") section = &config.can_run_as;
            else if (sect == "manages")    section = &config.manages;
            else throw_str("%s:%d Illegal section \"%s\"", path.c_str(), n, sect.c_str());
            continue;
        }

        if (!section) throw_str("%s:%d Line before section header", path.c_str(), n);

        size_t sep;
        string key = trim(line.substr(0, sep=line.find(':')));

        vector<string> list;
        if (sep != line.npos)
            split(list, trim(line.substr(sep+1)), string(","));

        // Expand @group in list to individual user names.
        for (auto it = list.begin(); it != list.end(); it++)
            if ((*it)[0] == '@') {
                string group = it->substr(1);
                struct group *g = getgrnam(group.c_str());
                list.erase(it);
                if (!g)
                    log(LOG_ERR, "%s:%d Couldn't find group \"%s\". Ignoring.\n", path.c_str(), n, group.c_str());
                else
                    for (int i=0; g->gr_mem[i]; i++)
                        list.push_back(string(g->gr_mem[i]));
                it = list.begin(); // start over--our iterator is invalidated by the erase().
            }

        // Expand @group in the key to individual user names by duplicating the list
        if (key[0] == '@') {
            string group = key.substr(1);
            struct group *g = getgrnam(group.c_str());
            if (!g)
                log(LOG_ERR, "%s:%d Couldn't find group \"%s\". Ignoring line.\n", path.c_str(), n, group.c_str());
            else
                for (int i=0; g->gr_mem[i]; i++)
                    (*section)[g->gr_mem[i]] = uniq(concat((*section)[g->gr_mem[i]], list));
        } else
            (*section)[key] = uniq(concat((*section)[key], list));
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
