//  Copyright (c) 2010 David Caldwell,  All Rights Reserved. -*- c++ -*-
#ifndef __DAEMON_H__
#define __DAEMON_H__

#include "user.h"
#include <string>
#include <time.h>

enum run_state { stopped, stopping, running, coolingdown };
const std::string _state_str[] = { "stopped", "stopping", "running", "coolingdown" };

class daemon {
  public:
    std::string id;
    std::string name;
    std::string config_file;
    time_t config_file_stamp;
    //int socket;
    int pid;
    class user *user;

    // From config file:
    std::string working_dir;
    int run_as_uid;
    std::string start_command;
    bool autostart;
    bool log_output;

    // stats:
    run_state state;
    time_t cooldown;
    time_t cooldown_start;
    size_t respawns;
    time_t start_time;
    time_t respawn_time;

    daemon(std::string config_file, class user *user);

    void load_config();
    bool exists();
    std::string sock_file();
    void create_sock_dir();
    std::string state_str() { return _state_str[state]; }

    void start(bool respawn=false);
    void stop();
    void respawn();
    void reap();

    time_t cooldown_remaining();
};

bool daemon_compare(class daemon *a, class daemon *b);

// Use as a unary predicate for find_if and similar.
class daemon_id_match {
    public: string &id;
            daemon_id_match(string &id) : id(id) {}
            bool operator()(class daemon *d) { return d->id == id; }
};

#endif /* __DAEMON_H__ */

