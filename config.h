//  Copyright (c) 2010-2023 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __MASTER_CONFIG_H__
#define __MASTER_CONFIG_H__

#include <string>
#include <map>
#include <vector>
#include <list>

using namespace std;

struct master_config {
    map<string,string> settings;
    map<string,vector<string> > can_run_as;
    map<string,vector<string> > manages;
};

struct daemon_config {
    map<string,string> config;
    map<string,string> env;
};

typedef map<string, vector<string> >::iterator config_it;
typedef vector<string>::iterator config_list_it;

struct master_config parse_master_config(string path);
struct daemon_config parse_daemon_config(string path);
list<string> validate_keys(map<string,string> cfg, const string &file, const vector<string> &valid_keys);
void validate_keys_pedantically(map<string,string> cfg, const string &file, const vector<string> &valid_keys);

#endif /* __MASTER_CONFIG_H__ */

