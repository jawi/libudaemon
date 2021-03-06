/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>

#include "udaemon/ud_logging.h"
#include "udaemon/ud_utils.h"
#include "udaemon/ud_version.h"
#include "udaemon/udaemon.h"

// Let's see how often this is not sufficient...
#define FD_MAX 5
#define TASK_MAX 10

typedef struct ud_taskdef {
    ud_task_t task;
    uint16_t interval;
    time_t next_deadline;
    void *context;
} ud_taskdef_t;

typedef struct ud_ehdef {
    ud_event_handler_t callback;
    void *context;
} ud_ehdef_t;

struct ud_state {
    volatile bool running;
    const ud_config_t *ud_config;
    /** the actual application configuration. */
    void *app_config;
    /** the application state. */
    void *app_state;

    struct pollfd pollfds[FD_MAX];
    ud_ehdef_t event_handlers[FD_MAX];
    ud_taskdef_t task_queue[TASK_MAX];
};

static int event_pipe[2] = { 0, 0 };

static int udaemon_initialize(const ud_state_t *ud_state) {
    const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);

    if (ud_cfg->initialize) {
        return ud_cfg->initialize(ud_state);
    }
    return 0;
}

static void udaemon_signal_handler(const ud_state_t *ud_state, ud_signal_t signal) {
    const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);
    if (ud_cfg->signal_handler) {
        ud_cfg->signal_handler(ud_state, signal);
    } else {
        log_debug("received signal: %d", signal);
    }
}

static int udaemon_replace_config(const ud_state_t *ud_state, void *new_app_cfg) {
    void *old_app_cfg = ud_state->app_config;
    // remove the const-ness to allow changing the configuration...
    ((ud_state_t *) ud_state)->app_config = new_app_cfg;

    if (old_app_cfg) {
        const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);

        log_debug("Cleaning up configuration...");

        if (ud_cfg->config_cleanup) {
            ud_cfg->config_cleanup(old_app_cfg);
        } else {
            // best effort; hope we do not leave stuff behind...
            log_debug("No config_cleanup hook defined! Using default configuration cleanup!");

            free(old_app_cfg);
        }
    }
    return 0;
}

static int udaemon_read_config(const ud_state_t *ud_state) {
    const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);

    if (ud_cfg->conf_file && ud_cfg->config_parser) {
        if (ud_state->app_config) {
            log_debug("Reloading configuration from %s", ud_cfg->conf_file);
        } else {
            log_debug("Loading configuration from %s", ud_cfg->conf_file);
        }

        void *new_cfg = ud_cfg->config_parser(ud_cfg->conf_file, ud_state->app_config);
        if (!new_cfg) {
            // leave the existing configuration as-is...
            return 1;
        }
        return udaemon_replace_config(ud_state, new_cfg);
    }
    return 0;
}

static int udaemon_cleanup(ud_state_t *ud_state) {
    const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);

    int retval;
    if (ud_cfg->cleanup) {
        retval = ud_cfg->cleanup(ud_state);
        if (retval) {
            log_warning("Failed to perform cleanup!");
        }
    }
    // Cleanup the configuration, if any...
    return udaemon_replace_config(ud_state, NULL);
}

static uint8_t read_signal_event(int fd) {
    static uint8_t buf[1] = { 0 };
    if (read(fd, &buf, sizeof(buf)) != sizeof(buf)) {
        log_warning("Did not read all event data?!");
    }
    return buf[0];
}

static void write_signal_event(int fd, uint8_t event_type) {
    uint8_t buf[1] = { event_type };
    if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
        log_warning("Did not write all event data?!");
    }
}

static void os_signal_handler(int signo) {
    if (signo == SIGTERM || signo == SIGINT) {
        write_signal_event(event_pipe[1], SIG_TERM);
    } else if (signo == SIGHUP) {
        write_signal_event(event_pipe[1], SIG_HUP);
    } else if (signo == SIGUSR1) {
        write_signal_event(event_pipe[1], SIG_USR1);
    } else if (signo == SIGUSR2) {
        write_signal_event(event_pipe[1], SIG_USR2);
    } else {
        log_debug("Unknown/unhandled signal: %d", signo);
    }
}

static ud_result_t main_signal_handler(const ud_state_t *ud_state, struct pollfd *pollfd, void *context) {
    (void)context;

    ud_signal_t signal = read_signal_event(pollfd->fd);

    if (signal == SIG_HUP) {
        udaemon_read_config(ud_state);
    }

    udaemon_signal_handler(ud_state, signal);

    if (signal == SIG_TERM) {
        log_debug("Terminating main event loop...");
        if (ud_terminate(ud_state)) {
            log_warning("Failed to terminate main event loop!");
        }
    }

    return RES_OK;
}

static void run_tasks(ud_state_t *ud_state, time_t now) {
    for (int i = 0; i < TASK_MAX; i++) {
        ud_taskdef_t *taskdef = &ud_state->task_queue[i];

        if (taskdef->task && taskdef->next_deadline < now) {
            int retval = taskdef->task(ud_state, taskdef->interval, taskdef->context);
            if (retval <= 0) {
                log_debug("Removing task at index %d", i);

                taskdef->task = NULL;
            } else {
                log_debug("Rescheduling task at index %d to run in %d seconds", i, retval);

                taskdef->interval = (uint16_t) retval;
                taskdef->next_deadline = now + retval;
            }
        }
    }
}

const char *ud_version() {
    return UD_VERSION;
}

ud_state_t *ud_init(const ud_config_t *config) {
    ud_state_t *state = malloc(sizeof(ud_state_t));
    if (!state) {
        perror("malloc");
        return NULL;
    }
    // Clear out the initial state...
    memset(state, 0, sizeof(ud_state_t));

    state->ud_config = config;

    for (int i = 0; i < FD_MAX; i++) {
        // ensure poll() doesn't do anything with these by default...
        state->pollfds[i].fd = -1;
    }

    return state;
}

void ud_destroy(ud_state_t *ud_state) {
    if (ud_state) {
        free(ud_state);
    }
}

inline const ud_config_t *ud_get_udaemon_config(const ud_state_t *ud_state) {
    if (ud_state) {
        return ud_state->ud_config;
    }
    return NULL;
}

const void *ud_get_app_config(const ud_state_t *ud_state) {
    const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);
    if (ud_state && ud_cfg && ud_cfg->conf_file) {
        // if we're configured *and* there's a config file set *then*
        // presume there's a valid application configuration present...
        return ud_state->app_config;
    }
    return NULL;
}

void *ud_get_app_state(const ud_state_t *ud_state) {
    if (ud_state) {
        return ud_state->app_state;
    }
    return NULL;
}

void *ud_set_app_state(const ud_state_t *ud_state, void *app_state) {
    void *old_state = ud_get_app_state(ud_state);
    if (ud_state) {
        // remove the const-ness to update our internal state...
        ((ud_state_t *) ud_state)->app_state = app_state;
    }
    return old_state;
}

bool ud_valid_event_handler_id(eh_id_t event_handler_id) {
    return event_handler_id < FD_MAX;
}

int ud_add_event_handler(const ud_state_t *ud_state, int fd, short emask,
                         ud_event_handler_t callback, void *context, eh_id_t *event_handler_id) {
    if (ud_state == NULL || callback == NULL) {
        return -EINVAL;
    }

    int idx = -1;
    for (int i = 0; i < FD_MAX; i++) {
        // Find first unused spot...
        if (ud_state->pollfds[i].fd < 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -ENOMEM;
    }

    log_debug("Adding event handler at idx: %d", idx);

    // cast away the const, the caller doesn't see this change...
    ud_state_t *state = (ud_state_t *)ud_state;

    state->pollfds[idx].fd = fd;
    state->pollfds[idx].events = emask;
    state->pollfds[idx].revents = 0;

    state->event_handlers[idx] = (ud_ehdef_t) {
        .callback = callback,
        .context = context,
    };

    if (event_handler_id) {
        *event_handler_id = (eh_id_t) idx;
    }

    return 0;
}

int ud_remove_event_handler(const ud_state_t *ud_state, eh_id_t event_handler_id) {
    if (ud_state == NULL || event_handler_id == 0) {
        return -EINVAL;
    }

    int idx = (int) event_handler_id;

    log_debug("Removing event handler at idx: %d", idx);

    // cast away the const, the caller doesn't see this change...
    ud_state_t *state = (ud_state_t *)ud_state;

    state->pollfds[idx].fd = -1;
    state->pollfds[idx].events = 0;
    state->pollfds[idx].revents = 0;

    state->event_handlers[idx].callback = NULL;
    state->event_handlers[idx].context = NULL;

    return 0;
}

int ud_schedule_task(const ud_state_t *ud_state, uint16_t interval, ud_task_t task, void *context) {
    if (ud_state == NULL) {
        return -EINVAL;
    }

    int idx = -1;
    for (int i = 0; i < TASK_MAX; i++) {
        // Find first unused spot...
        if (ud_state->task_queue[i].task == NULL) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -ENOMEM;
    }

    log_debug("Adding task at index %d", idx);

    time_t next_deadline = time(NULL) + interval;

    // cast away the const, the caller doesn't see this change...
    ud_state_t *state = (ud_state_t *)ud_state;

    state->task_queue[idx].task = task;
    state->task_queue[idx].interval = interval;
    state->task_queue[idx].next_deadline = next_deadline;
    state->task_queue[idx].context = context;

    return 0;
}

extern void destroy_logging(void);

int ud_main_loop(ud_state_t *ud_state) {
    const ud_config_t *ud_cfg = ud_get_udaemon_config(ud_state);

    int retval;
    // Indicate that we're currently running...
    ud_state->running = true;

    // close any file descriptors we inherited...
    ud_closefrom(STDERR_FILENO);

    /* catch all interesting signals */
    struct sigaction sigact;

    sigact.sa_handler = os_signal_handler;
    sigact.sa_flags = 0;

    sigemptyset(&sigact.sa_mask);

    sigaction(SIGUSR1, &sigact, NULL);
    sigaction(SIGUSR2, &sigact, NULL);
    sigaction(SIGHUP, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGALRM, &sigact, NULL);
    sigaction(SIGCHLD, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);

    // Ignore SIGPIPE
    sigact.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sigact, NULL);

    // allow events to be sent through a pipe...
    if (pipe(event_pipe) < 0) {
        perror("pipe");
        goto cleanup;
    }

    // reserve this for our own events...
    ud_add_event_handler(ud_state, event_pipe[0], POLLIN, main_signal_handler, NULL, NULL);

    if (!ud_cfg->foreground) {
        log_debug("Going drop privileges to uid %d, gid %d",
                  ud_cfg->priv_user, ud_cfg->priv_group);
        if (ud_cfg->pid_file) {
            log_debug("Using PID file '%s'", ud_cfg->pid_file);
        }

        retval = daemonize(ud_cfg->pid_file, ud_cfg->priv_user, ud_cfg->priv_group);

        if (retval) {
            log_warning("Daemonization failed!");
            goto cleanup;
        }
    }

    // read configuration right after we've dropped privileges...
    retval = udaemon_read_config(ud_state);
    if (retval) {
        log_warning("Failed to read/parse application configuration! Trying to continue with defaults...");
    }

    retval = udaemon_initialize(ud_state);
    if (retval) {
        log_warning("Initialization failed!");
        goto cleanup;
    }

    while (ud_state->running) {
        // Run all pending tasks first...
        run_tasks(ud_state, time(NULL));

        int count = poll(ud_state->pollfds, FD_MAX, 100);
        if (count < 0) {
            if (errno != EINTR) {
                log_warning("failed to poll: %m");
                break;
            }
        } else if (count == 0) {
            // Call back to the idle handler, if present...
            if (ud_cfg->idle_handler) {
                ud_cfg->idle_handler(ud_state);
            }
        } else {
            // There was something of interest; let's look a little closer...
            for (int i = 0; i < FD_MAX; i++) {
                if (ud_state->pollfds[i].revents) {
                    // something of interest happened...
                    ud_event_handler_t callback = ud_state->event_handlers[i].callback;

                    void *context = ud_state->event_handlers[i].context;

                    ud_result_t res = callback(ud_state, &ud_state->pollfds[i], context);
                    if (res == RES_ERROR) {
                        log_debug("Callback for fd#%d returned an error! Closing it...", ud_state->pollfds[i].fd);

                        close(ud_state->pollfds[i].fd);

                        // setting the fd to -1 is sufficient to ignore it in the next poll cycle...
                        ud_state->pollfds[i].fd = -1;
                    }
                }
            }
        }
    }

cleanup:
    log_debug("Cleaning up...");

    if (ud_cfg->pid_file) {
        // best effort; will only succeed if the permissions are set correctly...
        unlink(ud_cfg->pid_file);
    }

    if (udaemon_cleanup(ud_state)) {
        log_warning("Cleanup failed...");
    }

    destroy_logging();

    // Close our local resources...
    close(event_pipe[0]);
    close(event_pipe[1]);

    return 0;
}

int ud_terminate(const ud_state_t *ud_state) {
    if (ud_state) {
        ((ud_state_t *) ud_state)->running = false;
        return 0;
    }
    return 1;
}