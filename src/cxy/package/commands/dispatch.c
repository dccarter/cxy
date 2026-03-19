/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/commands/commands.h"
#include "core/log.h"

bool dispatchPackageCommand(const Options *options, struct StrPool *strings, Log *log)
{
    switch (options->package.subcmd) {
        case pkgSubCreate:
            return packageCreateCommand(options, strings, log);
        
        case pkgSubAdd:
            return packageAddCommand(options, strings, log);
        
        case pkgSubInstall:
            return packageInstallCommand(options, strings, log);
        
        case pkgSubRemove:
            return packageRemoveCommand(options, strings, log);
        
        case pkgSubUpdate:
            return packageUpdateCommand(options, strings, log);
        
        case pkgSubTest:
            return packageTestCommand(options, strings, log);
        
        case pkgSubPublish:
            logError(log, NULL, "package publish command not yet implemented", NULL);
            return false;
        
        case pkgSubList:
            logError(log, NULL, "package list command not yet implemented", NULL);
            return false;
        
        case pkgSubInfo:
            return packageInfoCommand(options, strings, log);
        
        case pkgSubBuild:
            return packageBuildCommand(options, strings, log);
        
        case pkgSubClean:
            return packageCleanCommand(options, strings, log);
        
        case pkgSubRun:
            return packageRunCommand(options, strings, log);
        
        case pkgSubFindSystem:
            return packageFindSystemCommand(options, strings, log);
    
        default:
            logError(log, NULL, "unknown package subcommand", NULL);
            return false;
    }
}