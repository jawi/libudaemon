/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

#include "udaemon/ud_logging.h"

static struct _log_config {
    bool initialized;
    bool foreground;
} log_config = {
    .initialized = false,
    .foreground = true,
};

void init_logging(void) {
    if (log_config.initialized) {
        // Already initialized; do not do this again...
        return;
    }

    int facility = LOG_DAEMON;
    int options = LOG_CONS | LOG_PID | LOG_ODELAY;
    if (log_config.foreground) {
        facility = LOG_USER;
        options |= LOG_PERROR;
    }

    openlog(program_invocation_short_name, options, facility);

    log_config.initialized = true;
}

void destroy_logging(void) {
    if (!log_config.initialized) {
        // Not initialized; do nothing...
        return;
    }

    closelog();

    log_config.initialized = false;
}

void setup_logging(bool foreground) {
    destroy_logging();

    log_config.foreground = foreground;

    init_logging();
}

void set_loglevel(loglevel_t loglevel) {
    int mask;
    if (loglevel == DEBUG) {
        mask = LOG_UPTO(LOG_DEBUG);
    } else if (loglevel == WARNING) {
        mask = LOG_UPTO(LOG_WARNING);
    } else if (loglevel == ERROR) {
        mask = LOG_UPTO(LOG_ERR);
    } else {
        mask = LOG_UPTO(LOG_INFO);
    }
    setlogmask(mask);
}

static int LEVEL[] = { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR };

__attribute__((__format__ (__printf__, 2, 0)))
void log_msg(const loglevel_t level, const char *msg, ...) {
    va_list ap;
    va_start(ap, msg);
    init_logging();
    vsyslog(LEVEL[level], msg, ap);
    va_end(ap);
}
