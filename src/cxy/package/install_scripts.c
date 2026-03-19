/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-20
 */

#include "package/install_scripts.h"
#include "package/types.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

/**
 * Parse a single line of script output and add to flags if valid
 */
static bool parseOutputLine(const char *line, DynArray *flags, StrPool *strings)
{
    // Skip empty lines and whitespace
    while (*line == ' ' || *line == '\t') {
        line++;
    }

    if (*line == '\0' || *line == '\n') {
        return true;
    }

    // Check if line starts with a valid flag
    if (strncmp(line, "--c-header-dir=", 15) == 0 ||
        strncmp(line, "--c-lib-dir=", 12) == 0 ||
        strncmp(line, "--c-lib=", 8) == 0 ||
        strncmp(line, "--c-flag=", 9) == 0 ||
        strncmp(line, "--c-define=", 11) == 0 ||
        strncmp(line, "--define=", 9) == 0) {

        // Remove trailing newline - use stack allocation
        char clean[4096];
        strncpy(clean, line, sizeof(clean) - 1);
        clean[sizeof(clean) - 1] = '\0';

        size_t len = strlen(clean);
        if (len > 0 && clean[len - 1] == '\n') {
            clean[len - 1] = '\0';
        }

        cstring flag = makeString(strings, clean);
        pushOnDynArray(flags, &flag);
        return true;
    }

    // Ignore non-flag lines (could be informational output)
    return true;
}

/**
 * Execute an install script and capture its output
 */
static bool executeInstallScript(const PackageInstallScript *script,
                                 const char *packageDir,
                                 DynArray *flags,
                                 StrPool *strings,
                                 Log *log,
                                 bool verbose)
{
    char originalDir[1024];
    if (!getcwd(originalDir, sizeof(originalDir))) {
        logError(log, NULL, "failed to get current directory", NULL);
        return false;
    }

    // Change to package directory to run script
    if (chdir(packageDir) != 0) {
        logError(log, NULL, "failed to change to package directory: {s}",
                (FormatArg[]){{.s = packageDir}});
        return false;
    }

    // Check if script is a file path or inline script
    bool isFilePath = false;
    struct stat st;
    if (stat(script->script, &st) == 0 && S_ISREG(st.st_mode)) {
        isFilePath = true;
    }

    // Build command to execute
    char command[4096];
    char tmpScriptPath[256] = {0};

    if (isFilePath) {
        snprintf(command, sizeof(command), "chmod +x '%s' && '%s'",
                script->script, script->script);
    } else {
        // Write inline script to a temp file to avoid newline/quoting issues
        snprintf(tmpScriptPath, sizeof(tmpScriptPath), "/tmp/cxy-install-%d.sh", getpid());
        FILE *tmpFp = fopen(tmpScriptPath, "w");
        if (!tmpFp) {
            logError(log, NULL, "failed to create temp script file for '{s}'",
                    (FormatArg[]){{.s = script->name}});
            chdir(originalDir);
            return false;
        }
        fprintf(tmpFp, "#!/bin/sh\nset -e\n%s\n", script->script);
        fclose(tmpFp);
        chmod(tmpScriptPath, 0700);
        snprintf(command, sizeof(command), "/bin/sh '%s'", tmpScriptPath);
    }

    // Execute command and capture output
    FILE *fp = popen(command, "r");
    if (!fp) {
        logError(log, NULL, "failed to execute script for '{s}'",
                (FormatArg[]){{.s = script->name}});
        chdir(originalDir);
        return false;
    }

    // Read output line by line, collecting flags
    char line[4096];
    u32 flagsBefore = flags->size;

    while (fgets(line, sizeof(line), fp)) {
        if (verbose) {
            printf("     " cWHT "%s" cDEF, line);
            fflush(stdout);
        }
        parseOutputLine(line, flags, strings);
    }

    // Get exit status
    int status = pclose(fp);
    int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Restore original directory and clean up temp script
    chdir(originalDir);
    if (tmpScriptPath[0] != '\0') {
        unlink(tmpScriptPath);
    }

    if (exitCode != 0) {
        if (script->required) {
            logError(log, NULL, "required install script '{s}' failed with exit code {i}",
                    (FormatArg[]){{.s = script->name}, {.i = exitCode}});
            return false;
        } else {
            logWarning(log, NULL, "optional install script '{s}' failed with exit code {i}",
                      (FormatArg[]){{.s = script->name}, {.i = exitCode}});
            return true;
        }
    }

    u32 collected = flags->size - flagsBefore;
    if (verbose && collected > 0) {
        printStatusSticky(log, "     " cBGRN "✔" cDEF " Collected %u flag%s",
                         collected, collected == 1 ? "" : "s");
    }

    return true;
}

/**
 * Write .install.yaml or .install.dev.yaml file with collected flags
 */
static bool writeInstallYaml(const char *buildDir,
                             const char *packageName,
                             const DynArray *flags,
                             bool useDev,
                             Log *log)
{
    char installYamlPath[1024];
    const char *filename = useDev ? ".install.dev.yaml" : ".install.yaml";
    snprintf(installYamlPath, sizeof(installYamlPath), "%s/%s", buildDir, filename);

    FILE *fp = fopen(installYamlPath, "w");
    if (!fp) {
        logError(log, NULL, "failed to create {s}: {s}",
                (FormatArg[]){{.s = filename}, {.s = strerror(errno)}});
        return false;
    }

    fprintf(fp, "# Generated by cxy package install%s\n", useDev ? " --dev" : "");
    fprintf(fp, "# DO NOT EDIT MANUALLY - will be regenerated on install\n");
    fprintf(fp, "\n");
    fprintf(fp, "packages:\n");
    fprintf(fp, "  %s:\n", packageName);
    fprintf(fp, "    flags:\n");

    for (u32 i = 0; i < flags->size; i++) {
        cstring flag = ((cstring *)flags->elems)[i];
        fprintf(fp, "      - %s\n", flag);
    }

    fclose(fp);
    return true;
}

/**
 * Execute install scripts and generate .install.yaml
 */
bool executeInstallScripts(const PackageMetadata *meta,
                           const char *packageDir,
                           const char *buildDir,
                           bool includeDev,
                           StrPool *strings,
                           Log *log,
                           bool verbose)
{
    if (meta->install.size == 0 && (!includeDev || meta->installDev.size == 0)) {
        // No install scripts to run
        return true;
    }

    DynArray allFlags = newDynArray(sizeof(cstring));

    u32 successCount = 0;
    u32 failCount = 0;
    u32 totalScripts = meta->install.size + (includeDev ? meta->installDev.size : 0);

    // Execute production install scripts
    if (meta->install.size > 0) {
        for (u32 i = 0; i < meta->install.size; i++) {
            PackageInstallScript *script = &((PackageInstallScript *)meta->install.elems)[i];
            printf(" " cCYN "►" cDEF " [%u/%u] %s " cWHT "(%s)\n" cDEF,
                    i + 1, totalScripts,
                    script->name,
                    script->required ? "required" : "optional");
            fflush(stdout);

            if (executeInstallScript(script, packageDir, &allFlags, strings, log, verbose)) {
                successCount++;
                if (!verbose) {
                    printf("\033[1A\033[2K " cBGRN "✔" cDEF " [%u/%u] %s " cWHT "(%s)\n" cDEF,
                           i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                } else {
                    printf(" " cBGRN "✔" cDEF " [%u/%u] %s " cWHT "(%s)" cDEF "\n",
                           i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                }
            } else {
                failCount++;
                if (!verbose) {
                    printf("\033[1A\033[2K " cBRED "✗" cDEF " [%u/%u] %s " cWHT "(%s)" cDEF "\n",
                           i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                } else {
                    printf(" " cBRED "✗" cDEF " [%u/%u] %s " cWHT "(%s)" cDEF "\n",
                           i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                }
                if (script->required) {
                    goto cleanup;
                }
            }
        }
    }

    // Execute dev install scripts if requested
    if (includeDev && meta->installDev.size > 0) {
        for (u32 i = 0; i < meta->installDev.size; i++) {
            PackageInstallScript *script = &((PackageInstallScript *)meta->installDev.elems)[i];
            // Print without newline so we can overwrite in-place on completion
            printf(" " cCYN "►" cDEF " [%u/%u] %s " cYLW "(dev, %s)\n" cDEF,
                    (u32)(meta->install.size) + i + 1, totalScripts,
                    script->name,
                    script->required ? "required" : "optional");
            fflush(stdout);

            if (executeInstallScript(script, packageDir, &allFlags, strings, log, verbose)) {
                successCount++;
                if (!verbose) {
                    printf("\r\033[K " cBGRN "✔" cDEF " [%u/%u] %s " cYLW "(dev, %s)" cDEF "\n",
                           (u32)(meta->install.size) + i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                } else {
                    printf(" " cBGRN "✔" cDEF " [%u/%u] %s " cYLW "(dev, %s)" cDEF "\n",
                           (u32)(meta->install.size) + i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                }
            } else {
                failCount++;
                if (!verbose) {
                    printf("\r\033[K " cBRED "✗" cDEF " [%u/%u] %s " cYLW "(dev, %s)" cDEF "\n",
                           (u32)(meta->install.size) + i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                } else {
                    printf(" " cBRED "✗" cDEF " [%u/%u] %s " cYLW "(dev, %s)" cDEF "\n",
                           (u32)(meta->install.size) + i + 1, totalScripts,
                           script->name,
                           script->required ? "required" : "optional");
                    fflush(stdout);
                }
                if (script->required) {
                    goto cleanup;
                }
            }
        }
    }

cleanup:
    ;  // Empty statement required after label in C99

    if (failCount == 0) {
        // Generate .install.yaml or .install.dev.yaml
        if (!makeDirectory(buildDir, true)) {
            logError(log, NULL, "failed to create build directory: {s}",
                    (FormatArg[]){{.s = strerror(errno)}});
            freeDynArray(&allFlags);
            return false;
        }

        if (allFlags.size > 0) {
            const char *filename = includeDev ? ".install.dev.yaml" : ".install.yaml";
            char installYamlPath[1024];
            snprintf(installYamlPath, sizeof(installYamlPath), "%s/%s", buildDir, filename);

            if (writeInstallYaml(buildDir, meta->name, &allFlags, includeDev, log)) {
                printStatusAlways(log, "\n " cBGRN "✔" cDEF " Install complete — %u flag%s written to " cCYN "%s" cDEF "\n",
                                 allFlags.size, allFlags.size == 1 ? "" : "s",
                                 installYamlPath);
            } else {
                freeDynArray(&allFlags);
                return false;
            }
        } else {
            printStatusAlways(log, "\n " cBGRN "✔" cDEF " Install complete — no flags collected\n");
        }
    } else {
        printStatusAlways(log, "\n " cBRED "✗" cDEF " Install scripts: %u/%u failed\n",
                         failCount, totalScripts);
    }

    freeDynArray(&allFlags);
    return failCount == 0;
}

/**
 * Read flags from .install.yaml or .install.dev.yaml for use during build/test
 * If useDev is true, tries .install.dev.yaml first, then falls back to .install.yaml
 */
bool readInstallYamlFlags(const char *buildDir,
                         DynArray *flags,
                         bool useDev,
                         StrPool *strings,
                         Log *log)
{
    char installYamlPath[1024];
    const char *filename = useDev ? ".install.dev.yaml" : ".install.yaml";
    snprintf(installYamlPath, sizeof(installYamlPath), "%s/%s", buildDir, filename);

    // Check if file exists
    struct stat st;
    if (stat(installYamlPath, &st) != 0) {
        // If dev file doesn't exist, try falling back to regular install.yaml
        if (useDev) {
            snprintf(installYamlPath, sizeof(installYamlPath), "%s/.install.yaml", buildDir);
            if (stat(installYamlPath, &st) != 0) {
                // Neither file exists - not an error, just no flags to add
                return true;
            }
        } else {
            // File doesn't exist - not an error, just no flags to add
            return true;
        }
    }

    FILE *fp = fopen(installYamlPath, "r");
    if (!fp) {
        // Can't open file - not a fatal error
        return true;
    }

    char line[4096];
    bool inFlags = false;

    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        // Check if we're in the flags section
        if (strstr(line, "flags:") != NULL) {
            inFlags = true;
            continue;
        }

        // Parse flag lines (indented with spaces or tabs, starting with -)
        if (inFlags && (line[0] == ' ' || line[0] == '\t')) {
            char *flagStart = strchr(line, '-');
            if (flagStart) {
                flagStart++; // Skip the '-'
                while (*flagStart == ' ' || *flagStart == '\t') {
                    flagStart++;
                }

                // Remove trailing newline
                size_t len = strlen(flagStart);
                if (len > 0 && flagStart[len - 1] == '\n') {
                    flagStart[len - 1] = '\0';
                }

                if (flagStart[0] != '\0') {
                    cstring flag = makeString(strings, flagStart);
                    pushOnDynArray(flags, &flag);
                }
            }
        } else if (inFlags && line[0] != ' ' && line[0] != '\t') {
            // End of flags section
            break;
        }
    }

    fclose(fp);
    return true;
}
