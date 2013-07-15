//  Copyright (c) 2010 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __PERMISSIONS_H__
#define __PERMISSIONS_H__

#include <string>
#include <sys/stat.h>

namespace permissions {

    struct stat check(std::string file, int bad_modes, uid_t required_uid=-1, gid_t required_gid=-1);

}
#endif /* __PERMISSIONS_H__ */

