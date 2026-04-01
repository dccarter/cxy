/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/cache.h"
#include "package/types.h"
#include "package/env.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/utils.h"

#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <glob.h>
#include <dirent.h>

/**
 * Recursively find files in a directory matching a suffix
 */
static bool findFilesRecursive(const char *dir,
                               const char *suffix,
                               DynArray *files,
                               StrPool *strings,
                               Log *log)
{
    DIR *d = opendir(dir);
    if (!d) {
        return true; // Not an error if directory doesn't exist
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char fullPath[2048];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(fullPath, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            // Recurse into subdirectory
            if (!findFilesRecursive(fullPath, suffix, files, strings, log)) {
                closedir(d);
                return false;
            }
        }
        else if (S_ISREG(st.st_mode)) {
            // Check if file matches suffix (or no suffix means match all)
            if (suffix == NULL || suffix[0] == '\0') {
                cstring pooledPath = makeString(strings, fullPath);
                pushOnDynArray(files, &pooledPath);
            }
            else {
                size_t nameLen = strlen(entry->d_name);
                size_t suffixLen = strlen(suffix);

                if (nameLen >= suffixLen && strcmp(entry->d_name + nameLen - suffixLen, suffix) == 0) {
                    cstring pooledPath = makeString(strings, fullPath);
                    pushOnDynArray(files, &pooledPath);
                }
            }
        }
    }

    closedir(d);
    return true;
}

/**
 * Expand a single glob pattern to file paths
 */
static bool expandSingleGlob(const char *packageDir,
                             const char *pattern,
                             DynArray *files,
                             StrPool *strings,
                             Log *log)
{
    // Check if pattern contains **
    if (strstr(pattern, "**") != NULL) {
        // Handle ** pattern by recursively searching
        char baseDir[2048];
        const char *suffix = NULL;

        // Find the directory part before **
        const char *doubleStarPos = strstr(pattern, "**");
        size_t baseDirLen = doubleStarPos - pattern;

        if (baseDirLen > 0 && pattern[baseDirLen - 1] == '/') {
            baseDirLen--; // Remove trailing /
        }

        if (baseDirLen > 0) {
            memcpy(baseDir, pattern, baseDirLen);
            baseDir[baseDirLen] = '\0';
        } else {
            strcpy(baseDir, ".");
        }

        // Extract suffix after ** (e.g., "/*.c" -> ".c")
        const char *afterDoubleStar = doubleStarPos + 2;
        if (afterDoubleStar[0] == '/' && afterDoubleStar[1] != '\0') {
            // Pattern like "src/**/*.c"
            const char *lastSlash = strrchr(afterDoubleStar, '/');
            if (lastSlash && lastSlash[1] != '\0') {
                suffix = lastSlash + 1;
                // Check if suffix has wildcards - if so, treat as no suffix
                if (strchr(suffix, '*') || strchr(suffix, '?')) {
                    suffix = NULL;
                }
            }
        }

        // Build full base directory path
        char fullBaseDir[2048];
        // If baseDir is already absolute, use it as-is
        if (baseDir[0] == '/') {
            snprintf(fullBaseDir, sizeof(fullBaseDir), "%s", baseDir);
        } else {
            snprintf(fullBaseDir, sizeof(fullBaseDir), "%s/%s", packageDir, baseDir);
        }

        // Recursively find all matching files
        size_t beforeCount = files->size;
        if (!findFilesRecursive(fullBaseDir, suffix, files, strings, log)) {
            return false;
        }

        // Make paths relative to packageDir
        for (size_t i = beforeCount; i < files->size; i++) {
            cstring *pathPtr = &((cstring *)files->elems)[i];
            const char *fullPath = *pathPtr;

            size_t packageDirLen = strlen(packageDir);
            if (strncmp(fullPath, packageDir, packageDirLen) == 0 &&
                fullPath[packageDirLen] == '/') {
                *pathPtr = makeString(strings, fullPath + packageDirLen + 1);
            }
        }

        return true;
    }

    // Standard glob pattern without **
    char fullPattern[2048];
    // If pattern is already absolute, use it as-is
    if (pattern[0] == '/') {
        snprintf(fullPattern, sizeof(fullPattern), "%s", pattern);
    } else {
        snprintf(fullPattern, sizeof(fullPattern), "%s/%s", packageDir, pattern);
    }

    glob_t globResult;
    memset(&globResult, 0, sizeof(globResult));

    int ret = glob(fullPattern, GLOB_TILDE | GLOB_MARK, NULL, &globResult);

    if (ret == GLOB_NOMATCH) {
        // No matches - not an error for patterns
        globfree(&globResult);
        return true;
    }
    else if (ret != 0) {
        logError(log, NULL, "glob pattern '{s}' failed: {i}",
                (FormatArg[]){{.s = pattern}, {.i = ret}});
        globfree(&globResult);
        return false;
    }

    // Add matches to files array
    for (size_t i = 0; i < globResult.gl_pathc; i++) {
        const char *path = globResult.gl_pathv[i];

        // Skip directories (marked with trailing /)
        size_t len = strlen(path);
        if (len > 0 && path[len-1] == '/')
            continue;

        // Make relative to packageDir
        const char *relativePath = path;
        size_t packageDirLen = strlen(packageDir);
        if (strncmp(path, packageDir, packageDirLen) == 0 &&
            path[packageDirLen] == '/') {
            relativePath = path + packageDirLen + 1;
        }

        cstring pooledPath = makeString(strings, relativePath);
        pushOnDynArray(files, &pooledPath);
    }

    globfree(&globResult);
    return true;
}

bool expandInputGlobs(const DynArray *inputs,
                      const char *packageDir,
                      DynArray *expandedFiles,
                      struct StrPool *strings,
                      Log *log)
{
    for (u32 i = 0; i < inputs->size; i++) {
        cstring pattern = ((cstring *)inputs->elems)[i];

        if (!expandSingleGlob(packageDir, pattern, expandedFiles, strings, log)) {
            return false;
        }
    }

    return true;
}

bool getFileModTime(const char *path, u64 *mtime, Log *log)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        // File doesn't exist or can't be accessed
        return false;
    }

    // Convert timespec to nanoseconds
    *mtime = timespecToNanoSeconds(&st.st_mtim);
    return true;
}

bool isPluginUpToDate(const char *entryFile,
                      const DynArray *inputs,
                      const char *packageDir,
                      const char *outputFile,
                      const DynArray *builtins,
                      struct StrPool *strings,
                      Log *log,
                      bool *upToDate)
{
    *upToDate = false;

    // If the output doesn't exist, rebuild unconditionally
    u64 outputMtime;
    if (!getFileModTime(outputFile, &outputMtime, log))
        return true;

    // Find the latest mtime across all inputs, starting with the entry file
    u64 latestInputMtime = 0;

    u64 entryMtime;
    if (!getFileModTime(entryFile, &entryMtime, log)) {
        // Entry file missing — let the build fail with a proper error
        return true;
    }
    latestInputMtime = entryMtime;

    // Expand and check any additional glob patterns
    if (inputs != NULL && inputs->size > 0) {
        // Substitute {{VAR}} in each pattern before glob expansion
        static const DynArray emptyEnv = {0};
        DynArray substituted = newDynArray(sizeof(cstring));
        for (u32 i = 0; i < inputs->size; i++) {
            cstring pattern = ((cstring *)inputs->elems)[i];
            cstring resolved = substituteEnvVars(pattern, &emptyEnv, builtins, strings, log);
            pushOnDynArray(&substituted, resolved ? &resolved : &pattern);
        }

        DynArray expanded = newDynArray(sizeof(cstring));
        bool ok = expandInputGlobs(&substituted, packageDir, &expanded, strings, log);
        freeDynArray(&substituted);

        if (!ok) {
            freeDynArray(&expanded);
            return false;
        }

        for (u32 i = 0; i < expanded.size; i++) {
            cstring file = ((cstring *)expanded.elems)[i];
            char fullPath[2048];
            if (file[0] == '/') {
                strncpy(fullPath, file, sizeof(fullPath) - 1);
                fullPath[sizeof(fullPath) - 1] = '\0';
            } else {
                snprintf(fullPath, sizeof(fullPath), "%s/%s", packageDir, file);
            }

            u64 mtime;
            if (!getFileModTime(fullPath, &mtime, log)) {
                // Missing input — treat as dirty
                freeDynArray(&expanded);
                return true;
            }
            if (mtime > latestInputMtime)
                latestInputMtime = mtime;
        }
        freeDynArray(&expanded);
    }

    // Up to date if the output is strictly newer than every input
    *upToDate = outputMtime > latestInputMtime;
    return true;
}

bool checkScriptCacheWithEnv(const PackageScript *script,
                             const char *packageDir,
                             const DynArray *envVars,
                             const DynArray *builtins,
                             struct StrPool *strings,
                             Log *log,
                             bool *isCached)
{
    *isCached = false;

    // No caching if no inputs or outputs defined
    if (script->inputs.size == 0 && script->outputs.size == 0) {
        // No cache configuration - always run
        return true;
    }

    // Warn if outputs specified but no inputs
    if (script->outputs.size > 0 && script->inputs.size == 0) {
        logWarning(log, NULL,
                  "script '{s}' has outputs but no inputs - will always run",
                  (FormatArg[]){{.s = script->name}});
        return true;
    }

    // If only inputs specified (no outputs), always run
    if (script->inputs.size > 0 && script->outputs.size == 0) {
        return true;
    }

    // Both inputs and outputs are specified - check cache
    
    // Substitute environment variables in inputs
    DynArray substitutedInputs = newDynArray(sizeof(cstring));
    for (u32 i = 0; i < script->inputs.size; i++) {
        cstring inputPattern = ((cstring *)script->inputs.elems)[i];
        cstring substituted = substituteEnvVars(inputPattern, envVars, builtins, strings, log);
        if (!substituted) {
            freeDynArray(&substitutedInputs);
            return false;
        }
        pushOnDynArray(&substitutedInputs, &substituted);
    }

    // Expand input globs to actual files
    DynArray inputFiles = newDynArray(sizeof(cstring));
    if (!expandInputGlobs(&substitutedInputs, packageDir, &inputFiles, strings, log)) {
        freeDynArray(&substitutedInputs);
        freeDynArray(&inputFiles);
        return false;
    }
    
    freeDynArray(&substitutedInputs);

    // If no input files matched, consider cache invalid (run script)
    if (inputFiles.size == 0) {
        freeDynArray(&inputFiles);
        return true;
    }
    
    // Substitute environment variables in outputs
    DynArray substitutedOutputs = newDynArray(sizeof(cstring));
    for (u32 i = 0; i < script->outputs.size; i++) {
        cstring outputPath = ((cstring *)script->outputs.elems)[i];
        cstring substituted = substituteEnvVars(outputPath, envVars, builtins, strings, log);
        if (!substituted) {
            freeDynArray(&inputFiles);
            freeDynArray(&substitutedOutputs);
            return false;
        }
        pushOnDynArray(&substitutedOutputs, &substituted);
    }

    // Find the latest modification time among input files
    u64 latestInputTime = 0;
    bool hasInputTime = false;

    for (u32 i = 0; i < inputFiles.size; i++) {
        cstring inputFile = ((cstring *)inputFiles.elems)[i];

        char fullPath[2048];
        if (inputFile[0] == '/') {
            strncpy(fullPath, inputFile, sizeof(fullPath) - 1);
        } else {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", packageDir, inputFile);
        }
        fullPath[sizeof(fullPath) - 1] = '\0';

        u64 mtime;
        if (!getFileModTime(fullPath, &mtime, log)) {
            // Input file doesn't exist - cache invalid
            freeDynArray(&inputFiles);
            return true;
        }

        if (!hasInputTime || mtime > latestInputTime) {
            latestInputTime = mtime;
            hasInputTime = true;
        }
    }

    freeDynArray(&inputFiles);

    if (!hasInputTime) {
        // No valid input times found
        return true;
    }

    // Find the earliest modification time among output files
    u64 earliestOutputTime = UINT64_MAX;
    bool hasOutputTime = false;

    for (u32 i = 0; i < substitutedOutputs.size; i++) {
        cstring outputFile = ((cstring *)substitutedOutputs.elems)[i];

        char fullPath[2048];
        if (outputFile[0] == '/') {
            strncpy(fullPath, outputFile, sizeof(fullPath) - 1);
        } else {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", packageDir, outputFile);
        }
        fullPath[sizeof(fullPath) - 1] = '\0';

        u64 mtime;
        if (!getFileModTime(fullPath, &mtime, log)) {
            // Output file doesn't exist - cache invalid, need to run
            freeDynArray(&substitutedOutputs);
            return true;
        }

        if (!hasOutputTime || mtime < earliestOutputTime) {
            earliestOutputTime = mtime;
            hasOutputTime = true;
        }
    }

    if (!hasOutputTime) {
        // No valid output times found (shouldn't happen as we check existence)
        freeDynArray(&substitutedOutputs);
        return true;
    }

    // Cache is valid if all outputs are newer than all inputs
    *isCached = (earliestOutputTime > latestInputTime);
    
    freeDynArray(&substitutedOutputs);
    return true;
}

bool checkScriptCache(const PackageScript *script,
                      const char *packageDir,
                      struct StrPool *strings,
                      Log *log,
                      bool *isCached)
{
    // No environment variables - call with empty arrays
    DynArray emptyEnv = newDynArray(sizeof(EnvVar));
    DynArray emptyBuiltins = newDynArray(sizeof(EnvVar));

    bool result = checkScriptCacheWithEnv(script, packageDir, &emptyEnv, &emptyBuiltins, strings, log, isCached);

    freeDynArray(&emptyEnv);
    freeDynArray(&emptyBuiltins);

    return result;
}
