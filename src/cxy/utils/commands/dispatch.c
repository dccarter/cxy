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

bool dispatchUtilsCommand(const Options *options, struct StrPool *strings, Log *log)
{
    switch (options->utils.subcmd) {
        case utlSubAsyncCmdStart:
            return utilsAsyncCmdStartCommand(options, strings, log);

        case utlSubAsyncCmdStop:
            return utilsAsyncCmdStopCommand(options, strings, log);

        case utlSubAsyncCmdLogs:
            return utilsAsyncCmdLogsCommand(options, strings, log);

        case utlSubAsyncCmdStatus:
            return utilsAsyncCmdStatusCommand(options, strings, log);

        case utlSubWaitFor:
            return utilsWaitForCommand(options, strings, log);

        case utlSubWaitForPort:
            return utilsWaitForPortCommand(options, strings, log);

        case utlSubFindFreePort:
            return utilsFindFreePortCommand(options, strings, log);

        case utlSubEnvCheck:
            return utilsEnvCheckCommand(options, strings, log);

        case utlSubLock:
            return utilsLockCommand(options, strings, log);

        default:
            logError(log, NULL, "unknown utils subcommand", NULL);
            return false;
    }
}