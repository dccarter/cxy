/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-25
 */

#include "package/commands/commands.h"
#include "core/log.h"
#include "driver/options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>

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
 * Run command once and return its exit code
 * Suppresses all output from the command.
 */
static int runCommandOnce(const char *cmd)
{
    // Redirect stdout/stderr to /dev/null so polling output is silent
    char silenced[4096];
    snprintf(silenced, sizeof(silenced), "%s > /dev/null 2>&1", cmd);

    int ret = system(silenced);
    if (ret == -1) {
        return -1;
    }

    return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

/**
 * Command: cxy package wait-for "<cmd>" [--timeout <ms>] [--period <ms>]
 *
 * Polls <cmd> every <period> ms until it exits with 0 or <timeout> ms elapses.
 * Exits 0 on success, 1 on timeout.
 */
bool packageWaitForCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *cmd = options->package.scriptName;
    if (!cmd || cmd[0] == '\0') {
        logError(log, NULL,
                "no command specified. "
                "Usage: cxy package wait-for \"<cmd>\" [--timeout <ms>] [--period <ms>]",
                NULL);
        return false;
    }

    i64 timeoutMs = options->package.waitForTimeout;
    i64 periodMs  = options->package.waitForPeriod;

    // Apply sensible minimums
    if (timeoutMs <= 0) timeoutMs = 30000;  // 30 seconds
    if (periodMs  <= 0) periodMs  = 500;    // 500ms

    i64 startMs  = currentTimeMs();
    i64 deadline = startMs + timeoutMs;
    i64 attempt  = 0;

    // Spinner frames for visual feedback
    static const char *spinnerFrames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    static const int spinnerCount = 10;

    while (1) {
        int exitCode = runCommandOnce(cmd);

        if (exitCode == 0) {
            // Clear spinner line and print success
            printf("\r\033[K " cBGRN "✔" cDEF " Ready (after %lldms)\n",
                   (long long)(currentTimeMs() - startMs));
            fflush(stdout);
            return true;
        }

        i64 now = currentTimeMs();
        i64 elapsed = now - startMs;
        i64 remaining = deadline - now;

        if (remaining <= 0) {
            printf("\r\033[K " cBRED "✘" cDEF " Timed out after %lldms waiting for: %s\n",
                   (long long)elapsed, cmd);
            fflush(stdout);
            logError(log, NULL, "wait-for timed out after {i}ms: {s}",
                    (FormatArg[]){{.i = (int)timeoutMs}, {.s = cmd}});
            return false;
        }

        // Print spinner with elapsed/remaining
        printf("\r\033[K %s Waiting... %lldms elapsed, %lldms remaining",
               spinnerFrames[attempt % spinnerCount],
               (long long)elapsed,
               (long long)remaining);
        fflush(stdout);

        attempt++;

        // Sleep for period (or remaining time if less)
        i64 sleepMs = periodMs < remaining ? periodMs : remaining;
        usleep((useconds_t)(sleepMs * 1000));
    }
}