//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.
#ifndef __USER_H__
#define __USER_H__

#include <string>
#include <map>
#include <vector>
#include <sys/un.h>

using namespace std;

class user {
  public:
    string name;
    int uid;
    int gid;
    string homedir;
    int command_socket;
    map<int,bool> can_run_as_uid;
    vector<user*> manages;

    user(string name);
    user(uid_t uid);
    void create_dirs();
    void open_server_socket();
    void open_client_socket();
    string socket_dir();
    string socket_path();
    string config_path();
    string log_dir();
    vector<string> config_files();
  private:
    void init(struct passwd *);
    struct sockaddr_un sock_addr();
};

#endif /* __USER_H__ */

