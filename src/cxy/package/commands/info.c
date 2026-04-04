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
#include "package/registry.h"
#include "core/log.h"
#include "core/strpool.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static bool isLocallyInstalled(const char *name, char *outDir, size_t dirSize)
{
    snprintf(outDir, dirSize, ".cxy/packages/%s", name);
    char cxyfilePath[2048];
    snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", outDir);
    struct stat st;
    return stat(cxyfilePath, &st) == 0 && S_ISREG(st.st_mode);
}

static void printField(const char *label, const char *value)
{
    if (value && value[0] != '\0')
        printf("  " cBBLU "%-14s" cDEF " %s\n", label, value);
}

static void printDownloadCount(i64 n)
{
    if (n >= 1000000)
        printf("  " cBBLU "%-14s" cDEF " %.1fM\n", "Downloads:", (double)n / 1000000.0);
    else if (n >= 1000)
        printf("  " cBBLU "%-14s" cDEF " %.1fK\n", "Downloads:", (double)n / 1000.0);
    else
        printf("  " cBBLU "%-14s" cDEF " %lld\n",  "Downloads:", (long long)n);
}

/* -------------------------------------------------------------------------
 * Human-readable output
 * ---------------------------------------------------------------------- */

static void outputHuman(const RegistryPackageDetails *reg,
                        const PackageMetadata *local,
                        bool installed,
                        const char *installDir)
{
    /* Use registry data if available, otherwise fall back to local */
    const char *name        = reg ? reg->pkg.name        : (local ? local->name        : NULL);
    const char *description = reg ? reg->pkg.description : (local ? local->description : NULL);
    const char *author      = reg ? reg->pkg.author      : (local ? local->author      : NULL);
    const char *license     = reg ? reg->pkg.license     : (local ? local->license     : NULL);
    const char *repository  = reg ? reg->pkg.repository  : (local ? local->repository  : NULL);
    const char *homepage    = reg ? reg->pkg.homepage    : (local ? local->homepage    : NULL);
    const char *latestVer   = (reg && reg->hasLatest) ? reg->latest.version : NULL;
    const char *localVer    = local ? local->version : NULL;

    printf("\n");
    printf(cBBLU " %s" cDEF, name ? name : "(unknown)");
    if (latestVer)
        printf("  v%s", latestVer);
    else if (localVer)
        printf("  v%s", localVer);
    printf("\n");
    printf(" %s\n", description ? description : "No description available.");
    printf("\n");

    printField("Author:",     author);
    printField("License:",    license);
    printField("Repository:", repository);
    printField("Homepage:",   homepage);

    if (reg)
        printDownloadCount(reg->pkg.totalDownloads);

    /* Installation status */
    printf("\n");
    if (installed) {
        printf("  " cBBLU "%-14s" cDEF " " cBGRN "Installed" cDEF, "Status:");
        if (localVer)
            printf(" (v%s)", localVer);
        printf("\n");
        printf("  " cBBLU "%-14s" cDEF " %s\n", "Location:", installDir);
    } else {
        printf("  " cBBLU "%-14s" cDEF " " cBYLW "Not installed" cDEF "\n", "Status:");
    }

    /* Dependencies from local Cxyfile */
    if (local && local->dependencies.size > 0) {
        printf("\n");
        printf("  " cBBLU "Dependencies (%u):\n" cDEF, (u32)local->dependencies.size);
        for (u32 i = 0; i < local->dependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)local->dependencies.elems)[i];
            if (dep->version && dep->version[0] != '\0')
                printf("    - %s  %s\n", dep->name, dep->version);
            else
                printf("    - %s\n", dep->name);
        }
    }

    /* All versions from registry */
    if (reg && reg->versionCount > 0) {
        printf("\n");
        printf("  " cBBLU "Versions (%u):\n" cDEF, reg->versionCount);
        /* Show newest first, cap at 10 */
        u32 show = reg->versionCount < 10 ? reg->versionCount : 10;
        for (u32 i = 0; i < show; i++) {
            const RegistryVersion *v = &reg->versions[i];
            printf("    %s%-8s" cDEF,
                   (latestVer && v->version && strcmp(v->version, latestVer) == 0)
                       ? cBGRN : "",
                   v->version ? v->version : "?");
            if (v->tag)
                printf("  %s", v->tag);
            if (v->yanked)
                printf("  " cBRED "[yanked]" cDEF);
            printf("\n");
        }
        if (reg->versionCount > 10)
            printf("    … and %u more\n", reg->versionCount - 10);
    }

    printf("\n");
}

/* -------------------------------------------------------------------------
 * JSON output
 * ---------------------------------------------------------------------- */

static void outputJson(const RegistryPackageDetails *reg,
                       const PackageMetadata *local,
                       bool installed,
                       const char *installDir)
{
    cJSON *root = cJSON_CreateObject();

    /* Basic fields — prefer registry */
    const char *name       = reg ? reg->pkg.name       : (local ? local->name       : NULL);
    const char *desc       = reg ? reg->pkg.description: (local ? local->description: NULL);
    const char *author     = reg ? reg->pkg.author     : (local ? local->author     : NULL);
    const char *license    = reg ? reg->pkg.license    : (local ? local->license    : NULL);
    const char *repository = reg ? reg->pkg.repository : (local ? local->repository : NULL);
    const char *homepage   = reg ? reg->pkg.homepage   : (local ? local->homepage   : NULL);

    if (name)       cJSON_AddStringToObject(root, "name",        name);
    if (desc)       cJSON_AddStringToObject(root, "description", desc);
    if (author)     cJSON_AddStringToObject(root, "author",      author);
    if (license)    cJSON_AddStringToObject(root, "license",     license);
    if (repository) cJSON_AddStringToObject(root, "repository",  repository);
    if (homepage)   cJSON_AddStringToObject(root, "homepage",    homepage);

    if (reg) {
        cJSON_AddNumberToObject(root, "total_downloads", (double)reg->pkg.totalDownloads);
        if (reg->hasLatest && reg->latest.version)
            cJSON_AddStringToObject(root, "latest_version", reg->latest.version);
    }

    /* Installation */
    cJSON_AddBoolToObject(root, "installed", installed);
    if (installed) {
        if (local && local->version)
            cJSON_AddStringToObject(root, "installed_version", local->version);
        if (installDir)
            cJSON_AddStringToObject(root, "install_location", installDir);
    }

    /* Dependencies from local */
    if (local && local->dependencies.size > 0) {
        cJSON *deps = cJSON_CreateArray();
        for (u32 i = 0; i < local->dependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)local->dependencies.elems)[i];
            cJSON *obj = cJSON_CreateObject();
            if (dep->name)       cJSON_AddStringToObject(obj, "name",       dep->name);
            if (dep->version)    cJSON_AddStringToObject(obj, "version",    dep->version);
            if (dep->repository) cJSON_AddStringToObject(obj, "repository", dep->repository);
            cJSON_AddItemToArray(deps, obj);
        }
        cJSON_AddItemToObject(root, "dependencies", deps);
    }

    /* Versions from registry */
    if (reg && reg->versionCount > 0) {
        cJSON *versions = cJSON_CreateArray();
        for (u32 i = 0; i < reg->versionCount; i++) {
            const RegistryVersion *v = &reg->versions[i];
            cJSON *obj = cJSON_CreateObject();
            if (v->version)     cJSON_AddStringToObject(obj, "version",      v->version);
            if (v->tag)         cJSON_AddStringToObject(obj, "tag",          v->tag);
            if (v->commit)      cJSON_AddStringToObject(obj, "commit",       v->commit);
            if (v->publishedAt) cJSON_AddStringToObject(obj, "published_at", v->publishedAt);
            cJSON_AddNumberToObject(obj, "downloads", (double)v->downloads);
            cJSON_AddBoolToObject(obj, "yanked", v->yanked);
            cJSON_AddItemToArray(versions, obj);
        }
        cJSON_AddItemToObject(root, "versions", versions);
    }

    char *json = cJSON_Print(root);
    if (json) {
        printf("%s\n", json);
        free(json);
    }
    cJSON_Delete(root);
}

/* -------------------------------------------------------------------------
 * Command
 * ---------------------------------------------------------------------- */

bool packageInfoCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *packageName  = options->package.package;
    const char *registryFile = options->package.registryFile;
    bool        asJson       = options->package.json;

    if (!packageName || packageName[0] == '\0') {
        logError(log, NULL,
                 "package name required. Usage: cxy package info <name>", NULL);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 1. Query registry (best-effort)                                     */
    /* ------------------------------------------------------------------ */

    RegistryPackageDetails regDetails;
    memset(&regDetails, 0, sizeof(regDetails));
    bool hasRegistry = false;

    RegistryClient *client = registryClientInit(strings, log, registryFile);
    if (client) {
        hasRegistry = registryGetPackage(client, packageName, &regDetails);
        registryClientFree(client);
    }

    /* ------------------------------------------------------------------ */
    /* 2. Check local installation                                         */
    /* ------------------------------------------------------------------ */

    char installDir[1024];
    bool installed = isLocallyInstalled(packageName, installDir, sizeof(installDir));

    PackageMetadata localMeta;
    PackageMetadata *localMetaPtr = NULL;

    initPackageMetadata(&localMeta, strings);

    if (installed) {
        char cxyfilePath[2048];
        snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", installDir);
        char *pkgDir = NULL;
        if (findAndLoadCxyfile(installDir, &localMeta, strings, log, &pkgDir)) {
            localMetaPtr = &localMeta;
            free(pkgDir);
        }
    }

    /* ------------------------------------------------------------------ */
    /* 3. Nothing found at all                                             */
    /* ------------------------------------------------------------------ */

    if (!hasRegistry && !installed) {
        logError(log, NULL,
                 "package '{s}' not found in registry or local installation",
                 (FormatArg[]){{.s = packageName}});
        freePackageMetadata(&localMeta);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 4. Output                                                           */
    /* ------------------------------------------------------------------ */

    if (asJson)
        outputJson(hasRegistry ? &regDetails : NULL, localMetaPtr, installed, installDir);
    else
        outputHuman(hasRegistry ? &regDetails : NULL, localMetaPtr, installed, installDir);

    if (hasRegistry)
        registryPackageDetailsFree(&regDetails);
    freePackageMetadata(&localMeta);
    return true;
}