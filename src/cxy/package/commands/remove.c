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
#include "package/resolver.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/**
 * Find and remove a dependency by name from a DynArray of PackageDependency
 */
static bool removeDependencyByName(DynArray *deps, const char *name, bool *found)
{
    *found = false;
    
    for (u32 i = 0; i < deps->size; i++) {
        PackageDependency *dep = &((PackageDependency *)deps->elems)[i];
        if (strcmp(dep->name, name) == 0) {
            *found = true;
            
            // Remove from array by shifting elements
            for (u32 j = i; j < deps->size - 1; j++) {
                ((PackageDependency *)deps->elems)[j] = ((PackageDependency *)deps->elems)[j + 1];
            }
            deps->size--;
            
            return true;
        }
    }
    
    return false;
}

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
 * Remove package directory from disk
 */
static bool removePackageFromDisk(const char *packageName, const char *packagesDir, Log *log)
{
    char packagePath[2048];
    snprintf(packagePath, sizeof(packagePath), "%s/%s", packagesDir, packageName);
    
    // Check if directory exists
    struct stat st;
    if (stat(packagePath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        // Directory doesn't exist, nothing to remove
        return true;
    }
    
    // Build remove command
    char rmCmd[2048];
    snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\"", packagePath);
    
    if (system(rmCmd) != 0) {
        logWarning(log, NULL, "failed to remove package directory: {s}",
                  (FormatArg[]){{.s = packagePath}});
        return false;
    }
    
    printStatusSticky(log, "  Removed package directory: %s", packagePath);
    return true;
}

/**
 * Package remove command implementation
 */
bool packageRemoveCommand(const Options *options, StrPool *strings, Log *log)
{
    const DynArray *packagesToRemove = &options->package.packages;
    
    // Validate that at least one package name was provided
    if (packagesToRemove->size == 0) {
        logError(log, NULL, "no package names specified. Usage: cxy package remove <name...>", NULL);
        return false;
    }
    
    // Load Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);
    
    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found", NULL);
        return false;
    }
    
    printStatusSticky(log, "Removing dependencies from package '%s'...", meta.name);
    
    // Track what we removed
    u32 removedCount = 0;
    DynArray removedPackages = newDynArray(sizeof(cstring));
    
    // Try to remove each package
    for (u32 i = 0; i < packagesToRemove->size; i++) {
        const char *packageName = ((const char **)packagesToRemove->elems)[i];
        bool found = false;
        
        // Try to remove from regular dependencies
        if (removeDependencyByName(&meta.dependencies, packageName, &found)) {
            printStatusSticky(log, "  Removed '%s' from dependencies", packageName);
            cstring name = makeString(strings, packageName);
            pushOnDynArray(&removedPackages, &name);
            removedCount++;
        }
        // Try to remove from dev dependencies
        else if (removeDependencyByName(&meta.devDependencies, packageName, &found)) {
            printStatusSticky(log, "  Removed '%s' from dev-dependencies", packageName);
            cstring name = makeString(strings, packageName);
            pushOnDynArray(&removedPackages, &name);
            removedCount++;
        }
        else {
            logWarning(log, NULL, "package '{s}' not found in dependencies",
                      (FormatArg[]){{.s = packageName}});
        }
    }
    
    // If nothing was removed, exit
    if (removedCount == 0) {
        logError(log, NULL, "no packages were removed", NULL);
        freeDynArray(&removedPackages);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }
    
    // Write updated Cxyfile.yaml
    char cxyfilePath[2048];
    snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packageDir);
    
    if (!writeCxyfile(cxyfilePath, &meta, log)) {
        logError(log, NULL, "failed to write updated Cxyfile.yaml", NULL);
        freeDynArray(&removedPackages);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }
    
    printStatusAlways(log, cBGRN "✔" cDEF " Updated Cxyfile.yaml");
    
    // Regenerate Cxyfile.lock with updated dependencies to preserve locked versions of remaining packages
    char lockfilePath[2048];
    snprintf(lockfilePath, sizeof(lockfilePath), "%s/Cxyfile.lock", packageDir);
    
    printStatusSticky(log, "  Regenerating Cxyfile.lock...");
    
    ResolverContext resolverCtx;
    initResolverContext(&resolverCtx, strings->pool, log);
    resolverCtx.allowDevDeps = true;
    
    if (resolveDependencies(&resolverCtx, &meta)) {
        if (generateLockFile(&resolverCtx, lockfilePath)) {
            printStatusSticky(log, "  Updated Cxyfile.lock");
        } else {
            logWarning(log, NULL, "failed to regenerate Cxyfile.lock", NULL);
        }
    } else {
        logWarning(log, NULL, "failed to resolve dependencies for lock file regeneration", NULL);
    }
    
    freeResolverContext(&resolverCtx);
    
    // Ask if user wants to remove packages from disk
    if (removedPackages.size > 0) {
        printf("\n");
        if (promptForConfirmation("Remove package directories from .cxy/packages?", log)) {
            char packagesDir[2048];
            snprintf(packagesDir, sizeof(packagesDir), "%s/.cxy/packages", packageDir);
            
            for (u32 i = 0; i < removedPackages.size; i++) {
                cstring packageName = ((cstring *)removedPackages.elems)[i];
                removePackageFromDisk(packageName, packagesDir, log);
            }
        } else {
            printStatusAlways(log, "  Package directories retained in .cxy/packages/");
        }
    }
    
    // Cleanup
    freeDynArray(&removedPackages);
    free(packageDir);
    freePackageMetadata(&meta);
    
    printStatusAlways(log, cBGRN "✔" cDEF " Removed %u package(s) successfully\n", removedCount);
    
    return true;
}