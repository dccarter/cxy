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
#include "package/install_scripts.h"
#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"

#include <string.h>
#include <unistd.h>

/**
 * Phase 3 implementation of package install command
 *
 * Installs all dependencies listed in Cxyfile.yaml using the resolver
 * for version constraint matching and transitive dependency resolution.
 * Generates Cxyfile.lock with exact versions and checksums.
 */
bool packageInstallCommand(const Options *options, StrPool *strings, Log *log)
{
    bool includeDev = options->package.includeDev;
    bool clean = options->package.clean;
    const char *packagesDir = options->package.packagesDir;
    bool verify = options->package.verify;
    bool offline = options->package.offline;

    // Find and load Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found in current directory or parent directories", NULL);
        return false;
    }

    printStatusSticky(log, "Installing dependencies for package '%s'...", meta.name);

    // Determine lockfile path
    char lockfilePath[1024];
    snprintf(lockfilePath, sizeof(lockfilePath), "%s/Cxyfile.lock", packageDir);

    // Initialize resolver context
    ResolverContext resolverCtx;
    initResolverContext(&resolverCtx, strings->pool, log);
    resolverCtx.allowDevDeps = includeDev;

    bool lockLoaded = false;
    if (!clean) {
        // Try to load existing lock file first (fast-path for reproducible installs)
        if (loadLockFile(lockfilePath, &resolverCtx)) {
            lockLoaded = true;
            printStatusSticky(log, " Loaded lock file '%s' with %u dependencies", lockfilePath, resolverCtx.resolved.size);
            
            // Validate that lockfile matches Cxyfile.yaml dependencies
            bool lockfileValid = true;
            
            // Check if all dependencies from Cxyfile.yaml are in the lockfile
            for (u32 i = 0; i < meta.dependencies.size; i++) {
                PackageDependency *dep = &((PackageDependency *)meta.dependencies.elems)[i];
                bool found = false;
                
                for (u32 j = 0; j < resolverCtx.resolved.size; j++) {
                    ResolvedDependency *resolved = &((ResolvedDependency *)resolverCtx.resolved.elems)[j];
                    if (strcmp(dep->name, resolved->name) == 0) {
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    printStatusSticky(log, " Dependency '%s' in Cxyfile.yaml not found in lockfile", dep->name);
                    lockfileValid = false;
                    break;
                }
            }
            
            // Check dev dependencies if included
            if (lockfileValid && includeDev) {
                for (u32 i = 0; i < meta.devDependencies.size; i++) {
                    PackageDependency *dep = &((PackageDependency *)meta.devDependencies.elems)[i];
                    bool found = false;
                    
                    for (u32 j = 0; j < resolverCtx.resolved.size; j++) {
                        ResolvedDependency *resolved = &((ResolvedDependency *)resolverCtx.resolved.elems)[j];
                        if (strcmp(dep->name, resolved->name) == 0) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (!found) {
                        printStatusSticky(log, " Dev dependency '%s' in Cxyfile.yaml not found in lockfile", dep->name);
                        lockfileValid = false;
                        break;
                    }
                }
            }
            
            // If lockfile is invalid, re-resolve
            if (!lockfileValid) {
                printStatusSticky(log, " Lockfile out of sync with Cxyfile.yaml, re-resolving...");
                lockLoaded = false;
                freeResolverContext(&resolverCtx);
                initResolverContext(&resolverCtx, strings->pool, log);
                resolverCtx.allowDevDeps = includeDev;
            }
        } else {
            if (options->package.frozenLockfile) {
                logError(log, NULL, "frozen lockfile enabled but lock file not found or invalid: {s}",
                         (FormatArg[]){{.s = lockfilePath}});
                freeResolverContext(&resolverCtx);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            } else {
                printStatusSticky(log, " No valid lock file found, proceeding with fresh resolution");
            }
        }
    } else {
        printStatusSticky(log, " Clean install requested - ignoring lock file");
    }

    // If we didn't load a lock file, perform fresh dependency resolution
    if (!lockLoaded) {
        printStatusSticky(log, " Resolving dependency tree...");
        if (!resolveDependencies(&resolverCtx, &meta)) {
            logError(log, NULL, "dependency resolution failed", NULL);
            freeResolverContext(&resolverCtx);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    }

    if (resolverCtx.resolved.size == 0) {
        printStatusSticky(log, "No dependencies to install");
        // Don't return early - still need to run install scripts
    }

    // Track installation results
    u32 successCount = 0;
    u32 failCount = 0;
    u32 skippedCount = 0;

    // Determine build directory for .install.yaml (used for both deps and root package)
    char buildDir[1024];
    if (options->package.buildDir && options->package.buildDir[0] != '\0') {
        strncpy(buildDir, options->package.buildDir, sizeof(buildDir) - 1);
        buildDir[sizeof(buildDir) - 1] = '\0';
    } else {
        snprintf(buildDir, sizeof(buildDir), "%s/.cxy/build", packageDir);
    }

    // Determine packages directory (used for both deps and root package install scripts)
    char targetPackagesDir[1024];
    if (packagesDir && packagesDir[0] != '\0') {
        strncpy(targetPackagesDir, packagesDir, sizeof(targetPackagesDir) - 1);
        targetPackagesDir[sizeof(targetPackagesDir) - 1] = '\0';
    } else {
        snprintf(targetPackagesDir, sizeof(targetPackagesDir), "%s/.cxy/packages", packageDir);
    }

    // Only install dependencies if there are any
    if (resolverCtx.resolved.size > 0) {

        if (offline) {
            printStatusSticky(log, " Running in offline mode - using only cached packages");
        }

        if (clean) {
            printStatusSticky(log, " Clean install - ignoring lock file");
        }

        // Install resolved dependencies
        u32 totalDeps = resolverCtx.resolved.size;
        printStatusSticky(log, " Installing %u resolved %s...",
                   totalDeps,
                   totalDeps == 1 ? "dependency" : "dependencies");

        for (u32 i = 0; i < resolverCtx.resolved.size; i++) {
        ResolvedDependency *resolved = &((ResolvedDependency *)resolverCtx.resolved.elems)[i];

        if (offline && resolved->repository != NULL) {
            printStatusSticky(log, " Skipping '%s' (offline mode, remote dependency)", resolved->name);
            skippedCount++;
            continue;
        }

        // Create a PackageDependency from ResolvedDependency for installation
        PackageDependency dep;
        dep.name = resolved->name;
        dep.repository = resolved->repository;
        dep.version = NULL; // Use resolved tag instead
        dep.tag = resolved->tag;
        dep.branch = NULL;
        dep.path = NULL;
        dep.isDev = resolved->isDev;

        printStatusSticky(log, " Installing %s%s...",
                   resolved->name,
                   resolved->isDev ? " (dev)" : "");

        if (installDependency(&dep, targetPackagesDir, strings->pool, log, false, options->package.verbose)) {
            successCount++;

            // Get commit hash and checksum after installation
            char installedPath[1024];
            snprintf(installedPath, sizeof(installedPath), "%s/%s", targetPackagesDir, resolved->name);

            // Get current commit if not already set
            if (!resolved->commit && resolved->repository) {
                cstring commit = NULL;
                if (gitGetCurrentCommit(installedPath, &commit, strings->pool, log)) {
                    resolved->commit = commit;
                }
            }

            // Calculate checksum
            cstring checksum = NULL;
            if (gitCalculateChecksum(installedPath, &checksum, strings->pool, log)) {
                resolved->checksum = checksum;
            }

            // Run dependency's own install scripts if it has any
            PackageMetadata depMeta;
            char *depDir = NULL;
            initPackageMetadata(&depMeta, strings);
            if (findAndLoadCxyfile(installedPath, &depMeta, strings, log, &depDir)) {
                bool depHasScripts = depMeta.install.size > 0 ||
                                     (includeDev && depMeta.installDev.size > 0);
                if (depHasScripts) {
                    printStatusSticky(log, "Running install scripts for '%s'...", resolved->name);
                    if (!executeInstallScripts(&depMeta, depDir, targetPackagesDir, buildDir, includeDev,
                                               strings, log, options->package.verbose)) {
                        logWarning(log, NULL, "install scripts for '{s}' failed",
                                  (FormatArg[]){{.s = resolved->name}});
                    }
                }
                free(depDir);
                freePackageMetadata(&depMeta);
            } else {
                freePackageMetadata(&depMeta);
            }
        } else {
            logError(log, NULL, "failed to install dependency '{s}'",
                    (FormatArg[]){{.s = resolved->name}});
            failCount++;
            }
        }

        // Print summary
        printStatusSticky(log, "");
        if (failCount == 0) {
            printStatusSticky(log, cBGRN "✔" cDEF " Successfully installed %u %s",
                             successCount,
                             successCount == 1 ? "package" : "packages");
        } else {
            printStatusSticky(log, cBRED "✘" cDEF " %u %s, %u failed",
                             successCount,
                             successCount == 1 ? "succeeded" : "succeeded",
                             failCount);
        }

        if (skippedCount > 0) {
            printStatusSticky(log, " (%u skipped)", skippedCount);
        }
        printStatusSticky(log, "");

        // Generate Cxyfile.lock with resolved versions and checksums
        // Only generate if we performed fresh resolution (not loaded from existing lock)
        if (failCount == 0 && !lockLoaded) {
        char lockfilePath[1024];
        snprintf(lockfilePath, sizeof(lockfilePath), "%s/Cxyfile.lock", packageDir);

        if (!generateLockFile(&resolverCtx, lockfilePath)) {
            logWarning(log, NULL, "failed to generate lock file", NULL);
        } else {
            printStatusSticky(log, " Generated Cxyfile.lock");
        }

        /* If --verify was requested, verify installed packages against the lockfile.
         * If verification fails, treat it as an installation failure (useful for CI).
         */
        if (options->package.verify) {
            printStatusSticky(log, "Verifying installed packages against lock file...");
            bool ok = verifyLockFile(lockfilePath, targetPackagesDir, strings->pool, log);
            if (!ok) {
                logError(log, NULL, "lockfile verification failed", NULL);
                freeResolverContext(&resolverCtx);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }
        }
        }
    }

    // Execute root package's own install scripts if any are defined
    if (failCount == 0 && (meta.install.size > 0 || (includeDev && meta.installDev.size > 0))) {
        printStatusSticky(log, "");
        printStatusSticky(log, "Running install scripts...");

        if (!executeInstallScripts(&meta, packageDir, targetPackagesDir, buildDir, includeDev, strings, log, options->package.verbose)) {
            logError(log, NULL, "install scripts failed", NULL);
            freeResolverContext(&resolverCtx);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    }

    freeResolverContext(&resolverCtx);

    free(packageDir);
    freePackageMetadata(&meta);

    return failCount == 0;
}
