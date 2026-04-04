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
#include "package/registry.h"
#include "core/log.h"
#include "core/strpool.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/** Truncate a string to maxLen, appending "…" if truncated. */
static void printTruncated(const char *str, int maxLen)
{
    if (!str || str[0] == '\0') {
        printf("%-*s", maxLen, "-");
        return;
    }
    int len = (int)strlen(str);
    if (len <= maxLen) {
        printf("%-*s", maxLen, str);
    } else {
        /* Print maxLen-1 chars + ellipsis */
        printf("%.*s…", maxLen - 1, str);
    }
}

/** Format a download count: 1234567 → "1.2M", 12345 → "12.3K", etc. */
static void printDownloads(i64 n)
{
    if (n >= 1000000)
        printf("%5.1fM", (double)n / 1000000.0);
    else if (n >= 1000)
        printf("%5.1fK", (double)n / 1000.0);
    else
        printf("%5lld ", (long long)n);
}

/* -------------------------------------------------------------------------
 * Output formats
 * ---------------------------------------------------------------------- */

static void outputTable(const RegistrySearchResult *result,
                        int offset)
{
    if (result->count == 0) {
        printf("No packages found.\n");
        return;
    }

    /* Header */
    printf("\n");
    printf(" %-30s %-12s %-20s %-7s  %s\n",
           "NAME", "VERSION", "AUTHOR", "DL", "DESCRIPTION");
    printf(" %-30s %-12s %-20s %-7s  %s\n",
           "------------------------------",
           "------------",
           "--------------------",
           "-------",
           "-----------");

    for (u32 i = 0; i < result->count; i++) {
        const PackageSummary *p = &result->packages[i];
        printf(" ");
        printTruncated(p->name,          30);
        printf(" ");
        printTruncated(p->latestVersion, 12);
        printf(" ");
        printTruncated(p->author,        20);
        printf(" ");
        printDownloads(p->totalDownloads);
        printf("  ");
        printTruncated(p->description,   50);
        printf("\n");
    }

    printf("\n");
    printf(" Showing %d–%d of %lld package(s)\n",
           offset + 1,
           offset + (int)result->count,
           (long long)result->total);
    printf("\n");
}

static void outputJson(const RegistrySearchResult *result)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "total", (double)result->total);

    cJSON *pkgs = cJSON_CreateArray();
    for (u32 i = 0; i < result->count; i++) {
        const PackageSummary *p = &result->packages[i];
        cJSON *obj = cJSON_CreateObject();

        if (p->name)          cJSON_AddStringToObject(obj, "name",            p->name);
        if (p->description)   cJSON_AddStringToObject(obj, "description",     p->description);
        if (p->author)        cJSON_AddStringToObject(obj, "author",          p->author);
        if (p->license)       cJSON_AddStringToObject(obj, "license",         p->license);
        if (p->repository)    cJSON_AddStringToObject(obj, "repository",      p->repository);
        if (p->latestVersion) cJSON_AddStringToObject(obj, "latest_version",  p->latestVersion);
        if (p->updatedAt)     cJSON_AddStringToObject(obj, "updated_at",      p->updatedAt);
        cJSON_AddNumberToObject(obj, "total_downloads", (double)p->totalDownloads);

        cJSON_AddItemToArray(pkgs, obj);
    }
    cJSON_AddItemToObject(root, "packages", pkgs);

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

bool packageListCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *query  = options->package.listQuery;
    int         limit  = options->package.listLimit;
    int         offset = options->package.listOffset;
    const char *sort   = options->package.listSort;
    bool        asJson = options->package.json;

    /* Defaults */
    if (limit  <= 0)  limit  = 20;
    if (offset <  0)  offset = 0;
    if (!sort || sort[0] == '\0') sort = "relevance";

    /* ------------------------------------------------------------------ */
    /* 1. Initialise registry client                                        */
    /* ------------------------------------------------------------------ */

    RegistryClient *client = registryClientInit(strings, log, options->package.registryFile);
    if (!client) {
        logError(log, NULL,
                 "could not initialise registry client. "
                 "Check network or set CXY_REGISTRY_URL.",
                 NULL);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 2. Search                                                           */
    /* ------------------------------------------------------------------ */

    if (!asJson) {
        if (query && query[0] != '\0')
            printStatusSticky(log, "Searching registry for '%s'...", query);
        else
            printStatusSticky(log, "Listing packages from registry...");
    }

    RegistrySearchResult result;
    if (!registrySearchPackages(client, query, limit, offset, sort, &result)) {
        registryClientFree(client);
        return false;
    }

    registryClientFree(client);

    /* ------------------------------------------------------------------ */
    /* 3. Output                                                           */
    /* ------------------------------------------------------------------ */

    if (asJson)
        outputJson(&result);
    else
        outputTable(&result, offset);

    registrySearchResultFree(&result);
    return true;
}