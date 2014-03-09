//  Copyright (c) 2014 David Caldwell,  All Rights Reserved.
#ifndef __COMMAND_SOCK_H__
#define __COMMAND_SOCK_H__

#include <sys/socket.h>
#include <sys/un.h>
#include <string>

struct sockaddr_un sock_addr(std::string socket_path);
struct sockaddr_un command_sock_addr();

#endif /* __COMMAND_SOCK_H__ */

