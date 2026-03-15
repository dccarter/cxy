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
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/**
 * Prompt user for confirmation
 */
static bool promptForConfirmation(const char *message, Log *log)
{
    printf("%s (y/N): ", message);
    fflush(stdout);
    
    char response[10];
    if (fgets(response, sizeof(response), stdin) == NULL) {
        return false;
    }
    
    // Check if response starts with 'y' or 'Y'
    return (response[0] == 'y' || response[0] == 'Y');
}

/**
 * Check if a directory exists
 */
static bool directoryExists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

/**
 * Remove a directory recursively
 */
static bool removeDirectory(const char *path, Log *log)
{
    if (!directoryExists(path)) {
        return true; // Already doesn't exist
    }
    
    char rmCmd[2048];
    snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\"", path);
    
    if (system(rmCmd) != 0) {
        logError(log, NULL, "failed to remove directory: {s}",
                (FormatArg[]){{.s = path}});
        return false;
    }
    
    return true;
}

/**
 * Get the size of a directory (rough estimate)
 */
static void getDirectorySize(const char *path, char *sizeStr, size_t sizeStrLen)
{
    char duCmd[2048];
    snprintf(duCmd, sizeof(duCmd), "du -sh \"%s\" 2>/dev/null | cut -f1", path);
    
    FILE *fp = popen(duCmd, "r");
    if (fp) {
        if (fgets(sizeStr, sizeStrLen, fp) != NULL) {
            // Remove trailing newline
            size_t len = strlen(sizeStr);
            if (len > 0 && sizeStr[len - 1] == '\n') {
                sizeStr[len - 1] = '\0';
            }
        } else {
            strcpy(sizeStr, "unknown");
        }
        pclose(fp);
    } else {
        strcpy(sizeStr, "unknown");
    }
}

/**
 * Package clean command implementation
 */
bool packageCleanCommand(const Options *options, StrPool *strings, Log *log)
{
    bool cleanCache = options->package.cleanCache;
    bool cleanBuild = options->package.cleanBuild;
    bool cleanAll = options->package.cleanAll;
    bool force = options->package.force;
    
    // If --all is specified, enable both cache and build
    if (cleanAll) {
        cleanCache = true;
        cleanBuild = true;
    }
    
    // Default behavior: clean build directory only
    if (!cleanCache && !cleanBuild) {
        cleanBuild = true;
    }
    
    // Load Cxyfile.yaml to get package info and build directory
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);
    
    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found", NULL);
        return false;
    }
    
    printStatusSticky(log, "Cleaning package '%s'...", meta.name);
    
    bool success = true;
    u32 cleanedCount = 0;
    
    // Clean build directory
    if (cleanBuild) {
        // Determine build directory (from Cxyfile or default)
        char buildDir[2048];
        
        if (meta.build.output && meta.build.output[0] != '\0') {
            // Extract directory from output path
            strncpy(buildDir, meta.build.output, sizeof(buildDir) - 1);
            buildDir[sizeof(buildDir) - 1] = '\0';
            
            char *lastSlash = strrchr(buildDir, '/');
            if (lastSlash) {
                *lastSlash = '\0';
            } else {
                // No slash, use default
                snprintf(buildDir, sizeof(buildDir), "%s/.cxy/build", packageDir);
            }
        } else {
            // Use default build directory
            snprintf(buildDir, sizeof(buildDir), "%s/.cxy/build", packageDir);
        }
        
        if (directoryExists(buildDir)) {
            char sizeStr[32];
            getDirectorySize(buildDir, sizeStr, sizeof(sizeStr));
            
            printStatusSticky(log, "  Build directory: %s (%s)", buildDir, sizeStr);
            
            bool proceed = force || promptForConfirmation("Remove build directory?", log);
            
            if (proceed) {
                if (removeDirectory(buildDir, log)) {
                    printStatusAlways(log, cBGRN "  ✔ " cDEF "Removed build directory");
                    cleanedCount++;
                } else {
                    success = false;
                }
            } else {
                printStatusSticky(log, "  Skipped build directory");
            }
        } else {
            printStatusSticky(log, "  Build directory does not exist: %s", buildDir);
        }
    }
    
    // Clean package cache
    if (cleanCache) {
        char packagesDir[2048];
        snprintf(packagesDir, sizeof(packagesDir), "%s/.cxy/packages", packageDir);
        
        if (directoryExists(packagesDir)) {
            char sizeStr[32];
            getDirectorySize(packagesDir, sizeStr, sizeof(sizeStr));
            
            printStatusSticky(log, "  Package cache: %s (%s)", packagesDir, sizeStr);
            
            bool proceed = force || promptForConfirmation("Remove package cache?", log);
            
            if (proceed) {
                if (removeDirectory(packagesDir, log)) {
                    printStatusAlways(log, cBGRN "  ✔ " cDEF "Removed package cache");
                    cleanedCount++;
                } else {
                    success = false;
                }
            } else {
                printStatusSticky(log, "  Skipped package cache");
            }
        } else {
            printStatusSticky(log, "  Package cache does not exist: %s", packagesDir);
        }
    }
    
    // Cleanup
    free(packageDir);
    freePackageMetadata(&meta);
    
    if (success && cleanedCount > 0) {
        printStatusAlways(log, cBGRN "✔" cDEF " Cleaned %u item(s) successfully\n", cleanedCount);
    } else if (cleanedCount == 0) {
        printStatusAlways(log, "Nothing to clean\n");
    }
    
    return success;
}