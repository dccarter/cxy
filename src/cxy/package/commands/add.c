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
#include "package/gitops.h"
#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"

#include <string.h>
#include <unistd.h>

/**
 * Derive package name from repository URL
 * Examples:
 *   https://github.com/user/repo.git -> repo
 *   https://github.com/user/my-package -> my-package
 *   github:user/json-parser -> json-parser
 *   git@github.com:user/crypto.git -> crypto
 */
static cstring derivePackageNameFromRepository(const char *repository, StrPool *strings)
{
    if (!repository || repository[0] == '\0') {
        return NULL;
    }

    const char *start = repository;
    const char *end = repository + strlen(repository);

    // Handle short format: "github:user/repo" or "gitlab:user/repo"
    const char *colon = strchr(repository, ':');
    if (colon && strncmp(repository, "github:", 7) == 0) {
        start = colon + 1;
    } else if (colon && strncmp(repository, "gitlab:", 7) == 0) {
        start = colon + 1;
    } else if (colon && strncmp(repository, "bitbucket:", 10) == 0) {
        start = colon + 1;
    }
    // Handle git@host:path format
    else if (strncmp(repository, "git@", 4) == 0 && colon) {
        start = colon + 1;
    }
    // Handle URL formats
    else if (strstr(repository, "://")) {
        // Find the last slash to get the repo name
        const char *lastSlash = strrchr(repository, '/');
        if (lastSlash) {
            start = lastSlash + 1;
        }
    }

    // Handle user/repo format in short syntax
    const char *slash = strchr(start, '/');
    if (slash) {
        start = slash + 1;
    }

    // Remove .git suffix if present
    if (end - start > 4 && strcmp(end - 4, ".git") == 0) {
        end -= 4;
    }

    // Extract the name
    size_t len = end - start;
    if (len == 0) {
        return NULL;
    }

    char buffer[256];
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, start, len);
    buffer[len] = '\0';

    return makeString(strings, buffer);
}

/**
 * Check if a dependency with the given name already exists
 */
static bool dependencyExists(const PackageMetadata *meta, const char *name, bool checkDevDeps)
{
    // Check regular dependencies
    for (u32 i = 0; i < meta->dependencies.size; i++) {
        PackageDependency *dep = &((PackageDependency *)meta->dependencies.elems)[i];
        if (strcmp(dep->name, name) == 0) {
            return true;
        }
    }

    // Check dev dependencies if requested
    if (checkDevDeps) {
        for (u32 i = 0; i < meta->devDependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)meta->devDependencies.elems)[i];
            if (strcmp(dep->name, name) == 0) {
                return true;
            }
        }
    }

    return false;
}

/**
 * Phase 2 implementation of package add command with repository validation
 *
 * Phase 3 TODO:
 * - Update/generate Cxyfile.lock
 * - Clone/download package to .cxy/packages/
 * - Resolve transitive dependencies
 */
bool packageAddCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *repository = options->package.repository;
    const char *customName = options->package.packageName;
    const char *version = options->package.constraint;
    const char *tag = options->package.tag;
    const char *branch = options->package.branch;
    const char *path = options->package.path;
    bool isDev = options->package.dev;

    // Find and load existing Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found. Run 'cxy package create' first.", NULL);
        return false;
    }

    printStatusSticky(log, "Adding dependency to package '%s'...", meta.name);

    // Derive package name if not provided
    cstring packageName;
    if (customName && customName[0] != '\0') {
        packageName = makeString(strings, customName);
    } else if (repository && repository[0] != '\0') {
        packageName = derivePackageNameFromRepository(repository, strings);
        if (!packageName) {
            logError(log, NULL, "failed to derive package name from repository. Use --name to specify.", NULL);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    } else if (path && path[0] != '\0') {
        // For local paths, use the directory name
        const char *lastSlash = strrchr(path, '/');
        packageName = makeString(strings, lastSlash ? lastSlash + 1 : path);
    } else {
        logError(log, NULL, "must specify either repository or --path", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Check for duplicate dependency
    if (dependencyExists(&meta, packageName, true)) {
        logError(log, NULL, "dependency '{s}' already exists in Cxyfile.yaml",
                (FormatArg[]){{.s = packageName}});
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Validate constraints: can't have both tag and branch
    if (tag && tag[0] != '\0' && branch && branch[0] != '\0') {
        logError(log, NULL, "cannot specify both --tag and --branch", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Create new dependency
    PackageDependency newDep = {0};
    newDep.name = packageName;
    newDep.repository = (repository && repository[0] != '\0') ? makeString(strings, repository) : NULL;
    newDep.version = (version && version[0] != '\0') ? makeString(strings, version) : NULL;
    newDep.tag = (tag && tag[0] != '\0') ? makeString(strings, tag) : NULL;
    newDep.branch = (branch && branch[0] != '\0') ? makeString(strings, branch) : NULL;
    newDep.path = (path && path[0] != '\0') ? makeString(strings, path) : NULL;
    newDep.isDev = isDev;

    // Validate and install dependency (unless --no-install is specified)
    bool noInstall = options->package.noInstall;

    // Build packages directory path for installation
    char packagesDir[1024];
    snprintf(packagesDir, sizeof(packagesDir), "%s/.cxy/packages", packageDir);

    if (repository && repository[0] != '\0') {
        // Normalize the repository URL first
        cstring normalizedUrl = NULL;
        if (!gitNormalizeRepositoryUrl(repository, &normalizedUrl, strings->mem_pool, log)) {
            logError(log, NULL, "failed to normalize repository URL: {s}",
                    (FormatArg[]){{.s = repository}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        // Update dependency to use normalized URL
        newDep.repository = normalizedUrl;

        // Validate accessibility
        printStatusSticky(log, " Validating repository accessibility...");
        if (!gitIsRepositoryAccessible(normalizedUrl, log)) {
            logError(log, NULL, "repository '{s}' is not accessible or does not exist",
                    (FormatArg[]){{.s = normalizedUrl}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        // Fetch and validate tags - remote dependencies must have at least one released version
        printStatusSticky(log, " Fetching available versions...");
        DynArray tags = newDynArray(sizeof(GitTag));

        if (!gitFetchTags(normalizedUrl, &tags, strings->mem_pool, log)) {
            logError(log, NULL, "failed to fetch tags from repository '{s}'",
                    (FormatArg[]){{.s = normalizedUrl}});
            freeDynArray(&tags);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        if (tags.size == 0) {
            logError(log, NULL, "repository '{s}' has no released versions (tags). Remote dependencies must have at least one semantic version tag.",
                    (FormatArg[]){{.s = normalizedUrl}});
            freeDynArray(&tags);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        printStatusSticky(log, " Found %u released version(s)", tags.size);

        // If version is "*" or not specified, use the latest tag
        if (!newDep.version || strcmp(newDep.version, "*") == 0) {
            // Get the latest tag (tags are sorted ascending, so last element is latest)
            GitTag *latestTag = &((GitTag *)tags.elems)[tags.size - 1];
            newDep.tag = latestTag->name;
            printStatusSticky(log, " Resolved latest version to: %s", latestTag->name);
        }

        // Install dependency to validate it's a valid Cxy package
        if (!installDependency(&newDep, packagesDir, strings->mem_pool, log, noInstall, options->package.verbose)) {
            logError(log, NULL, "failed to install dependency - not a valid Cxy package or installation failed", NULL);
            freeDynArray(&tags);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        // Clean up tags array - we're done with it
        freeDynArray(&tags);
    } else if (path && path[0] != '\0') {
        // Validate local path dependency
        if (!installDependency(&newDep, packagesDir, strings->mem_pool, log, true, options->package.verbose)) {
            logError(log, NULL, "local path '{s}' does not contain a valid Cxy package",
                    (FormatArg[]){{.s = path}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    }

    // Add to appropriate dependency list
    if (isDev) {
        pushOnDynArray(&meta.devDependencies, &newDep);
        printStatusSticky(log, " Added dev dependency: %s", packageName);
    } else {
        pushOnDynArray(&meta.dependencies, &newDep);
        printStatusSticky(log, " Added dependency: %s", packageName);
    }

    // Write updated Cxyfile.yaml
    char cxyfilePath[1024];
    snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packageDir);

    if (!writeCxyfile(cxyfilePath, &meta, log)) {
        logError(log, NULL, "failed to write updated Cxyfile.yaml", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    printStatusAlways(log, cBGRN "✔" cDEF " Dependency '%s' added successfully\n", packageName);

    if (noInstall) {
        printStatusAlways(log, cBMGN " Run 'cxy package install' to download and install dependencies" cDEF);
    }

    free(packageDir);
    freePackageMetadata(&meta);
    return true;
}
