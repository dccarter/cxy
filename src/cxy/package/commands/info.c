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
#include "cJSON.h"

#include <string.h>
#include <sys/stat.h>

/**
 * Check if a string looks like a git URL
 */
static bool isGitUrl(const char *str)
{
    if (!str || str[0] == '\0')
        return false;

    // Check for common git URL patterns
    return (strstr(str, "://") != NULL ||
            strstr(str, "git@") != NULL ||
            strstr(str, ".git") != NULL);
}

/**
 * Check if a local package is installed
 */
static bool isLocalPackageInstalled(const char *name, char *outPath, size_t pathSize)
{
    snprintf(outPath, pathSize, ".cxy/packages/%s/Cxyfile.yaml", name);

    struct stat st;
    return (stat(outPath, &st) == 0 && S_ISREG(st.st_mode));
}

/**
 * Add dependency array to JSON
 */
static void addDependenciesToJson(cJSON *json, const char *key, const DynArray *deps)
{
    if (deps->size == 0)
        return;

    cJSON *depsArray = cJSON_CreateArray();

    for (u32 i = 0; i < deps->size; i++) {
        PackageDependency *dep = &((PackageDependency *)deps->elems)[i];
        cJSON *depObj = cJSON_CreateObject();

        cJSON_AddItemToObject(depObj, "name", cJSON_CreateString(dep->name));

        if (dep->repository && dep->repository[0] != '\0') {
            cJSON_AddItemToObject(depObj, "repository", cJSON_CreateString(dep->repository));
        }
        if (dep->version && dep->version[0] != '\0') {
            cJSON_AddItemToObject(depObj, "version", cJSON_CreateString(dep->version));
        }
        if (dep->tag && dep->tag[0] != '\0') {
            cJSON_AddItemToObject(depObj, "tag", cJSON_CreateString(dep->tag));
        }
        if (dep->branch && dep->branch[0] != '\0') {
            cJSON_AddItemToObject(depObj, "branch", cJSON_CreateString(dep->branch));
        }
        if (dep->path && dep->path[0] != '\0') {
            cJSON_AddItemToObject(depObj, "path", cJSON_CreateString(dep->path));
        }

        cJSON_AddItemToArray(depsArray, depObj);
    }

    cJSON_AddItemToObject(json, key, depsArray);
}

/**
 * Add tests array to JSON
 */
static void addTestsToJson(cJSON *json, const DynArray *tests)
{
    if (tests->size == 0)
        return;

    cJSON *testsArray = cJSON_CreateArray();

    for (u32 i = 0; i < tests->size; i++) {
        PackageTest *test = &((PackageTest *)tests->elems)[i];
        cJSON *testObj = cJSON_CreateObject();

        cJSON_AddItemToObject(testObj, "file", cJSON_CreateString(test->file));

        if (test->args.size > 0) {
            cJSON *argsArray = cJSON_CreateArray();
            for (u32 j = 0; j < test->args.size; j++) {
                cstring arg = dynArrayAt(cstring *, &test->args, j);
                cJSON_AddItemToArray(argsArray, cJSON_CreateString(arg));
            }
            cJSON_AddItemToObject(testObj, "args", argsArray);
        }

        cJSON_AddItemToArray(testsArray, testObj);
    }

    cJSON_AddItemToObject(json, "tests", testsArray);
}

/**
 * Add scripts to JSON
 */
static void addScriptsToJson(cJSON *json, const DynArray *scripts)
{
    if (scripts->size == 0)
        return;

    cJSON *scriptsObj = cJSON_CreateObject();

    for (u32 i = 0; i < scripts->size; i++) {
        PackageScript *script = &((PackageScript *)scripts->elems)[i];

        if (script->dependencies.size == 0) {
            // Simple script
            cJSON_AddItemToObject(scriptsObj, script->name, cJSON_CreateString(script->command));
        } else {
            // Script with dependencies
            cJSON *scriptObj = cJSON_CreateObject();
            cJSON_AddItemToObject(scriptObj, "command", cJSON_CreateString(script->command));

            cJSON *depsArray = cJSON_CreateArray();
            for (u32 j = 0; j < script->dependencies.size; j++) {
                cstring dep = dynArrayAt(cstring *, &script->dependencies, j);
                cJSON_AddItemToArray(depsArray, cJSON_CreateString(dep));
            }
            cJSON_AddItemToObject(scriptObj, "depends", depsArray);

            cJSON_AddItemToObject(scriptsObj, script->name, scriptObj);
        }
    }

    cJSON_AddItemToObject(json, "scripts", scriptsObj);
}

/**
 * Add build configuration to JSON
 */
static void addBuildToJson(cJSON *json, const PackageBuildConfig *build)
{
    if (!build->entry || build->entry[0] == '\0')
        return;

    cJSON *buildObj = cJSON_CreateObject();

    cJSON_AddItemToObject(buildObj, "entry", cJSON_CreateString(build->entry));

    if (build->output && build->output[0] != '\0') {
        cJSON_AddItemToObject(buildObj, "output", cJSON_CreateString(build->output));
    }

    if (build->cLibs.size > 0) {
        cJSON *libsArray = cJSON_CreateArray();
        for (u32 i = 0; i < build->cLibs.size; i++) {
            cstring lib = dynArrayAt(cstring *, &build->cLibs, i);
            cJSON_AddItemToArray(libsArray, cJSON_CreateString(lib));
        }
        cJSON_AddItemToObject(buildObj, "cLibs", libsArray);
    }

    if (build->cLibDirs.size > 0) {
        cJSON *dirsArray = cJSON_CreateArray();
        for (u32 i = 0; i < build->cLibDirs.size; i++) {
            cstring dir = dynArrayAt(cstring *, &build->cLibDirs, i);
            cJSON_AddItemToArray(dirsArray, cJSON_CreateString(dir));
        }
        cJSON_AddItemToObject(buildObj, "cLibDirs", dirsArray);
    }

    cJSON_AddItemToObject(json, "build", buildObj);
}

/**
 * Output package info as JSON
 */
static void outputJson(const PackageMetadata *meta, bool installed, const char *location, Log *log)
{
    cJSON *json = cJSON_CreateObject();

    // Basic info
    cJSON_AddItemToObject(json, "name", cJSON_CreateString(meta->name));
    cJSON_AddItemToObject(json, "version", cJSON_CreateString(meta->version));

    if (meta->description && meta->description[0] != '\0') {
        cJSON_AddItemToObject(json, "description", cJSON_CreateString(meta->description));
    }
    if (meta->author && meta->author[0] != '\0') {
        cJSON_AddItemToObject(json, "author", cJSON_CreateString(meta->author));
    }
    if (meta->license && meta->license[0] != '\0') {
        cJSON_AddItemToObject(json, "license", cJSON_CreateString(meta->license));
    }
    if (meta->repository && meta->repository[0] != '\0') {
        cJSON_AddItemToObject(json, "repository", cJSON_CreateString(meta->repository));
    }
    if (meta->homepage && meta->homepage[0] != '\0') {
        cJSON_AddItemToObject(json, "homepage", cJSON_CreateString(meta->homepage));
    }

    // Dependencies
    addDependenciesToJson(json, "dependencies", &meta->dependencies);
    addDependenciesToJson(json, "devDependencies", &meta->devDependencies);

    // Tests
    addTestsToJson(json, &meta->tests);

    // Scripts
    addScriptsToJson(json, &meta->scripts);

    // Build
    addBuildToJson(json, &meta->build);

    // Installation info
    cJSON_AddItemToObject(json, "installed", cJSON_CreateBool(installed));
    if (installed && location) {
        cJSON_AddItemToObject(json, "installLocation", cJSON_CreateString(location));
    }

    // Print JSON
    char *jsonStr = cJSON_Print(json);
    printf("%s\n", jsonStr);
    free(jsonStr);
    cJSON_Delete(json);
}

/**
 * Output package info in human-readable format
 */
static void outputHumanReadable(const PackageMetadata *meta, bool installed, const char *location, Log *log)
{
    // Header
    printStatusSticky(log, cBBLU "Package:" cDEF " %s", meta->name);
    printStatusSticky(log, cBBLU "Version:" cDEF " %s", meta->version);

    if (meta->description && meta->description[0] != '\0') {
        printStatusSticky(log, cBBLU "Description:" cDEF " %s", meta->description);
    }

    // Metadata
    if (meta->author && meta->author[0] != '\0') {
        printStatusSticky(log, cBBLU "Author:" cDEF " %s", meta->author);
    }
    if (meta->license && meta->license[0] != '\0') {
        printStatusSticky(log, cBBLU "License:" cDEF " %s", meta->license);
    }
    if (meta->repository && meta->repository[0] != '\0') {
        printStatusSticky(log, cBBLU "Repository:" cDEF " %s", meta->repository);
    }
    if (meta->homepage && meta->homepage[0] != '\0') {
        printStatusSticky(log, cBBLU "Homepage:" cDEF " %s", meta->homepage);
    }

    // Dependencies
    if (meta->dependencies.size > 0) {
        printf("\n");
        printStatusSticky(log, cBBLU "Dependencies (%u):" cDEF, meta->dependencies.size);
        for (u32 i = 0; i < meta->dependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)meta->dependencies.elems)[i];
            if (dep->version && dep->version[0] != '\0') {
                printStatusSticky(log, "  - %s (%s)", dep->name, dep->version);
            } else {
                printStatusSticky(log, "  - %s", dep->name);
            }
        }
    }

    // Dev Dependencies
    if (meta->devDependencies.size > 0) {
        printf("\n");
        printStatusSticky(log, cBBLU "Dev Dependencies (%u):" cDEF, meta->devDependencies.size);
        for (u32 i = 0; i < meta->devDependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)meta->devDependencies.elems)[i];
            if (dep->version && dep->version[0] != '\0') {
                printStatusSticky(log, "  - %s (%s)", dep->name, dep->version);
            } else {
                printStatusSticky(log, "  - %s", dep->name);
            }
        }
    }

    // Tests
    if (meta->tests.size > 0) {
        printf("\n");
        printStatusSticky(log, cBBLU "Tests:" cDEF);
        for (u32 i = 0; i < meta->tests.size; i++) {
            PackageTest *test = &((PackageTest *)meta->tests.elems)[i];
            printStatusSticky(log, "  - %s", test->file);
        }
    }

    // Scripts
    if (meta->scripts.size > 0) {
        printf("\n");
        printStatusSticky(log, cBBLU "Scripts (%u):" cDEF, meta->scripts.size);
        for (u32 i = 0; i < meta->scripts.size; i++) {
            PackageScript *script = &((PackageScript *)meta->scripts.elems)[i];
            printStatusSticky(log, "  - %s: %s", script->name, script->command);
        }
    }

    // Build Configuration
    if (meta->build.entry && meta->build.entry[0] != '\0') {
        printf("\n");
        printStatusSticky(log, cBBLU "Build Configuration:" cDEF);
        printStatusSticky(log, "  Entry: %s", meta->build.entry);
        if (meta->build.output && meta->build.output[0] != '\0') {
            printStatusSticky(log, "  Output: %s", meta->build.output);
        }
        if (meta->build.cLibs.size > 0) {
            printf("  C Libraries: ");
            for (u32 i = 0; i < meta->build.cLibs.size; i++) {
                cstring lib = dynArrayAt(cstring *, &meta->build.cLibs, i);
                printf("%s", lib);
                if (i < meta->build.cLibs.size - 1) printf(", ");
            }
            printf("\n");
        }
    }

    // Installation status
    printf("\n");
    printStatusSticky(log, cBBLU "Installation:" cDEF);
    if (installed) {
        printStatusSticky(log, "  Status: %sInstalled%s", cBGRN, cDEF);
        if (location) {
            printStatusSticky(log, "  Location: %s", location);
        }
    } else {
        printStatusSticky(log, "  Status: %sNot installed%s", cBYLW, cDEF);
    }
}

/**
 * Package info command implementation
 */
bool packageInfoCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *packageArg = options->package.package;

    // Validate argument
    if (!packageArg || packageArg[0] == '\0') {
        logError(log, NULL, "package name or URL required. Usage: cxy package info <name>", NULL);
        return false;
    }

    PackageMetadata meta;
    initPackageMetadata(&meta, strings);

    bool installed = false;
    char installPath[2048];
    char *installLocation = NULL;

    // Check if it's a URL or local package name
    if (isGitUrl(packageArg)) {
        // Remote package - not implemented yet
        logError(log, NULL, "remote package info not yet implemented. Use package name for installed packages.", NULL);
        freePackageMetadata(&meta);
        return false;
    } else {
        // Local package
        if (isLocalPackageInstalled(packageArg, installPath, sizeof(installPath))) {
            // Load from local installation
            if (!parseCxyfile(installPath, &meta, strings, log)) {
                logError(log, NULL, "failed to load Cxyfile from {s}",
                        (FormatArg[]){{.s = installPath}});
                freePackageMetadata(&meta);
                return false;
            }

            installed = true;
            snprintf(installPath, sizeof(installPath), ".cxy/packages/%s", packageArg);
            installLocation = installPath;
        } else {
            logError(log, NULL, "package '{s}' not found locally. Install it first with 'cxy package install' or use a git URL.",
                    (FormatArg[]){{.s = packageArg}});
            freePackageMetadata(&meta);
            return false;
        }
    }

    // Output the information
    if (options->package.json) {
        outputJson(&meta, installed, installLocation, log);
    } else {
        outputHumanReadable(&meta, installed, installLocation, log);
    }

    freePackageMetadata(&meta);
    return true;
}
