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
#include "package/gitops.h"
#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"

#include <string.h>
#include <unistd.h>

/**
 * Represents an update candidate
 */
typedef struct UpdateCandidate {
    cstring name;                   // Package name
    cstring repository;             // Git repository URL
    SemanticVersion currentVersion; // Current version from lock file
    SemanticVersion newVersion;     // New version to update to
    cstring currentTag;             // Current tag from lock file
    cstring newTag;                 // New tag to update to
    bool isDev;                     // Whether this is a dev dependency
    bool hasUpdate;                 // Whether an update is available
} UpdateCandidate;

/**
 * Find a dependency by name in the metadata
 */
static PackageDependency* findDependencyByName(const PackageMetadata *meta, cstring name, bool *isDev)
{
    // Check regular dependencies first
    for (u32 i = 0; i < meta->dependencies.size; i++) {
        PackageDependency *dep = &((PackageDependency *)meta->dependencies.elems)[i];
        if (strcmp(dep->name, name) == 0) {
            if (isDev) *isDev = false;
            return dep;
        }
    }

    // Check dev dependencies
    for (u32 i = 0; i < meta->devDependencies.size; i++) {
        PackageDependency *dep = &((PackageDependency *)meta->devDependencies.elems)[i];
        if (strcmp(dep->name, name) == 0) {
            if (isDev) *isDev = true;
            return dep;
        }
    }

    return NULL;
}

/**
 * Find a resolved dependency by name in the resolver context
 */
static ResolvedDependency* findResolvedDependencyByName(const ResolverContext *ctx, cstring name)
{
    for (u32 i = 0; i < ctx->resolved.size; i++) {
        ResolvedDependency *resolved = &((ResolvedDependency *)ctx->resolved.elems)[i];
        if (strcmp(resolved->name, name) == 0) {
            return resolved;
        }
    }
    return NULL;
}

/**
 * Check if an update is available for a package
 */
static bool checkPackageForUpdate(cstring packageName,
                                  const PackageMetadata *meta,
                                  const ResolverContext *lockCtx,
                                  bool latest,
                                  bool includeDev,
                                  UpdateCandidate *candidate,
                                  MemPool *pool,
                                  Log *log)
{
    // Find the package in Cxyfile.yaml
    bool isDev = false;
    PackageDependency *dep = findDependencyByName(meta, packageName, &isDev);
    if (!dep) {
        logError(log, NULL, "package '{s}' not found in dependencies",
                (FormatArg[]){{.s = packageName}});
        return false;
    }

    // Skip dev dependencies if not included
    if (isDev && !includeDev) {
        printStatusSticky(log, "Skipping dev dependency '%s' (use --include-dev to update)", packageName);
        return false;
    }

    // Find current version in lock file
    ResolvedDependency *locked = findResolvedDependencyByName(lockCtx, packageName);
    if (!locked) {
        logWarning(log, NULL, "package '{s}' not found in lock file - treating as new install",
                  (FormatArg[]){{.s = packageName}});

        // Initialize candidate for new install
        candidate->name = packageName;
        candidate->repository = dep->repository;
        candidate->currentVersion = (SemanticVersion){0, 0, 0, NULL, NULL};
        candidate->newVersion = (SemanticVersion){0, 0, 0, NULL, NULL}; // Will be resolved later
        candidate->currentTag = NULL;
        candidate->newTag = NULL;
        candidate->isDev = isDev;
        candidate->hasUpdate = true;
        return true;
    }

    // For local path dependencies, no updates available
    if (dep->path && dep->path[0] != '\0') {
        printStatusSticky(log, "Package '%s' is a local dependency - no updates available", packageName);
        candidate->hasUpdate = false;
        return true;
    }

    // For remote dependencies, check for newer versions
    if (!dep->repository || dep->repository[0] == '\0') {
        logError(log, NULL, "package '{s}' has no repository specified",
                (FormatArg[]){{.s = packageName}});
        return false;
    }

    printStatusSticky(log, "Checking for updates to %s (current: %u.%u.%u)...",
               packageName,
               locked->version.major,
               locked->version.minor,
               locked->version.patch);

    // Fetch available tags
    DynArray tags = newDynArray(sizeof(GitTag));
    if (!gitFetchTags(dep->repository, &tags, pool, log)) {
        logError(log, NULL, "failed to fetch tags for '{s}'",
                (FormatArg[]){{.s = packageName}});
        freeDynArray(&tags);
        return false;
    }

    if (tags.size == 0) {
        logWarning(log, NULL, "no semantic version tags found for '{s}'",
                  (FormatArg[]){{.s = packageName}});
        freeDynArray(&tags);
        candidate->hasUpdate = false;
        return true;
    }

    GitTag *bestTag = NULL;

    if (latest) {
        // --latest: use absolute newest version
        bestTag = &((GitTag *)tags.elems)[tags.size - 1];
        printStatusSticky(log, "  Latest available: %u.%u.%u (ignoring constraints)",
                   bestTag->version.major,
                   bestTag->version.minor,
                   bestTag->version.patch);
    } else {
        // Respect version constraints from Cxyfile.yaml
        if (dep->version && dep->version[0] != '\0') {
            VersionConstraint constraint;
            if (!parseVersionConstraint(dep->version, &constraint, log)) {
                logError(log, NULL, "invalid version constraint '{s}' for '{s}'",
                        (FormatArg[]){{.s = dep->version}, {.s = packageName}});
                freeDynArray(&tags);
                return false;
            }

            // Find best version satisfying constraint
            DynArray constraints = newDynArray(sizeof(VersionConstraint));
            pushOnDynArray(&constraints, &constraint);

            GitTag candidateTag;
            if (findBestMatchingVersion(dep->repository, &constraints, &candidateTag, pool, log)) {
                bestTag = &candidateTag;
                printStatusSticky(log, "  Best matching constraint '%s': %u.%u.%u",
                           dep->version,
                           bestTag->version.major,
                           bestTag->version.minor,
                           bestTag->version.patch);
            } else {
                printStatusSticky(log, "  No versions satisfy constraint '%s'", dep->version);
            }

            freeDynArray(&constraints);
        } else {
            // No constraint specified, use latest
            bestTag = &((GitTag *)tags.elems)[tags.size - 1];
            printStatusSticky(log, "  Latest available: %u.%u.%u (no constraints)",
                       bestTag->version.major,
                       bestTag->version.minor,
                       bestTag->version.patch);
        }
    }

    // Initialize candidate
    candidate->name = packageName;
    candidate->repository = dep->repository;
    candidate->currentVersion = locked->version;
    candidate->currentTag = locked->tag;
    candidate->isDev = isDev;

    if (bestTag) {
        candidate->newVersion = bestTag->version;
        candidate->newTag = bestTag->name;

        // Check if this is actually an update
        int comparison = compareSemanticVersions(&locked->version, &bestTag->version);
        candidate->hasUpdate = (comparison < 0); // current < new

        if (candidate->hasUpdate) {
            printStatusSticky(log, "  → Update available: %u.%u.%u → %u.%u.%u",
                       locked->version.major, locked->version.minor, locked->version.patch,
                       bestTag->version.major, bestTag->version.minor, bestTag->version.patch);
        } else if (comparison == 0) {
            printStatusSticky(log, "  → Already at latest version");
        } else {
            printStatusSticky(log, "  → Current version is newer than available versions");
        }
    } else {
        candidate->hasUpdate = false;
    }

    freeDynArray(&tags);
    return true;
}

/**
 * Perform updates for packages with available updates
 */
static bool performUpdates(const DynArray *candidates,
                          const char *packagesDir,
                          MemPool *pool,
                          Log *log)
{
    u32 updatedCount = 0;
    u32 failedCount = 0;

    for (u32 i = 0; i < candidates->size; i++) {
        UpdateCandidate *candidate = &((UpdateCandidate *)candidates->elems)[i];

        if (!candidate->hasUpdate) {
            continue;
        }

        printStatusSticky(log, "Updating %s%s...",
                   candidate->name,
                   candidate->isDev ? " (dev)" : "");

        // Remove existing installation
        char packagePath[1024];
        snprintf(packagePath, sizeof(packagePath), "%s/%s", packagesDir, candidate->name);

        char rmCmd[1100];
        snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\"", packagePath);
        if (system(rmCmd) != 0) {
            logWarning(log, NULL, "failed to remove existing package directory for '{s}'",
                      (FormatArg[]){{.s = candidate->name}});
        }

        // Install new version
        PackageDependency dep;
        dep.name = candidate->name;
        dep.repository = candidate->repository;
        dep.version = NULL; // Use specific tag
        dep.tag = candidate->newTag;
        dep.branch = NULL;
        dep.path = NULL;
        dep.isDev = candidate->isDev;

        if (installDependency(&dep, packagesDir, pool, log, false)) {
            printStatusSticky(log, "  ✓ Updated %s to %s",
                       candidate->name, candidate->newTag);
            updatedCount++;
        } else {
            logError(log, NULL, "  ✗ Failed to update {s}",
                    (FormatArg[]){{.s = candidate->name}});
            failedCount++;
        }
    }

    // Print summary
    if (updatedCount > 0) {
        printStatusSticky(log, cBGRN "✔" cDEF " Updated %u %s",
                   updatedCount,
                   updatedCount == 1 ? "package" : "packages");
    }

    if (failedCount > 0) {
        logError(log, NULL, cBRED "✘" cDEF " Failed to update %u %s",
                (FormatArg[]){{.i = failedCount},
                             {.s = failedCount == 1 ? "package" : "packages"}});
    }

    return failedCount == 0;
}

/**
 * Package update command implementation
 */
bool packageUpdateCommand(const Options *options, StrPool *strings, Log *log)
{
    DynArray updatePackages = options->package.packages;
    bool latest = options->package.latest;
    bool dryRun = options->package.dryRun;
    bool includeDev = options->package.includeDev;

    // Require explicit package names
    if (updatePackages.size == 0) {
        logError(log, NULL, "no packages specified for update. Use: cxy package update <pkg1> <pkg2> ...", NULL);
        return false;
    }

    // Find and load Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found in current directory or parent directories", NULL);
        return false;
    }

    // Load existing lock file
    char lockfilePath[1024];
    snprintf(lockfilePath, sizeof(lockfilePath), "%s/Cxyfile.lock", packageDir);

    ResolverContext lockCtx;
    initResolverContext(&lockCtx, strings->mem_pool, log);

    if (!loadLockFile(lockfilePath, &lockCtx)) {
        logError(log, NULL, "failed to load lock file: {s}. Run 'cxy package install' first.",
                (FormatArg[]){{.s = lockfilePath}});
        freeResolverContext(&lockCtx);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    printStatusSticky(log, "Checking for updates to %u %s%s...",
               updatePackages.size,
               updatePackages.size == 1 ? "package" : "packages",
               latest ? " (latest versions)" : "");

    // Check each specified package for updates
    DynArray candidates = newDynArray(sizeof(UpdateCandidate));
    bool allValid = true;

    for (u32 i = 0; i < updatePackages.size; i++) {
        cstring packageName = ((cstring *)updatePackages.elems)[i];
        UpdateCandidate candidate;

        if (checkPackageForUpdate(packageName, &meta, &lockCtx, latest, includeDev,
                                  &candidate, strings->mem_pool, log)) {
            pushOnDynArray(&candidates, &candidate);
        } else {
            allValid = false;
        }
    }

    if (!allValid) {
        logError(log, NULL, "some packages could not be checked for updates", NULL);
        freeDynArray(&candidates);
        freeResolverContext(&lockCtx);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Count packages with available updates
    u32 updatesAvailable = 0;
    for (u32 i = 0; i < candidates.size; i++) {
        UpdateCandidate *candidate = &((UpdateCandidate *)candidates.elems)[i];
        if (candidate->hasUpdate) {
            updatesAvailable++;
        }
    }

    if (updatesAvailable == 0) {
        printStatusSticky(log, cBGRN "✔" cDEF " All specified packages are up to date");
        freeDynArray(&candidates);
        freeResolverContext(&lockCtx);
        free(packageDir);
        freePackageMetadata(&meta);
        return true;
    }

    printStatusSticky(log, "Found %u %s with available updates",
               updatesAvailable,
               updatesAvailable == 1 ? "package" : "packages");

    // If dry run, just show what would be updated
    if (dryRun) {
        printStatusSticky(log, "\nDry run - changes that would be made:");
        for (u32 i = 0; i < candidates.size; i++) {
            UpdateCandidate *candidate = &((UpdateCandidate *)candidates.elems)[i];
            if (candidate->hasUpdate) {
                if (candidate->currentTag) {
                    printStatusSticky(log, "  %s: %s → %s",
                               candidate->name,
                               candidate->currentTag,
                               candidate->newTag);
                } else {
                    printStatusSticky(log, "  %s: (new install) → %s",
                               candidate->name,
                               candidate->newTag);
                }
            }
        }
        printStatusSticky(log, "\nRun without --dry-run to perform these updates");

        freeDynArray(&candidates);
        freeResolverContext(&lockCtx);
        free(packageDir);
        freePackageMetadata(&meta);
        return true;
    }

    // Determine packages directory
    char targetPackagesDir[1024];
    snprintf(targetPackagesDir, sizeof(targetPackagesDir), "%s/.cxy/packages", packageDir);

    // Perform the updates
    bool success = performUpdates(&candidates, targetPackagesDir, strings->mem_pool, log);

    if (success && updatesAvailable > 0) {
        // Regenerate lock file with updated versions
        printStatusSticky(log, "Regenerating lock file...");

        // Create new resolver context for fresh resolution
        ResolverContext newCtx;
        initResolverContext(&newCtx, strings->mem_pool, log);
        newCtx.allowDevDeps = true; // Always include dev deps for lockfile generation

        if (resolveDependencies(&newCtx, &meta)) {
            // Capture commit hashes and checksums for updated packages
            for (u32 i = 0; i < newCtx.resolved.size; i++) {
                ResolvedDependency *resolved = &((ResolvedDependency *)newCtx.resolved.elems)[i];

                char installedPath[1024];
                snprintf(installedPath, sizeof(installedPath), "%s/%s", targetPackagesDir, resolved->name);

                // Get current commit
                if (resolved->repository && !resolved->commit) {
                    cstring commit = NULL;
                    if (gitGetCurrentCommit(installedPath, &commit, strings->mem_pool, log)) {
                        resolved->commit = commit;
                    }
                }

                // Calculate checksum
                if (!resolved->checksum) {
                    cstring checksum = NULL;
                    if (gitCalculateChecksum(installedPath, &checksum, strings->mem_pool, log)) {
                        resolved->checksum = checksum;
                    }
                }
            }

            if (!generateLockFile(&newCtx, lockfilePath)) {
                logWarning(log, NULL, "failed to regenerate lock file", NULL);
            }
        } else {
            logWarning(log, NULL, "failed to resolve updated dependencies for lock file", NULL);
        }

        freeResolverContext(&newCtx);
    }

    freeDynArray(&candidates);
    freeResolverContext(&lockCtx);
    free(packageDir);
    freePackageMetadata(&meta);

    return success;
}
