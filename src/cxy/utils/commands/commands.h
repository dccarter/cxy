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

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/options.h"
#include <stdbool.h>

typedef struct Log Log;
struct StrPool;

/**
 * Command handler signature for utils commands.
 */
typedef bool (*UtilsCommandHandler)(const Options *options, struct StrPool *strings, Log *log);

/**
 * Start a background command.
 * Usage: cxy utils async-cmd-start "<cmd>" [--capture]
 */
bool utilsAsyncCmdStartCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Stop a background command by PID.
 * Usage: cxy utils async-cmd-stop <pid>
 */
bool utilsAsyncCmdStopCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Print (or follow) the captured log output of a background command.
 * Usage: cxy utils async-cmd-logs <pid> [--follow]
 */
bool utilsAsyncCmdLogsCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Poll a command until it exits 0 or timeout is reached.
 * Usage: cxy utils wait-for "<cmd>" [--timeout <ms>] [--period <ms>]
 */
bool utilsWaitForCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Wait until a TCP port is open and accepting connections.
 * Usage: cxy utils wait-for-port <port> [--timeout <ms>] [--period <ms>]
 */
bool utilsWaitForPortCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Find and print an available TCP port in a given range.
 * Usage: cxy utils find-free-port [--range-start <port>] [--range-end <port>]
 */
bool utilsFindFreePortCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Assert that required environment variables are set.
 * Usage: cxy utils env-check VAR1 VAR2 ...
 */
bool utilsEnvCheckCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Run a command while holding a named lock file.
 * Usage: cxy utils lock <name> "<cmd>" [--timeout <ms>]
 */
bool utilsLockCommand(const Options *options, struct StrPool *strings, Log *log);

/**
 * Dispatch the parsed utils command to the corresponding handler.
 */
bool dispatchUtilsCommand(const Options *options, struct StrPool *strings, Log *log);

#ifdef __cplusplus
}
#endif