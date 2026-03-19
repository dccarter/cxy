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
#include <time.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
 * Run command once and return its exit code.
 * Suppresses all output from the command.
 */
static int runCommandOnce(const char *cmd)
{
    char silenced[4096];
    snprintf(silenced, sizeof(silenced), "%s > /dev/null 2>&1", cmd);

    int ret = system(silenced);
    if (ret == -1) {
        return -1;
    }

    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

/**
 * Check whether a TCP port on localhost is accepting connections.
 * Returns true if a connection was established, false otherwise.
 */
static bool isPortOpen(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int result = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);

    return result == 0;
}

/* Shared spinner state */
static const char *spinnerFrames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static const int   spinnerCount    = 10;

/**
 * Shared polling loop used by wait-for and wait-for-port.
 *
 * @param label    Human-readable description of what we're waiting for
 * @param pollFn   Function called each iteration; returns true when ready
 * @param pollCtx  Opaque context passed to pollFn
 * @param timeoutMs Total timeout in milliseconds
 * @param periodMs  Poll interval in milliseconds
 */
static bool pollUntilReady(const char *label,
                           bool (*pollFn)(void *ctx),
                           void *pollCtx,
                           i64 timeoutMs,
                           i64 periodMs,
                           Log *log)
{
    if (timeoutMs <= 0) timeoutMs = 30000;
    if (periodMs  <= 0) periodMs  = 500;

    i64 startMs  = currentTimeMs();
    i64 deadline = startMs + timeoutMs;
    i64 attempt  = 0;

    while (1) {
        if (pollFn(pollCtx)) {
            printf("\r\033[K " cBGRN "✔" cDEF " Ready: %s (after %lldms)\n",
                   label, (long long)(currentTimeMs() - startMs));
            fflush(stdout);
            return true;
        }

        i64 now       = currentTimeMs();
        i64 elapsed   = now - startMs;
        i64 remaining = deadline - now;

        if (remaining <= 0) {
            printf("\r\033[K " cBRED "✘" cDEF " Timed out after %lldms: %s\n",
                   (long long)elapsed, label);
            fflush(stdout);
            logError(log, NULL, "wait-for timed out after {i}ms: {s}",
                    (FormatArg[]){{.i = (int)timeoutMs}, {.s = label}});
            return false;
        }

        printf("\r\033[K %s Waiting for %s... %lldms elapsed, %lldms remaining",
               spinnerFrames[attempt % spinnerCount],
               label,
               (long long)elapsed,
               (long long)remaining);
        fflush(stdout);

        attempt++;

        i64 sleepMs = periodMs < remaining ? periodMs : remaining;
        usleep((useconds_t)(sleepMs * 1000));
    }
}

/* --- wait-for --- */

static bool pollCommand(void *ctx)
{
    const char *cmd = (const char *)ctx;
    return runCommandOnce(cmd) == 0;
}

/**
 * Command: cxy utils wait-for "<cmd>" [--timeout <ms>] [--period <ms>]
 */
bool utilsWaitForCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *cmd = options->utils.cmd;
    if (!cmd || cmd[0] == '\0') {
        logError(log, NULL,
                "no command specified. "
                "Usage: cxy utils wait-for \"<cmd>\" [--timeout <ms>] [--period <ms>]",
                NULL);
        return false;
    }

    return pollUntilReady(cmd,
                          pollCommand,
                          (void *)cmd,
                          options->utils.waitForTimeout,
                          options->utils.waitForPeriod,
                          log);
}

/* --- wait-for-port --- */

static bool pollPort(void *ctx)
{
    int port = (int)(intptr_t)ctx;
    return isPortOpen(port);
}

/**
 * Command: cxy utils wait-for-port <port> [--timeout <ms>] [--period <ms>]
 */
bool utilsWaitForPortCommand(const Options *options, StrPool *strings, Log *log)
{
    int port = (int)options->utils.port;
    if (port <= 0 || port > 65535) {
        logError(log, NULL,
                "invalid port number {i}. "
                "Usage: cxy utils wait-for-port <port> [--timeout <ms>] [--period <ms>]",
                (FormatArg[]){{.i = port}});
        return false;
    }

    char label[64];
    snprintf(label, sizeof(label), "localhost:%d", port);

    return pollUntilReady(label,
                          pollPort,
                          (void *)(intptr_t)port,
                          options->utils.waitForTimeout,
                          options->utils.waitForPeriod,
                          log);
}

/* --- find-free-port --- */

/**
 * Command: cxy utils find-free-port [--range-start <port>] [--range-end <port>]
 *
 * Scans the given range and prints the first port that is not listening.
 */
bool utilsFindFreePortCommand(const Options *options, StrPool *strings, Log *log)
{
    int rangeStart = (int)options->utils.portRangeStart;
    int rangeEnd   = (int)options->utils.portRangeEnd;

    if (rangeStart <= 0) rangeStart = 8000;
    if (rangeEnd   <= 0) rangeEnd   = 9000;

    if (rangeStart > rangeEnd) {
        logError(log, NULL,
                "range-start ({i}) must be <= range-end ({i})",
                (FormatArg[]){{.i = rangeStart}, {.i = rangeEnd}});
        return false;
    }

    for (int port = rangeStart; port <= rangeEnd; port++) {
        // Try binding to the port - if we can bind it's free
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            continue;
        }

        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons((uint16_t)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        int bound = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
        close(fd);

        if (bound == 0) {
            // Successfully bound - port is free
            printf("%d\n", port);
            fflush(stdout);
            return true;
        }
    }

    logError(log, NULL,
            "no free port found in range {i}-{i}",
            (FormatArg[]){{.i = rangeStart}, {.i = rangeEnd}});
    return false;
}

/* --- env-check --- */

/**
 * Command: cxy utils env-check VAR1 VAR2 ...
 *
 * Checks that all listed environment variables are set and non-empty.
 * Prints a clear error message for each missing variable.
 */
bool utilsEnvCheckCommand(const Options *options, StrPool *strings, Log *log)
{
    if (options->utils.envVars.size == 0) {
        logError(log, NULL,
                "no variables specified. "
                "Usage: cxy utils env-check VAR1 VAR2 ...", NULL);
        return false;
    }

    bool allSet = true;

    for (u32 i = 0; i < options->utils.envVars.size; i++) {
        const char *name = ((cstring *)options->utils.envVars.elems)[i];
        const char *val  = getenv(name);

        if (!val || val[0] == '\0') {
            logError(log, NULL,
                    "required environment variable '{s}' is not set",
                    (FormatArg[]){{.s = name}});
            allSet = false;
        } else {
            printf(" " cBGRN "✔" cDEF " %s=%s\n", name, val);
            fflush(stdout);
        }
    }

    return allSet;
}