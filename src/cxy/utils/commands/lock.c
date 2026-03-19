/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-25
 */

#include "utils/commands/commands.h"
#include "core/log.h"
#include "driver/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>

/**
 * Get current time in milliseconds
 */
static i64 currentTimeMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000) + (i64)(ts.tv_nsec / 1000000);
}

/**
 * Build the lock file path from the lock name.
 * Lock files live in $TMPDIR (or /tmp) as .cxy-lock-<name>.lock
 * so they are shared across processes on the same machine.
 */
static void buildLockPath(const char *name, char *out, size_t outSize)
{
    const char *tmpDir = getenv("TMPDIR");
    if (!tmpDir || tmpDir[0] == '\0') {
        tmpDir = "/tmp";
    }
    snprintf(out, outSize, "%s/.cxy-lock-%s.lock", tmpDir, name);
}

/**
 * Try to acquire an exclusive flock on the given fd.
 * Polls every 100ms until timeout is reached.
 *
 * @param fd         Open file descriptor to lock
 * @param timeoutMs  How long to wait. 0 = try once and fail immediately.
 * @return true if lock acquired, false on timeout or error
 */
static bool acquireLock(int fd, i64 timeoutMs, Log *log)
{
    i64 deadline = currentTimeMs() + timeoutMs;

    // Spinner frames for visual feedback while waiting
    static const char *spinnerFrames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    static const int   spinnerCount    = 10;
    i64 attempt = 0;

    while (1) {
        // Try non-blocking first
        int ret = flock(fd, LOCK_EX | LOCK_NB);
        if (ret == 0) {
            // Got the lock
            if (attempt > 0) {
                // Clear spinner line
                printf("\r\033[K");
                fflush(stdout);
            }
            return true;
        }

        if (errno != EWOULDBLOCK) {
            logError(log, NULL, "flock failed: {s}", (FormatArg[]){{.s = strerror(errno)}});
            return false;
        }

        // Lock is held by someone else
        if (timeoutMs == 0) {
            // Fail immediately if no timeout
            return false;
        }

        i64 now       = currentTimeMs();
        i64 remaining = deadline - now;

        if (remaining <= 0) {
            if (attempt > 0) {
                printf("\r\033[K");
                fflush(stdout);
            }
            return false;
        }

        printf("\r\033[K %s Waiting for lock... %lldms remaining",
               spinnerFrames[attempt % spinnerCount],
               (long long)remaining);
        fflush(stdout);

        attempt++;
        usleep(100000);  // 100ms poll interval
    }
}

/**
 * Command: cxy utils lock <name> "<cmd>" [--timeout <ms>]
 *
 * Acquires an exclusive lock named <name>, runs <cmd>, then releases the lock.
 * If another process holds the lock, waits up to --timeout ms.
 * The lock is automatically released when the process exits (even on crash),
 * because the OS releases flocks when the file descriptor is closed.
 *
 * Lock files are stored in TMPDIR as .cxy-lock-<name>.lock.
 *
 * Exit codes:
 *   0  - command ran and succeeded
 *   1  - lock acquisition timed out, or command failed
 */
bool utilsLockCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *name = options->utils.lockName;
    const char *cmd  = options->utils.cmd;

    if (!name || name[0] == '\0') {
        logError(log, NULL,
                "no lock name specified. "
                "Usage: cxy utils lock <name> \"<cmd>\" [--timeout <ms>]",
                NULL);
        return false;
    }

    if (!cmd || cmd[0] == '\0') {
        logError(log, NULL,
                "no command specified. "
                "Usage: cxy utils lock <name> \"<cmd>\" [--timeout <ms>]",
                NULL);
        return false;
    }

    i64 timeoutMs = options->utils.lockTimeout;
    if (timeoutMs < 0) timeoutMs = 30000;

    // Build lock file path
    char lockPath[1024];
    buildLockPath(name, lockPath, sizeof(lockPath));

    // Open (or create) the lock file
    int fd = open(lockPath, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        logError(log, NULL,
                "failed to open lock file '{s}': {s}",
                (FormatArg[]){{.s = lockPath}, {.s = strerror(errno)}});
        return false;
    }

    // Write our PID into the lock file (informational, useful for debugging)
    // Truncate first so stale PID from previous holder is gone after we acquire
    // (we do this after acquiring the lock below)

    // Acquire the lock (blocking with timeout)
    if (!acquireLock(fd, timeoutMs, log)) {
        // Read the PID of the current lock holder for a helpful error message
        char pidBuf[32] = {0};
        pread(fd, pidBuf, sizeof(pidBuf) - 1, 0);
        pid_t holder = atoi(pidBuf);

        close(fd);

        if (holder > 0) {
            logError(log, NULL,
                    "could not acquire lock '{s}' after {i}ms (held by PID {i})",
                    (FormatArg[]){{.s = name},
                                  {.i = (int)timeoutMs},
                                  {.i = (int)holder}});
        } else {
            logError(log, NULL,
                    "could not acquire lock '{s}' after {i}ms",
                    (FormatArg[]){{.s = name}, {.i = (int)timeoutMs}});
        }
        return false;
    }

    // Write our PID now that we hold the lock
    ftruncate(fd, 0);
    lseek(fd, 0, SEEK_SET);
    char pidBuf[32];
    snprintf(pidBuf, sizeof(pidBuf), "%d\n", getpid());
    write(fd, pidBuf, strlen(pidBuf));

    // Run the command via sh -c
    char command[4096];
    snprintf(command, sizeof(command), "sh -c %s", cmd);

    pid_t child = fork();
    if (child < 0) {
        logError(log, NULL, "fork failed: {s}", (FormatArg[]){{.s = strerror(errno)}});
        close(fd);
        return false;
    }

    if (child == 0) {
        // Child: close lock fd so it doesn't inherit it, then exec
        close(fd);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    // Parent: wait for child to finish
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            logError(log, NULL, "waitpid failed: {s}", (FormatArg[]){{.s = strerror(errno)}});
            close(fd);
            return false;
        }
    }

    // Release the lock (closing the fd releases flock automatically)
    close(fd);

    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

    if (exitCode != 0) {
        logError(log, NULL,
                "command failed with exit code {i} (lock: {s})",
                (FormatArg[]){{.i = exitCode}, {.s = name}});
        return false;
    }

    return true;
}