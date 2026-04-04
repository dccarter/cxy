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
#include "package/types.h"
#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/**
 * Prompt the user for a yes/no answer. Returns true if the user answered yes.
 * `defaultYes` controls what a bare Enter press means.
 */
static bool promptYesNo(const char *question, bool defaultYes)
{
    printf("%s [%s] ", question, defaultYes ? "Y/n" : "y/N");
    fflush(stdout);

    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) {
        printf("\n");
        return defaultYes;
    }

    int n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    if (n == 0)
        return defaultYes;

    return (buf[0] == 'y' || buf[0] == 'Y');
}

/**
 * Display a proposed string and allow the user to accept or override it.
 * An empty input accepts the proposed value.
 * Writes the chosen value into `out` (size `outLen`).
 */
static void promptConfirmString(const char *label,
                                const char *proposed,
                                char *out,
                                size_t outLen)
{
    printf("%s [%s]: ", label, proposed ? proposed : "");
    fflush(stdout);

    char buf[512];
    if (!fgets(buf, sizeof(buf), stdin)) {
        printf("\n");
        strncpy(out, proposed ? proposed : "", outLen - 1);
        out[outLen - 1] = '\0';
        return;
    }

    int n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    if (n == 0)
        strncpy(out, proposed ? proposed : "", outLen - 1);
    else
        strncpy(out, buf, outLen - 1);

    out[outLen - 1] = '\0';
}

/**
 * Read a file into a heap-allocated buffer.
 * Returns NULL if the file does not exist or cannot be read.
 * Caller must free() the result.
 */
static char *readFileContents(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[nread] = '\0';
    return buf;
}

/* -------------------------------------------------------------------------
 * Version bumping
 * ---------------------------------------------------------------------- */

typedef enum {
    BumpNone,
    BumpMajor,
    BumpMinor,
    BumpPatch,
} BumpKind;

static BumpKind parseBumpKind(const char *bump)
{
    if (!bump || bump[0] == '\0') return BumpNone;
    if (strcmp(bump, "major") == 0) return BumpMajor;
    if (strcmp(bump, "minor") == 0) return BumpMinor;
    if (strcmp(bump, "patch") == 0) return BumpPatch;
    return BumpNone;
}

/**
 * Increment a semver string according to `kind`.
 * Writes the result into `out` (size `outLen`). Returns true on success.
 */
static bool bumpVersion(const char *current,
                        BumpKind kind,
                        char *out,
                        size_t outLen,
                        Log *log)
{
    SemanticVersion sv;
    if (!parseSemanticVersion(current, &sv, log)) {
        logError(log, NULL,
                 "cannot parse current version '{s}' as semver",
                 (FormatArg[]){{.s = current}});
        return false;
    }

    switch (kind) {
        case BumpMajor: sv.major++; sv.minor = 0; sv.patch = 0; break;
        case BumpMinor: sv.minor++;               sv.patch = 0; break;
        case BumpPatch: sv.patch++;                             break;
        default:
            strncpy(out, current, outLen - 1);
            out[outLen - 1] = '\0';
            return true;
    }

    snprintf(out, outLen, "%u.%u.%u", sv.major, sv.minor, sv.patch);
    return true;
}

/* -------------------------------------------------------------------------
 * Command
 * ---------------------------------------------------------------------- */

bool packagePublishCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *bumpArg      = options->package.bump;
    const char *tagArg       = options->package.tagName;
    const char *messageArg   = options->package.message;
    bool        dryRun       = options->package.dryRun;
    bool        verbose      = options->package.verbose;
    const char *registryFile = options->package.registryFile;

    BumpKind bumpKind = parseBumpKind(bumpArg);

    if (bumpArg && bumpArg[0] != '\0' && bumpKind == BumpNone) {
        logError(log, NULL,
                 "invalid --bump value '{s}': expected major, minor, or patch",
                 (FormatArg[]){{.s = bumpArg}});
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 1. Load Cxyfile.yaml                                                */
    /* ------------------------------------------------------------------ */

    PackageMetadata meta;
    char *packageDir = NULL;
    initPackageMetadata(&meta, strings);

    if (!findAndLoadCxyfile(NULL, &meta, strings, log, &packageDir)) {
        logError(log, NULL,
                 "no Cxyfile.yaml found in current directory or parent directories",
                 NULL);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 2. Validate metadata                                                */
    /* ------------------------------------------------------------------ */

    if (!validatePackageMetadata(&meta, log)) {
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 3. Check for uncommitted changes                                    */
    /* ------------------------------------------------------------------ */

    bool hasDirtyTree = false;

    if (!dryRun) {
        if (!gitHasUncommittedChanges(packageDir, &hasDirtyTree, log)) {
            logError(log, NULL, "failed to check git working tree status", NULL);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        if (hasDirtyTree) {
            if (bumpKind == BumpNone) {
                /* No bump — dirty tree is not allowed */
                logError(log, NULL,
                         "working tree has uncommitted changes. "
                         "Commit or stash them before publishing.",
                         NULL);
                free(packageDir);
                freePackageMetadata(&meta);
                return false;
            } else {
                /* Bump requested — warn and ask the user */
                printf("\n");
                printf(cBYLW "Warning:" cDEF
                       " Your working tree has uncommitted changes.\n"
                       "         They will be included in the version bump commit.\n\n");
                if (!promptYesNo("Continue?", false)) {
                    logError(log, NULL, "publish aborted by user", NULL);
                    free(packageDir);
                    freePackageMetadata(&meta);
                    return false;
                }
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* 4. Version bump (if requested)                                      */
    /* ------------------------------------------------------------------ */

    char newVersion[64];
    strncpy(newVersion,
            meta.version ? meta.version : "0.0.0",
            sizeof(newVersion) - 1);
    newVersion[sizeof(newVersion) - 1] = '\0';

    if (bumpKind != BumpNone && !dryRun) {
        if (!bumpVersion(meta.version, bumpKind, newVersion, sizeof(newVersion), log)) {
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        printf("\n");
        printf("Bumping version: " cBCYN "%s" cDEF " → " cBGRN "%s" cDEF "\n",
               meta.version, newVersion);

        /* Let the user confirm/edit the commit message */
        char defaultMsg[128];
        snprintf(defaultMsg, sizeof(defaultMsg), "Bump version to %s", newVersion);

        char commitMsg[512];
        promptConfirmString("Commit message", defaultMsg, commitMsg, sizeof(commitMsg));
        printf("\n");

        if (!promptYesNo("Proceed with commit?", true)) {
            logError(log, NULL, "publish aborted by user", NULL);
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        /* Update in-memory metadata and persist Cxyfile.yaml */
        meta.version = makeString(strings, newVersion);

        char cxyfilePath[1024];
        snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packageDir);

        if (!writeCxyfile(cxyfilePath, &meta, log)) {
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        /* Stage and commit via gitops (stageAll when user confirmed dirty tree) */
        if (!gitStageAndCommit(packageDir, hasDirtyTree, commitMsg, log)) {
            free(packageDir);
            freePackageMetadata(&meta);
            return false;
        }

        printStatusAlways(log, cBGRN "✔" cDEF " Committed version bump: %s\n", newVersion);
    }

    /* ------------------------------------------------------------------ */
    /* 5. Resolve tag name                                                 */
    /* ------------------------------------------------------------------ */

    char tagName[128];
    if (tagArg && tagArg[0] != '\0') {
        strncpy(tagName, tagArg, sizeof(tagName) - 1);
        tagName[sizeof(tagName) - 1] = '\0';
    } else {
        snprintf(tagName, sizeof(tagName), "v%s", newVersion);
    }

    /* ------------------------------------------------------------------ */
    /* 6. Resolve tag annotation message                                   */
    /* ------------------------------------------------------------------ */

    char tagMessage[512];
    if (messageArg && messageArg[0] != '\0') {
        strncpy(tagMessage, messageArg, sizeof(tagMessage) - 1);
        tagMessage[sizeof(tagMessage) - 1] = '\0';
    } else {
        snprintf(tagMessage, sizeof(tagMessage),
                 "%s %s", meta.name ? meta.name : "package", tagName);
    }

    /* ------------------------------------------------------------------ */
    /* 7. Dry-run summary                                                  */
    /* ------------------------------------------------------------------ */

    if (dryRun) {
        printf("\n");
        printf(cBCYN "Dry run — no changes will be made\n\n" cDEF);
        printf("  Package   : %s\n", meta.name       ? meta.name       : "(unset)");
        printf("  Version   : %s\n", newVersion);
        printf("  Tag       : %s\n", tagName);
        printf("  Message   : %s\n", tagMessage);
        printf("  Repository: %s\n", meta.repository ? meta.repository : "(unset)");
        printf("  Registry  : %s\n", registryFile    ? registryFile    : "(default)");
        printf("\n");
        free(packageDir);
        freePackageMetadata(&meta);
        return true;
    }

    /* ------------------------------------------------------------------ */
    /* 8. Create and push git tag                                          */
    /* ------------------------------------------------------------------ */

    printStatusSticky(log, "Creating and pushing tag '%s'...", tagName);

    if (!gitCreateTag(packageDir, tagName, tagMessage, /*push=*/true, log, verbose)) {
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    printStatusAlways(log, cBGRN "✔" cDEF " Tag '%s' created and pushed\n", tagName);

    /* ------------------------------------------------------------------ */
    /* 9. Gather git metadata                                              */
    /* ------------------------------------------------------------------ */

    cstring commit   = NULL;
    cstring repoUrl  = NULL;
    cstring checksum = NULL;

    gitGetCurrentCommit(packageDir, &commit, strings->pool, log);

    /* Prefer what git reports for 'origin'; fall back to Cxyfile.yaml */
    if (!gitGetRemoteUrl(packageDir, "origin", &repoUrl, strings->pool, log) || !repoUrl)
        repoUrl = meta.repository;

    gitCalculateChecksum(packageDir, &checksum, strings->pool, log);

    /* ------------------------------------------------------------------ */
    /* 10. Read README.md (optional)                                       */
    /* ------------------------------------------------------------------ */

    char readmePath[1024];
    snprintf(readmePath, sizeof(readmePath), "%s/README.md", packageDir);
    char *readme = readFileContents(readmePath);
    /* readme may be NULL — the registry field is optional */

    /* ------------------------------------------------------------------ */
    /* 11. Build RegistryPublishRequest                                    */
    /* ------------------------------------------------------------------ */

    /* Flatten dependencies into the request's dep array */
    u32 totalDeps = meta.dependencies.size + meta.devDependencies.size;

    RegistryPublishRequest req;
    memset(&req, 0, sizeof(req));
    req.repository  = repoUrl;
    req.tag         = tagName;
    req.commit      = commit;
    req.checksum    = checksum;
    req.readme      = readme;
    req.name        = meta.name;
    req.version     = newVersion;
    req.description = meta.description;
    req.author      = meta.author;
    req.license     = meta.license;
    req.homepage    = meta.homepage;
    req.depCount    = totalDeps;

    if (totalDeps > 0) {
        req.deps = calloc(totalDeps, sizeof(*req.deps));
        if (req.deps) {
            u32 idx = 0;
            for (u32 i = 0; i < meta.dependencies.size; i++) {
                PackageDependency *d = &((PackageDependency *)meta.dependencies.elems)[i];
                req.deps[idx].name              = d->name;
                req.deps[idx].repository        = d->repository;
                req.deps[idx].versionConstraint = d->version;
                req.deps[idx].isDev             = false;
                idx++;
            }
            for (u32 i = 0; i < meta.devDependencies.size; i++) {
                PackageDependency *d = &((PackageDependency *)meta.devDependencies.elems)[i];
                req.deps[idx].name              = d->name;
                req.deps[idx].repository        = d->repository;
                req.deps[idx].versionConstraint = d->version;
                req.deps[idx].isDev             = true;
                idx++;
            }
        }
    }

    /* ------------------------------------------------------------------ */
    /* 12. Initialise registry client                                      */
    /* ------------------------------------------------------------------ */

    RegistryClient *client = registryClientInit(strings, log, registryFile);
    if (!client) {
        logError(log, NULL,
                 "could not initialise registry client. "
                 "Check network or set CXY_REGISTRY_URL.",
                 NULL);
        free(readme);
        free(req.deps);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    if (!registryClientHasApiKey(client)) {
        logError(log, NULL,
                 "no API key found. Run 'cxy package login' first.",
                 NULL);
        registryClientFree(client);
        free(readme);
        free(req.deps);
        free(packageDir);
        freePackageMetadata(&meta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 13. Publish                                                         */
    /* ------------------------------------------------------------------ */

    printStatusSticky(log, "Publishing %s@%s to registry...", meta.name, newVersion);

    const char *outUrl = NULL;
    bool ok = registryPublish(client, &req, &outUrl);

    registryClientFree(client);
    free(readme);
    free(req.deps);
    free(packageDir);
    freePackageMetadata(&meta);

    if (!ok)
        return false;

    printf("\n");
    printStatusAlways(log, cBGRN "✔" cDEF " Published " cBCYN "%s" cDEF "@" cBCYN "%s" cDEF "\n",
                      meta.name ? meta.name : "package", newVersion);
    if (outUrl)
        printStatusAlways(log, "  " cBCYN "%s" cDEF "\n", outUrl);
    printf("\n");

    return true;
}