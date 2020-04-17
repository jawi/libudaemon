/*
 * libudaemon - micro daemon library.
 *
 * Copyright: (C) 2020 jawi
 *   License: Apache License 2.0
 */
#ifndef UD_UTILS_H_
#define UD_UTILS_H_

#include <pwd.h>

typedef enum ud_daemon_result {
    err_none = 0,

    err_pipe_create = 10,
    err_fork = 11,
    err_pipe_read = 12,

    err_setsid = 20,
    err_daemonize = 21,
    err_dev_null = 22,
    err_pid_file = 23,
    err_config = 24,
    err_chdir = 25,
    err_drop_privs = 26
} ud_daemon_result;

/**
 * Utility method to close/delete all open file descriptors for the current
 * process. It mimicks the `closefrom` syscall that is available in *BSD and/or
 * other UNIX variants.
 *
 * If the given argument is invalid, errno is set to EINVAL and nothing is closed.
 *
 * @param lowfd the file descriptor (exclusive) to start closing/deleting from.
 *        Should be greater than zero.
 */
void ud_closefrom(int lowfd);

/**
 * Parses a given username/group description into a `uid_t` and `gid_t`. If no
 * valid description is given (NULL), this method yields the uid/gid for the
 * user "nobody".
 *
 * NOTE: the format in which the given `entry` is expected is:
 * `username`, `username:groupname`, `uid` or `uid:gid`.
 *
 * @param entry the username/group description to parse, may be NULL;
 * @param uid the resulting `uid`;
 * @param gid the resulting `gid`;
 * @return 0 if successful, or non-zero in case of failure.
 */
int ud_parse_uid(const char *entry, uid_t *uid, gid_t *gid);

/**
 * Daemonizes the current process into the background.
 *
 * This method will drop privileges to the given user and group, as well as
 * write a PID file to indicate the process ID of the daemonized process.
 *
 * NOTE: calling this function causes the calling (= parent) process to
 * terminate *after* the daemonization process is complete. This function
 * returns as the running, daemonized, process.
 *
 * @param pid_file the PID file to write with the PID of the background process;
 * @param uid the owning user ID of the PID file;
 * @param gid the owning group ID of the PID file.
 * @return 0 if successful, or non-zero in case of failure.
 */
int daemonize(const char *pid_file, uid_t uid, gid_t gid);

#endif /* UD_UTILS_H_ */
