/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#ifndef UDAEMON_H_
#define UDAEMON_H_

#include <stdbool.h>
#include <stdint.h>
#include <poll.h>

#include <sys/types.h>

#include "ud_logging.h"

/**
 * Represents a simplification of the OS signals that might occur.
 */
typedef enum ud_signal {
    SIG_TERM = 1,
    SIG_HUP = 2,
    SIG_USR1 = 3,
    SIG_USR2 = 4,
} ud_signal_t;

/**
 * Represents the (private) state of udaemon.
 */
typedef struct ud_state ud_state_t;

/**
 * Represents the configuration of udaemon.
 */
typedef struct ud_config {
    /** true to enable "debug" logging, false to disable it. */
    bool debug;
    /** true to stay in the foreground, false to daemonize the application. */
    bool foreground;
    /** the user ID the daemon is running as. */
    uid_t priv_user;
    /** the group ID the daemon is running as. */
    gid_t priv_group;
    /** the program name to use for logging purposes. */
    char *progname;
    /** the path to the PID file to write when running as daemon. */
    char *pid_file;

    // Hooks and callbacks...

    /** called during initialization of udaemon. */
    int (*initialize)(ud_state_t *ud_state);
    /** called for each OS signal that is received. */
    void (*signal_handler)(ud_state_t *ud_state, ud_signal_t);
    /** called when no data or event is received. */
    void (*idle_handler)(ud_state_t *ud_state);
    /** called during cleanup of udaemon. */
    int (*cleanup)(ud_state_t *ud_state);
} ud_config_t;

/**
 * Callback event handler for polled event handling.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param pollfd the polling information, such as file descriptor, cannot be NULL.
 */
typedef void (*ud_event_handler_t)(ud_state_t *ud_state, struct pollfd *pollfd);

/**
 * Represents a short-lived task.
 *
 * @param ud_state the current state of udaemon, cannot be NULL;
 * @param interval the current interval of the task, > 0;
 * @param context the user-defined context, can be NULL.
 * @return 0 if the task terminated normally, a negative value if the task
 *         terminated abnormally or a positive value to reschedule the task
 *         after N seconds.
 */
typedef int (*ud_task_t)(ud_state_t *ud_state, int interval, void *context);

/**
 * Denotes an identifier of event handlers.
 */
typedef uint8_t eh_id_t;

/**
 * Initializes a new udaemon state value.
 *
 * @param config the udaemon configuration to use.
 * @return a new udaemon state value, or NULL in case of out of memory.
 */
ud_state_t *ud_init(ud_config_t *config);

/**
 * Destroys a given udaemon state value.
 *
 * NOTE: after calling this method, the given `ud_state` value must not be
 * used anymore!
 *
 * @param ud_state the udaemon state to use, may be NULL.
 */
void ud_destroy(ud_state_t *ud_state);

/**
 * Adds a new event handler for polling (file-based) events.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param fd the file descriptor to poll;
 * @param emask the event mask to poll for;
 * @param callback the event callback to call once an event is available;
 * @param event_handler_id (optional) the event handler identifier that is
 * @return a non-zero value in case of errors, or zero in case of success.
 */
int ud_add_event_handler(ud_state_t *ud_state, int fd, short emask,
                         ud_event_handler_t callback,
                         eh_id_t *event_handler_id);

/**
 * Removes a previously registered event handler.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param event_handler_id the event handler identifier to remove.
 * @return zero in case of a successful removal, a non-zero value in case of errors.
 */
int ud_remove_event_handler(ud_state_t *ud_state, eh_id_t event_handler_id);

/**
 * Schedules a given task to be executed after a given interval.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param interval the interval in seconds to schedule the task in;
 * @param task the task to schedule;
 * @param context the (optional) context to pass on to the task.
 * @return zero in case of success, a non-zero value in case of errors.
 */
int ud_schedule_task(ud_state_t *ud_state, int interval,
                     ud_task_t task, void *context);

/**
 * Runs the main loop of udaemon.
 *
 * This method will fork to the background, unless configured otherwise, and
 * start listening for events. For every received event, the corresponding
 * event handler is called.
 * Scheduled tasks are invoked when their deadline is hit.
 *
 * This method will return if the main loop is terminated by either a SIGINT or
 * SIGTERM.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @return non-zero in case of errors, zero if successful.
 */
int ud_main_loop(ud_state_t *ud_state);

#endif /* UDAEMON_H_ */
