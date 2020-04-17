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

#include <pwd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "udaemon/udaemon.h"
#include "udaemon/ud_utils.h"

#define PROGNAME "test"
#define VERSION "1.0"

#define CONF_FILE "test.cfg"
#define PID_FILE "/var/run/test.pid"

#define PORT 9000

typedef struct {
    int server_port;
    char *msg;
} test_config_t;

typedef struct {
    bool connected;
    int test_server_fd;
    struct sockaddr_in test_server;
    eh_id_t test_event_handler_id;
} run_state_t;

static int reconnect_server(const ud_state_t *ud_state, const uint16_t interval, void *context);
static int disconnect_server(const ud_state_t *ud_state, void *context);

static void test_file_callback(const ud_state_t *ud_state, struct pollfd *pollfd, void *context) {
    run_state_t *run_state = context;

    if (pollfd->revents & (POLLHUP | POLLERR | POLLNVAL)) {
        log_info("Socket closed by server...");

        ud_schedule_task(ud_state, 0, reconnect_server, context);
    }
    if ((pollfd->revents & POLLIN)) {
        static uint8_t buf[128] = { 0 };
        int cnt = read(pollfd->fd, &buf, sizeof(buf));
        if (cnt > 0) {
            log_info("Read %d bytes from server!", cnt);
        } else if (cnt == 0) {
            log_info("Socket closed by server (EOF)...");

            // Signal that we no longer want to read anything; otherwise
            // we are called many times more after this call with the same
            // signal. This task will terminate when `reconnect_server` is
            // called, so it does not make us lose anything...
            pollfd->events = pollfd->events & ~POLLIN;

            ud_schedule_task(ud_state, 0, reconnect_server, context);
        } else {
            log_warning("Error obtained while reading from server?!");
        }
    }
}

static int connect_server(const ud_state_t *ud_state, void *context) {
    const test_config_t *cfg = ud_get_app_config(ud_state);
    run_state_t *run_state = context;

    bzero(&run_state->test_server, sizeof(run_state->test_server));
    run_state->test_server.sin_family = AF_INET;
    run_state->test_server.sin_port = htons(cfg->server_port);
    run_state->test_server.sin_addr.s_addr = inet_addr("127.0.0.1");

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        log_error("Unable to create socket!");
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *)&run_state->test_server, sizeof(run_state->test_server))) {
        log_error("Unable to connect to server!");
        close(sockfd); // avoid leaking FDs!
        return 1;
    }

    run_state->test_server_fd = sockfd;

    if (ud_add_event_handler(ud_state, run_state->test_server_fd, POLLIN,
                             test_file_callback, context, &run_state->test_event_handler_id)) {
        log_warning("Failed to register event handler!");
        return -1;
    }

    return 0;
}

static int disconnect_server(const ud_state_t *ud_state, void *context) {
    run_state_t *run_state = context;

    int old_fd = run_state->test_server_fd;
    run_state->test_server_fd = 0;

    if (old_fd) {
        close(old_fd);
    }

    if (run_state->test_event_handler_id) {
        if (ud_remove_event_handler(ud_state, run_state->test_event_handler_id)) {
            log_debug("Failed to remove event handler?!");
            return -1;
        }

        run_state->test_event_handler_id = 0;
    }

    return 0;
}

static int reconnect_server(const ud_state_t *ud_state, const uint16_t interval, void *context) {
    run_state_t *run_state = context;

    int rc;

    if (run_state->connected) {
        log_debug("Reconnecting to server (interval %d)...", interval);

        rc = disconnect_server(ud_state, context);
        if (rc < 0) {
            // signal error...
            return rc;
        }
    } else {
        log_debug("Connecting to server (interval %d)...", interval);
    }

    rc = connect_server(ud_state, context);
    if (rc < 0) {
        // signal error...
        return rc;
    } else if (rc == 0) {
        // all is well...
        run_state->connected = true;

        return 0;
    }

    return interval ? interval << 1 : 1;
}

static void test_signal_handler(const ud_state_t *ud_state, ud_signal_t signal) {
    if (signal == SIG_HUP) {
        run_state_t *run_state = ud_get_app_state(ud_state);

        // close and recreate socket connection...
        ud_schedule_task(ud_state, 0, reconnect_server, run_state);
    } else if (signal == SIG_USR1) {
        log_info("Turning off debug logging...");

        set_loglevel(INFO);

        log_debug("No longer logging at debug level...");
    } else if (signal == SIG_USR2) {
        log_info("Turning on debug logging...");

        set_loglevel(DEBUG);

        log_debug("Now logging at debug level...");
    } else {
        log_debug("Got signal: %d", signal);
    }
}

static int test_initialize(const ud_state_t *ud_state) {
    run_state_t *run_state = ud_get_app_state(ud_state);

    log_debug("Initializing test, running against udaemon %s...", ud_version());
    log_debug("Application configuration is %s", ud_get_app_config(ud_state) ? "present" : "NOT present");
    log_debug("Application state is %s", run_state ? "present" : "NOT present");

    return ud_schedule_task(ud_state, 0, reconnect_server, run_state);
}

static int test_cleanup(const ud_state_t *ud_state) {
    run_state_t *run_state = ud_get_app_state(ud_state);

    log_debug("Cleaning up test...");

    disconnect_server(ud_state, run_state);

    return 0;
}

static void *test_config_parser(const char *conf_file, const void *config) {
    log_debug("Parsing test configuration...");

    test_config_t *cfg = malloc(sizeof(test_config_t));
    cfg->server_port = PORT;
    cfg->msg = strdup("hello world!");
    return cfg;
}

static void test_config_free(void *config) {
    log_debug("Freeing test configuration...");

    test_config_t *cfg = config;

    free(cfg->msg);
    free(cfg);
}

int main(int argc, char *argv[]) {
    run_state_t run_state = {
        .connected = false,
        .test_server_fd = 0,
        .test_server = 0,
        .test_event_handler_id = 0,
    };

    ud_config_t daemon_config = {
        .pid_file = NULL,
        .conf_file = NULL,
        .initialize = test_initialize,
        .signal_handler = test_signal_handler,
        .cleanup = test_cleanup,
        // configuration handling...
        .config_parser = test_config_parser,
        .config_cleanup = test_config_free,
    };

    // parse arguments...
    int opt;
    bool debug = false;
    char *uid_gid = NULL;

    while ((opt = getopt(argc, argv, "c:dfhp:u:v")) != -1) {
        switch (opt) {
        case 'c':
            daemon_config.conf_file = strdup(optarg);
            break;
        case 'd':
            debug = true;
            break;
        case 'f':
            daemon_config.foreground = true;
            break;
        case 'p':
            daemon_config.pid_file = strdup(optarg);
            break;
        case 'u':
            uid_gid = strdup(optarg);
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

    // setup logging for our application...
    setup_logging(daemon_config.foreground);
    set_loglevel(debug ? DEBUG : INFO);

    if (uid_gid) {
        uid_t uid = { 0 };
        gid_t gid = { 0 };
        if (ud_parse_uid(uid_gid, &uid, &gid)) {
            log_warning("Failed to parse %s as uid:gid!\n", uid_gid);
        } else {
            log_info("Requested to run as user: %d:%d...\n", uid, gid);
        }
        free(uid_gid);
    }

    // Use sane defaults...
    if (daemon_config.conf_file == NULL) {
        daemon_config.conf_file = strdup(CONF_FILE);
    }
    if (daemon_config.pid_file == NULL) {
        daemon_config.pid_file = strdup(PID_FILE);
    }

    ud_state_t *daemon = ud_init(&daemon_config);

    ud_set_app_state(daemon, &run_state);

    int retval = ud_main_loop(daemon);

    ud_destroy(daemon);

    free(daemon_config.conf_file);
    free(daemon_config.pid_file);

    return retval;
}