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

typedef struct Options Options;
typedef struct StrPool StrPool;
typedef struct Log Log;

/**
 * Command: cxy package async-cmd-start [--capture] <cmd> [args...]
 * 
 * Starts a command in the background and returns its PID.
 * The process is tracked and will be automatically killed when the parent script exits.
 * 
 * @param options Parsed command-line options
 * @param strings String pool for allocations
 * @param log Logger for output
 * @return true on success, false on error
 */
bool packageAsyncCmdStartCommand(const Options *options, StrPool *strings, Log *log);

/**
 * Command: cxy package async-cmd-stop <pid>
 * 
 * Stops a background process by PID.
 * 
 * @param options Parsed command-line options
 * @param strings String pool for allocations
 * @param log Logger for output
 * @return true on success, false on error
 */
bool packageAsyncCmdStopCommand(const Options *options, StrPool *strings, Log *log);

/**
 * Internal: Spawn a background process
 * 
 * @param command Command to execute
 * @param args Argument array (including command as args[0])
 * @param argCount Number of arguments
 * @param captureOutput If true, redirect stdout/stderr to log file
 * @param buildDir Build directory for log files
 * @param log Logger for errors
 * @return PID on success, -1 on error
 */
pid_t spawnAsyncProcess(const char *command,
                        const char **args,
                        u32 argCount,
                        bool captureOutput,
                        const char *buildDir,
                        Log *log);

/**
 * Internal: Stop a specific process
 * 
 * @param pid Process ID to stop
 * @param log Logger for warnings/errors
 * @return true on success, false on error
 */
bool stopAsyncProcess(pid_t pid, Log *log);

#ifdef __cplusplus
}
#endif