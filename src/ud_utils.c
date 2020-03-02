/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "udaemon/ud_utils.h"
#include "udaemon/ud_logging.h"

/**
 * Drops the privileges of the running process to the user identified by the given UID/GID.
 *
 * @param uid the user id to drop privileges to;
 * @param gid the group id to drop privileges to.
 * @return 0 if successful, or non-zero in case of failure.
 */
static int drop_privileges(uid_t uid, gid_t gid) {
    if (getuid() != 0) {
        // not running as root...
        log_debug("not running as root, no need to drop privileges...");
        return 0;
    }

    if (setgid(gid) != 0) {
        log_error("unable to drop group privileges: %m");
        return -1;
    }

    if (setuid(uid) != 0) {
        log_error("unable to drop user privileges: %m");
        return -1;
    }

    return 0;
}

/**
 * Writes the process ID of the running process to a file.
 *
 * @param pidfile the path to the PID file to write;
 * @param uid the owning user ID of the PID file;
 * @param gid the owning group ID of the PID file;
 * @return 0 if successful, or non-zero in case of failure.
 */
static int write_pidfile(const char *pidfile, uid_t uid, gid_t gid) {
    const bool is_root = (getuid() == 0);

    if (unlink(pidfile)) {
        if (is_root && (errno != ENOENT)) {
            log_error("unable to remove pidfile: %m");
        }
    }

    int fd;

    if ((fd = open(pidfile, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH)) < 0) {
        if (is_root) {
            log_error("unable to create pidfile: %m");
            return -1;
        } else {
            // no sense in continuing here, write will fail any way...
            log_debug("not running as root, but unable to write PID file: %m");
            return 0;
        }
    }

    // ensure the pid file has the correct permissions...
    if (is_root && uid != 0) {
        if (fchown(fd, uid, gid)) {
            log_error("unable to change ownership of pidfile: %m");
        }
    }

    dprintf(fd, "%d\n", getpid());

    close(fd);

    return 0;
}

#define SAFE_SIGNAL(rc) \
    int _rc = rc; \
    if (write(err_pipe[1], &_rc, 1) != 1) { \
        log_warning("failed to write single byte to pipe!"); \
    } \
    close(err_pipe[1]);

#define SIGNAL_SUCCESS() \
	do { \
        SAFE_SIGNAL(err_none) \
	} while (0)

#define SIGNAL_FAILURE(rc) \
	do { \
        SAFE_SIGNAL(rc) \
		exit(rc); \
	} while (0)

int daemonize(const char *pid_file, uid_t uid, gid_t gid) {
    // create a anonymous pipe to communicate between daemon and our parent...
    int err_pipe[2] = { 0 };
    if (pipe(err_pipe) < 0) {
        perror("pipe");
        return -err_pipe_create;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -err_fork;
    } else if (pid > 0) {
        // parent, wait until daemon is finished initializing...
        close(err_pipe[1]);

        int rc = 0;
        if (read(err_pipe[0], &rc, 1) < 0) {
            rc = err_pipe_read;
        }
        exit(rc);
    } else { /* pid == 0 */
        // first child continues here...
        // NOTE: we can/should communicate our state to our parent in order for it to terminate!

        // we only write to this pipe...
        close(err_pipe[0]);

        // create a new session...
        pid = setsid();
        if (pid < 0) {
            SIGNAL_FAILURE(err_setsid);
        }

        // fork again to ensure the daemon cannot take back the controlling tty...
        pid = fork();
        if (pid < 0) {
            SIGNAL_FAILURE(err_daemonize);
        } else if (pid > 0) {
            // terminate first child...
            exit(err_none);
        } else { /* pid == 0 */
            // actual daemon starts here...
            int fd;

            if ((fd = open("/dev/null", O_RDWR)) < 0) {
                log_error("unable to open /dev/null: %m");
                SIGNAL_FAILURE(err_dev_null);
            } else {
                dup2(fd, STDIN_FILENO);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            umask(0);

            if (chdir("/") != 0) {
                log_error("unable to change directory: %m");
                SIGNAL_FAILURE(err_chdir);
            }

            if (write_pidfile(pid_file, uid, gid)) {
                SIGNAL_FAILURE(err_pid_file);
            }

            if (drop_privileges(uid, gid)) {
                SIGNAL_FAILURE(err_drop_privs);
            }

            // Finish startup...
            if (err_pipe[1] != -1) {
                SIGNAL_SUCCESS();
            }
        } /* daemon pid == 0 */
    } /* first child pid == 0 */

    return err_none;
}
