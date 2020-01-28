/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

#include "ud_logging.h"

static bool show_debug_log = false;

/**
 * Initializes the logging layer.
 *
 * @param progname the name of the program to include in the logging result;
 * @param debug true if debug logging is to be enabled, false to disable debug
 *              logging;
 * @param foreground true if logging should be outputted on stderr, false to
 *                   output log results only to the logging result.
 */
void init_logging(const char *progname, bool debug, bool foreground) {
    show_debug_log = debug;

    int facility = LOG_DAEMON;
    int options = LOG_CONS | LOG_PID | LOG_ODELAY;
    if (foreground) {
        facility = LOG_USER;
        options |= LOG_PERROR;
    }

    openlog(progname, options, facility);
}

/**
 * Closes the logging layer.
 */
void destroy_logging(void) {
    closelog();
}

#define DO_LOG(LEVEL, MSG) \
    do { \
        va_list ap; \
        va_start(ap, msg); \
        vsyslog(LEVEL, MSG, ap); \
        va_end(ap); \
    } while (0)

/**
 * Logs a message on debug level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_debug(const char *msg, ...) {
    if (show_debug_log) {
        DO_LOG(LOG_DEBUG, msg);
    }
}

/**
 * Logs a message on info level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_info(const char *msg, ...) {
    DO_LOG(LOG_INFO, msg);
}

/**
 * Logs a message on warning level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_warning(const char *msg, ...) {
    DO_LOG(LOG_WARNING, msg);
}

/**
 * Logs a message on error level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_error(const char *msg, ...) {
    DO_LOG(LOG_ERR, msg);
}
