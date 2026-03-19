/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-25
 */

#pragma once

#include "core/utils.h"
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;

/**
 * Maximum number of async processes that can be tracked per cxy session
 */
#define MAX_ASYNC_PROCESSES 64

/**
 * Magic header for tracker file format validation
 */
#define ASYNC_TRACKER_MAGIC "CXYASYNC1"

/**
 * Information about a tracked async process
 */
typedef struct {
    pid_t pid;              // Process ID
    bool captureOutput;     // Whether output is captured to log file
} AsyncProcessInfo;

/**
 * Global async tracker state
 */
typedef struct {
    pid_t cxy_pid;           // PID of cxy process
    char tracker_path[1024]; // Path to .async-cmds.<pid>
    char build_dir[1024];    // Build directory
    bool initialized;        // Whether tracker is active
} AsyncTracker;

/**
 * Global tracker instance (one per cxy process)
 */
extern AsyncTracker g_async_tracker;

/**
 * Initialize async tracker for current cxy session
 * Should be called at start of script execution
 * 
 * @param buildDir Build directory where tracker file will be created
 * @param log Logger for errors
 * @return true on success, false on error
 */
bool asyncTrackerInit(const char *buildDir, Log *log);

/**
 * Add a spawned PID to tracking file
 * 
 * @param pid Process ID to track
 * @param captureOutput Whether output is captured to log file
 * @param log Logger for errors
 * @return true on success, false on error
 */
bool asyncTrackerAdd(pid_t pid, bool captureOutput, Log *log);

/**
 * Remove a PID from tracking (when manually stopped)
 * 
 * @param pid Process ID to remove
 * @param log Logger for errors
 * @return true on success, false on error
 */
bool asyncTrackerRemove(pid_t pid, Log *log);

/**
 * Kill all tracked processes and cleanup
 * Called at script exit (success, failure, or interrupt)
 * 
 * @param log Logger for warnings/errors
 */
void asyncTrackerCleanup(Log *log);

/**
 * Check if tracker is initialized and active
 * 
 * @return true if tracker is active, false otherwise
 */
bool asyncTrackerIsActive(void);

/**
 * Get the tracker file path
 * 
 * @return Path to tracker file, or NULL if not initialized
 */
const char* asyncTrackerGetPath(void);

/**
 * Get count of currently tracked processes
 * 
 * @return Number of tracked processes
 */
u32 asyncTrackerGetCount(void);

/**
 * Read all tracked processes from tracker file
 * 
 * @param processes Output array to fill with process info
 * @param count Output parameter for number of processes read
 * @param maxCount Maximum number of processes to read (size of array)
 * @param log Logger for errors
 * @return true on success, false on error
 */
bool asyncTrackerReadAll(AsyncProcessInfo *processes, 
                         u32 *count, 
                         u32 maxCount, 
                         Log *log);

/**
 * Clean up stale tracker files from previous crashed sessions
 * Should be called at start of script execution
 * 
 * @param buildDir Build directory to scan for stale files
 * @param log Logger for info/warnings
 */
void asyncTrackerCleanupStale(const char *buildDir, Log *log);

#ifdef __cplusplus
}
#endif