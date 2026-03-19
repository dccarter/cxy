/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/types.h"

#include "core/log.h"
#include "core/strpool.h"
#include "driver/options.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

void initPackageMetadata(PackageMetadata *meta, StrPool *strings)
{
    memset(meta, 0, sizeof(PackageMetadata));
    meta->dependencies = newDynArray(sizeof(PackageDependency));
    meta->devDependencies = newDynArray(sizeof(PackageDependency));
    meta->tests = newDynArray(sizeof(PackageTest));
    meta->scriptEnv = newDynArray(sizeof(EnvVar));
    meta->scripts = newDynArray(sizeof(PackageScript));
    meta->install = newDynArray(sizeof(PackageInstallScript));
    meta->installDev = newDynArray(sizeof(PackageInstallScript));
    meta->builds = newDynArray(sizeof(PackageBuild));
    meta->hasMultipleBuilds = false;
    meta->build.cLibs = newDynArray(sizeof(cstring));
    meta->build.cLibDirs = newDynArray(sizeof(cstring));
    meta->build.cHeaderDirs = newDynArray(sizeof(cstring));
    meta->build.cDefines = newDynArray(sizeof(cstring));
    meta->build.cFlags = newDynArray(sizeof(cstring));
    meta->build.defines = newDynArray(sizeof(cstring));
    meta->build.flags = newDynArray(sizeof(cstring));
}

void freePackageMetadata(PackageMetadata *meta)
{
    if (meta == NULL)
        return;

    // Free dependency arrays
    if (meta->dependencies.elems != NULL) {
        for (u32 i = 0; i < meta->dependencies.size; i++) {
            PackageDependency *dep = &dynArrayAt(PackageDependency*, &meta->dependencies, i);
            // Strings are owned by StrPool, no need to free
            (void)dep;
        }
        freeDynArray(&meta->dependencies);
    }

    if (meta->devDependencies.elems != NULL) {
        for (u32 i = 0; i < meta->devDependencies.size; i++) {
            PackageDependency *dep = &dynArrayAt(PackageDependency*, &meta->devDependencies, i);
            (void)dep;
        }
        freeDynArray(&meta->devDependencies);
    }

    // Free test arrays
    if (meta->tests.elems != NULL) {
        for (u32 i = 0; i < meta->tests.size; i++) {
            PackageTest *test = &dynArrayAt(PackageTest*, &meta->tests, i);
            if (test->args.elems != NULL) {
                freeDynArray(&test->args);
            }
        }
        freeDynArray(&meta->tests);
    }

    // Free script environment variables
    if (meta->scriptEnv.elems != NULL) {
        freeDynArray(&meta->scriptEnv);
    }

    // Free scripts array
    if (meta->scripts.elems != NULL) {
        for (u32 i = 0; i < meta->scripts.size; i++) {
            PackageScript *script = &dynArrayAt(PackageScript*, &meta->scripts, i);
            if (script->dependencies.elems != NULL) {
                freeDynArray(&script->dependencies);
            }
            if (script->inputs.elems != NULL) {
                freeDynArray(&script->inputs);
            }
            if (script->outputs.elems != NULL) {
                freeDynArray(&script->outputs);
            }
        }
        freeDynArray(&meta->scripts);
    }

    // Free install scripts arrays
    if (meta->install.elems != NULL) {
        freeDynArray(&meta->install);
    }
    if (meta->installDev.elems != NULL) {
        freeDynArray(&meta->installDev);
    }

    // Free build config arrays (single build)
    if (meta->build.cLibs.elems != NULL)
        freeDynArray(&meta->build.cLibs);
    if (meta->build.cLibDirs.elems != NULL)
        freeDynArray(&meta->build.cLibDirs);
    if (meta->build.cHeaderDirs.elems != NULL)
        freeDynArray(&meta->build.cHeaderDirs);
    if (meta->build.cDefines.elems != NULL)
        freeDynArray(&meta->build.cDefines);
    if (meta->build.cFlags.elems != NULL)
        freeDynArray(&meta->build.cFlags);
    if (meta->build.defines.elems != NULL)
        freeDynArray(&meta->build.defines);
    if (meta->build.flags.elems != NULL)
        freeDynArray(&meta->build.flags);
    
    // Free multiple builds array
    if (meta->builds.elems != NULL) {
        for (u32 i = 0; i < meta->builds.size; i++) {
            PackageBuild *build = &dynArrayAt(PackageBuild*, &meta->builds, i);
            if (build->config.cLibs.elems != NULL)
                freeDynArray(&build->config.cLibs);
            if (build->config.cLibDirs.elems != NULL)
                freeDynArray(&build->config.cLibDirs);
            if (build->config.cHeaderDirs.elems != NULL)
                freeDynArray(&build->config.cHeaderDirs);
            if (build->config.cDefines.elems != NULL)
                freeDynArray(&build->config.cDefines);
            if (build->config.cFlags.elems != NULL)
                freeDynArray(&build->config.cFlags);
            if (build->config.defines.elems != NULL)
                freeDynArray(&build->config.defines);
            if (build->config.flags.elems != NULL)
                freeDynArray(&build->config.flags);
        }
        freeDynArray(&meta->builds);
    }
}

void initPackageLockFile(PackageLockFile *lock)
{
    memset(lock, 0, sizeof(PackageLockFile));
    lock->version = 1;
    lock->packages = newDynArray(sizeof(PackageLock));
    lock->resolved = newDynArray(sizeof(cstring) * 2); // name, version pairs
}

void freePackageLockFile(PackageLockFile *lock)
{
    if (lock == NULL)
        return;

    if (lock->packages.elems != NULL) {
        for (u32 i = 0; i < lock->packages.size; i++) {
            PackageLock *pkg = &dynArrayAt(PackageLock*, &lock->packages, i);
            if (pkg->dependencies.elems != NULL) {
                freeDynArray(&pkg->dependencies);
            }
        }
        freeDynArray(&lock->packages);
    }

    if (lock->resolved.elems != NULL)
        freeDynArray(&lock->resolved);
}

/**
 * Parse a number from string, updating pointer
 */
static bool parseNumber(const char **str, u32 *num)
{
    if (!isdigit(**str))
        return false;

    *num = 0;
    while (isdigit(**str)) {
        u32 digit = **str - '0';
        // Check for overflow
        if (*num > (UINT32_MAX - digit) / 10)
            return false;
        *num = (*num * 10) + digit;
        (*str)++;
    }
    return true;
}

bool parseSemanticVersion(cstring str, SemanticVersion *version, Log *log)
{
    if (str == NULL || str[0] == '\0') {
        if (log)
            logError(log, NULL, "empty version string", NULL);
        return false;
    }

    memset(version, 0, sizeof(SemanticVersion));

    const char *p = str;

    // Skip optional 'v' prefix
    if (*p == 'v' || *p == 'V')
        p++;

    // Parse major version
    if (!parseNumber(&p, &version->major)) {
        if (log)
            logError(log, NULL, "invalid major version in '{s}'",
                    (FormatArg[]){{.s = str}});
        return false;
    }

    // Expect '.'
    if (*p != '.') {
        if (log)
            logError(log, NULL, "expected '.' after major version in '{s}'",
                    (FormatArg[]){{.s = str}});
        return false;
    }
    p++;

    // Parse minor version
    if (!parseNumber(&p, &version->minor)) {
        if (log)
            logError(log, NULL, "invalid minor version in '{s}'",
                    (FormatArg[]){{.s = str}});
        return false;
    }

    // Expect '.'
    if (*p != '.') {
        if (log)
            logError(log, NULL, "expected '.' after minor version in '{s}'",
                    (FormatArg[]){{.s = str}});
        return false;
    }
    p++;

    // Parse patch version
    if (!parseNumber(&p, &version->patch)) {
        if (log)
            logError(log, NULL, "invalid patch version in '{s}'",
                    (FormatArg[]){{.s = str}});
        return false;
    }
    
    // After patch, only '-', '+', or end-of-string are allowed
    if (*p != '\0' && *p != '-' && *p != '+') {
        if (log)
            logError(log, NULL, "invalid version format in '{s}': unexpected characters after patch version",
                    (FormatArg[]){{.s = str}});
        return false;
    }
    
    // Optional: prerelease identifier
    if (*p == '-') {
        p++;
        version->prerelease = p;
        // Find end of prerelease (until '+' or end)
        while (*p && *p != '+')
            p++;
    }

    // Optional: build metadata
    if (*p == '+') {
        p++;
        version->build = p;
    }

    return true;
}

bool parseVersionConstraint(cstring str, VersionConstraint *constraint, Log *log)
{
    if (str == NULL || str[0] == '\0') {
        // Empty or NULL means "any version"
        constraint->type = vcAny;
        constraint->raw = str;
        return true;
    }

    memset(constraint, 0, sizeof(VersionConstraint));
    constraint->raw = str;

    const char *p = str;

    // Skip whitespace
    while (isspace(*p))
        p++;

    // Check for special symbols
    if (*p == '^') {
        constraint->type = vcCaret;
        p++;
    }
    else if (*p == '~') {
        constraint->type = vcTilde;
        p++;
    }
    else if (*p == '>') {
        p++;
        if (*p == '=') {
            constraint->type = vcGreaterEq;
            p++;
        } else {
            constraint->type = vcGreater;
        }
    }
    else if (*p == '<') {
        p++;
        if (*p == '=') {
            constraint->type = vcLessEq;
            p++;
        } else {
            constraint->type = vcLess;
        }
    }
    else if (*p == '*') {
        constraint->type = vcAny;
        return true;
    }
    else {
        // Check for wildcard in version (e.g., "1.2.x")
        if (strstr(p, "x") || strstr(p, "X") || strstr(p, "*")) {
            constraint->type = vcWildcard;
        } else {
            constraint->type = vcExact;
        }
    }

    // Skip whitespace after operator
    while (isspace(*p))
        p++;

    // For wildcard, parse what we can
    if (constraint->type == vcWildcard) {
        const char *v = p;
        parseNumber(&v, &constraint->version.major);
        if (*v == '.') {
            v++;
            if (*v != 'x' && *v != 'X' && *v != '*') {
                parseNumber(&v, &constraint->version.minor);
                if (*v == '.') {
                    v++;
                    if (*v != 'x' && *v != 'X' && *v != '*') {
                        parseNumber(&v, &constraint->version.patch);
                    }
                }
            }
        }
        return true;
    }

    // Parse the version part
    if (!parseSemanticVersion(p, &constraint->version, log)) {
        return false;
    }

    return true;
}

i32 compareSemanticVersions(const SemanticVersion *a, const SemanticVersion *b)
{
    // Compare major
    if (a->major < b->major)
        return -1;
    if (a->major > b->major)
        return 1;

    // Compare minor
    if (a->minor < b->minor)
        return -1;
    if (a->minor > b->minor)
        return 1;

    // Compare patch
    if (a->patch < b->patch)
        return -1;
    if (a->patch > b->patch)
        return 1;

    // If versions are equal, check prerelease
    // Version without prerelease > version with prerelease
    if (a->prerelease == NULL && b->prerelease != NULL)
        return 1;
    if (a->prerelease != NULL && b->prerelease == NULL)
        return -1;

    // Both have prerelease, compare lexicographically
    if (a->prerelease != NULL && b->prerelease != NULL) {
        i32 cmp = strcmp(a->prerelease, b->prerelease);
        if (cmp != 0)
            return cmp < 0 ? -1 : 1;
    }

    // Versions are equal
    return 0;
}

bool versionSatisfiesConstraint(const SemanticVersion *version,
                               const VersionConstraint *constraint)
{
    i32 cmp = compareSemanticVersions(version, &constraint->version);

    switch (constraint->type) {
        case vcExact:
            return cmp == 0;

        case vcCaret:
            // ^1.2.3 means >=1.2.3 <2.0.0
            // ^0.2.3 means >=0.2.3 <0.3.0
            // ^0.0.3 means >=0.0.3 <0.0.4
            if (cmp < 0)
                return false;
            if (constraint->version.major > 0) {
                return version->major == constraint->version.major;
            }
            else if (constraint->version.minor > 0) {
                return version->major == 0 &&
                       version->minor == constraint->version.minor;
            }
            else {
                return version->major == 0 &&
                       version->minor == 0 &&
                       version->patch == constraint->version.patch;
            }

        case vcTilde:
            // ~1.2.3 means >=1.2.3 <1.3.0
            if (cmp < 0)
                return false;
            return version->major == constraint->version.major &&
                   version->minor == constraint->version.minor;

        case vcGreater:
            return cmp > 0;

        case vcGreaterEq:
            return cmp >= 0;

        case vcLess:
            return cmp < 0;

        case vcLessEq:
            return cmp <= 0;

        case vcWildcard:
            // 1.2.x means >=1.2.0 <1.3.0
            // 1.x means >=1.0.0 <2.0.0
            if (version->major != constraint->version.major)
                return false;
            if (constraint->version.minor > 0) {
                return version->minor == constraint->version.minor;
            }
            return true;

        case vcAny:
            return true;

        default:
            return false;
    }
}

/**
 * Find a script by name
 */
static PackageScript* findScript(const PackageMetadata *meta, cstring name)
{
    for (u32 i = 0; i < meta->scripts.size; i++) {
        PackageScript *script = &dynArrayAt(PackageScript*, &meta->scripts, i);
        if (strcmp(script->name, name) == 0) {
            return script;
        }
    }
    return NULL;
}

/**
 * Recursive helper for dependency resolution with cycle detection
 */
static bool resolveScriptDepsRecursive(const PackageMetadata *meta,
                                       cstring scriptName,
                                       DynArray *executionOrder,
                                       DynArray *visiting,
                                       DynArray *visited,
                                       Log *log)
{
    // Check if already visited (already in execution order)
    for (u32 i = 0; i < visited->size; i++) {
        cstring visitedName = ((cstring *)visited->elems)[i];
        if (strcmp(visitedName, scriptName) == 0) {
            return true;
        }
    }

    // Check for cycles (currently being visited)
    for (u32 i = 0; i < visiting->size; i++) {
        cstring visitingName = ((cstring *)visiting->elems)[i];
        if (strcmp(visitingName, scriptName) == 0) {
            logError(log, NULL, "circular script dependency detected: '{s}'",
                    (FormatArg[]){{.s = scriptName}});
            return false;
        }
    }

    // Find the script
    PackageScript *script = findScript(meta, scriptName);
    if (script == NULL) {
        logError(log, NULL, "undefined script referenced: '{s}'",
                (FormatArg[]){{.s = scriptName}});
        return false;
    }

    // Mark as currently visiting
    pushOnDynArray(visiting, &scriptName);

    // Recursively resolve dependencies
    for (u32 i = 0; i < script->dependencies.size; i++) {
        cstring depName = ((cstring *)script->dependencies.elems)[i];
        if (!resolveScriptDepsRecursive(meta, depName, executionOrder, visiting, visited, log)) {
            return false;
        }
    }

    // Remove from visiting set
    popDynArray(visiting);

    // Add to execution order and visited set
    pushOnDynArray(executionOrder, &scriptName);
    pushOnDynArray(visited, &scriptName);

    return true;
}

bool validateScripts(const PackageMetadata *meta, Log *log)
{
    bool valid = true;

    // Validate each script
    for (u32 i = 0; i < meta->scripts.size; i++) {
        PackageScript *script = &dynArrayAt(PackageScript*, &meta->scripts, i);

        if (script->name == NULL || script->name[0] == '\0') {
            logError(log, NULL, "script name is required", NULL);
            valid = false;
            continue;
        }

        if (script->command == NULL || script->command[0] == '\0') {
            logError(log, NULL, "script '{s}' is missing command",
                    (FormatArg[]){{.s = script->name}});
            valid = false;
        }

        // Validate dependencies exist
        for (u32 j = 0; j < script->dependencies.size; j++) {
            cstring depName = ((cstring *)script->dependencies.elems)[j];
            PackageScript *depScript = findScript(meta, depName);
            if (depScript == NULL) {
                logError(log, NULL, "script '{s}' depends on undefined script '{s}'",
                        (FormatArg[]){{.s = script->name}, {.s = depName}});
                valid = false;
            }
        }
    }

    // Check for circular dependencies
    if (valid) {
        for (u32 i = 0; i < meta->scripts.size; i++) {
            PackageScript *script = &dynArrayAt(PackageScript*, &meta->scripts, i);
            DynArray executionOrder = newDynArray(sizeof(cstring));
            DynArray visiting = newDynArray(sizeof(cstring));
            DynArray visited = newDynArray(sizeof(cstring));

            if (!resolveScriptDepsRecursive(meta, script->name, &executionOrder, &visiting, &visited, log)) {
                valid = false;
            }

            freeDynArray(&executionOrder);
            freeDynArray(&visiting);
            freeDynArray(&visited);

            if (!valid)
                break;
        }
    }

    return valid;
}

bool resolveScriptDependencies(const PackageMetadata *meta,
                               cstring scriptName,
                               DynArray *executionOrder,
                               Log *log)
{
    // Ensure executionOrder is initialized
    if (executionOrder->elems == NULL) {
        *executionOrder = newDynArray(sizeof(cstring));
    }
    else {
        clearDynArray(executionOrder);
    }

    DynArray visiting = newDynArray(sizeof(cstring));
    DynArray visited = newDynArray(sizeof(cstring));

    bool success = resolveScriptDepsRecursive(meta, scriptName, executionOrder, &visiting, &visited, log);

    freeDynArray(&visiting);
    freeDynArray(&visited);

    return success;
}
