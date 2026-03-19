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
#include "package/install_scripts.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/**
 * Try to find entry point in default locations
 */
static cstring findDefaultEntry(const char *packageDir, MemPool *pool, Log *log)
{
    const char *candidates[] = {
        "app.cxy",
        "lib.cxy",
        "src/main.cxy",
        "src/lib.cxy",
        NULL
    };

    for (int i = 0; candidates[i]; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", packageDir, candidates[i]);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            printStatusSticky(log, "Auto-detected entry point: %s", candidates[i]);
            // Allocate from pool and return
            size_t len = strlen(candidates[i]);
            char *result = (char *)allocFromMemPool(pool, len + 1);
            strcpy(result, candidates[i]);
            return result;
        }
    }

    return NULL; // Not found
}

/**
 * Construct the cxy build command from build configuration
 */
static cstring constructBuildCommand(const PackageBuildConfig *config,
                                     cstring entryPoint,
                                     StrPool *strings,
                                     Log *log)
{
    FormatState state = newFormatState(NULL, true);

    // Start with cxy build and entry point
    format(&state, "cxy build {s}", (FormatArg[]){{.s = entryPoint}});

    // Add --no-progress to disable interactive progress indicators when piped
    format(&state, " --no-progress", NULL);

    // Add output if specified
    if (config->output && config->output[0] != '\0') {
        format(&state, " -o {s}", (FormatArg[]){{.s = config->output}});
    }

    // Add C libraries (--c-lib lib1 --c-lib lib2)
    for (u32 i = 0; i < config->cLibs.size; i++) {
        cstring lib = ((cstring *)config->cLibs.elems)[i];
        format(&state, " --c-lib {s}", (FormatArg[]){{.s = lib}});
    }

    // Add C library directories (--c-lib-dir dir1 --c-lib-dir dir2)
    for (u32 i = 0; i < config->cLibDirs.size; i++) {
        cstring dir = ((cstring *)config->cLibDirs.elems)[i];
        format(&state, " --c-lib-dir {s}", (FormatArg[]){{.s = dir}});
    }

    // Add C header directories (--c-header-dir dir1 --c-header-dir dir2)
    for (u32 i = 0; i < config->cHeaderDirs.size; i++) {
        cstring dir = ((cstring *)config->cHeaderDirs.elems)[i];
        format(&state, " --c-header-dir {s}", (FormatArg[]){{.s = dir}});
    }

    // Add C defines (-DDEBUG -DVERSION=1.0)
    for (u32 i = 0; i < config->cDefines.size; i++) {
        cstring define = ((cstring *)config->cDefines.elems)[i];
        format(&state, " -D{s}", (FormatArg[]){{.s = define}});
    }

    // Add C flags (--c-flag=-O2 --c-flag=-Wall)
    for (u32 i = 0; i < config->cFlags.size; i++) {
        cstring flag = ((cstring *)config->cFlags.elems)[i];
        format(&state, " --c-flag={s}", (FormatArg[]){{.s = flag}});
    }

    // Add Cxy defines (-DTEST_MODE -DREDIS_DEBUG=1)
    for (u32 i = 0; i < config->defines.size; i++) {
        cstring define = ((cstring *)config->defines.elems)[i];
        format(&state, " -D{s}", (FormatArg[]){{.s = define}});
    }

    // Add Cxy flags (pass through as-is)
    for (u32 i = 0; i < config->flags.size; i++) {
        cstring flag = ((cstring *)config->flags.elems)[i];
        format(&state, " {s}", (FormatArg[]){{.s = flag}});
    }

    // Add plugins directory
    if (config->pluginsDir && config->pluginsDir[0] != '\0') {
        format(&state, " --plugins-dir {s}", (FormatArg[]){{.s = config->pluginsDir}});
    }

    // Add stdlib path
    if (config->stdlib && config->stdlib[0] != '\0') {
        format(&state, " --stdlib {s}", (FormatArg[]){{.s = config->stdlib}});
    }

    // Get the formatted string (malloc'd) then copy to pool
    char *temp = formatStateToString(&state);
    cstring result = makeString(strings, temp);
    free(temp);
    freeFormatState(&state);

    return result;
}

/**
 * Add build directory option to command if specified
 */
static void addBuildDirOption(FormatState *state, const char *buildDir)
{
    if (buildDir && buildDir[0] != '\0') {
        format(state, " --build-dir {s}", (FormatArg[]){{.s = buildDir}});
    }
}


/**
 * Find a build by name in the builds array
 */
static PackageBuild* findBuildByName(const DynArray *builds, const char *name)
{
    for (u32 i = 0; i < builds->size; i++) {
        PackageBuild *build = &((PackageBuild *)builds->elems)[i];
        if (strcmp(build->name, name) == 0) {
            return build;
        }
    }
    return NULL;
}

/**
 * Get the default build from builds array
 */
static PackageBuild* getDefaultBuild(const DynArray *builds)
{
    // First, look for explicitly marked default
    for (u32 i = 0; i < builds->size; i++) {
        PackageBuild *build = &((PackageBuild *)builds->elems)[i];
        if (build->isDefault) {
            return build;
        }
    }

    // If no default marked, return first build
    if (builds->size > 0) {
        return &((PackageBuild *)builds->elems)[0];
    }

    return NULL;
}

/**
 * List all available build targets
 */
static void listAvailableBuilds(const PackageMetadata *meta, Log *log)
{
    if (!meta->hasMultipleBuilds || meta->builds.size == 0) {
        printStatusSticky(log, "No named builds defined. Using single build configuration.");
        return;
    }

    printStatusSticky(log, "Available build targets:");
    printStatusSticky(log, "(Note: Builds starting with '_' are templates and not listed)");
    for (u32 i = 0; i < meta->builds.size; i++) {
        PackageBuild *build = &((PackageBuild *)meta->builds.elems)[i];
        const char *defaultMarker = build->isDefault ? " (default)" : "";

        printStatusSticky(log, "  - %s%s", build->name, defaultMarker);

        if (build->config.entry) {
            printStatusSticky(log, "      entry: %s", build->config.entry);
        }
        if (build->config.output) {
            printStatusSticky(log, "      output: %s", build->config.output);
        }
    }
}

/**
 * Package build command implementation
 */
bool packageBuildCommand(const Options *options, StrPool *strings, Log *log)
{
    bool clean = options->package.clean;
    bool verbose = options->package.verbose;
    const char *buildDir = options->package.buildDir;
    const DynArray *restArgs = &options->package.rest;
    const char *buildTarget = options->package.buildTarget;
    bool buildAll = options->package.buildAll;
    bool listBuilds = options->package.listBuilds;

    // Default build directory if not specified
    char defaultBuildDir[1024];
    if (!buildDir || buildDir[0] == '\0') {
        // Will be set after packageDir is known
    }



    // Find and load Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found in current directory or parent directories", NULL);
        return false;
    }

    // Resolve default build directory now that we have packageDir
    if (!buildDir || buildDir[0] == '\0') {
        snprintf(defaultBuildDir, sizeof(defaultBuildDir), "%s/.cxy/build", packageDir);
        buildDir = defaultBuildDir;
    }

    // Auto-run install scripts if needed (install sections defined but .install.yaml missing or stale)
    if (meta.install.size > 0) {
        char installYamlPath[1024];
        snprintf(installYamlPath, sizeof(installYamlPath), "%s/.install.yaml", buildDir);

        struct stat installSt, cxyfileSt;
        char cxyfilePath[1024];
        snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packageDir);

        bool needsInstall = (stat(installYamlPath, &installSt) != 0);

        // Also re-run if Cxyfile.yaml is newer than .install.yaml
        if (!needsInstall &&
            stat(cxyfilePath, &cxyfileSt) == 0 &&
            cxyfileSt.st_mtime > installSt.st_mtime) {
            needsInstall = true;
            printStatusSticky(log, "Cxyfile.yaml changed, re-running install scripts...");
        }

        if (needsInstall) {
            printStatusSticky(log, "Running install scripts...");
            if (!executeInstallScripts(&meta, packageDir, buildDir, false, strings, log, verbose)) {
                logError(log, NULL, "install scripts failed", NULL);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }
        }
    }

    // Read install flags from .install.yaml
    DynArray installFlags = newDynArray(sizeof(cstring));
    readInstallYamlFlags(buildDir, &installFlags, false, strings, log);

    // Validate builds configuration
    // Note: Template builds (starting with '_') are already filtered out during parsing
    if (meta.hasMultipleBuilds && meta.builds.size > 0) {
        u32 buildsWithoutOutput = 0;
        for (u32 i = 0; i < meta.builds.size; i++) {
            PackageBuild *build = &((PackageBuild *)meta.builds.elems)[i];
            if (!build->config.output || build->config.output[0] == '\0') {
                buildsWithoutOutput++;
            }
        }

        if (buildsWithoutOutput > 1) {
            logError(log, NULL,
                    "multiple builds without 'output' field. At most one build can omit the output field.",
                    NULL);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    }

    // Handle --list flag
    if (listBuilds) {
        listAvailableBuilds(&meta, log);
        free(packageDir);
        freePackageMetadata(&meta);
        return true;
    }

    printStatusSticky(log, "Building package '%s'...", meta.name);

    // Determine which builds to run
    DynArray buildsToRun = newDynArray(sizeof(PackageBuild*));

    if (meta.hasMultipleBuilds) {
        if (buildAll) {
            // Build all targets
            for (u32 i = 0; i < meta.builds.size; i++) {
                PackageBuild *build = &((PackageBuild *)meta.builds.elems)[i];
                pushOnDynArray(&buildsToRun, &build);
            }
        } else if (buildTarget && buildTarget[0] != '\0') {
            // Build specific target
            PackageBuild *build = findBuildByName(&meta.builds, buildTarget);
            if (!build) {
                logError(log, NULL, "build target '{s}' not found. Use --list to see available targets.",
                        (FormatArg[]){{.s = buildTarget}});
                freeDynArray(&buildsToRun);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }
            pushOnDynArray(&buildsToRun, &build);
        } else {
            // Build default target
            PackageBuild *defaultBuild = getDefaultBuild(&meta.builds);
            if (!defaultBuild) {
                logError(log, NULL, "no default build found. Specify a target or use --all", NULL);
                freeDynArray(&buildsToRun);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }
            pushOnDynArray(&buildsToRun, &defaultBuild);
        }
    } else {
        // Single build mode - use legacy build config
        // Create a temporary PackageBuild for consistency
        PackageBuild singleBuild = {
            .name = "default",
            .isDefault = true,
            .config = meta.build
        };
        PackageBuild *buildPtr = &singleBuild;
        pushOnDynArray(&buildsToRun, &buildPtr);
    }

    // Change to package directory before building
    char originalDir[1024];
    if (!getcwd(originalDir, sizeof(originalDir))) {
        logError(log, NULL, "failed to get current directory", NULL);
        freeDynArray(&buildsToRun);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (chdir(packageDir) != 0) {
        logError(log, NULL, "failed to change to package directory: {s}",
                (FormatArg[]){{.s = packageDir}});
        freeDynArray(&buildsToRun);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Handle --clean flag before building any targets
    if (clean && buildDir && buildDir[0] != '\0') {
        printStatusSticky(log, "Cleaning directory: %s", buildDir);

        FormatState cleanState = newFormatState(NULL, true);
        format(&cleanState, "rm -rf \"{s}\"", (FormatArg[]){{.s = buildDir}});
        char *tempClean = formatStateToString(&cleanState);
        cstring cleanCmd = makeString(strings, tempClean);
        free(tempClean);
        freeFormatState(&cleanState);

        if (system(cleanCmd) != 0) {
            logWarning(log, NULL, "failed to clean directory (may not exist)", NULL);
        }
    }

    // Ensure build directory exists (after any cleaning)
    if (buildDir && buildDir[0] != '\0') {
        if (!makeDirectory(buildDir, true)) {
            logError(log, NULL, "failed to create build directory: {s}",
                      (FormatArg[]){{.s = strerror(errno)}});
            freeDynArray(&buildsToRun);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    }

    // Build each target
    bool allSuccess = true;
    for (u32 i = 0; i < buildsToRun.size; i++) {
        PackageBuild *build = *((PackageBuild **)buildsToRun.elems + i);

        // Use the build's config directly (YAML anchors handle inheritance)
        PackageBuildConfig *config = &build->config;

        // Determine entry point
        cstring entryPoint = config->entry;
        if (!entryPoint || entryPoint[0] == '\0') {
            entryPoint = findDefaultEntry(packageDir, strings->mem_pool, log);
            if (!entryPoint) {
                logError(log, NULL,
                        "no entry point specified for build '{s}' and none of the default locations exist.",
                        (FormatArg[]){{.s = build->name}});
                allSuccess = false;
                continue;
            }
        }

        // Verify entry point exists
        char entryPath[1024];
        snprintf(entryPath, sizeof(entryPath), "%s/%s", packageDir, entryPoint);

        struct stat st;
        if (stat(entryPath, &st) != 0 || !S_ISREG(st.st_mode)) {
            logError(log, NULL, "entry point does not exist for build '{s}': {s}",
                    (FormatArg[]){{.s = build->name}, {.s = entryPath}});
            allSuccess = false;
            continue;
        }

        // Ensure output directory exists
        // Unconditionally ensure output directory exists after cleaning
        if (config->output && config->output[0] != '\0') {
            char outputDir[1024];
            strncpy(outputDir, config->output, sizeof(outputDir) - 1);
            outputDir[sizeof(outputDir) - 1] = '\0';

            char *lastSlash = strrchr(outputDir, '/');
            if (lastSlash) {
                *lastSlash = '\0';
                if (!makeDirectory(outputDir, true)) {
                    logWarning(log, NULL, "failed to create output directory: {s}",
                              (FormatArg[]){{.s = strerror(errno)}});
                }
            }
        }

        // Construct build command
        cstring buildCommand = constructBuildCommand(config, entryPoint, strings, log);
        if (!buildCommand) {
            logError(log, NULL, "failed to construct build command for '{s}'",
                    (FormatArg[]){{.s = build->name}});
            allSuccess = false;
            continue;
        }

        // Add build-dir and rest arguments
        FormatState finalCmd = newFormatState(NULL, true);
        format(&finalCmd, "{s}", (FormatArg[]){{.s = buildCommand}});
        addBuildDirOption(&finalCmd, buildDir);

        // Inject flags from .install.yaml
        for (u32 j = 0; j < installFlags.size; j++) {
            cstring flag = ((cstring *)installFlags.elems)[j];
            format(&finalCmd, " {s}", (FormatArg[]){{.s = flag}});
        }

        // Validate and add rest arguments
        if (restArgs && restArgs->size > 0) {
            for (u32 j = 0; j < restArgs->size; j++) {
                const char *arg = ((const char **)restArgs->elems)[j];

                if (arg[0] != '-') {
                    logError(log, NULL,
                            "invalid argument '{s}': arguments must start with '-'",
                            (FormatArg[]){{.s = arg}});
                    freeFormatState(&finalCmd);
                    allSuccess = false;
                    goto next_build;
                }

                format(&finalCmd, " {s}", (FormatArg[]){{.s = arg}});
            }
        }

        char *finalCmdTemp = formatStateToString(&finalCmd);
        buildCommand = makeString(strings, finalCmdTemp);
        free(finalCmdTemp);
        freeFormatState(&finalCmd);

        // Execute build
        char header[256];
        if (meta.hasMultipleBuilds) {
            snprintf(header, sizeof(header), "Building target '%s'", build->name);
        } else {
            snprintf(header, sizeof(header), "Building package '%s'", meta.name);
        }

        bool success = runCommandWithProgressFull(header, buildCommand, log, verbose);
        if (!success) {
            logError(log, NULL, "build failed for target '{s}'",
                    (FormatArg[]){{.s = build->name}});
            allSuccess = false;
        }

next_build:
        continue;
    }

    // Restore original directory
    if (chdir(originalDir) != 0) {
        logWarning(log, NULL, "failed to restore original directory", NULL);
    }

    freeDynArray(&buildsToRun);
    freeDynArray(&installFlags);
    free(packageDir);
    freePackageMetadata(&meta);
    return allSuccess;
}
