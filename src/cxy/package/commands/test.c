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
#include <glob.h>
#include <dirent.h>

typedef struct TestResult {
    cstring testFile;
    bool passed;
    int exitCode;
    cstring output;  // Captured stdout/stderr
} TestResult;

/**
 * Simple glob pattern matching for filenames (supports * wildcard)
 */
static bool matchesPattern(const char *filename, const char *pattern) {
    // If pattern is *.cxy, match any file ending with .cxy
    if (pattern[0] == '*') {
        const char *suffix = pattern + 1;
        size_t filenameLen = strlen(filename);
        size_t suffixLen = strlen(suffix);
        if (filenameLen >= suffixLen) {
            return strcmp(filename + filenameLen - suffixLen, suffix) == 0;
        }
        return false;
    }
    // Otherwise, exact match
    return strcmp(filename, pattern) == 0;
}

/**
 * Recursively find .cxy files in a directory
 */
static bool findCxyFilesRecursive(const char *dir,
                                  const char *filePattern,
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
            if (!findCxyFilesRecursive(fullPath, filePattern, files, strings, log)) {
                closedir(d);
                return false;
            }
        }
        else if (S_ISREG(st.st_mode)) {
            // Check if file matches the pattern
            if (matchesPattern(entry->d_name, filePattern)) {
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
        char filePattern[256];
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

        // Extract the filename pattern after **/
        const char *afterDoubleStar = doubleStarPos + 2;
        if (afterDoubleStar[0] == '/') {
            afterDoubleStar++;
        }
        strncpy(filePattern, afterDoubleStar, sizeof(filePattern) - 1);
        filePattern[sizeof(filePattern) - 1] = '\0';

        // Build full base directory path
        char fullBaseDir[2048];
        snprintf(fullBaseDir, sizeof(fullBaseDir), "%s/%s", packageDir, baseDir);

        // Recursively find all .cxy files matching the pattern
        size_t beforeCount = files->size;
        if (!findCxyFilesRecursive(fullBaseDir, filePattern, files, strings, log)) {
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
                        const DynArray *installFlags, // from .install.yaml
                        const char *buildDir,
                        TestResult *result,
                        StrPool *strings,
                        Log *log,
                        bool verbose)
{
    result->testFile = testFile;
    result->passed = false;
    result->exitCode = -1;
    result->output = NULL;

    // Generate unique output path in /tmp to avoid overwriting app binaries
    char outputPath[2048];
    snprintf(outputPath, sizeof(outputPath), "/tmp/cxy-test-%d", getpid());

    // Build command: cxy test <file> -o <unique-path>
    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cxy test {s} -o {s}", (FormatArg[]){{.s = testFile}, {.s = outputPath}});
    // Add --no-progress to disable interactive progress indicators when piped
    format(&cmd, " --no-progress", NULL);

    // Add build directory if specified
    if (buildDir && buildDir[0] != '\0') {
        format(&cmd, " --build-dir {s}", (FormatArg[]){{.s = buildDir}});
        // Default --plugins-dir to buildDir/plugins so plugins installed via
        // cxy package install are visible to the test runner (mirrors build.c).
        format(&cmd, " --plugins-dir={s}/plugins", (FormatArg[]){{.s = buildDir}});
    }

    // Inject flags from .install.yaml
    if (installFlags && installFlags->size > 0) {
        for (u32 i = 0; i < installFlags->size; i++) {
            cstring flag = ((cstring *)installFlags->elems)[i];
            format(&cmd, " {s}", (FormatArg[]){{.s = flag}});
        }
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

    if (verbose) {
        snprintf(header, sizeof(header), "Running test " cCYN "'\033[3m%s\033[0m'", testCommand);
    } else {
        snprintf(header, sizeof(header), "Running test " cCYN "\033[3m%s\033[0m", testFile);
    }

    // Execute test with progress indicator
    bool success = runCommandWithProgressFull(header, testCommand, log, verbose);

    result->exitCode = success ? 0 : 1;
    result->passed = success;

    // Note: runCommandWithProgress doesn't capture output, so we don't have detailed output
    // This is acceptable since the command output is shown during execution
    result->output = NULL;

    free(testCommand);

    // Clean up all compiler-generated files (binary, .c, .o, etc.)
    // Use a glob pattern to match <output-path>.*
    FormatState cleanupCmd = newFormatState(NULL, true);
    format(&cleanupCmd, "rm -f \"{s}\" \"{s}\".*", (FormatArg[]){{.s = outputPath}, {.s = outputPath}});
    char *cleanupCmdStr = formatStateToString(&cleanupCmd);
    freeFormatState(&cleanupCmd);

    if (system(cleanupCmdStr) != 0) {
        // Log warning but don't fail the test if cleanup fails
        logWarning(log, NULL, "failed to clean up test artifacts", NULL);
    }

    free(cleanupCmdStr);

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
    bool verbose = options->package.verbose;
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

    // Auto-run install scripts if needed (install sections defined but .install.dev.yaml missing or stale)
    if (meta.install.size > 0 || meta.installDev.size > 0) {
        char installYamlPath[2048];
        snprintf(installYamlPath, sizeof(installYamlPath), "%s/.install.dev.yaml", buildDir);

        struct stat installSt, cxyfileSt;
        char cxyfilePath[2048];
        snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packageDir);

        bool needsInstall = (stat(installYamlPath, &installSt) != 0);

        // Also re-run if Cxyfile.yaml is newer than .install.dev.yaml
        if (!needsInstall &&
            stat(cxyfilePath, &cxyfileSt) == 0 &&
            cxyfileSt.st_mtime > installSt.st_mtime) {
            needsInstall = true;
            printStatusSticky(log, "Cxyfile.yaml changed, re-running install scripts...");
        }

        if (needsInstall) {
            printStatusSticky(log, "Running install scripts (with dev dependencies)...");
            const char *packagesDirOpt = options->package.packagesDir;
            char defaultPackagesDir[1024];
            if (!packagesDirOpt || packagesDirOpt[0] == '\0') {
                snprintf(defaultPackagesDir, sizeof(defaultPackagesDir), "%s/.cxy/packages", packageDir);
                packagesDirOpt = defaultPackagesDir;
            }
            if (!executeInstallScripts(&meta, packageDir, packagesDirOpt, buildDir, true, strings, log, verbose)) {
                logError(log, NULL, "install scripts failed", NULL);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }
        }
    }

    // Read install flags from .install.dev.yaml (with fallback to .install.yaml)
    DynArray installFlags = newDynArray(sizeof(cstring));
    readInstallYamlFlags(buildDir, &installFlags, true, strings, log);

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
        // Prioritize exact matches over pattern matches for specificity
        PackageTest *testConfig = NULL;
        PackageTest *patternMatch = NULL;
        
        // First pass: look for exact matches
        for (u32 j = 0; j < meta.tests.size; j++) {
            PackageTest *t = &((PackageTest *)meta.tests.elems)[j];
            
            if (!t->isPattern && strcmp(t->file, testFile) == 0) {
                testConfig = t;
                break;
            }
        }
        
        // Second pass: if no exact match, look for pattern matches
        if (!testConfig) {
            for (u32 j = 0; j < meta.tests.size; j++) {
                PackageTest *t = &((PackageTest *)meta.tests.elems)[j];
                
                if (t->isPattern) {
                    // For patterns, check if this file was expanded from this pattern
                    // by seeing if the pattern structure matches
                    if (strstr(t->file, "**") != NULL) {
                        // Extract filename from pattern (part after **/)
                        const char *patternFile = strrchr(t->file, '/');
                        if (patternFile) {
                            patternFile++; // Skip the /
                        } else {
                            patternFile = t->file;
                        }
                        
                        // Extract filename from test file path
                        const char *testFileName = strrchr(testFile, '/');
                        if (testFileName) {
                            testFileName++; // Skip the /
                        } else {
                            testFileName = testFile;
                        }
                        
                        // Check if filenames match using pattern matching
                        if (matchesPattern(testFileName, patternFile)) {
                            patternMatch = t;
                            break;
                        }
                    } else {
                        // Non-** pattern - try basic glob matching
                        // For now, use simple string comparison as fallback
                        if (strcmp(t->file, testFile) == 0) {
                            patternMatch = t;
                            break;
                        }
                    }
                }
            }
            testConfig = patternMatch;
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
        if (!runTestFile(testFile, testConfig, restArgs, &installFlags, buildDir,
                        &result, strings, log, verbose)) {
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
    freeDynArray(&installFlags);
    free(packageDir);
    freePackageMetadata(&meta);

    return failedCount == 0;
}
