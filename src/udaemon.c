/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
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
    ud_config_t *config;
    struct pollfd pollfds[FD_MAX];
    ud_ehdef_t event_handlers[FD_MAX];
    ud_taskdef_t task_queue[TASK_MAX];
};

static int event_pipe[2] = { 0, 0 };

static int udaemon_initialize(ud_state_t *ud_state) {
    if (ud_state->config->initialize) {
        return ud_state->config->initialize(ud_state);
    }
    return 0;
}

static void udaemon_signal_handler(ud_state_t *ud_state, ud_signal_t signal) {
    if (ud_state->config->signal_handler) {
        ud_state->config->signal_handler(ud_state, signal);
    } else {
        log_debug("received signal: %d", signal);
    }
}

static int udaemon_cleanup(ud_state_t *ud_state) {
    if (ud_state->config->cleanup) {
        return ud_state->config->cleanup(ud_state);
    }
    return 0;
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

static void main_signal_handler(ud_state_t *ud_state, struct pollfd *pollfd, void *context) {
    bool *loop = context;

    ud_signal_t signal = read_signal_event(pollfd->fd);

    udaemon_signal_handler(ud_state, signal);

    if (signal == SIG_TERM) {
        log_debug("terminating main event loop...");
        *loop = false;
    }
}

static void run_tasks(ud_state_t *ud_state, time_t now) {
    for (int i = 0; i < TASK_MAX; i++) {
        ud_taskdef_t *taskdef = &ud_state->task_queue[i];

        if (taskdef->task && taskdef->next_deadline < now) {
            int retval = taskdef->task(ud_state, taskdef->interval, taskdef->context);
            if (retval <= 0) {
                taskdef->task = NULL;
            } else {
                taskdef->interval = (uint16_t) retval;
                taskdef->next_deadline = now + retval;
            }
        }
    }
}

const char *ud_version() {
    return UD_VERSION;
}

ud_state_t *ud_init(ud_config_t *config) {
    ud_state_t *state = malloc(sizeof(ud_state_t));
    if (!state) {
        perror("malloc");
        return NULL;
    }
    // Clear out the initial state...
    bzero(state, sizeof(ud_state_t));

    state->config = config;

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

bool ud_valid_event_handler_id(eh_id_t event_handler_id) {
    return event_handler_id < FD_MAX;
}

int ud_add_event_handler(ud_state_t *ud_state, int fd, short emask,
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

    ud_state->pollfds[idx].fd = fd;
    ud_state->pollfds[idx].events = emask;
    ud_state->pollfds[idx].revents = 0;

    ud_state->event_handlers[idx] = (ud_ehdef_t) {
        .callback = callback,
        .context = context,
    };

    if (event_handler_id) {
        *event_handler_id = (eh_id_t) idx;
    }

    return 0;
}

int ud_remove_event_handler(ud_state_t *ud_state, eh_id_t event_handler_id) {
    if (ud_state == NULL || event_handler_id == 0) {
        return -EINVAL;
    }

    int idx = (int) event_handler_id;
    if (ud_state->pollfds[idx].fd < 0) {
        return -EINVAL;
    }

    log_debug("Removing event handler at idx: %d", idx);

    ud_state->pollfds[idx].fd = -1;
    ud_state->pollfds[idx].events = 0;
    ud_state->pollfds[idx].revents = 0;

    ud_state->event_handlers[idx].callback = NULL;
    ud_state->event_handlers[idx].context = NULL;

    return 0;
}

int ud_schedule_task(ud_state_t *ud_state, uint16_t interval, ud_task_t task, void *context) {
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

    ud_state->task_queue[idx].task = task;
    ud_state->task_queue[idx].interval = interval;
    ud_state->task_queue[idx].next_deadline = next_deadline;
    ud_state->task_queue[idx].context = context;

    return 0;
}

int ud_main_loop(ud_state_t *ud_state) {
    bool loop = true;
    int retval;

    // close any file descriptors we inherited...
    const long max_fd = sysconf(_SC_OPEN_MAX);
    for (int fd = 3; fd < max_fd; fd++) {
        close(fd);
    }
    // do this *after* we've closed the file descriptors!
    init_logging(ud_state->config->progname, ud_state->config->debug, ud_state->config->foreground);

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
    ud_add_event_handler(ud_state, event_pipe[0], POLLIN, main_signal_handler, &loop, NULL);

    if (!ud_state->config->foreground) {
        log_debug("Using PID file '%s', uid %d, gid %d",
                  ud_state->config->pid_file,
                  ud_state->config->priv_user,
                  ud_state->config->priv_group);

        if (ud_state->config->pid_file) {
            retval = daemonize(
                         ud_state->config->pid_file,
                         ud_state->config->priv_user,
                         ud_state->config->priv_group);

            if (retval) {
                log_warning("Daemonization failed!");
                goto cleanup;
            }
        }
    }

    retval = udaemon_initialize(ud_state);
    if (retval) {
        log_warning("Initialization failed!");
        goto cleanup;
    }

    while (loop) {
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
            if (ud_state->config->idle_handler) {
                ud_state->config->idle_handler(ud_state);
            }
        } else {
            // There was something of interest; let's look a little closer...
            for (int i = 0; i < FD_MAX; i++) {
                if (ud_state->pollfds[i].revents) {
                    // something of interest happened...
                    ud_event_handler_t callback = ud_state->event_handlers[i].callback;
                    void *context = ud_state->event_handlers[i].context;
                    callback(ud_state, &ud_state->pollfds[i], context);
                }
            }
        }
    }

cleanup:
    log_debug("Cleaning up...");

    if (ud_state->config->pid_file) {
        // best effort; will only succeed if the permissions are set correctly...
        unlink(ud_state->config->pid_file);
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
