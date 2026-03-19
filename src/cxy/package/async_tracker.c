/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-25
 */

#include "async_tracker.h"
#include "core/log.h"
#include "core/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <dirent.h>

/**
 * Global async tracker instance
 */
AsyncTracker g_async_tracker = {
    .cxy_pid = 0,
    .tracker_path = {0},
    .build_dir = {0},
    .initialized = false
};

/**
 * Clean up a single process (SIGTERM then SIGKILL if needed)
 */
static void cleanupProcess(pid_t pid, Log *log)
{
    // Check if process exists
    if (kill(pid, 0) != 0) {
        // Already dead - reap zombie if any
        waitpid(pid, NULL, WNOHANG);
        return;
    }

    // Kill process directly (SIGTERM first)
    kill(pid, SIGTERM);

    // Also try the process group in case it spawned children
    kill(-pid, SIGTERM);

    // Poll up to 2 seconds for graceful shutdown - never block
    for (int i = 0; i < 20; i++) {
        usleep(100000);  // 100ms
        if (waitpid(pid, NULL, WNOHANG) > 0) {
            // Process reaped
            return;
        }
        if (kill(pid, 0) != 0) {
            // Process exited, reap any zombie
            waitpid(pid, NULL, WNOHANG);
            return;
        }
    }

    // Force kill - never block on waitpid after this
    if (log) {
        logWarning(log, NULL, "force killing process {i}",
                  (FormatArg[]){{.i = (int)pid}});
    }
    kill(pid, SIGKILL);
    kill(-pid, SIGKILL);
    // Non-blocking reap - don't wait if it's not dead yet
    waitpid(pid, NULL, WNOHANG);
}

bool asyncTrackerInit(const char *buildDir, Log *log)
{
    if (g_async_tracker.initialized) {
        logWarning(log, NULL, "async tracker already initialized", NULL);
        return true;
    }

    if (!buildDir || buildDir[0] == '\0') {
        logError(log, NULL, "build directory not specified", NULL);
        return false;
    }

    // Create build directory if it doesn't exist
    if (!makeDirectory(buildDir, true)) {
        logError(log, NULL, "failed to create build directory: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    g_async_tracker.cxy_pid = getpid();
    strncpy(g_async_tracker.build_dir, buildDir, sizeof(g_async_tracker.build_dir) - 1);
    g_async_tracker.build_dir[sizeof(g_async_tracker.build_dir) - 1] = '\0';

    snprintf(g_async_tracker.tracker_path, sizeof(g_async_tracker.tracker_path),
             "%s/.async-cmds.%d", buildDir, g_async_tracker.cxy_pid);

    // Create tracker file with magic header
    FILE *fp = fopen(g_async_tracker.tracker_path, "w");
    if (!fp) {
        logError(log, NULL, "failed to create tracker file: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    fprintf(fp, "%s\n", ASYNC_TRACKER_MAGIC);
    fclose(fp);

    // Set permissions to user-only
    chmod(g_async_tracker.tracker_path, 0600);

    g_async_tracker.initialized = true;

    // Clean up stale tracker files from previous crashed sessions
    asyncTrackerCleanupStale(buildDir, log);

    return true;
}

bool asyncTrackerAdd(pid_t pid, bool captureOutput, Log *log)
{
    if (!g_async_tracker.initialized) {
        logError(log, NULL, "async tracker not initialized", NULL);
        return false;
    }

    // Check if we've hit the limit
    u32 count = asyncTrackerGetCount();
    if (count >= MAX_ASYNC_PROCESSES) {
        logError(log, NULL, "maximum async processes limit reached ({i})",
                (FormatArg[]){{.i = MAX_ASYNC_PROCESSES}});
        return false;
    }

    // Open file for appending with exclusive lock
    int fd = open(g_async_tracker.tracker_path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        logError(log, NULL, "failed to open tracker file: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    // Acquire exclusive lock
    if (flock(fd, LOCK_EX) != 0) {
        logError(log, NULL, "failed to lock tracker file: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        close(fd);
        return false;
    }

    // Write PID and capture flag
    char line[64];
    snprintf(line, sizeof(line), "%d %d\n", pid, captureOutput ? 1 : 0);
    ssize_t written = write(fd, line, strlen(line));

    // Release lock
    flock(fd, LOCK_UN);
    close(fd);

    if (written < 0) {
        logError(log, NULL, "failed to write to tracker file: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    return true;
}

bool asyncTrackerRemove(pid_t pid, Log *log)
{
    if (!g_async_tracker.initialized) {
        logError(log, NULL, "async tracker not initialized", NULL);
        return false;
    }

    // Read all processes
    AsyncProcessInfo processes[MAX_ASYNC_PROCESSES];
    u32 count = 0;

    if (!asyncTrackerReadAll(processes, &count, MAX_ASYNC_PROCESSES, log)) {
        return false;
    }

    // Rewrite file without the specified PID
    FILE *fp = fopen(g_async_tracker.tracker_path, "w");
    if (!fp) {
        logError(log, NULL, "failed to open tracker file for writing: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    fprintf(fp, "%s\n", ASYNC_TRACKER_MAGIC);

    bool found = false;
    for (u32 i = 0; i < count; i++) {
        if (processes[i].pid != pid) {
            fprintf(fp, "%d %d\n", processes[i].pid, processes[i].captureOutput ? 1 : 0);
        } else {
            found = true;
        }
    }

    fclose(fp);

    if (!found) {
        logWarning(log, NULL, "PID {i} not found in tracker",
                  (FormatArg[]){{.i = (int)pid}});
    }

    return true;
}

void asyncTrackerCleanup(Log *log)
{
    if (!g_async_tracker.initialized) {
        return;
    }

    AsyncProcessInfo processes[MAX_ASYNC_PROCESSES];
    u32 count = 0;

    if (!asyncTrackerReadAll(processes, &count, MAX_ASYNC_PROCESSES, log)) {
        // If we can't read the file, still try to delete it
        unlink(g_async_tracker.tracker_path);
        g_async_tracker.initialized = false;
        return;
    }

    // Kill all processes
    for (u32 i = 0; i < count; i++) {
        cleanupProcess(processes[i].pid, log);

        // Remove log file if output was captured
        if (processes[i].captureOutput) {
            char logPath[1024];
            snprintf(logPath, sizeof(logPath),
                     "%s/.async-cmd-%d.log",
                     g_async_tracker.build_dir,
                     processes[i].pid);
            unlink(logPath);  // Ignore errors
        }
    }

    // Remove tracker file
    unlink(g_async_tracker.tracker_path);
    g_async_tracker.initialized = false;
}

bool asyncTrackerIsActive(void)
{
    return g_async_tracker.initialized;
}

const char* asyncTrackerGetPath(void)
{
    if (!g_async_tracker.initialized) {
        return NULL;
    }
    return g_async_tracker.tracker_path;
}

u32 asyncTrackerGetCount(void)
{
    if (!g_async_tracker.initialized) {
        return 0;
    }

    FILE *fp = fopen(g_async_tracker.tracker_path, "r");
    if (!fp) {
        return 0;
    }

    u32 count = 0;
    char line[256];

    // Skip magic header
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }

    // Count lines
    while (fgets(line, sizeof(line), fp)) {
        pid_t pid = 0;
        if (sscanf(line, "%d", &pid) == 1 && pid > 0) {
            count++;
        }
    }

    fclose(fp);
    return count;
}

bool asyncTrackerReadAll(AsyncProcessInfo *processes,
                         u32 *count,
                         u32 maxCount,
                         Log *log)
{
    if (!g_async_tracker.initialized) {
        if (log) {
            logError(log, NULL, "async tracker not initialized", NULL);
        }
        return false;
    }

    FILE *fp = fopen(g_async_tracker.tracker_path, "r");
    if (!fp) {
        if (log) {
            logError(log, NULL, "failed to open tracker file: {s}",
                    (FormatArg[]){{.s = strerror(errno)}});
        }
        return false;
    }

    char line[256];
    *count = 0;

    // Read and validate magic header
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        if (log) {
            logError(log, NULL, "tracker file is empty", NULL);
        }
        return false;
    }

    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    if (strcmp(line, ASYNC_TRACKER_MAGIC) != 0) {
        fclose(fp);
        if (log) {
            logError(log, NULL, "invalid tracker file format", NULL);
        }
        return false;
    }

    // Read process entries
    while (fgets(line, sizeof(line), fp) && *count < maxCount) {
        pid_t pid = 0;
        int capture = 0;

        if (sscanf(line, "%d %d", &pid, &capture) == 2 && pid > 0) {
            processes[*count].pid = pid;
            processes[*count].captureOutput = (capture != 0);
            (*count)++;
        }
    }

    fclose(fp);
    return true;
}

void asyncTrackerCleanupStale(const char *buildDir, Log *log)
{
    DIR *dir = opendir(buildDir);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Look for .async-cmds.<pid> files
        if (strncmp(entry->d_name, ".async-cmds.", 12) != 0) {
            continue;
        }

        // Extract PID from filename
        pid_t parent_pid = atoi(entry->d_name + 12);
        if (parent_pid <= 0) {
            continue;
        }

        // Check if parent cxy process still exists
        if (kill(parent_pid, 0) != 0 && errno == ESRCH) {
            // Process doesn't exist - stale file
            char tracker_path[1024];
            snprintf(tracker_path, sizeof(tracker_path), "%s/%s", buildDir, entry->d_name);

            if (log) {
                logWarning(log, NULL, "cleaning up stale tracker file from PID {i}",
                          (FormatArg[]){{.i = (int)parent_pid}});
            }

            // Try to read and kill any processes it was tracking
            FILE *fp = fopen(tracker_path, "r");
            if (fp) {
                char line[256];

                // Skip magic header
                if (fgets(line, sizeof(line), fp) != NULL) {
                    // Read and kill tracked processes
                    while (fgets(line, sizeof(line), fp)) {
                        pid_t pid = 0;
                        int capture = 0;

                        if (sscanf(line, "%d %d", &pid, &capture) == 2 && pid > 0) {
                            // Check if process is still alive
                            if (kill(pid, 0) == 0) {
                                if (log) {
                                    logWarning(log, NULL, "killing orphaned process {i}",
                                             (FormatArg[]){{.i = (int)pid}});
                                }
                                cleanupProcess(pid, log);
                            }

                            // Clean up log file if it exists
                            if (capture) {
                                char logPath[1024];
                                snprintf(logPath, sizeof(logPath), "%s/.async-cmd-%d.log",
                                        buildDir, pid);
                                unlink(logPath);
                            }
                        }
                    }
                }

                fclose(fp);
            }

            // Remove stale tracker file
            unlink(tracker_path);
        }
    }

    closedir(dir);
}