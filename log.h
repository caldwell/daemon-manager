//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org>
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __LOG_H__
#define __LOG_H__

#include <syslog.h>

void init_log(bool _use_syslog, int minimum_log_level);
void log(int priority, const char *message, ...)
    __attribute__ ((format (printf, 2, 3)));

#endif /* __LOG_H__ */

