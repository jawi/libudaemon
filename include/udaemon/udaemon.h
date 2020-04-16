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
    /** the path to the configuration file. */
    char *conf_file;

    // Hooks and callbacks...

    /**
     * Callback method called during right before the mainloop of udaemon
     * starts. This method can be used to setup tasks that need to be run as
     * part of the mainloop of udaemon.
     *
     * @param ud_state the current state of udaemon, cannot be NULL.
     * @return 0 upon success, or any non-zero value in case of errors.
     */
    int (*initialize)(const ud_state_t *ud_state);
    /**
     * Callback method called for each OS signal that is received.
     *
     * @param ud_state the current state of udaemon, cannot be NULL;
     * @param signal the OS signal that is received.
     */
    void (*signal_handler)(const ud_state_t *ud_state, const ud_signal_t signal);
    /**
     * Callback method called when no data or event is received during the
     * mainloop of udaemon. This method should perform as little work as
     * possible to avoid the mainloop from missing events.
     *
     * @param ud_state the current state of udaemon, cannot be NULL.
     */
    void (*idle_handler)(const ud_state_t *ud_state);
    /**
     * Callback method called when the cleanup of udaemon is performed. Is
     * called automatically when the mainloop of udaemon terminates.
     *
     * @param ud_state the current state of udaemon, cannot be NULL.
     * @return 0 upon success, or any non-zero value in case of errors.
     */
    int (*cleanup)(const ud_state_t *ud_state);
    /**
     * Callback method called for parsing the application-specific
     * configuration. Is called automatically at the start of the mainloop of
     * udaemon before daemonizing the application and dropping privileges.
     *
     * In case parsing fails for whatever reason, NULL can be returned. In case
     * the configuration is reloaded (SIGHUP), this condition will cause to old
     * configuration *not* to be replaced.
     *
     * @param conf_file the configuration file to parse, cannot be NULL;
     * @param cur_config the current configuration (if present), can be NULL.
     * @return the parsed application configuration, or NULL in case of errors.
     */
    void*(*config_parser)(const char *conf_file, const void *cur_config);
    /**
     * Callback method called when the application-specific configuration needs
     * to be freed. Called automatically when the configuration is reloaded, or
     * when the mainloop of udaemon terminates.
     *
     * @param config the application configuration to free.
     */
    void (*config_cleanup)(void *config);
} ud_config_t;

/**
 * Callback event handler for polled event handling.
 * 
 * Event handlers are called automatically when a poll()-event is retrieved. In
 * all cases, the implementation must take care of error handling. Note that in
 * case of POLLIN, the number of bytes that can be read can be 0 in case of an
 * EOF condition. Be aware that in such cases the event handler can/will be 
 * called multiple times if the file descriptor is not closed or otherwise 
 * ignored from reading. A solution for this is to clear the POLLIN bit from
 * pollfd->event.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param pollfd the polling information, such as file descriptor, cannot be NULL;
 * @param context the context registered with the event handler, can be NULL.
 */
typedef void (*ud_event_handler_t)(const ud_state_t *ud_state, struct pollfd *pollfd, void *context);

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
typedef int (*ud_task_t)(const ud_state_t *ud_state, const uint16_t interval, void *context);

/**
 * Denotes an identifier of event handlers.
 */
typedef uint8_t eh_id_t;

/**
 * Denotes an invalid event handler ID.
 */
#define UD_INVALID_ID (uint8_t)(-1)

/**
 * Returns the current version of udaemon, as string.
 *
 * @return the version of udaemon as string.
 */
const char *ud_version(void);

/**
 * Initializes a new udaemon state value.
 *
 * @param config the udaemon configuration to use.
 * @return a new udaemon state value, or NULL in case of out of memory.
 */
ud_state_t *ud_init(const ud_config_t *config);

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
 * Provides access to the udaemon configuration.
 *
 * @param ud_state the udaemon state to use, cannot be NULL.
 * @return the udaemon configuration, cannot be NULL.
 */
const ud_config_t *ud_get_udaemon_config(const ud_state_t *ud_state);

/**
 * Provides access to the application configuration.
 *
 * @param ud_state the udaemon state to use, cannot be NULL.
 * @return the application configuration, if defined, can be NULL.
 */
const void *ud_get_app_config(const ud_state_t *ud_state);

/**
 * Tests whether or not a given event handler ID is valid.
 *
 * NOTE: this method only does a heuristic check. When this method returns
 * true, there is no guarantee an event handler is still registered!
 *
 * @param event_handler_id the event handler ID to test.
 */
bool ud_valid_event_handler_id(const eh_id_t event_handler_id);

/**
 * Adds a new event handler for polling (file-based) events.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param fd the file descriptor to poll;
 * @param emask the event mask to poll for;
 * @param callback the event callback to call once an event is available;
 * @param context the (optional) context to pass to the event handler callback;
 * @param event_handler_id (optional) the event handler identifier that is
 * @return a non-zero value in case of errors, or zero in case of success.
 */
int ud_add_event_handler(const ud_state_t *ud_state, const int fd, const short emask,
                         const ud_event_handler_t callback,
                         void *context,
                         eh_id_t *event_handler_id);

/**
 * Removes a previously registered event handler.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param event_handler_id the event handler identifier to remove.
 * @return zero in case of a successful removal, a non-zero value in case of errors.
 */
int ud_remove_event_handler(const ud_state_t *ud_state, const eh_id_t event_handler_id);

/**
 * Schedules a given task to be executed after a given interval.
 *
 * @param ud_state the udaemon state to use, cannot be NULL;
 * @param interval the interval in seconds to schedule the task in;
 * @param task the task to schedule;
 * @param context the (optional) context to pass on to the task.
 * @return zero in case of success, a non-zero value in case of errors.
 */
int ud_schedule_task(const ud_state_t *ud_state, const uint16_t interval,
                     const ud_task_t task, void *context);

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
