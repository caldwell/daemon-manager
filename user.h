//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.
#ifndef __USER_H__
#define __USER_H__

#include <string>
#include <map>
#include <vector>

using namespace std;

class user {
  public:
    string name;
    int uid;
    int gid;
    string homedir;
    int fifo_req;
    int fifo_resp;
    int fifo_req_wr;
    int fifo_resp_rd;
    map<int,bool> can_run_as_uid;
    vector<user*> manages;

    user(string name);
    user(uid_t uid);
    void create_files();
    void open_fifos(bool client = false);
    string fifo_dir();
    string fifo_path(bool request);
    string config_path();
    vector<string> config_files();
  private:
    void create_dirs();
    void create_fifos();
    void init(struct passwd *);
};

#endif /* __USER_H__ */

