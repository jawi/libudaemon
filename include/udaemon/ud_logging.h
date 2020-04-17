/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#ifndef UD_LOGGING_H_
#define UD_LOGGING_H_

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <syslog.h>
#include <time.h>

/**
 * Represents the log levels we provide.
 */
typedef enum loglevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
} loglevel_t;

/**
 * Initializes the logging layer.
 *
 * @param foreground true if logging should be outputted on stderr as well as
 *                   to syslog, false to output log results only to the logging
 *                   result.
 */
void setup_logging(bool foreground);

/**
 * Sets the loglevel on which messages are going to be logged.
 *
 * @param loglevel the minimum loglevel to log messages at.
 */
void set_loglevel(loglevel_t loglevel);

/**
 * Logs a message at the given level.
 *
 * @param level the level at which the message should be logged;
 * @param msg the message (printf-style) that should be logged.
 */
void log_msg(const loglevel_t level, const char *msg, ...);

/**
 * Logs a message on debug level.
 *
 * @param msg the log message; any message parameter can be added after the message.
 */
#define log_debug(msg...) log_msg(DEBUG, msg)

/**
 * Logs a message on info level.
 *
 * @param msg the log message; any message parameter can be added after the message.
 */
#define log_info(msg...) log_msg(INFO, msg)

/**
 * Logs a message on warning level.
 *
 * @param msg the log message; any message parameter can be added after the message.
 */
#define log_warning(msg...) log_msg(WARNING, msg)

/**
 * Logs a message on error level.
 *
 * @param msg the log message; any message parameter can be added after the message.
 */
#define log_error(msg...) log_msg(ERROR, msg)

#endif /* UD_LOGGING_H_ */
