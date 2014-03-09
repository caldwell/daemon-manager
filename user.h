//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org>
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
    map<uid_t,bool> can_run_as_uid;
    vector<user*> manages;

    user(string name);
    user(uid_t uid);
    void create_dirs();
    string config_path();
    string log_dir();
    vector<string> config_files();
  private:
    void init(struct passwd *);
};

#endif /* __USER_H__ */

