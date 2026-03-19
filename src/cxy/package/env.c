/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/env.h"
#include "package/types.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"

#include <string.h>
#include <stdlib.h>

/**
 * Find an environment variable by name in an array
 */
static const EnvVar* findEnvVar(const char *name, const DynArray *envVars)
{
    if (!envVars || !envVars->elems)
        return NULL;

    for (u32 i = 0; i < envVars->size; i++) {
        const EnvVar *var = &((const EnvVar *)envVars->elems)[i];
        if (strcmp(var->name, name) == 0) {
            return var;
        }
    }
    return NULL;
}

/**
 * Substitute {{VAR}} placeholders in a string
 */
cstring substituteEnvVars(const char *template,
                         const DynArray *envVars,
                         const DynArray *builtins,
                         StrPool *strings,
                         Log *log)
{
    if (!template || template[0] == '\0')
        return template;

    FormatState result = newFormatState(NULL, true);
    const char *p = template;

    while (*p) {
        // Look for {{
        if (p[0] == '{' && p[1] == '{') {
            // Find closing }}
            const char *start = p + 2;
            const char *end = strstr(start, "}}");

            if (!end) {
                // Malformed template - no closing }}, treat as literal
                appendString(&result, "{{");
                p += 2;
                continue;
            }

            // Extract variable name
            size_t nameLen = end - start;
            char varName[256];
            if (nameLen >= sizeof(varName)) {
                logError(log, NULL, "variable name too long in template: {s}",
                        (FormatArg[]){{.s = template}});
                freeFormatState(&result);
                return NULL;
            }

            strncpy(varName, start, nameLen);
            varName[nameLen] = '\0';

            // Look up variable (check builtins first, then env vars)
            const EnvVar *var = findEnvVar(varName, builtins);
            if (!var) {
                var = findEnvVar(varName, envVars);
            }

            if (var && var->value) {
                // Substitute with value
                appendString(&result, var->value);
            } else {
                // Variable not found - leave as-is or error?
                // For now, leave the placeholder
                logWarning(log, NULL, "undefined variable '{s}' in template",
                          (FormatArg[]){{.s = varName}});
                format(&result, "{{{{{s}}}}}", (FormatArg[]){{.s = varName}});
            }

            // Move past the closing }}
            p = end + 2;
        } else {
            // Regular character
            append(&result, p, 1);
            p++;
        }
    }

    char *tempResult = formatStateToString(&result);
    cstring substituted = makeString(strings, tempResult);
    free(tempResult);
    freeFormatState(&result);
    return substituted;
}

/**
 * Build built-in environment variables
 */
DynArray buildBuiltinEnvVars(const PackageMetadata *meta,
                             const char *packageDir,
                             const char *packagesDir,
                             const char *buildDir,
                             StrPool *strings)
{
    DynArray builtins = newDynArray(sizeof(EnvVar));

    // SOURCE_DIR - absolute path to directory containing Cxyfile.yaml
    EnvVar sourceDir;
    sourceDir.name = makeString(strings, "SOURCE_DIR");
    sourceDir.value = makeString(strings, packageDir);
    pushOnDynArray(&builtins, &sourceDir);

    // PACKAGE_NAME
    if (meta->name && meta->name[0] != '\0') {
        EnvVar packageName;
        packageName.name = makeString(strings, "PACKAGE_NAME");
        packageName.value = meta->name;
        pushOnDynArray(&builtins, &packageName);
    }

    // PACKAGE_VERSION
    if (meta->version && meta->version[0] != '\0') {
        EnvVar packageVersion;
        packageVersion.name = makeString(strings, "PACKAGE_VERSION");
        packageVersion.value = meta->version;
        pushOnDynArray(&builtins, &packageVersion);
    }

    // CXY_PACKAGES_DIR
    EnvVar cxyPackagesDir;
    cxyPackagesDir.name = makeString(strings, "CXY_PACKAGES_DIR");
    cxyPackagesDir.value = makeString(strings, packagesDir);
    pushOnDynArray(&builtins, &cxyPackagesDir);

    // CXY_BUILD_DIR
    if (buildDir && buildDir[0] != '\0') {
        EnvVar cxyBuildDir;
        cxyBuildDir.name = makeString(strings, "CXY_BUILD_DIR");
        cxyBuildDir.value = makeString(strings, buildDir);
        pushOnDynArray(&builtins, &cxyBuildDir);

        // CXY_ASYNC_LAST_PID_FILE
        char lastPidPath[2048];
        snprintf(lastPidPath, sizeof(lastPidPath), "%s/.async-last-pid", buildDir);
        EnvVar cxyAsyncLastPidFile;
        cxyAsyncLastPidFile.name = makeString(strings, "CXY_ASYNC_LAST_PID_FILE");
        cxyAsyncLastPidFile.value = makeString(strings, lastPidPath);
        pushOnDynArray(&builtins, &cxyAsyncLastPidFile);
    }

    return builtins;
}

/**
 * Resolve environment variables with substitution
 */
bool resolveEnvVars(DynArray *envVars,
                   const DynArray *builtins,
                   StrPool *strings,
                   Log *log)
{
    if (!envVars || envVars->size == 0)
        return true;

    // Process each env var in order, substituting references to previous vars
    for (u32 i = 0; i < envVars->size; i++) {
        EnvVar *var = &((EnvVar *)envVars->elems)[i];

        // Check if value contains {{}} placeholders
        if (strstr(var->value, "{{") != NULL) {
            // Create a temporary array with only the vars defined before this one
            DynArray previousVars = newDynArray(sizeof(EnvVar));
            for (u32 j = 0; j < i; j++) {
                EnvVar *prev = &((EnvVar *)envVars->elems)[j];
                pushOnDynArray(&previousVars, prev);
            }

            // Substitute using builtins and previous vars
            cstring resolved = substituteEnvVars(var->value, &previousVars, builtins, strings, log);
            freeDynArray(&previousVars);

            if (!resolved) {
                logError(log, NULL, "failed to resolve environment variable '{s}'",
                        (FormatArg[]){{.s = var->name}});
                return false;
            }

            // Detect circular reference (if resolved value still contains {{}} with same var)
            char circularCheck[256];
            snprintf(circularCheck, sizeof(circularCheck), "{{%s}}", var->name);
            if (strstr(resolved, circularCheck) != NULL) {
                logError(log, NULL, "circular reference detected in environment variable '{s}'",
                        (FormatArg[]){{.s = var->name}});
                return false;
            }

            // Update with resolved value
            var->value = resolved;
        }
    }

    return true;
}

/**
 * Set environment variables for script execution
 */
bool setScriptEnvironment(const DynArray *envVars,
                         const DynArray *builtins,
                         Log *log)
{
    // Set built-in variables first
    if (builtins && builtins->elems) {
        for (u32 i = 0; i < builtins->size; i++) {
            const EnvVar *var = &((const EnvVar *)builtins->elems)[i];
            if (setenv(var->name, var->value, 1) != 0) {
                logError(log, NULL, "failed to set environment variable '{s}'",
                        (FormatArg[]){{.s = var->name}});
                return false;
            }
        }
    }

    // Set user-defined variables
    if (envVars && envVars->elems) {
        for (u32 i = 0; i < envVars->size; i++) {
            const EnvVar *var = &((const EnvVar *)envVars->elems)[i];
            if (setenv(var->name, var->value, 1) != 0) {
                logError(log, NULL, "failed to set environment variable '{s}'",
                        (FormatArg[]){{.s = var->name}});
                return false;
            }
        }
    }

    return true;
}

/**
 * Clear script environment variables
 */
void clearScriptEnvironment(const DynArray *envVars,
                            const DynArray *builtins)
{
    // Unset user-defined variables
    if (envVars && envVars->elems) {
        for (u32 i = 0; i < envVars->size; i++) {
            const EnvVar *var = &((const EnvVar *)envVars->elems)[i];
            unsetenv(var->name);
        }
    }

    // Unset built-in variables
    if (builtins && builtins->elems) {
        for (u32 i = 0; i < builtins->size; i++) {
            const EnvVar *var = &((const EnvVar *)builtins->elems)[i];
            unsetenv(var->name);
        }
    }
}