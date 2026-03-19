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
#include "utils/async_tracker.h"
#include "core/log.h"
#include "driver/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define LOGS_READ_BUF_SIZE 4096

pid_t spawnAsyncProcess(const char *command,
                        const char **args,
                        u32 argCount,
                        bool captureOutput,
                        const char *buildDir,
                        Log *log)
{
    // Check tracker limit
    if (asyncTrackerGetCount() >= MAX_ASYNC_PROCESSES) {
        logError(log, NULL, "maximum async processes limit reached ({i})",
                (FormatArg[]){{.i = MAX_ASYNC_PROCESSES}});
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        logError(log, NULL, "fork failed: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return -1;
    }

    if (pid == 0) {
        // Child process

        // Create new process group (allows killing entire process tree)
        setpgid(0, 0);

        // Create new session (detach from parent terminal)
        setsid();

        // Always redirect stdin from /dev/null so the process is never
        // attached to the parent's pipe (which runCommandWithProgressFull sets up).
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        if (captureOutput) {
            // Redirect stdout/stderr to log file
            char logPath[1024];
            snprintf(logPath, sizeof(logPath),
                     "%s/.async-cmd-%d.log", buildDir, getpid());

            int fd = open(logPath, O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (fd >= 0) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
        } else {
            // Redirect stdout/stderr to /dev/null so the process doesn't
            // write into the parent's progress runner pipe and avoids SIGPIPE.
            int null_fd = open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                close(null_fd);
            }
        }

        // Execute command
        execvp(command, (char *const *)args);

        // If exec fails
        fprintf(stderr, "exec failed: %s\n", strerror(errno));
        exit(1);
    }

    // Parent process - track the PID
    if (!asyncTrackerAdd(pid, captureOutput, log)) {
        // Failed to track - kill the process we just spawned
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    return pid;
}

bool stopAsyncProcess(pid_t pid, Log *log)
{
    // Check if process exists
    if (kill(pid, 0) != 0) {
        if (errno == ESRCH) {
            logWarning(log, NULL, "process {i} does not exist",
                      (FormatArg[]){{.i = (int)pid}});
            return true;  // Already dead - consider it success
        } else {
            logError(log, NULL, "cannot signal process {i}: {s}",
                    (FormatArg[]){{.i = (int)pid}, {.s = strerror(errno)}});
            return false;
        }
    }

    // Kill process group (negative PID)
    kill(-pid, SIGTERM);

    // Wait up to 2 seconds for graceful shutdown
    for (int i = 0; i < 20; i++) {
        if (kill(pid, 0) != 0) {
            // Process exited
            waitpid(pid, NULL, WNOHANG);
            return true;
        }
        usleep(100000);  // 100ms
    }

    // Force kill if still running
    logWarning(log, NULL, "force killing process {i}",
              (FormatArg[]){{.i = (int)pid}});
    kill(-pid, SIGKILL);
    waitpid(pid, NULL, WNOHANG);

    return true;
}

/**
 * Attach to the parent run's async tracker via CXY_ASYNC_TRACKER env var.
 * Returns true if successfully attached or already active.
 */
static bool attachToParentTracker(Log *log)
{
    if (asyncTrackerIsActive()) {
        return true;
    }

    const char *trackerPath = getenv("CXY_ASYNC_TRACKER");
    if (!trackerPath || trackerPath[0] == '\0') {
        logError(log, NULL,
                "async tracker not initialized. "
                "This command must be called from within a script "
                "executed by 'cxy package run <script>'", NULL);
        return false;
    }

    // Attach to the parent tracker directly - no init, no stale cleanup
    strncpy(g_async_tracker.tracker_path, trackerPath,
            sizeof(g_async_tracker.tracker_path) - 1);
    g_async_tracker.tracker_path[sizeof(g_async_tracker.tracker_path) - 1] = '\0';
    g_async_tracker.cxy_pid = getppid();
    g_async_tracker.initialized = true;

    // Extract build dir from tracker path (strip filename)
    strncpy(g_async_tracker.build_dir, trackerPath,
            sizeof(g_async_tracker.build_dir) - 1);
    g_async_tracker.build_dir[sizeof(g_async_tracker.build_dir) - 1] = '\0';
    char *slash = strrchr(g_async_tracker.build_dir, '/');
    if (slash) *slash = '\0';

    return true;
}

bool utilsAsyncCmdStartCommand(const Options *options, StrPool *strings, Log *log)
{
    if (!attachToParentTracker(log)) {
        return false;
    }

    // Get the command string (run via sh -c so quoting/pipes etc. work)
    const char *cmdStr = options->utils.cmd;
    if (!cmdStr || cmdStr[0] == '\0') {
        logError(log, NULL,
                "no command specified. "
                "Usage: cxy utils async-cmd-start \"<cmd>\"", NULL);
        return false;
    }

    bool captureOutput = options->utils.captureOutput;

    const char *args[] = {"sh", "-c", cmdStr, NULL};
    const char *cmd = "sh";
    u32 argCount = 3;

    // Determine build directory from tracker
    const char *buildDir = g_async_tracker.build_dir;
    if (!buildDir || buildDir[0] == '\0') {
        buildDir = ".cxy/build";
    }

    pid_t pid = spawnAsyncProcess(cmd, args, argCount,
                                   captureOutput, buildDir, log);

    if (pid > 0) {
        // Write PID to .async-last-pid file so scripts can read it
        // even when stdout is captured by the progress runner pipe.
        char lastPidPath[1024];
        snprintf(lastPidPath, sizeof(lastPidPath), "%s/.async-last-pid", buildDir);

        FILE *fp = fopen(lastPidPath, "w");
        if (fp) {
            fprintf(fp, "%d\n", pid);
            fclose(fp);
        }

        // Also print to stdout (works when called directly)
        printf("%d\n", pid);
        fflush(stdout);

        if (captureOutput) {
            fprintf(stderr, "Started process %d (output captured to %s/.async-cmd-%d.log)\n",
                    pid, buildDir, pid);
        }

        return true;
    }

    return false;
}

bool utilsAsyncCmdLogsCommand(const Options *options, StrPool *strings, Log *log)
{
    if (!attachToParentTracker(log)) {
        return false;
    }

    const char *pid_str = options->utils.cmd;
    if (!pid_str || pid_str[0] == '\0') {
        logError(log, NULL,
                "no PID specified. "
                "Usage: cxy utils async-cmd-logs <pid> [--follow]", NULL);
        return false;
    }

    pid_t pid = (pid_t)atoi(pid_str);
    if (pid <= 0) {
        logError(log, NULL, "invalid PID: {s}", (FormatArg[]){{.s = pid_str}});
        return false;
    }

    const char *buildDir = g_async_tracker.build_dir;
    if (!buildDir || buildDir[0] == '\0') {
        buildDir = ".cxy/build";
    }

    char logPath[1024];
    snprintf(logPath, sizeof(logPath), "%s/.async-cmd-%d.log", buildDir, pid);

    FILE *fp = fopen(logPath, "r");
    if (!fp) {
        logError(log, NULL, "no log file found for PID {i} (looked at {s})",
                (FormatArg[]){{.i = (int)pid}, {.s = logPath}});
        return false;
    }

    bool follow = options->utils.logsFollow;
    char buf[LOGS_READ_BUF_SIZE];

    // Drain whatever is already written
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    fflush(stdout);

    if (follow) {
        // Keep tailing until the process is gone
        while (kill(pid, 0) == 0 || errno != ESRCH) {
            clearerr(fp);
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
                fwrite(buf, 1, n, stdout);
                fflush(stdout);
            }
            usleep(100000); // 100 ms poll
        }

        // Final drain after process exits
        clearerr(fp);
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            fwrite(buf, 1, n, stdout);
        }
        fflush(stdout);
    }

    fclose(fp);
    return true;
}

bool utilsAsyncCmdStopCommand(const Options *options, StrPool *strings, Log *log)
{
    if (!attachToParentTracker(log)) {
        return false;
    }

    const char *pid_str = options->utils.cmd;
    if (!pid_str || pid_str[0] == '\0') {
        logError(log, NULL,
                "no PID specified. "
                "Usage: cxy utils async-cmd-stop <pid>", NULL);
        return false;
    }

    pid_t pid = (pid_t)atoi(pid_str);

    if (pid <= 0) {
        logError(log, NULL, "invalid PID: {s}",
                (FormatArg[]){{.s = pid_str}});
        return false;
    }

    if (stopAsyncProcess(pid, log)) {
        asyncTrackerRemove(pid, log);
        printStatusSticky(log, "Stopped process %d", pid);
        return true;
    }

    return false;
}