//  Copyright (c) 2010 David Caldwell <david@porkrind.org>

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include "log.h"

static const char *const priority_name[8] = { "Emergency", "Alert", "Critical", "Error", "Warning", "Notice", "Info", "Debug" };

static bool use_syslog;
static int log_level;
void init_log(bool _use_syslog, int minimum_log_level)
{
    use_syslog = _use_syslog;
    log_level = minimum_log_level;
    openlog("daemon-manager", LOG_NDELAY | LOG_PID, LOG_DAEMON);
    setlogmask(LOG_UPTO(log_level));
}

void log(int priority, const char *message, ...)
{
    va_list ap;
    va_start(ap, message);
    if (use_syslog)
        vsyslog(priority, (std::string("[")+priority_name[priority]+"] "+message).c_str(), ap);
    else if (priority <= log_level) {
        fprintf(stderr, "[%s] ", priority_name[priority]);
        vfprintf(stderr, message, ap);
    }
    va_end(ap);
}
