//  Copyright (c) 2014 David Caldwell,  All Rights Reserved.

#include <sys/socket.h>
#include <sys/un.h>
#include <string>
#include "command-sock.h"

struct sockaddr_un sock_addr(std::string socket_path)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_LOCAL;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path)-1] = '\0';
    return addr;
}

struct sockaddr_un command_sock_addr()
{
    return sock_addr(COMMAND_SOCKET_PATH);
}
