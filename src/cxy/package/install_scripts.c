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
#include "package/cache.h"
#include "package/env.h"
#include "package/types.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
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
                                 const DynArray *builtins,
                                 StrPool *strings,
                                 Log *log,
                                 bool verbose)
{
    // Plugin build path — compile a .c file into a shared library via cxy build --plugin
    if (script->isPlugin) {
        static const DynArray emptyEnv = {0};

        // Resolve entry path (may contain {{VAR}} references)
        cstring entry = substituteEnvVars(script->pluginEntry, &emptyEnv, builtins, strings, log);
        if (!entry) {
            logError(log, NULL, "failed to substitute variables in plugin entry for '{s}'",
                    (FormatArg[]){{.s = script->name}});
            return false;
        }

        // Find BUILD_DIR from builtins for the output path
        cstring buildDir = NULL;
        for (u32 i = 0; i < builtins->size; i++) {
            const EnvVar *v = &((const EnvVar *)builtins->elems)[i];
            if (strcmp(v->name, "BUILD_DIR") == 0) {
                buildDir = v->value;
                break;
            }
        }

        // Check if the plugin is already up to date before building
        if (buildDir && buildDir[0] != '\0') {
            char outputFile[2048];
            snprintf(outputFile, sizeof(outputFile), "%s/plugins/%s", buildDir, script->name);

            char entryFile[2048];
            snprintf(entryFile, sizeof(entryFile), "%s/%s", packageDir, entry);

            bool upToDate = false;
            if (isPluginUpToDate(entryFile, &script->pluginInputs, packageDir,
                                 outputFile, builtins, strings, log, &upToDate)
                && upToDate) {
                if (verbose) {
                    printf("     " cYLW "⊙" cDEF " Plugin '%s' is up to date, skipping build\n",
                           script->name);
                    fflush(stdout);
                }
                return true;
            }
        }

        // Build the full entry path (relative to packageDir)
        FormatState cmdState = newFormatState(NULL, true);
        format(&cmdState, "cxy build --plugin {s}/{s} --no-progress",
               (FormatArg[]){{.s = packageDir}, {.s = entry}});

        // Output name only (-o name); build-dir controls where the file lands
        // (cxy build --plugin rejects absolute -o paths)
        format(&cmdState, " -o {s}", (FormatArg[]){{.s = script->name}});
        if (buildDir && buildDir[0] != '\0') {
            format(&cmdState, " --build-dir {s}", (FormatArg[]){{.s = buildDir}});
        }

        // Append any extra args (each may contain {{VAR}} references)
        for (u32 i = 0; i < script->pluginArgs.size; i++) {
            cstring rawArg = ((cstring *)script->pluginArgs.elems)[i];
            cstring arg = substituteEnvVars(rawArg, &emptyEnv, builtins, strings, log);
            format(&cmdState, " {s}", (FormatArg[]){{.s = arg ? arg : rawArg}});
        }

        char *cmd = formatStateToString(&cmdState);
        freeFormatState(&cmdState);

        char header[2048];
        if (verbose)
            snprintf(header, sizeof(header), "Building plugin " cCYN "'\033[3m%s\033[0m'", cmd);
        else
            snprintf(header, sizeof(header), "Building plugin '%s'", script->name);
        bool success = runCommandWithProgressFull(header, cmd, log, verbose);
        free(cmd);

        if (!success && script->required) {
            logError(log, NULL, "required plugin build '{s}' failed",
                    (FormatArg[]){{.s = script->name}});
        } else if (!success) {
            logWarning(log, NULL, "optional plugin build '{s}' failed",
                      (FormatArg[]){{.s = script->name}});
            return true; // optional failure is not fatal
        }
        return success;
    }

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

    // Substitute {{VAR}} placeholders in the script path/content
    static const DynArray emptyEnv = {0};
    cstring scriptContent = substituteEnvVars(script->script, &emptyEnv, builtins, strings, log);
    if (!scriptContent) {
        logError(log, NULL, "failed to substitute variables in install script '{s}'",
                (FormatArg[]){{.s = script->name}});
        chdir(originalDir);
        return false;
    }

    // Expose builtins as real environment variables so the shell script can
    // access them as $BUILD_DIR, $PACKAGE_DIR, $PACKAGE_SOURCE_DIR, etc.
    setScriptEnvironment(&emptyEnv, builtins, log);

    // Check if script is a file path or inline script
    bool isFilePath = false;
    struct stat st;
    if (stat(scriptContent, &st) == 0 && S_ISREG(st.st_mode)) {
        isFilePath = true;
    }

    // Build command to execute
    char command[4096];
    char tmpScriptPath[256] = {0};

    if (isFilePath) {
        snprintf(command, sizeof(command), "chmod +x '%s' && '%s'",
                scriptContent, scriptContent);
    } else {
        // Write inline script to a temp file to avoid newline/quoting issues
        snprintf(tmpScriptPath, sizeof(tmpScriptPath), "/tmp/cxy-install-%d.sh", getpid());
        FILE *tmpFp = fopen(tmpScriptPath, "w");
        if (!tmpFp) {
            logError(log, NULL, "failed to create temp script file for '{s}'",
                    (FormatArg[]){{.s = script->name}});
            clearScriptEnvironment(&emptyEnv, builtins);
            chdir(originalDir);
            return false;
        }
        fprintf(tmpFp, "#!/bin/sh\nset -e\n%s\n", scriptContent);
        fclose(tmpFp);
        chmod(tmpScriptPath, 0700);
        snprintf(command, sizeof(command), "/bin/sh '%s'", tmpScriptPath);
    }

    // Execute command and capture output
    FILE *fp = popen(command, "r");
    if (!fp) {
        logError(log, NULL, "failed to execute script for '{s}'",
                (FormatArg[]){{.s = script->name}});
        clearScriptEnvironment(&emptyEnv, builtins);
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

    // Restore original directory, clean up temp script, and clear env vars
    chdir(originalDir);
    if (tmpScriptPath[0] != '\0') {
        unlink(tmpScriptPath);
    }
    clearScriptEnvironment(&emptyEnv, builtins);

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
 * Per-package entry read from an existing .install.yaml
 */
typedef struct {
    cstring  name;
    DynArray flags; // DynArray of cstring
} InstallYamlEntry;

/**
 * Read an existing .install.yaml into an array of InstallYamlEntry.
 * Returns the number of entries read (0 if file absent or empty).
 * Caller must freeDynArray on each entry's flags array.
 */
static u32 readInstallYamlEntries(const char *path,
                                  InstallYamlEntry *entries,
                                  u32 maxEntries,
                                  StrPool *strings,
                                  Log *log)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    yaml_parser_t parser;
    yaml_event_t  event;

    if (!yaml_parser_initialize(&parser)) {
        fclose(fp);
        return 0;
    }
    yaml_parser_set_input_file(&parser, fp);

    typedef enum {
        ES_ROOT, ES_PACKAGES, ES_PACKAGE_MAP, ES_FLAGS_SEQ
    } EntryState;

    EntryState state       = ES_ROOT;
    cstring    lastKey     = NULL;
    cstring    pendingName = NULL;
    u32        count       = 0;
    bool       ok          = true;

    while (ok) {
        if (!yaml_parser_parse(&parser, &event)) { ok = false; break; }

        switch (event.type) {
            case YAML_STREAM_END_EVENT:
                yaml_event_delete(&event);
                goto done;

            case YAML_MAPPING_START_EVENT:
                if (state == ES_PACKAGES && count < maxEntries && pendingName) {
                    entries[count].name  = pendingName;
                    entries[count].flags = newDynArray(sizeof(cstring));
                    pendingName = NULL;
                    state = ES_PACKAGE_MAP;
                }
                break;

            case YAML_MAPPING_END_EVENT:
                if (state == ES_PACKAGE_MAP) { count++; state = ES_PACKAGES; }
                else if (state == ES_PACKAGES) state = ES_ROOT;
                lastKey = NULL;
                break;

            case YAML_SEQUENCE_START_EVENT:
                if (state == ES_PACKAGE_MAP && lastKey &&
                    strcmp(lastKey, "flags") == 0)
                    state = ES_FLAGS_SEQ;
                break;

            case YAML_SEQUENCE_END_EVENT:
                if (state == ES_FLAGS_SEQ) { state = ES_PACKAGE_MAP; lastKey = NULL; }
                break;

            case YAML_SCALAR_EVENT: {
                cstring val = (cstring)event.data.scalar.value;
                if (state == ES_ROOT) {
                    if (strcmp(val, "packages") == 0) state = ES_PACKAGES;
                } else if (state == ES_PACKAGES) {
                    // scalar here is the package name key - store until MAPPING_START
                    pendingName = makeString(strings, val);
                } else if (state == ES_PACKAGE_MAP) {
                    lastKey = makeString(strings, val);
                } else if (state == ES_FLAGS_SEQ) {
                    cstring f = makeString(strings, val);
                    pushOnDynArray(&entries[count].flags, &f);
                }
                break;
            }

            default: break;
        }
        yaml_event_delete(&event);
    }

done:
    yaml_parser_delete(&parser);
    fclose(fp);
    return ok ? count : 0;
}

/**
 * Write .install.yaml or .install.dev.yaml, merging with any existing content.
 */
static bool writeInstallYaml(const char *buildDir,
                             const char *packageName,
                             const DynArray *flags,
                             bool useDev,
                             StrPool *strings,
                             Log *log)
{
    char installYamlPath[1024];
    const char *filename = useDev ? ".install.dev.yaml" : ".install.yaml";
    snprintf(installYamlPath, sizeof(installYamlPath), "%s/%s", buildDir, filename);

    // Read existing entries so we can preserve other packages' flags
#define MAX_INSTALL_ENTRIES 64
    InstallYamlEntry existing[MAX_INSTALL_ENTRIES];
    u32 existingCount = readInstallYamlEntries(installYamlPath, existing,
                                               MAX_INSTALL_ENTRIES, strings, log);

    // Find or create the slot for the current package
    int cur = -1;
    for (u32 i = 0; i < existingCount; i++) {
        if (strcmp(existing[i].name, packageName) == 0) { cur = (int)i; break; }
    }
    if (cur < 0 && existingCount < MAX_INSTALL_ENTRIES) {
        cur = (int)existingCount++;
        existing[cur].name  = makeString(strings, packageName);
        existing[cur].flags = newDynArray(sizeof(cstring));
    }

    // Merge new flags in — skip duplicates
    if (cur >= 0) {
        for (u32 fi = 0; fi < flags->size; fi++) {
            cstring newFlag = ((cstring *)flags->elems)[fi];
            bool found = false;
            for (u32 ei = 0; ei < existing[cur].flags.size; ei++) {
                if (strcmp(((cstring *)existing[cur].flags.elems)[ei], newFlag) == 0) {
                    found = true; break;
                }
            }
            if (!found) pushOnDynArray(&existing[cur].flags, &newFlag);
        }
    }

    // Rewrite the file with all packages
    FILE *fp = fopen(installYamlPath, "w");
    if (!fp) {
        logError(log, NULL, "failed to create {s}: {s}",
                (FormatArg[]){{.s = filename}, {.s = strerror(errno)}});
        for (u32 i = 0; i < existingCount; i++) freeDynArray(&existing[i].flags);
        return false;
    }

    fprintf(fp, "# Generated by cxy package install%s\n", useDev ? " --dev" : "");
    fprintf(fp, "# DO NOT EDIT MANUALLY - will be regenerated on install\n");
    fprintf(fp, "\n");
    fprintf(fp, "packages:\n");

    for (u32 i = 0; i < existingCount; i++) {
        fprintf(fp, "  %s:\n", existing[i].name);
        fprintf(fp, "    flags:\n");
        for (u32 fi = 0; fi < existing[i].flags.size; fi++) {
            fprintf(fp, "      - %s\n", ((cstring *)existing[i].flags.elems)[fi]);
        }
        freeDynArray(&existing[i].flags);
    }

    fclose(fp);
#undef MAX_INSTALL_ENTRIES
    return true;
}

/**
 * Execute install scripts and generate .install.yaml
 */
bool executeInstallScripts(const PackageMetadata *meta,
                           const char *packageDir,
                           const char *packagesDir,
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

    // Resolve packagesDir to an absolute path so $PACKAGE_DIR is always
    // absolute regardless of how the caller constructed the path.
    // realpath(3) requires the path to exist; if it doesn't yet (fresh
    // install before the directory is created) we fall back to manually
    // prepending the current working directory.
    char absolutePackagesDir[PATH_MAX];
    if (realpath(packagesDir, absolutePackagesDir) == NULL) {
        if (packagesDir[0] != '/') {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                snprintf(absolutePackagesDir, sizeof(absolutePackagesDir),
                         "%s/%s", cwd, packagesDir);
            } else {
                // Last resort: use as-is
                strncpy(absolutePackagesDir, packagesDir, sizeof(absolutePackagesDir) - 1);
                absolutePackagesDir[sizeof(absolutePackagesDir) - 1] = '\0';
            }
        } else {
            strncpy(absolutePackagesDir, packagesDir, sizeof(absolutePackagesDir) - 1);
            absolutePackagesDir[sizeof(absolutePackagesDir) - 1] = '\0';
        }
    }

    // Build builtins available to all install scripts:
    //   BUILD_DIR          - root package's build output directory
    //   PACKAGE_DIR        - root package's installed-packages directory (always absolute)
    //   PACKAGE_SOURCE_DIR - directory of the package whose scripts are running
    DynArray builtins = newDynArray(sizeof(EnvVar));
    EnvVar buildDirVar = {
        .name  = makeString(strings, "BUILD_DIR"),
        .value = makeString(strings, buildDir),
    };
    pushOnDynArray(&builtins, &buildDirVar);
    EnvVar packageDirVar = {
        .name  = makeString(strings, "PACKAGE_DIR"),
        .value = makeString(strings, absolutePackagesDir),
    };
    pushOnDynArray(&builtins, &packageDirVar);
    EnvVar sourceDirVar = {
        .name  = makeString(strings, "PACKAGE_SOURCE_DIR"),
        .value = makeString(strings, packageDir),
    };
    pushOnDynArray(&builtins, &sourceDirVar);

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

            if (executeInstallScript(script, packageDir, &allFlags, &builtins, strings, log, verbose)) {
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

            if (executeInstallScript(script, packageDir, &allFlags, &builtins, strings, log, verbose)) {
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
    freeDynArray(&builtins);

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

            if (writeInstallYaml(buildDir, meta->name, &allFlags, includeDev, strings, log)) {
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

    // If dev file doesn't exist, fall back to regular install.yaml
    struct stat st;
    if (stat(installYamlPath, &st) != 0) {
        if (useDev) {
            snprintf(installYamlPath, sizeof(installYamlPath), "%s/.install.yaml", buildDir);
            if (stat(installYamlPath, &st) != 0)
                return true; // Neither file exists - not an error
        } else {
            return true; // File doesn't exist - not an error
        }
    }

    FILE *fp = fopen(installYamlPath, "r");
    if (!fp)
        return true; // Can't open - not a fatal error

    yaml_parser_t parser;
    yaml_event_t  event;

    if (!yaml_parser_initialize(&parser)) {
        logError(log, NULL, "failed to initialize YAML parser for {s}", (FormatArg[]){{.s = installYamlPath}});
        fclose(fp);
        return false;
    }

    yaml_parser_set_input_file(&parser, fp);

    // State machine:
    //   top-level key "packages" -> mapping of package-name -> mapping
    //   inside each package mapping, key "flags" -> sequence of flag strings
    typedef enum {
        RS_ROOT,          // waiting for top-level "packages" key
        RS_PACKAGES,      // inside packages mapping, reading package names
        RS_PACKAGE_MAP,   // inside a single package's mapping, looking for "flags"
        RS_FLAGS_SEQ,     // inside the flags sequence, collecting values
    } ReadState;

    ReadState state   = RS_ROOT;
    cstring   lastKey = NULL;
    bool      success = true;

    while (success) {
        if (!yaml_parser_parse(&parser, &event)) {
            logError(log, NULL, "YAML parse error in {s}", (FormatArg[]){{.s = installYamlPath}});
            success = false;
            break;
        }

        switch (event.type) {
            case YAML_STREAM_END_EVENT:
                yaml_event_delete(&event);
                goto done;

            case YAML_MAPPING_START_EVENT:
                if (state == RS_PACKAGES)
                    state = RS_PACKAGE_MAP;
                break;

            case YAML_MAPPING_END_EVENT:
                if (state == RS_PACKAGE_MAP)
                    state = RS_PACKAGES;
                else if (state == RS_PACKAGES)
                    state = RS_ROOT;
                lastKey = NULL;
                break;

            case YAML_SEQUENCE_START_EVENT:
                if (state == RS_PACKAGE_MAP && lastKey &&
                    strcmp(lastKey, "flags") == 0)
                    state = RS_FLAGS_SEQ;
                break;

            case YAML_SEQUENCE_END_EVENT:
                if (state == RS_FLAGS_SEQ) {
                    state   = RS_PACKAGE_MAP;
                    lastKey = NULL;
                }
                break;

            case YAML_SCALAR_EVENT: {
                cstring value = (cstring)event.data.scalar.value;

                if (state == RS_ROOT) {
                    if (strcmp(value, "packages") == 0)
                        state = RS_PACKAGES;
                } else if (state == RS_PACKAGES) {
                    // package name key — nothing to store, just transition handled
                    // by MAPPING_START above; the scalar here is the package name
                    (void)value;
                } else if (state == RS_PACKAGE_MAP) {
                    lastKey = makeString(strings, value);
                } else if (state == RS_FLAGS_SEQ) {
                    cstring flag = makeString(strings, value);
                    pushOnDynArray(flags, &flag);
                }
                break;
            }

            default:
                break;
        }

        yaml_event_delete(&event);
    }

done:
    yaml_parser_delete(&parser);
    fclose(fp);
    return success;
}
