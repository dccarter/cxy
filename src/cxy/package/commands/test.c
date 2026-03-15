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
#include <errno.h>
#include <glob.h>
#include <dirent.h>

typedef struct TestResult {
    cstring testFile;
    bool passed;
    int exitCode;
    cstring output;  // Captured stdout/stderr
} TestResult;

/**
 * Recursively find .cxy files in a directory
 */
static bool findCxyFilesRecursive(const char *dir,
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
            if (!findCxyFilesRecursive(fullPath, suffix, files, strings, log)) {
                closedir(d);
                return false;
            }
        }
        else if (S_ISREG(st.st_mode)) {
            // Check if file matches suffix
            size_t nameLen = strlen(entry->d_name);
            size_t suffixLen = strlen(suffix);

            if (nameLen >= suffixLen && strcmp(entry->d_name + nameLen - suffixLen, suffix) == 0) {
                cstring pooledPath = makeString(strings, fullPath);
                pushOnDynArray(files, &pooledPath);
            }
        }
    }

    closedir(d);
    return true;
}

/**
 * Helper function to expand glob pattern
 * Supports ** for recursive directory traversal
 */
static bool expandGlobPattern(const char *packageDir,
                               const char *pattern,
                               DynArray *files,  // Array of cstring
                               StrPool *strings,
                               Log *log)
{
    // Check if pattern contains **
    if (strstr(pattern, "**") != NULL) {
        // Handle ** pattern by recursively searching
        // Extract base directory and file pattern
        char baseDir[2048];
        const char *suffix = ".cxy";

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

        // Build full base directory path
        char fullBaseDir[2048];
        snprintf(fullBaseDir, sizeof(fullBaseDir), "%s/%s", packageDir, baseDir);

        // Recursively find all .cxy files
        size_t beforeCount = files->size;
        if (!findCxyFilesRecursive(fullBaseDir, suffix, files, strings, log)) {
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
    snprintf(fullPattern, sizeof(fullPattern), "%s/%s", packageDir, pattern);

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

        // Only .cxy files
        if (len < 4 || strcmp(path + len - 4, ".cxy") != 0)
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

/**
 * Collect test files based on CLI args or Cxyfile
 */
static bool collectTestFilesToRun(const Options *options,
                                  const PackageMetadata *meta,
                                  const char *packageDir,
                                  DynArray *testFiles,
                                  StrPool *strings,
                                  Log *log)
{
    const DynArray *specificFiles = &options->package.testFiles;
    const char *filter = options->package.filter;

    // If specific files provided via CLI, use those
    if (specificFiles->size > 0) {
        for (u32 i = 0; i < specificFiles->size; i++) {
            const char *file = ((const char **)specificFiles->elems)[i];

            // Verify file exists
            char fullPath[2048];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", packageDir, file);

            struct stat st;
            if (stat(fullPath, &st) != 0 || !S_ISREG(st.st_mode)) {
                logError(log, NULL, "test file not found: {s}",
                        (FormatArg[]){{.s = file}});
                return false;
            }

            // Apply filter if specified
            if (filter && filter[0] != '\0' && !strstr(file, filter)) {
                continue;
            }

            cstring pooledPath = makeString(strings, file);
            pushOnDynArray(testFiles, &pooledPath);
        }
        return true;
    }

    // Otherwise, use tests from Cxyfile.yaml
    for (u32 i = 0; i < meta->tests.size; i++) {
        PackageTest *test = &((PackageTest *)meta->tests.elems)[i];

        if (test->isPattern) {
            // Expand glob pattern
            if (!expandGlobPattern(packageDir, test->file, testFiles, strings, log)) {
                return false;
            }
        }
        else {
            // Simple file path
            char fullPath[2048];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", packageDir, test->file);

            struct stat st;
            if (stat(fullPath, &st) != 0 || !S_ISREG(st.st_mode)) {
                logWarning(log, NULL, "test file not found: {s}",
                          (FormatArg[]){{.s = test->file}});
                continue;
            }

            // Apply filter if specified
            if (filter && filter[0] != '\0' && !strstr(test->file, filter)) {
                continue;
            }

            cstring pooledPath = makeString(strings, test->file);
            pushOnDynArray(testFiles, &pooledPath);
        }
    }

    return true;
}

/**
 * Run a test file using cxy test command
 */
static bool runTestFile(const char *testFile,
                        const PackageTest *testConfig,
                        const DynArray *restArgs,  // from options->package.rest (after --)
                        const char *buildDir,
                        TestResult *result,
                        StrPool *strings,
                        Log *log)
{
    result->testFile = testFile;
    result->passed = false;
    result->exitCode = -1;
    result->output = NULL;

    // Build command: cxy test <file>
    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cxy test {s}", (FormatArg[]){{.s = testFile}});

    // Add build directory if specified
    if (buildDir && buildDir[0] != '\0') {
        format(&cmd, " --build-dir {s}", (FormatArg[]){{.s = buildDir}});
    }

    // Add test-specific arguments from Cxyfile
    if (testConfig && testConfig->args.size > 0) {
        for (u32 i = 0; i < testConfig->args.size; i++) {
            cstring arg = ((cstring *)testConfig->args.elems)[i];
            format(&cmd, " {s}", (FormatArg[]){{.s = arg}});
        }
    }

    // Add rest arguments from command line (after --)
    if (restArgs && restArgs->size > 0) {
        for (u32 i = 0; i < restArgs->size; i++) {
            const char *arg = ((const char **)restArgs->elems)[i];
            
            // Validate: argument must start with '-'
            if (arg[0] != '-') {
                logError(log, NULL, 
                        "invalid argument '{s}': arguments must start with '-'",
                        (FormatArg[]){{.s = arg}});
                freeFormatState(&cmd);
                return false;
            }
            
            format(&cmd, " {s}", (FormatArg[]){{.s = arg}});
        }
    }

    char *testCommand = formatStateToString(&cmd);
    freeFormatState(&cmd);

    // Build header with formatted test path (cyan italic)
    char header[2048];
    snprintf(header, sizeof(header), "Running test " cCYN "\033[3m%s\033[0m", testFile);

    // Execute test with progress indicator
    bool success = runCommandWithProgress(header, testCommand, log);

    result->exitCode = success ? 0 : 1;
    result->passed = success;

    // Note: runCommandWithProgress doesn't capture output, so we don't have detailed output
    // This is acceptable since the command output is shown during execution
    result->output = NULL;

    free(testCommand);

    return true;
}

/**
 * Package test command implementation
 */
bool packageTestCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *buildDir = options->package.buildDir;
    const char *filter = options->package.filter;
    int parallel = options->package.parallel;
    const DynArray *specificTestFiles = &options->package.testFiles;
    const DynArray *restArgs = &options->package.rest;

    // Default build directory if not specified
    char defaultBuildDir[2048];
    bool useDefaultBuildDir = false;
    if (!buildDir || buildDir[0] == '\0') {
        useDefaultBuildDir = true;
    }

    // Load Cxyfile.yaml
    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL, "no Cxyfile.yaml found", NULL);
        return false;
    }

    // Set default build dir if needed
    if (useDefaultBuildDir) {
        snprintf(defaultBuildDir, sizeof(defaultBuildDir),
                "%s/.cxy/build", packageDir);
        buildDir = defaultBuildDir;
    }

    printStatusSticky(log, "Running tests for package '%s'...", meta.name);

    // Handle parallel warning
    if (parallel > 1) {
        logWarning(log, NULL,
                  "parallel test execution (--parallel/-j) not yet implemented, "
                  "running tests sequentially",
                  NULL);
    }

    // Collect test files
    DynArray testFiles = newDynArray(sizeof(cstring));
    if (!collectTestFilesToRun(options, &meta, packageDir, &testFiles,
                               strings, log)) {
        freeDynArray(&testFiles);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (testFiles.size == 0) {
        if (specificTestFiles->size > 0) {
            logWarning(log, NULL, "no test files matched the filter", NULL);
        } else if (meta.tests.size == 0) {
            logWarning(log, NULL, "no tests defined in Cxyfile.yaml", NULL);
        } else {
            logWarning(log, NULL, "no test files found", NULL);
        }
        freeDynArray(&testFiles);
        free(packageDir);
        freePackageMetadata(&meta);
        return true;
    }

    printStatusSticky(log, "Found %u test file(s)", testFiles.size);

    // Ensure build directory exists if specified
    if (buildDir && buildDir[0] != '\0') {
        if (!makeDirectory(buildDir, true)) {
            logWarning(log, NULL, "failed to create build directory: {s}",
                      (FormatArg[]){{.s = strerror(errno)}});
        }
    }

    // Change to package directory
    char originalDir[1024];
    if (!getcwd(originalDir, sizeof(originalDir))) {
        logError(log, NULL, "failed to get current directory", NULL);
        freeDynArray(&testFiles);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (chdir(packageDir) != 0) {
        logError(log, NULL, "failed to change to package directory", NULL);
        freeDynArray(&testFiles);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    // Run tests
    DynArray results = newDynArray(sizeof(TestResult));
    u32 passedCount = 0;
    u32 failedCount = 0;

    for (u32 i = 0; i < testFiles.size; i++) {
        cstring testFile = ((cstring *)testFiles.elems)[i];

        // Find corresponding test config from Cxyfile (for args)
        PackageTest *testConfig = NULL;
        for (u32 j = 0; j < meta.tests.size; j++) {
            PackageTest *t = &((PackageTest *)meta.tests.elems)[j];
            if (strcmp(t->file, testFile) == 0) {
                testConfig = t;
                break;
            }
        }

        // Default empty config if not found
        PackageTest defaultConfig = {
            .file = testFile,
            .args = newDynArray(sizeof(cstring)),
            .isPattern = false
        };
        if (!testConfig)
            testConfig = &defaultConfig;

        // Run test using cxy test command
        TestResult result;
        if (!runTestFile(testFile, testConfig, restArgs, buildDir,
                        &result, strings, log)) {
            failedCount++;
            if (testConfig == &defaultConfig)
                freeDynArray(&defaultConfig.args);
            continue;
        }

        pushOnDynArray(&results, &result);

        if (result.passed) {
            passedCount++;
        } else {
            failedCount++;
        }

        if (testConfig == &defaultConfig)
            freeDynArray(&defaultConfig.args);
    }

    // Restore directory
    if (chdir(originalDir) != 0) {
        logWarning(log, NULL, "failed to restore original directory", NULL);
    }

    // Print summary
    printStatusAlways(log, "\n" cBOLD "Test Summary:" cDEF);
    printStatusAlways(log, "  Total:  %u", passedCount + failedCount);
    printStatusAlways(log, "  " cBGRN "Passed: %u" cDEF, passedCount);
    if (failedCount > 0) {
        printStatusAlways(log, "  " cBRED "Failed: %u" cDEF, failedCount);
    }

    // Clean up
    freeDynArray(&results);
    freeDynArray(&testFiles);
    free(packageDir);
    freePackageMetadata(&meta);

    return failedCount == 0;
}
