//  Copyright (c) 2010 David Caldwell <david@porkrind.org> -*- c++ -*-
#ifndef __MASTER_CONFIG_H__
#define __MASTER_CONFIG_H__

#include <string>
#include <map>
#include <vector>

using namespace std;

struct master_config {
    map<string,vector<string> > can_run_as;
    map<string,vector<string> > manages;
};

typedef map<string, vector<string> >::iterator config_it;
typedef vector<string>::iterator config_list_it;

struct master_config parse_master_config(string path);
map<string,string> parse_daemon_config(string path);

#endif /* __MASTER_CONFIG_H__ */

