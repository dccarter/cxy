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
#include "package/cxyfile.h"
#include "package/types.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <string.h>
#include <unistd.h>

/**
 * Find a script by name in the scripts array
 */
static PackageScript* findScriptByName(const DynArray *scripts, const char *name)
{
    for (u32 i = 0; i < scripts->size; i++) {
        PackageScript *script = &((PackageScript *)scripts->elems)[i];
        if (strcmp(script->name, name) == 0) {
            return script;
        }
    }
    return NULL;
}

/**
 * List all available scripts
 */
static void listAvailableScripts(const PackageMetadata *meta, Log *log)
{
    if (meta->scripts.size == 0) {
        printStatusSticky(log, "No scripts defined in Cxyfile.yaml");
        return;
    }

    printStatusSticky(log, "Available scripts:");
    for (u32 i = 0; i < meta->scripts.size; i++) {
        PackageScript *script = &((PackageScript *)meta->scripts.elems)[i];
        
        printStatusSticky(log, "  - %s: %s", script->name, script->command);
        
        if (script->dependencies.size > 0) {
            printStatusSticky(log, "      depends: [", NULL);
            for (u32 j = 0; j < script->dependencies.size; j++) {
                cstring dep = ((cstring *)script->dependencies.elems)[j];
                if (j > 0) {
                    printf(", ");
                }
                printf("%s", dep);
            }
            printf("]\n");
        }
    }
}

/**
 * Execute a script command with proper shell argument passing
 */
static bool executeScript(const char *scriptName, 
                         const char *command,
                         const DynArray *restArgs,
                         StrPool *strings,
                         Log *log,
                         bool showOutput)
{
    // Build command with proper shell argument passing
    // Format: sh -c 'command' sh arg1 arg2 arg3...
    // This makes $1, $2, etc. available in the script
    FormatState cmdState = newFormatState(NULL, true);
    
    if (restArgs && restArgs->size > 0) {
        // Create a wrapper that passes arguments properly to the shell
        format(&cmdState, "sh -c '{s}' sh", (FormatArg[]){{.s = command}});
        
        // Append rest arguments as positional parameters
        for (u32 i = 0; i < restArgs->size; i++) {
            const char *arg = ((const char **)restArgs->elems)[i];
            // Quote arguments to handle spaces and special characters
            format(&cmdState, " '{s}'", (FormatArg[]){{.s = arg}});
        }
    } else {
        // No arguments, just run the command directly
        format(&cmdState, "{s}", (FormatArg[]){{.s = command}});
    }
    
    char *finalCommand = formatStateToString(&cmdState);
    freeFormatState(&cmdState);
    
    // Execute with progress indicator
    char header[256];
    snprintf(header, sizeof(header), "Running script '%s'", scriptName);
    
    bool success = runCommandWithProgressFull(header, finalCommand, log, showOutput);
    
    free(finalCommand);
    
    return success;
}

/**
 * Package run command implementation
 */
bool packageRunCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *scriptName = options->package.scriptName;
    bool listScripts = options->package.listScripts;
    const DynArray *restArgs = &options->package.rest;

    // Load Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found", NULL);
        return false;
    }

    // Handle --list flag
    if (listScripts) {
        listAvailableScripts(&meta, log);
        free(packageDir);
        freePackageMetadata(&meta);
        return true;
    }

    // Validate script name is provided
    if (!scriptName || scriptName[0] == '\0') {
        logError(log, NULL, "script name required. Use --list to see available scripts.", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Check if script exists
    PackageScript *script = findScriptByName(&meta.scripts, scriptName);
    if (!script) {
        logError(log, NULL, "script '{s}' not found. Use --list to see available scripts.",
                (FormatArg[]){{.s = scriptName}});
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    printStatusSticky(log, "Running script '%s' in package '%s'...", scriptName, meta.name);

    // Validate scripts for circular dependencies
    if (!validateScripts(&meta, log)) {
        logError(log, NULL, "script validation failed", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Resolve script dependencies to get execution order
    DynArray executionOrder = newDynArray(sizeof(cstring));
    if (!resolveScriptDependencies(&meta, scriptName, &executionOrder, log)) {
        logError(log, NULL, "failed to resolve script dependencies", NULL);
        freeDynArray(&executionOrder);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Change to package directory before executing scripts
    char originalDir[1024];
    if (!getcwd(originalDir, sizeof(originalDir))) {
        logError(log, NULL, "failed to get current directory", NULL);
        freeDynArray(&executionOrder);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (chdir(packageDir) != 0) {
        logError(log, NULL, "failed to change to package directory", NULL);
        freeDynArray(&executionOrder);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Execute scripts in order
    bool allSuccess = true;
    for (u32 i = 0; i < executionOrder.size; i++) {
        cstring currentScriptName = ((cstring *)executionOrder.elems)[i];
        
        // Find the script
        PackageScript *currentScript = findScriptByName(&meta.scripts, currentScriptName);
        if (!currentScript) {
            logError(log, NULL, "script '{s}' not found during execution",
                    (FormatArg[]){{.s = currentScriptName}});
            allSuccess = false;
            break;
        }

        // Only pass rest args to the final script in the chain
        const DynArray *argsToPass = (i == executionOrder.size - 1) ? restArgs : NULL;
        
        // Execute the script (show output if verbose is enabled)
        if (!executeScript(currentScriptName, currentScript->command, argsToPass, strings, log, options->package.verbose)) {
            logError(log, NULL, "script '{s}' failed",
                    (FormatArg[]){{.s = currentScriptName}});
            allSuccess = false;
            break;
        }
    }

    // Restore original directory
    if (chdir(originalDir) != 0) {
        logWarning(log, NULL, "failed to restore original directory", NULL);
    }

    // Cleanup
    freeDynArray(&executionOrder);
    free(packageDir);
    freePackageMetadata(&meta);

    if (allSuccess) {
        printStatusAlways(log, cBGRN "✔" cDEF " All scripts completed successfully\n");
    }

    return allSuccess;
}