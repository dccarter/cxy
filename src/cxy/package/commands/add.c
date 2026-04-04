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
#include "package/registry.h"
#include "package/resolver.h"
#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"

#include <string.h>
#include <unistd.h>
#include <ctype.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/**
 * Return true when `arg` looks like a bare registry package name rather than
 * a URL, a git-shorthand ("github:user/repo"), or a filesystem path.
 *
 * A bare package name:
 *   - Contains only [a-z0-9_-]
 *   - Does not contain '/' or '.'
 *   - Does not contain "://"
 */
static bool isBarePackageName(const char *arg)
{
    if (!arg || arg[0] == '\0')
        return false;

    /* Absolute or relative paths */
    if (arg[0] == '/' || arg[0] == '.')
        return false;

    /* URL-like strings */
    if (strstr(arg, "://"))
        return false;

    /* git shorthand: github:user/repo, gitlab:…, git@… */
    if (strncmp(arg, "github:", 7) == 0 ||
        strncmp(arg, "gitlab:", 7) == 0  ||
        strncmp(arg, "bitbucket:", 10) == 0 ||
        strncmp(arg, "git@", 4) == 0)
        return false;

    /* Any '/' means it's at least a "user/repo" shorthand */
    if (strchr(arg, '/'))
        return false;

    /* Must be composed of valid package-name characters */
    for (const char *p = arg; *p; p++) {
        char c = *p;
        if (!islower((unsigned char)c) && !isdigit((unsigned char)c) &&
            c != '-' && c != '_')
            return false;
    }

    return true;
}

/**
 * Derive package name from repository URL.
 *
 * Examples:
 *   https://github.com/user/repo.git  →  repo
 *   https://github.com/user/my-pkg    →  my-pkg
 *   github:user/json-parser           →  json-parser
 *   git@github.com:user/crypto.git    →  crypto
 */
static cstring derivePackageNameFromRepository(const char *repository, StrPool *strings)
{
    if (!repository || repository[0] == '\0')
        return NULL;

    const char *start = repository;
    const char *end   = repository + strlen(repository);

    /* Handle short formats: "github:user/repo", "gitlab:user/repo" */
    if (strncmp(repository, "github:", 7) == 0 ||
        strncmp(repository, "gitlab:", 7) == 0  ||
        strncmp(repository, "bitbucket:", 10) == 0) {
        const char *colon = strchr(repository, ':');
        start = colon + 1;
    }
    /* Handle git@host:path format */
    else if (strncmp(repository, "git@", 4) == 0) {
        const char *colon = strchr(repository, ':');
        if (colon)
            start = colon + 1;
    }
    /* Handle URL formats */
    else if (strstr(repository, "://")) {
        const char *lastSlash = strrchr(repository, '/');
        if (lastSlash)
            start = lastSlash + 1;
    }

    /* Handle "user/repo" — take the part after the slash */
    const char *slash = strchr(start, '/');
    if (slash)
        start = slash + 1;

    /* Strip ".git" suffix */
    if (end - start > 4 && strcmp(end - 4, ".git") == 0)
        end -= 4;

    size_t len = (size_t)(end - start);
    if (len == 0)
        return NULL;

    char buf[256];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';

    return makeString(strings, buf);
}

/**
 * Return true if a dependency with the given name already exists.
 */
static bool dependencyExists(const PackageMetadata *meta, const char *name)
{
    for (u32 i = 0; i < meta->dependencies.size; i++) {
        const PackageDependency *dep = &((PackageDependency *)meta->dependencies.elems)[i];
        if (strcmp(dep->name, name) == 0)
            return true;
    }
    for (u32 i = 0; i < meta->devDependencies.size; i++) {
        const PackageDependency *dep = &((PackageDependency *)meta->devDependencies.elems)[i];
        if (strcmp(dep->name, name) == 0)
            return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * Registry-assisted resolution
 *
 * Called when the user supplies a bare package name instead of a URL.
 * Populates `newDep` with the repository URL, resolved tag, and commit from
 * the registry, then falls through to the normal git-clone install path.
 * ---------------------------------------------------------------------- */

typedef struct {
    cstring repository;    /* Canonical git URL from registry */
    cstring tag;           /* Resolved git tag, e.g. "v1.2.3" */
    cstring commit;        /* Full commit SHA (may be NULL)   */
    cstring version;       /* Semver string, e.g. "1.2.3"    */
} RegistryResolution;

/**
 * Look up a package in the registry and resolve the best matching version.
 *
 * @param packageName  Bare package name (e.g. "cxy-json").
 * @param constraint   Version constraint ("^1.0.0", "*", NULL for latest).
 * @param strings      String pool.
 * @param log          Logger.
 * @param out          Filled on success.
 * @return true on success.
 */
static bool resolveFromRegistry(const char *packageName,
                                const char *constraint,
                                const char *registryFile,
                                StrPool *strings,
                                Log *log,
                                RegistryResolution *out)
{
    memset(out, 0, sizeof(*out));

    RegistryClient *client = registryClientInit(strings, log, registryFile);
    if (!client) {
        logError(log, NULL,
                 "could not initialise registry client – check network or set CXY_REGISTRY_URL",
                 NULL);
        return false;
    }

    printStatusSticky(log, " Looking up '%s' in registry %s...",
                      packageName, registryClientBaseUrl(client));

    RegistryVersion resolved;
    bool ok = registryResolveVersion(client, packageName, constraint, &resolved);

    if (!ok) {
        /* registryResolveVersion already logged a specific error */
        registryClientFree(client);
        return false;
    }

    if (!resolved.tag || resolved.tag[0] == '\0') {
        logError(log, NULL,
                 "registry entry for '{s}' version '{s}' has no git tag",
                 (FormatArg[]){{.s = packageName}, {.s = resolved.version ? resolved.version : "?"}});
        registryClientFree(client);
        return false;
    }

    /*
     * We need the repository URL.  registryResolveVersion only gives us a
     * RegistryVersion (no repo URL).  Fetch the full package details to get it.
     */
    RegistryPackageDetails details;
    if (!registryGetPackage(client, packageName, &details)) {
        logError(log, NULL,
                 "failed to fetch package details for '{s}' from registry",
                 (FormatArg[]){{.s = packageName}});
        registryClientFree(client);
        return false;
    }

    if (!details.pkg.repository || details.pkg.repository[0] == '\0') {
        logError(log, NULL,
                 "registry package '{s}' has no repository URL",
                 (FormatArg[]){{.s = packageName}});
        registryPackageDetailsFree(&details);
        registryClientFree(client);
        return false;
    }

    out->repository = details.pkg.repository; /* allocated in pool */
    out->tag        = resolved.tag;
    out->commit     = resolved.commit;
    out->version    = resolved.version;

    printStatusSticky(log, " Resolved %s@%s (%s) from registry",
                      packageName,
                      resolved.version ? resolved.version : "?",
                      resolved.tag);

    registryPackageDetailsFree(&details);
    registryClientFree(client);
    return true;
}

/**
 * Post a download record to the registry (best-effort, never fatal).
 */
static void recordDownload(const char *packageName,
                           const char *version,
                           const char *registryFile,
                           StrPool *strings,
                           Log *log)
{
    RegistryClient *client = registryClientInit(strings, log, registryFile);
    if (!client)
        return;
    registryRecordDownload(client, packageName, version);
    registryClientFree(client);
}

/* -------------------------------------------------------------------------
 * packageAddCommand
 * ---------------------------------------------------------------------- */

/**
 * Add a dependency to the current package.
 *
 * The first positional argument may be:
 *
 *   a) A bare registry package name (e.g. "cxy-json"):
 *      The command contacts the registry to resolve the repository URL and
 *      best matching version, clones it, and records a download.
 *
 *   b) A git repository URL or shorthand (e.g. "https://github.com/…",
 *      "github:user/repo", "git@github.com:user/repo.git"):
 *      The existing behaviour – fetch tags, pick the best tag, clone.
 *
 *   c) Nothing, together with --path <dir>:
 *      Local filesystem dependency.
 */
bool packageAddCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *firstArg    = options->package.repository; /* positional[0] */
    const char *customName  = options->package.packageName;
    const char *constraint  = options->package.constraint;
    const char *tag         = options->package.tag;
    const char *branch      = options->package.branch;
    const char *path        = options->package.path;
    const char *registryFile = options->package.registryFile;
    bool        isDev       = options->package.dev;
    bool        noInstall   = options->package.noInstall;

    /* ------------------------------------------------------------------ */
    /* 1. Find and load Cxyfile.yaml                                       */
    /* ------------------------------------------------------------------ */

    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL,
                 "no Cxyfile.yaml found. Run 'cxy package create' first.", NULL);
        return false;
    }

    printStatusSticky(log, "Adding dependency to package '%s'...", meta.name);

    /* ------------------------------------------------------------------ */
    /* 2. Classify the first argument                                      */
    /* ------------------------------------------------------------------ */

    bool        fromRegistry = false;
    const char *repository   = NULL;  /* git URL (set after classification) */

    if (firstArg && firstArg[0] != '\0') {
        if (isBarePackageName(firstArg)) {
            fromRegistry = true;
            /* repository will be filled by resolveFromRegistry() below */
        } else {
            repository = firstArg;
        }
    } else if (path && path[0] != '\0') {
        /* local path — handled below */
    } else {
        logError(log, NULL,
                 "must specify a package name, repository URL, or --path <dir>", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 3. Derive / validate the package name                               */
    /* ------------------------------------------------------------------ */

    cstring packageName = NULL;

    if (customName && customName[0] != '\0') {
        packageName = makeString(strings, customName);
    } else if (fromRegistry) {
        packageName = makeString(strings, firstArg);
    } else if (repository) {
        packageName = derivePackageNameFromRepository(repository, strings);
        if (!packageName) {
            logError(log, NULL,
                     "failed to derive package name from '{s}'. Use --name to specify.",
                     (FormatArg[]){{.s = repository}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
    } else {
        /* path-only: use the directory's basename */
        const char *lastSlash = strrchr(path, '/');
        packageName = makeString(strings, lastSlash ? lastSlash + 1 : path);
    }

    /* ------------------------------------------------------------------ */
    /* 4. Reject duplicates                                                */
    /* ------------------------------------------------------------------ */

    if (dependencyExists(&meta, packageName)) {
        logError(log, NULL,
                 "dependency '{s}' already exists in Cxyfile.yaml",
                 (FormatArg[]){{.s = packageName}});
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 5. Reject conflicting flags                                         */
    /* ------------------------------------------------------------------ */

    if (tag && tag[0] != '\0' && branch && branch[0] != '\0') {
        logError(log, NULL, "cannot specify both --tag and --branch", NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (fromRegistry && ((tag && tag[0] != '\0') || (branch && branch[0] != '\0'))) {
        logError(log, NULL,
                 "cannot use --tag or --branch when adding a registry package; "
                 "use --version to specify a version constraint instead.",
                 NULL);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 6. Packages directory                                               */
    /* ------------------------------------------------------------------ */

    char packagesDir[1024];
    if (options->package.packagesDir && options->package.packagesDir[0] != '\0') {
        strncpy(packagesDir, options->package.packagesDir, sizeof(packagesDir) - 1);
        packagesDir[sizeof(packagesDir) - 1] = '\0';
    } else {
        snprintf(packagesDir, sizeof(packagesDir), "%s/.cxy/packages", packageDir);
    }

    /* ------------------------------------------------------------------ */
    /* 7. Build the PackageDependency record                               */
    /* ------------------------------------------------------------------ */

    PackageDependency newDep;
    memset(&newDep, 0, sizeof(newDep));
    newDep.name  = packageName;
    newDep.isDev = isDev;

    /* ------------------------------------------------------------------ */
    /* 8. Path: validate locally, then skip straight to writing            */
    /* ------------------------------------------------------------------ */

    if (path && path[0] != '\0') {
        newDep.path = makeString(strings, path);

        if (!installDependency(&newDep, packagesDir, strings->pool, log, /*noInstall=*/true,
                               options->package.verbose)) {
            logError(log, NULL,
                     "local path '{s}' does not contain a valid Cxy package",
                     (FormatArg[]){{.s = path}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
        goto write_cxyfile;
    }

    /* ------------------------------------------------------------------ */
    /* 9a. Registry path: resolve repository + version from registry       */
    /* ------------------------------------------------------------------ */

    if (fromRegistry) {
        RegistryResolution res;
        if (!resolveFromRegistry(packageName, constraint, registryFile, strings, log, &res)) {
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        repository      = res.repository;
        newDep.tag      = res.tag;
        newDep.version  = (constraint && constraint[0] != '\0')
                            ? makeString(strings, constraint)
                            : NULL;
        /* We intentionally don't set newDep.version to the exact semver —
         * we keep the user's original constraint so Cxyfile.yaml stays
         * correct for future `cxy package update` runs.               */
    }

    /* ------------------------------------------------------------------ */
    /* 9b. URL path: normalise, validate, fetch tags                       */
    /* ------------------------------------------------------------------ */

    if (!fromRegistry) {
        /* Normalise the repository URL */
        cstring normalizedUrl = NULL;
        if (!gitNormalizeRepositoryUrl(repository, &normalizedUrl, strings->pool, log)) {
            logError(log, NULL,
                     "failed to normalise repository URL '{s}'",
                     (FormatArg[]){{.s = repository}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }
        newDep.repository = normalizedUrl;

        /* Validate accessibility */
        printStatusSticky(log, " Validating repository accessibility...");
        if (!gitIsRepositoryAccessible(normalizedUrl, log)) {
            logError(log, NULL,
                     "repository '{s}' is not accessible or does not exist",
                     (FormatArg[]){{.s = normalizedUrl}});
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        /* For URL-based adds, if no tag was provided by the registry or the
         * user, fetch available tags and pick the best one.               */
        if (!newDep.tag || newDep.tag[0] == '\0') {
            printStatusSticky(log, " Fetching available versions...");
            DynArray tags = newDynArray(sizeof(GitTag));

            if (!gitFetchTags(normalizedUrl, &tags, strings->pool, log)) {
                logError(log, NULL,
                         "failed to fetch tags from repository '{s}'",
                         (FormatArg[]){{.s = normalizedUrl}});
                freeDynArray(&tags);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }

            if (tags.size == 0) {
                logError(log, NULL,
                         "repository '{s}' has no released versions (tags). "
                         "Remote dependencies must have at least one semantic version tag.",
                         (FormatArg[]){{.s = normalizedUrl}});
                freeDynArray(&tags);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            }

            printStatusSticky(log, " Found %u released version(s)", tags.size);

            if (!newDep.version || strcmp(newDep.version, "*") == 0) {
                /* Use the latest tag */
                GitTag *latest = &((GitTag *)tags.elems)[tags.size - 1];
                newDep.tag = latest->name;
                printStatusSticky(log, " Resolved latest version to: %s", latest->name);
            } else {
                /* Apply the version constraint against the fetched git tags */
                DynArray constraints = newDynArray(sizeof(VersionConstraint));
                VersionConstraint vc;
                if (parseVersionConstraint(newDep.version, &vc, log))
                    pushOnDynArray(&constraints, &vc);

                GitTag bestTag;
                if (!findBestMatchingVersion(normalizedUrl, &constraints, &bestTag,
                                             strings->pool, log)) {
                    logError(log, NULL,
                             "no version of '{s}' satisfies constraint '{s}'",
                             (FormatArg[]){{.s = normalizedUrl}, {.s = newDep.version}});
                    freeDynArray(&constraints);
                    freeDynArray(&tags);
                    free(packageDir);
                    freePackageMetadata(&meta);
                    return false;
                }
                newDep.tag = bestTag.name;
                printStatusSticky(log, " Resolved version to: %s", bestTag.name);
                freeDynArray(&constraints);
            }

            /* Also keep branch if explicitly provided */
            if (branch && branch[0] != '\0')
                newDep.branch = makeString(strings, branch);

            freeDynArray(&tags);
        } else if (branch && branch[0] != '\0') {
            newDep.branch = makeString(strings, branch);
        }

        if (!newDep.version && constraint && constraint[0] != '\0')
            newDep.version = makeString(strings, constraint);
    } /* end URL path */

    /* ------------------------------------------------------------------ */
    /* 10. Clone / install the dependency                                  */
    /* ------------------------------------------------------------------ */

    if (!installDependency(&newDep, packagesDir, strings->pool, log, noInstall,
                           options->package.verbose)) {
        logError(log, NULL,
                 "failed to install dependency '{s}' — not a valid Cxy package or "
                 "installation failed",
                 (FormatArg[]){{.s = packageName}});
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 11. Record download (best-effort, only for registry packages)       */
    /* ------------------------------------------------------------------ */

    if (fromRegistry && newDep.version && newDep.version[0] != '\0') {
        /*
         * Strip any constraint prefix (^, ~, >=, …) to get the plain semver
         * string for the download endpoint.  The tag is "v1.2.3"; the version
         * field on the registry is "1.2.3".  Use newDep.tag, strip leading 'v'.
         */
        const char *semver = newDep.tag;
        if (semver && semver[0] == 'v')
            semver++;
        if (semver && semver[0] != '\0')
            recordDownload(packageName, semver, registryFile, strings, log);
    }

    /* ------------------------------------------------------------------ */
    /* 12. Persist to Cxyfile.yaml                                         */
    /* ------------------------------------------------------------------ */

write_cxyfile:
    if (isDev) {
        pushOnDynArray(&meta.devDependencies, &newDep);
        printStatusSticky(log, " Added dev dependency: %s", packageName);
    } else {
        pushOnDynArray(&meta.dependencies, &newDep);
        printStatusSticky(log, " Added dependency: %s", packageName);
    }

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
        printStatusAlways(log,
                          cBMGN " Run 'cxy package install' to download and install dependencies"
                          cDEF "\n");
    }

    free(packageDir);
    freePackageMetadata(&meta);
    return true;
}