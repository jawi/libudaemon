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
 * Initializes the logging layer.
 *
 * @param progname the name of the program to include in the logging result;
 * @param debug true if debug logging is to be enabled, false to disable debug
 *              logging;
 * @param foreground true if logging should be outputted on stderr, false to
 *                   output log results only to the logging result.
 */
void init_logging(const char *progname, bool debug, bool foreground);

/**
 * Closes the logging layer.
 */
void destroy_logging(void);

/**
 * Logs a message on debug level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_debug(const char *msg, ...);

/**
 * Logs a message on info level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_info(const char *msg, ...);

/**
 * Logs a message on warning level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_warning(const char *msg, ...);

/**
 * Logs a message on error level.
 *
 * @param msg the log message;
 * @param args the arguments to include in the log message.
 */
void log_error(const char *msg, ...);

#endif /* UD_LOGGING_H_ */
