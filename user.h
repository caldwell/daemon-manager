//  Copyright (c) 2010-2023 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __USER_H__
#define __USER_H__

#include <string>
#include <map>
#include <vector>
#include <sys/types.h>

using namespace std;

class user {
  public:
    string name;
    uid_t uid;
    gid_t gid;
    string homedir;
    string logdir;
    string daemondir;
    map<uid_t,bool> can_run_as_uid;
    vector<user*> manages;

    user(string name, string daemondir, string logdir);
    void create_dirs();
    string config_path();
    string log_dir();
    vector<string> config_files();
  private:
    void init(struct passwd *, string daemondir, string logdir);
    string replace_dir_patterns(string pattern);
};

#endif /* __USER_H__ */

