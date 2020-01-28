/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "udaemon.h"

#define PROGNAME "test"
#define VERSION "1.0"

#define PID_FILE "/var/run/test.pid"

#define PORT 9000

typedef struct {
    int test_server_fd;
    struct sockaddr_in test_server;
    eh_id_t test_event_handler_id;
} run_state_t;

static run_state_t run_state = {
    .test_server_fd = 0,
    .test_server = 0,
    .test_event_handler_id = 0,
};

static int reconnect_server(ud_state_t *ud_state, int interval, void *context);
static int disconnect_server(ud_state_t *ud_state);

static void test_file_callback(ud_state_t *ud_state, struct pollfd *pollfd) {
    if (pollfd->revents & (POLLHUP | POLLERR | POLLNVAL)) {
        log_info("Socket closed by server...");

        disconnect_server(ud_state);

        ud_schedule_task(ud_state, 0, reconnect_server, NULL);
    }
    if ((pollfd->revents & POLLIN)) {
        static uint8_t buf[128] = { 0 };
        int cnt = read(pollfd->fd, &buf, sizeof(buf));
        if (cnt > 0) {
            log_info("Read %d bytes from server!", cnt);
        } else if (cnt == 0) {
            log_info("Socket closed by server (EOF)...");

            disconnect_server(ud_state);

            ud_schedule_task(ud_state, 0, reconnect_server, NULL);
        } else {
            log_warning("Error obtained while reading from server?!");
        }
    }
}

static int connect_server(ud_state_t *ud_state, int port) {
    bzero(&run_state.test_server, sizeof(run_state.test_server));
    run_state.test_server.sin_family = AF_INET;
    run_state.test_server.sin_port = htons(port);
    run_state.test_server.sin_addr.s_addr = inet_addr("127.0.0.1");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_error("Unable to create socket!");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&run_state.test_server, sizeof(run_state.test_server))) {
        log_error("Unable to connect to server!");
        return 1;
    }

    run_state.test_server_fd = sockfd;

    if (ud_add_event_handler(ud_state, run_state.test_server_fd, POLLIN, test_file_callback, &run_state.test_event_handler_id)) {
        log_warning("Failed to register event handler!");
        return -1;
    }

    return 0;
}

static int disconnect_server(ud_state_t *ud_state) {
    int old_fd = run_state.test_server_fd;
    run_state.test_server_fd = 0;

    if (old_fd) {
        close(old_fd);
    }

    if (run_state.test_event_handler_id) {
        if (ud_remove_event_handler(ud_state, run_state.test_event_handler_id)) {
            log_debug("Failed to remove event handler?!");
            return -1;
        }

        run_state.test_event_handler_id = 0;
    }

    return 0;
}

static int test_initialize(ud_state_t *ud_state) {
    log_debug("Initializing test...");

    if (connect_server(ud_state, PORT)) {
        return 1;
    }

    return 0;
}

static int reconnect_server(ud_state_t *ud_state, int interval, void *context) {
    int rc;

    log_debug("Reconnecting to server (interval %d)...", interval);

    rc = disconnect_server(ud_state);
    if (rc < 0) {
        return rc;
    }

    rc = connect_server(ud_state, PORT);
    if (rc <= 0) {
        return rc;
    }

    return interval ? interval << 1 : 1;
}

static void test_signal_handler(ud_state_t *ud_state, ud_signal_t signal) {
    if (signal == SIG_HUP) {
        // close and recreate socket connection...
        reconnect_server(ud_state, 0, NULL);
    } else {
        log_debug("Got signal: %d", signal);
    }
}

static int test_cleanup(ud_state_t *ud_state) {
    log_debug("Cleaning up test...");

    disconnect_server(ud_state);

    return 0;
}

int main(int argc, char *argv[]) {
    ud_config_t daemon_config = {
        .progname = PROGNAME,
        .pid_file = PID_FILE,
        .initialize = test_initialize,
        .signal_handler = test_signal_handler,
        .cleanup = test_cleanup,
    };

    // parse arguments...
    int opt;

    while ((opt = getopt(argc, argv, "dfhp:v")) != -1) {
        switch (opt) {
        case 'd':
            daemon_config.debug = true;
            break;
        case 'f':
            daemon_config.foreground = true;
            break;
        case 'p':
            daemon_config.pid_file = strdup(optarg);
            break;
        case 'v':
        case 'h':
        default:
            fprintf(stderr, PROGNAME " v" VERSION "\n");
            if (opt == 'v') {
                exit(0);
            }
            fprintf(stderr, "Usage: %s [-d] [-f] [-c config file] [-p pid file] [-v]\n", PROGNAME);
            exit(1);
        }
    }

    ud_state_t *daemon = ud_init(&daemon_config);

    int retval = ud_main_loop(daemon);

    ud_destroy(daemon);

    if (daemon_config.pid_file && daemon_config.pid_file != PID_FILE) {
        free(daemon_config.pid_file);
    }

    return retval;
}