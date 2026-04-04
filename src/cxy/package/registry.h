/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

/**
 * registry.h — Cxy Package Registry HTTP client
 *
 * Provides a reusable client for the Cxy Package Registry REST API.
 * All commands that need registry interaction (add, publish, search, …)
 * should use this module instead of making raw HTTP calls themselves.
 *
 * Configuration (evaluated in priority order):
 *   1. CXY_REGISTRY_URL  – override the base URL (default: https://registry.cxy-lang.org)
 *   2. CXY_API_KEY       – API key for authenticated endpoints
 *   3. ~/.cxy/credentials – file with "api_key=<value>" and/or "registry_url=<value>"
 *
 * Thread safety: a RegistryClient must not be shared across threads without
 * external synchronisation. Each thread should create its own client.
 */

#pragma once

#include "core/utils.h"   /* cstring, bool, u32, i64, … */
#include "core/array.h"   /* DynArray                     */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Forward declarations */
typedef struct Log Log;
struct StrPool;

/* -------------------------------------------------------------------------
 * Result types (maps closely to the OpenAPI schemas)
 * ---------------------------------------------------------------------- */

/**
 * A single published version of a package.
 * Corresponds to the VersionSummary / Version schemas.
 */
typedef struct RegistryVersion {
    i64     id;           /* DB id (0 if not returned by this endpoint)  */
    cstring version;      /* Semver string, e.g. "1.2.3"                 */
    cstring tag;          /* Git tag,        e.g. "v1.2.3"               */
    cstring commit;       /* Full commit SHA (may be NULL)               */
    cstring checksum;     /* sha256:<hex>    (may be NULL)               */
    cstring publishedAt;  /* ISO 8601 timestamp                          */
    i64     downloads;
    bool    yanked;
} RegistryVersion;

/**
 * Top-level package record.
 * Corresponds to the Package schema.
 */
typedef struct RegistryPackage {
    i64     id;
    cstring name;
    cstring description;
    cstring author;
    cstring license;
    cstring repository;   /* Canonical git URL                           */
    cstring homepage;     /* May be NULL                                 */
    i64     totalDownloads;
    cstring createdAt;
    cstring updatedAt;
} RegistryPackage;

/**
 * Full details returned by GET /api/v1/packages/{name}.
 * Corresponds to the PackageDetails schema.
 */
typedef struct RegistryPackageDetails {
    RegistryPackage  pkg;
    RegistryVersion  latest;      /* Latest non-yanked version            */
    bool             hasLatest;   /* false if package has no versions yet */
    RegistryVersion *versions;    /* All versions, newest first           */
    u32              versionCount;
} RegistryPackageDetails;

/**
 * Response from GET /api/v1/packages/{name}/versions.
 */
typedef struct RegistryVersionList {
    cstring          packageName;
    RegistryVersion *versions;
    u32              count;
} RegistryVersionList;

/* -------------------------------------------------------------------------
 * Client
 * ---------------------------------------------------------------------- */

/**
 * Opaque HTTP client context.
 * Initialise with registryClientInit(), release with registryClientFree().
 */
typedef struct RegistryClient RegistryClient;

/**
 * Initialise a registry client.
 *
 * Reads configuration from environment variables and/or ~/.cxy/credentials.
 * Calling code retains ownership of `pool` and `log`; they must outlive the
 * client.
 *
 * @param pool  Memory pool used for all string allocations inside result structs.
 * @param log   Logger for errors and verbose output.
 * @return      Heap-allocated client, or NULL on failure (e.g. libcurl unavailable).
 */
RegistryClient *registryClientInit(struct StrPool *strings, Log *log, const char *filePath);

/**
 * Release a registry client and all internal resources (curl handle, etc.).
 * Does nothing if `client` is NULL.
 */
void registryClientFree(RegistryClient *client);

/**
 * Return the base registry URL the client is configured to use.
 * The pointer is valid for the lifetime of `client`.
 */
const char *registryClientBaseUrl(const RegistryClient *client);

/**
 * Return true if the client has an API key configured.
 * Authenticated endpoints (publish, manage keys) require this.
 */
bool registryClientHasApiKey(const RegistryClient *client);

/* -------------------------------------------------------------------------
 * Package lookup
 * ---------------------------------------------------------------------- */

/**
 * Fetch full details for a package by name.
 *
 * Calls GET /api/v1/packages/{name}.
 *
 * @param client    Initialised registry client.
 * @param name      Package name (e.g. "cxy-json").
 * @param out       Filled in on success; zero-initialised on failure.
 * @return true on success, false if the package was not found or a network /
 *         parse error occurred.
 */
bool registryGetPackage(RegistryClient *client,
                        const char *name,
                        RegistryPackageDetails *out);

/**
 * Free heap memory inside a RegistryPackageDetails that was filled by
 * registryGetPackage().  Does not free `details` itself.
 */
void registryPackageDetailsFree(RegistryPackageDetails *details);

/**
 * Fetch a specific version of a package.
 *
 * Calls GET /api/v1/packages/{name}/{version}.
 *
 * @param client    Initialised registry client.
 * @param name      Package name.
 * @param version   Semver string, e.g. "1.2.3" (without the "v" prefix).
 * @param out       Filled in on success.
 * @return true on success.
 */
bool registryGetVersion(RegistryClient *client,
                        const char *name,
                        const char *version,
                        RegistryVersion *out);

/**
 * List all versions of a package (newest first).
 *
 * Calls GET /api/v1/packages/{name}/versions.
 *
 * @param client    Initialised registry client.
 * @param name      Package name.
 * @param out       Filled in on success.
 * @return true on success.
 */
bool registryListVersions(RegistryClient *client,
                          const char *name,
                          RegistryVersionList *out);

/**
 * Free heap memory inside a RegistryVersionList.  Does not free `list` itself.
 */
void registryVersionListFree(RegistryVersionList *list);

/* -------------------------------------------------------------------------
 * Version resolution
 * ---------------------------------------------------------------------- */

/**
 * Find the best version of a package that satisfies a semver constraint.
 *
 * Fetches the version list from the registry and applies the constraint
 * (same syntax as Cxyfile.yaml: "^1.0.0", "~2.1.0", ">=1.0.0", "*", …).
 * Yanked versions are always skipped.
 *
 * @param client      Initialised registry client.
 * @param name        Package name.
 * @param constraint  Version constraint string, or NULL/"*" for latest.
 * @param out         Best matching version, filled on success.
 * @return true if a satisfying version was found.
 */
bool registryResolveVersion(RegistryClient *client,
                            const char *name,
                            const char *constraint,
                            RegistryVersion *out);

/* -------------------------------------------------------------------------
 * Download tracking
 * ---------------------------------------------------------------------- */

/**
 * Record a download for a specific package version.
 *
 * Calls POST /api/v1/packages/{name}/{version}/download.
 * This endpoint is unauthenticated; failures are logged as warnings only
 * so that network issues never block an install.
 *
 * @param client   Initialised registry client.
 * @param name     Package name.
 * @param version  Semver version string (e.g. "1.2.3").
 * @return true if the server acknowledged the download, false otherwise.
 */
bool registryRecordDownload(RegistryClient *client,
                            const char *name,
                            const char *version);

/* -------------------------------------------------------------------------
 * Publishing (used by `cxy package publish`)
 * ---------------------------------------------------------------------- */

/**
 * Data needed to publish a new package version.
 * Corresponds to the PublishRequest schema.
 */
typedef struct RegistryPublishRequest {
    const char *repository;    /* Git repository URL (required)           */
    const char *tag;           /* Git tag, e.g. "v1.2.3" (required)      */
    const char *commit;        /* Full commit SHA  (required)             */
    const char *checksum;      /* sha256:<hex>     (optional)            */
    /* PackageMetadata fields */
    const char *name;
    const char *version;
    const char *description;
    const char *author;
    const char *license;
    const char *homepage;
    const char *readme;        /* Raw markdown content (optional)         */
    /* Dependencies to record (may be NULL / count 0) */
    u32         depCount;
    struct {
        const char *name;
        const char *repository;
        const char *versionConstraint;
        bool        isDev;
    } *deps;
} RegistryPublishRequest;

/**
 * Publish a new package version to the registry.
 *
 * Calls POST /api/v1/packages.
 * Requires an API key (bearer token).
 *
 * @param client  Initialised client (must have an API key).
 * @param req     Publish request data.
 * @param outUrl  If non-NULL, receives the canonical registry URL of the
 *                published package (allocated from the client's pool).
 * @return true on success (HTTP 201).
 */
bool registryPublish(RegistryClient *client,
                     const RegistryPublishRequest *req,
                     const char **outUrl);

/* -------------------------------------------------------------------------
 * Credentials helpers
 * ---------------------------------------------------------------------- */

/**
 * Load registry credentials from the user's credentials file
 * (~/.cxy/credentials).
 *
 * The file uses a simple line-oriented key=value format:
 *   api_key=cxy_live_xxxx
 *   registry_url=https://my-mirror.example.com
 *
 * Lines starting with '#' are comments.  Unknown keys are ignored.
 *
 * @param outApiKey       If non-NULL, receives the api_key value (may remain
 *                        NULL if not present in file).
 * @param outRegistryUrl  If non-NULL, receives the registry_url value.
 * @param strings         String pool for allocations.
 * @return true if the file was found and parsed without errors, false if
 *         the file does not exist (not an error) or is malformed.
 */
bool registryLoadCredentials(const char **outApiKey,
                             const char **outRegistryUrl,
                             struct StrPool *strings);

/**
 * Write (or update) a key in the credentials file.
 *
 * Creates ~/.cxy/ and ~/.cxy/credentials if they don't exist.
 *
 * @param key    Credential key ("api_key" or "registry_url").
 * @param value  New value.
 * @return true on success.
 */
bool registrySaveCredential(const char *key, const char *value);


/* -------------------------------------------------------------------------
 * Authentication
 * ---------------------------------------------------------------------- */

typedef struct RegistryLoginResponse {
    i64     id;
    cstring login;
    cstring name;
    cstring email;
    cstring avatarUrl;
    bool    isAdmin;
} RegistryLoginResponse;

bool registryLogin(RegistryClient *client, RegistryLoginResponse *out);

/* -------------------------------------------------------------------------
 * Yanking
 * ---------------------------------------------------------------------- */

/**
 * Yank (or un-yank) a specific version of a package.
 *
 * Yanked versions are hidden from fresh dependency resolution but remain
 * accessible to projects that have already pinned that version in their
 * lockfile. This matches the semantics used by Cargo and PyPI.
 *
 * Calls PATCH /api/v1/packages/{name}/{version}/yank.
 * Requires an API key (bearer token).
 *
 * @param client   Initialised client (must have an API key).
 * @param name     Package name (e.g. "cxy-json").
 * @param version  Semver version string without the "v" prefix (e.g. "1.2.3").
 * @param undo     If true, un-yank the version (restore it to the index).
 * @return true on success (HTTP 200).
 */
bool registryYank(RegistryClient *client,
                  const char *name,
                  const char *version,
                  bool undo);

/* -------------------------------------------------------------------------
 * Search
 * ---------------------------------------------------------------------- */

typedef struct PackageSummary {
    cstring name;
    cstring description;
    cstring author;
    cstring license;
    cstring repository;
    cstring latestVersion;
    i64     totalDownloads;
    cstring updatedAt;
} PackageSummary;

typedef struct RegistrySearchResult {
    i64             total;
    PackageSummary *packages;
    u32             count;
} RegistrySearchResult;

bool registrySearchPackages(RegistryClient *client,
                            const char *query,
                            int limit,
                            int offset,
                            const char *sort,
                            RegistrySearchResult *out);

void registrySearchResultFree(RegistrySearchResult *result);

#ifdef __cplusplus
}
#endif