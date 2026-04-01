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
#include "package/env.h"
#include "package/cache.h"
#include "utils/async_tracker.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Global log pointer for signal handler
 */
static Log *g_signal_log = NULL;

/**
 * Signal handler for cleanup on interruption
 */
static void signalHandler(int sig)
{
    // Call async tracker cleanup
    asyncTrackerCleanup(g_signal_log);
    
    // Re-raise signal for default behavior
    signal(sig, SIG_DFL);
    raise(sig);
}

/**
 * Cleanup function registered with atexit
 */
static void cleanupOnExit(void)
{
    asyncTrackerCleanup(g_signal_log);
}

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
static void listAvailableScripts(const PackageMetadata *meta, Log *log, bool verbose)
{
    if (meta->scripts.size == 0) {
        printStatusSticky(log, "No scripts defined in Cxyfile.yaml");
        return;
    }

    printStatusSticky(log, "Available scripts:");
    for (u32 i = 0; i < meta->scripts.size; i++) {
        PackageScript *script = &((PackageScript *)meta->scripts.elems)[i];

        if (verbose) {
            // Verbose mode: show command (truncate if multi-line)
            const char *cmd = script->command;
            const char *newline = strchr(cmd, '\n');
            bool hasMultipleLines = (newline != NULL);
            
            // Check if we need to show "command:" label
            bool showLabel = (script->dependencies.size > 0 || 
                            script->inputs.size > 0 || 
                            script->outputs.size > 0);
            
            if (showLabel) {
                printf("  - %s\n", script->name);
                printf("      command: " cIWHT);
            } else {
                printf("  - %s: " cIWHT, script->name);
            }
            
            if (hasMultipleLines) {
                // Print first line only with ellipsis
                size_t firstLineLen = newline - cmd;
                printf("%.*s..." cDEF "\n", (int)firstLineLen, cmd);
            } else {
                printf("%s" cDEF "\n", cmd);
            }

            if (script->dependencies.size > 0) {
                printf("      depends: " cICYN "[");
                for (u32 j = 0; j < script->dependencies.size; j++) {
                    cstring dep = ((cstring *)script->dependencies.elems)[j];
                    if (j > 0) {
                        printf(", ");
                    }
                    printf("%s", dep);
                }
                printf("]" cDEF "\n");
            }

            if (script->inputs.size > 0) {
                printf("      inputs: " cIMGN "[");
                for (u32 j = 0; j < script->inputs.size; j++) {
                    cstring input = ((cstring *)script->inputs.elems)[j];
                    if (j > 0) {
                        printf(", ");
                    }
                    printf("%s", input);
                }
                printf("]" cDEF "\n");
            }

            if (script->outputs.size > 0) {
                printf("      outputs: " cIGRN "[");
                for (u32 j = 0; j < script->outputs.size; j++) {
                    cstring output = ((cstring *)script->outputs.elems)[j];
                    if (j > 0) {
                        printf(", ");
                    }
                    printf("%s", output);
                }
                printf("]" cDEF "\n");
            }
        } else {
            // Non-verbose mode: just show script name
            printf("  - %s\n", script->name);
        }
    }
}

/**
 * Execute a script command with proper shell argument passing and environment
 */
static bool executeScript(const char *scriptName,
                         const char *command,
                         const DynArray *restArgs,
                         const DynArray *envVars,
                         const DynArray *builtins,
                         StrPool *strings,
                         Log *log,
                         bool showOutput)
{
    // Substitute {{VAR}} in command
    cstring substitutedCommand = substituteEnvVars(command, envVars, builtins, strings, log);
    if (!substitutedCommand) {
        logError(log, NULL, "failed to substitute environment variables in script '{s}'",
                (FormatArg[]){{.s = scriptName}});
        return false;
    }

    // Build command with proper shell argument passing
    // Format: sh -c 'command' sh arg1 arg2 arg3...
    // This makes $1, $2, etc. available in the script
    FormatState cmdState = newFormatState(NULL, true);

    if (restArgs && restArgs->size > 0) {
        // Create a wrapper that passes arguments properly to the shell
        format(&cmdState, "sh -c '{s}' sh", (FormatArg[]){{.s = substitutedCommand}});

        // Append rest arguments as positional parameters
        for (u32 i = 0; i < restArgs->size; i++) {
            const char *arg = ((const char **)restArgs->elems)[i];
            // Quote arguments to handle spaces and special characters
            format(&cmdState, " '{s}'", (FormatArg[]){{.s = arg}});
        }
    } else {
        // No arguments, just run the command directly
        format(&cmdState, "{s}", (FormatArg[]){{.s = substitutedCommand}});
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

    // Determine build directory for async tracker
    const char *buildDir = options->package.buildDir;
    if (!buildDir || buildDir[0] == '\0') {
        buildDir = ".cxy/build";
    }

    // Build full build path if relative
    char fullBuildDir[2048];
    if (buildDir[0] == '/') {
        // Absolute path, use as-is
        strncpy(fullBuildDir, buildDir, sizeof(fullBuildDir) - 1);
        fullBuildDir[sizeof(fullBuildDir) - 1] = '\0';
    } else {
        // Relative path, make relative to packageDir
        snprintf(fullBuildDir, sizeof(fullBuildDir), "%s/%s", packageDir, buildDir);
    }

    // Handle --list flag
    if (listScripts) {
        listAvailableScripts(&meta, log, options->package.verbose);
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

    // Initialize async tracker for background process management
    g_signal_log = log;
    if (!asyncTrackerInit(fullBuildDir, log)) {
        logWarning(log, NULL, "failed to initialize async tracker, background process management disabled", NULL);
    } else {
        // Register cleanup handlers
        atexit(cleanupOnExit);
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        // Export tracker path so child cxy processes (async-cmd-start)
        // can append to this tracker instead of creating their own.
        setenv("CXY_ASYNC_TRACKER", asyncTrackerGetPath(), 1);
    }

    printStatusSticky(log, "Running script '%s' in package '%s'...", scriptName, meta.name);

    // Determine packages directory
    const char *packagesDir = options->package.packagesDir;
    if (!packagesDir || packagesDir[0] == '\0') {
        packagesDir = ".cxy/packages";
    }

    // Build full packages path if relative
    char fullPackagesDir[2048];
    if (packagesDir[0] == '/') {
        // Absolute path, use as-is
        strncpy(fullPackagesDir, packagesDir, sizeof(fullPackagesDir) - 1);
        fullPackagesDir[sizeof(fullPackagesDir) - 1] = '\0';
    } else {
        // Relative path, make relative to packageDir
        snprintf(fullPackagesDir, sizeof(fullPackagesDir), "%s/%s", packageDir, packagesDir);
    }

    // Build built-in environment variables
    DynArray builtins = buildBuiltinEnvVars(&meta, packageDir, fullPackagesDir, fullBuildDir, strings);

    // Resolve environment variables (substitute {{VAR}} references between env vars)
    if (!resolveEnvVars(&meta.scriptEnv, &builtins, strings, log)) {
        logError(log, NULL, "failed to resolve environment variables", NULL);
        freeDynArray(&builtins);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Set environment variables for script execution
    if (!setScriptEnvironment(&meta.scriptEnv, &builtins, log)) {
        logError(log, NULL, "failed to set script environment", NULL);
        freeDynArray(&builtins);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Validate scripts for circular dependencies
    if (!validateScripts(&meta, log)) {
        logError(log, NULL, "script validation failed", NULL);
        clearScriptEnvironment(&meta.scriptEnv, &builtins);
        freeDynArray(&builtins);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Resolve script dependencies to get execution order
    DynArray executionOrder = newDynArray(sizeof(cstring));
    if (!resolveScriptDependencies(&meta, scriptName, &executionOrder, log)) {
        logError(log, NULL, "failed to resolve script dependencies", NULL);
        freeDynArray(&executionOrder);
        clearScriptEnvironment(&meta.scriptEnv, &builtins);
        freeDynArray(&builtins);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Change to package directory before executing scripts
    char originalDir[1024];
    if (!getcwd(originalDir, sizeof(originalDir))) {
        logError(log, NULL, "failed to get current directory", NULL);
        freeDynArray(&executionOrder);
        clearScriptEnvironment(&meta.scriptEnv, &builtins);
        freeDynArray(&builtins);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (chdir(packageDir) != 0) {
        logError(log, NULL, "failed to change to package directory", NULL);
        freeDynArray(&executionOrder);
        clearScriptEnvironment(&meta.scriptEnv, &builtins);
        freeDynArray(&builtins);
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

        // Check if script is cached (unless --no-cache is specified)
        bool isCached = false;
        if (!options->package.noCache) {
            if (!checkScriptCacheWithEnv(currentScript, packageDir, &meta.scriptEnv, &builtins, strings, log, &isCached)) {
                logError(log, NULL, "failed to check cache for script '{s}'",
                        (FormatArg[]){{.s = currentScriptName}});
                allSuccess = false;
                break;
            }

            if (isCached) {
                printStatusSticky(log, cYLW "⊙" cDEF " Skipping script '%s' (cached)", currentScriptName);
                continue;
            }
        }

        // Only pass rest args to the final script in the chain
        const DynArray *argsToPass = (i == executionOrder.size - 1) ? restArgs : NULL;

        // Build SCRIPT_INPUTS: expand globs and flatten to space-separated string
        cstring scriptInputsValue = NULL;
        if (currentScript->inputs.size > 0) {
            DynArray substitutedInputs = newDynArray(sizeof(cstring));
            for (u32 j = 0; j < currentScript->inputs.size; j++) {
                cstring pattern = ((cstring *)currentScript->inputs.elems)[j];
                cstring substituted = substituteEnvVars(pattern, &meta.scriptEnv, &builtins, strings, log);
                if (substituted)
                    pushOnDynArray(&substitutedInputs, &substituted);
            }
            DynArray expandedInputs = newDynArray(sizeof(cstring));
            expandInputGlobs(&substitutedInputs, packageDir, &expandedInputs, strings, log);
            freeDynArray(&substitutedInputs);

            FormatState fs = newFormatState(NULL, true);
            for (u32 j = 0; j < expandedInputs.size; j++) {
                if (j > 0) append(&fs, " ", 1);
                appendString(&fs, ((cstring *)expandedInputs.elems)[j]);
            }
            char *flat = formatStateToString(&fs);
            freeFormatState(&fs);
            scriptInputsValue = makeString(strings, flat);
            free(flat);
            freeDynArray(&expandedInputs);
        }

        // Build SCRIPT_OUTPUTS: flatten patterns to space-separated string (no glob expansion)
        cstring scriptOutputsValue = NULL;
        if (currentScript->outputs.size > 0) {
            FormatState fs = newFormatState(NULL, true);
            for (u32 j = 0; j < currentScript->outputs.size; j++) {
                cstring pattern = ((cstring *)currentScript->outputs.elems)[j];
                cstring substituted = substituteEnvVars(pattern, &meta.scriptEnv, &builtins, strings, log);
                if (j > 0) append(&fs, " ", 1);
                appendString(&fs, substituted ? substituted : pattern);
            }
            char *flat = formatStateToString(&fs);
            freeFormatState(&fs);
            scriptOutputsValue = makeString(strings, flat);
            free(flat);
        }

        // Push into builtins so {{SCRIPT_INPUTS}} / {{SCRIPT_OUTPUTS}} substitution works,
        // and also setenv so $SCRIPT_INPUTS / $SCRIPT_OUTPUTS work in shell
        u32 builtinsCountBefore = builtins.size;
        if (scriptInputsValue) {
            EnvVar inputsVar = { .name = "SCRIPT_INPUTS", .value = scriptInputsValue };
            pushOnDynArray(&builtins, &inputsVar);
            setenv("SCRIPT_INPUTS", scriptInputsValue, 1);
        } else {
            unsetenv("SCRIPT_INPUTS");
        }
        if (scriptOutputsValue) {
            EnvVar outputsVar = { .name = "SCRIPT_OUTPUTS", .value = scriptOutputsValue };
            pushOnDynArray(&builtins, &outputsVar);
            setenv("SCRIPT_OUTPUTS", scriptOutputsValue, 1);
        } else {
            unsetenv("SCRIPT_OUTPUTS");
        }

        // Execute the script with environment variables (show output if verbose is enabled)
        if (!executeScript(currentScriptName, currentScript->command, argsToPass,
                          &meta.scriptEnv, &builtins, strings, log, options->package.verbose)) {
            logError(log, NULL, "script '{s}' failed",
                    (FormatArg[]){{.s = currentScriptName}});
            allSuccess = false;
            break;
        }

        // Pop script-specific vars back off builtins
        builtins.size = builtinsCountBefore;
        unsetenv("SCRIPT_INPUTS");
        unsetenv("SCRIPT_OUTPUTS");
    }

    // Clear script-specific environment variables
    unsetenv("SCRIPT_INPUTS");
    unsetenv("SCRIPT_OUTPUTS");

    // Restore original directory
    if (chdir(originalDir) != 0) {
        logWarning(log, NULL, "failed to restore original directory", NULL);
    }

    // Cleanup async processes (if any were started)
    asyncTrackerCleanup(log);

    // Clear environment variables
    clearScriptEnvironment(&meta.scriptEnv, &builtins);

    // Cleanup
    freeDynArray(&builtins);
    freeDynArray(&executionOrder);
    free(packageDir);
    freePackageMetadata(&meta);

    if (allSuccess) {
        printStatusSticky(log, cBGRN "✔" cDEF " All scripts completed successfully\n");
    }

    return allSuccess;
}
