//  Copyright (c) 2014 David Caldwell,  All Rights Reserved.

#include <sys/un.h>
#include <sys/socket.h>
#if defined(LOCAL_PEERCRED)
#  include <sys/ucred.h>
#endif
#include "peercred.h"
#include "strprintf.h"

uid_t get_peer_uid(int socket)
{
#if defined(SO_PEERCRED)
    struct ucred cred;
    socklen_t cred_len = sizeof(cred);
    getsockopt(socket, SOCK_STREAM, SO_PEERCRED, &cred, &cred_len) == 0
        || throw_strerr("Couldn't determine user: getsockopt() failed");
    if (cred_len != sizeof(cred)) throw_str("getsockopt returned %d but I wanted %zd", cred_len, sizeof(cred));
    return cred.uid;
#elif defined(LOCAL_PEERCRED)
    struct xucred cred;
    socklen_t cred_len = sizeof(cred);
    getsockopt(socket, SOCK_STREAM, LOCAL_PEERCRED, &cred, &cred_len) == 0
        || throw_strerr("Couldn't determine user: getsockopt() failed");
    if (cred_len != sizeof(cred)) throw_str("getsockopt returned %d but I wanted %zd", cred_len, sizeof(cred));
    return cred.cr_uid;
#else
# error "daemon-manager requires the PEERCRED socket option for security. If your system doesn't support this, I feel bad for you, son."
#endif
}
