/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/registry.h"
#include "package/types.h"
#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <unistd.h>

/* -------------------------------------------------------------------------
 * Internal types
 * ---------------------------------------------------------------------- */

#define REGISTRY_DEFAULT_URL "https://registry.cxy-lang.org"
#define CREDENTIALS_FILE     ".cxy/credentials"
#define REGISTRY_API_VERSION "/api/v1"

/** Dynamic byte buffer used as a curl write target. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} ByteBuf;

struct RegistryClient {
    CURL        *curl;
    char         baseUrl[512];
    char         apiKey[256];      /* empty string if not configured */
    struct StrPool *strings;
    Log          *log;
    bool          verbose;
};

/* -------------------------------------------------------------------------
 * ByteBuf helpers
 * ---------------------------------------------------------------------- */

static void byteBufInit(ByteBuf *buf)
{
    buf->data = NULL;
    buf->len  = 0;
    buf->cap  = 0;
}

static void byteBufFree(ByteBuf *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len  = 0;
    buf->cap  = 0;
}

static size_t byteBufWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ByteBuf *buf   = (ByteBuf *)userdata;
    size_t   bytes = size * nmemb;
    size_t   needed = buf->len + bytes + 1;

    if (needed > buf->cap) {
        size_t newCap = buf->cap == 0 ? 4096 : buf->cap * 2;
        while (newCap < needed)
            newCap *= 2;
        char *newData = realloc(buf->data, newCap);
        if (!newData)
            return 0; /* signal error to curl */
        buf->data = newData;
        buf->cap  = newCap;
    }

    memcpy(buf->data + buf->len, ptr, bytes);
    buf->len += bytes;
    buf->data[buf->len] = '\0';
    return bytes;
}

/* -------------------------------------------------------------------------
 * Credentials file
 * ---------------------------------------------------------------------- */

static void getCredentialsPath(char *out, size_t outLen)
{
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    snprintf(out, outLen, "%s/%s", home, CREDENTIALS_FILE);
}

bool registryLoadCredentials(const char **outApiKey,
                              const char **outRegistryUrl,
                              struct StrPool *strings)
{
    char credPath[1024];
    getCredentialsPath(credPath, sizeof(credPath));

    FILE *f = fopen(credPath, "r");
    if (!f)
        return false; /* not an error — file simply doesn't exist yet */

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* skip blank lines and comments */
        if (len == 0 || line[0] == '#')
            continue;

        char *eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        const char *key   = line;
        const char *value = eq + 1;

        if (strcmp(key, "api_key") == 0) {
            if (outApiKey)
                *outApiKey = makeString(strings, value);
        } else if (strcmp(key, "registry_url") == 0) {
            if (outRegistryUrl)
                *outRegistryUrl = makeString(strings, value);
        }
    }

    fclose(f);
    return true;
}

bool registrySaveCredential(const char *key, const char *value)
{
    char credDir[1024];
    char credPath[1024];
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    snprintf(credDir,  sizeof(credDir),  "%s/.cxy", home);
    snprintf(credPath, sizeof(credPath), "%s/%s", home, CREDENTIALS_FILE);

    /* Ensure directory exists */
    struct stat st;
    if (stat(credDir, &st) != 0) {
        if (mkdir(credDir, 0700) != 0 && errno != EEXIST)
            return false;
    }

    /* Read existing lines */
    char   **lines    = NULL;
    size_t   lineCount = 0;
    size_t   lineCap   = 0;
    bool     keyFound  = false;

    FILE *rf = fopen(credPath, "r");
    if (rf) {
        char buf[1024];
        while (fgets(buf, sizeof(buf), rf)) {
            /* keep a heap copy */
            if (lineCount >= lineCap) {
                lineCap = lineCap == 0 ? 16 : lineCap * 2;
                lines = realloc(lines, lineCap * sizeof(char *));
            }

            /* check if this line carries our key */
            char *eq = strchr(buf, '=');
            if (eq) {
                size_t klen = (size_t)(eq - buf);
                if (strncmp(buf, key, klen) == 0 && strlen(key) == klen) {
                    /* replace this line */
                    char newLine[1024];
                    snprintf(newLine, sizeof(newLine), "%s=%s\n", key, value);
                    lines[lineCount++] = strdup(newLine);
                    keyFound = true;
                    continue;
                }
            }
            lines[lineCount++] = strdup(buf);
        }
        fclose(rf);
    }

    if (!keyFound) {
        if (lineCount >= lineCap) {
            lineCap = lineCap == 0 ? 16 : lineCap * 2;
            lines = realloc(lines, lineCap * sizeof(char *));
        }
        char newLine[1024];
        snprintf(newLine, sizeof(newLine), "%s=%s\n", key, value);
        lines[lineCount++] = strdup(newLine);
    }

    FILE *wf = fopen(credPath, "w");
    if (!wf) {
        for (size_t i = 0; i < lineCount; i++) free(lines[i]);
        free(lines);
        return false;
    }
    /* Restrict permissions to owner only */
    chmod(credPath, 0600);

    for (size_t i = 0; i < lineCount; i++) {
        fputs(lines[i], wf);
        free(lines[i]);
    }
    free(lines);
    fclose(wf);
    return true;
}

/* -------------------------------------------------------------------------
 * Client lifecycle
 * ---------------------------------------------------------------------- */

/**
 * Load credentials from a JSON file of the form:
 *   { "url": "https://...", "api_key": "cxy_live_..." }
 *
 * Expands a leading "~/" to the user's home directory.
 * Returns true if the file was found and at least partially parsed.
 */
static bool loadJsonCredentials(const char *filePath,
                                const char **outApiKey,
                                const char **outUrl,
                                struct StrPool *strings)
{
    if (!filePath || filePath[0] == '\0')
        return false;

    /* Expand leading ~/ */
    char expanded[1024];
    if (filePath[0] == '~' && filePath[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        snprintf(expanded, sizeof(expanded), "%s%s", home, filePath + 1);
        filePath = expanded;
    }

    FILE *f = fopen(filePath, "r");
    if (!f)
        return false;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return false; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root)
        return false;

    cJSON *urlItem    = cJSON_GetObjectItem(root, "url");
    cJSON *keyItem    = cJSON_GetObjectItem(root, "api_key");

    if (outUrl && urlItem && (urlItem->type & cJSON_String) && urlItem->valuestring)
        *outUrl    = makeString(strings, urlItem->valuestring);
    if (outApiKey && keyItem && (keyItem->type & cJSON_String) && keyItem->valuestring)
        *outApiKey = makeString(strings, keyItem->valuestring);

    cJSON_Delete(root);
    return true;
}

RegistryClient *registryClientInit(struct StrPool *strings, Log *log, const char *filePath)
{
    RegistryClient *client = calloc(1, sizeof(RegistryClient));
    if (!client) {
        logError(log, NULL, "out of memory allocating registry client", NULL);
        return NULL;
    }
    client->strings = strings;
    client->log     = log;

    /* --- Determine base URL -------------------------------------------- */
    const char *envUrl     = getenv("CXY_REGISTRY_URL");
    const char *fileUrl    = NULL;
    const char *envApiKey  = getenv("CXY_API_KEY");
    const char *fileApiKey = NULL;

    /* Priority: env vars > explicit JSON file > default credentials file */
    if (filePath && filePath[0] != '\0')
        loadJsonCredentials(filePath, &fileApiKey, &fileUrl, strings);
    else
        registryLoadCredentials(&fileApiKey, &fileUrl, strings);

    const char *chosenUrl = envUrl    ? envUrl    :
                            fileUrl   ? fileUrl   : REGISTRY_DEFAULT_URL;
    const char *chosenKey = envApiKey ? envApiKey : fileApiKey;

    strncpy(client->baseUrl, chosenUrl, sizeof(client->baseUrl) - 1);
    /* Strip trailing slash */
    size_t ulen = strlen(client->baseUrl);
    if (ulen > 0 && client->baseUrl[ulen - 1] == '/')
        client->baseUrl[ulen - 1] = '\0';

    if (chosenKey)
        strncpy(client->apiKey, chosenKey, sizeof(client->apiKey) - 1);

    /* --- Initialise curl ------------------------------------------------ */
    client->curl = curl_easy_init();
    if (!client->curl) {
        logError(log, NULL, "failed to initialise libcurl", NULL);
        free(client);
        return NULL;
    }

    return client;
}

void registryClientFree(RegistryClient *client)
{
    if (!client)
        return;
    if (client->curl)
        curl_easy_cleanup(client->curl);
    free(client);
}

const char *registryClientBaseUrl(const RegistryClient *client)
{
    return client ? client->baseUrl : NULL;
}

bool registryClientHasApiKey(const RegistryClient *client)
{
    return client && client->apiKey[0] != '\0';
}

/* -------------------------------------------------------------------------
 * Internal HTTP helpers
 * ---------------------------------------------------------------------- */

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PATCH,
} HttpMethod;

/**
 * Execute a single HTTP request.
 *
 * @param client      Registry client.
 * @param method      GET or POST.
 * @param path        Path relative to the API base (must start with '/').
 * @param body        JSON body for POST requests (may be NULL for GET).
 * @param outBuf      Buffer that receives the response body.
 * @param outStatus   Receives the HTTP status code.
 * @return true if the request was dispatched (check outStatus for result).
 */
static bool httpRequest(RegistryClient *client,
                        HttpMethod      method,
                        const char     *path,
                        const char     *body,
                        ByteBuf        *outBuf,
                        long           *outStatus)
{
    CURL *curl = client->curl;
    char  url[1024];
    snprintf(url, sizeof(url), "%s%s%s", client->baseUrl, REGISTRY_API_VERSION, path);

    curl_easy_reset(curl);

    /* URL */
    curl_easy_setopt(curl, CURLOPT_URL, url);

    /* Write callback */
    byteBufInit(outBuf);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, byteBufWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, outBuf);

    /* Timeout: 30 s connect, 60 s total */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    /* Follow redirects */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    /* Build headers */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");

    if (client->apiKey[0] != '\0') {
        char authHeader[300];
        snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", client->apiKey);
        headers = curl_slist_append(headers, authHeader);
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* Method-specific setup */
    if (method == HTTP_POST) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
    } else if (method == HTTP_PATCH) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
    }

    /* Execute */
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        logError(client->log, NULL,
                 "HTTP request failed: {s}",
                 (FormatArg[]){{.s = curl_easy_strerror(res)}});
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, outStatus);
    return true;
}

/**
 * Parse a JSON body and log an error if it fails.
 */
static cJSON *parseJsonBody(RegistryClient *client, const ByteBuf *buf, const char *context)
{
    if (!buf->data || buf->len == 0) {
        logError(client->log, NULL,
                 "empty response body from registry ({s})",
                 (FormatArg[]){{.s = context}});
        return NULL;
    }

    cJSON *json = cJSON_Parse(buf->data);
    if (!json) {
        logError(client->log, NULL,
                 "invalid JSON in registry response ({s}): {s}",
                 (FormatArg[]){{.s = context}, {.s = buf->data}});
    }
    return json;
}

/**
 * Log an error code returned in a standard {"error":{"code":"…","message":"…"}} body.
 */
static void logApiError(RegistryClient *client, const ByteBuf *buf,
                        long status, const char *context)
{
    cJSON *json = cJSON_Parse(buf && buf->data ? buf->data : "");
    if (json) {
        cJSON *errObj = cJSON_GetObjectItem(json, "error");
        if (errObj) {
            cJSON *msg = cJSON_GetObjectItem(errObj, "message");
            cJSON *code = cJSON_GetObjectItem(errObj, "code");
            logError(client->log, NULL,
                     "registry error ({s}): [{s}] {s}",
                     (FormatArg[]){
                         {.s = context},
                         {.s = (code && (code->type & cJSON_String)) ? code->valuestring : "?"},
                         {.s = (msg  && (msg->type  & cJSON_String)) ? msg->valuestring  : "unknown"}
                     });
        } else {
            logError(client->log, NULL,
                     "registry returned HTTP {i64} for {s}",
                     (FormatArg[]){{.i64 = status}, {.s = context}});
        }
        cJSON_Delete(json);
    } else {
        logError(client->log, NULL,
                 "registry returned HTTP {i64} for {s}",
                 (FormatArg[]){{.i64 = status}, {.s = context}});
    }
}

/* -------------------------------------------------------------------------
 * JSON → struct helpers
 * ---------------------------------------------------------------------- */

static cstring dupJsonString(RegistryClient *client, const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItem(obj, field);
    if (!item || !(item->type & cJSON_String) || !item->valuestring)
        return NULL;
    return makeString(client->strings, item->valuestring);
}

static i64 dupJsonInt(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItem(obj, field);
    if (!item || !(item->type & cJSON_Number))
        return 0;
    return (i64)item->valuedouble;
}

static bool dupJsonBool(const cJSON *obj, const char *field)
{
    const cJSON *item = cJSON_GetObjectItem(obj, field);
    if (!item)
        return false;
    return (item->type & cJSON_True) != 0;
}

static void parseRegistryVersion(RegistryClient *client, const cJSON *obj, RegistryVersion *out)
{
    memset(out, 0, sizeof(*out));
    out->id          = dupJsonInt(obj, "id");
    out->version     = dupJsonString(client, obj, "version");
    out->tag         = dupJsonString(client, obj, "tag");
    out->commit      = dupJsonString(client, obj, "commit");
    out->checksum    = dupJsonString(client, obj, "checksum");
    out->publishedAt = dupJsonString(client, obj, "published_at");
    out->downloads   = dupJsonInt(obj, "downloads");
    out->yanked      = dupJsonBool(obj, "yanked");
}

static void parseRegistryPackage(RegistryClient *client, const cJSON *obj, RegistryPackage *out)
{
    memset(out, 0, sizeof(*out));
    out->id             = dupJsonInt(obj, "id");
    out->name           = dupJsonString(client, obj, "name");
    out->description    = dupJsonString(client, obj, "description");
    out->author         = dupJsonString(client, obj, "author");
    out->license        = dupJsonString(client, obj, "license");
    out->repository     = dupJsonString(client, obj, "repository");
    out->homepage       = dupJsonString(client, obj, "homepage");
    out->totalDownloads = dupJsonInt(obj, "total_downloads");
    out->createdAt      = dupJsonString(client, obj, "created_at");
    out->updatedAt      = dupJsonString(client, obj, "updated_at");
}

/* -------------------------------------------------------------------------
 * Package lookup
 * ---------------------------------------------------------------------- */

bool registryGetPackage(RegistryClient *client,
                        const char *name,
                        RegistryPackageDetails *out)
{
    if (!client || !name || !out) return false;
    memset(out, 0, sizeof(*out));

    char path[512];
    snprintf(path, sizeof(path), "/packages/%s", name);

    ByteBuf buf;
    long    status = 0;
    if (!httpRequest(client, HTTP_GET, path, NULL, &buf, &status)) {
        return false;
    }

    if (status == 404) {
        /* Package simply does not exist — not a hard error for callers */
        byteBufFree(&buf);
        return false;
    }

    if (status != 200) {
        logApiError(client, &buf, status, path);
        byteBufFree(&buf);
        return false;
    }

    cJSON *root = parseJsonBody(client, &buf, path);
    byteBufFree(&buf);
    if (!root) return false;

    /* Schema: { pkg: Package, latest: Version, versions: [VersionSummary] } */
    const cJSON *pkgObj = cJSON_GetObjectItem(root, "pkg");
    if (pkgObj)
        parseRegistryPackage(client, pkgObj, &out->pkg);

    const cJSON *latestObj = cJSON_GetObjectItem(root, "latest");
    if (latestObj && !(latestObj->type & cJSON_NULL)) {
        parseRegistryVersion(client, latestObj, &out->latest);
        out->hasLatest = true;
    }

    const cJSON *versionsArr = cJSON_GetObjectItem(root, "versions");
    if (versionsArr && (versionsArr->type & cJSON_Array)) {
        int count = cJSON_GetArraySize(versionsArr);
        if (count > 0) {
            out->versions = calloc((size_t)count, sizeof(RegistryVersion));
            out->versionCount = (u32)count;
            int idx = 0;
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, versionsArr) {
                parseRegistryVersion(client, item, &out->versions[idx++]);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

void registryPackageDetailsFree(RegistryPackageDetails *details)
{
    if (!details) return;
    free(details->versions);
    details->versions     = NULL;
    details->versionCount = 0;
}

bool registryGetVersion(RegistryClient *client,
                        const char *name,
                        const char *version,
                        RegistryVersion *out)
{
    if (!client || !name || !version || !out) return false;
    memset(out, 0, sizeof(*out));

    char path[512];
    snprintf(path, sizeof(path), "/packages/%s/%s", name, version);

    ByteBuf buf;
    long    status = 0;
    if (!httpRequest(client, HTTP_GET, path, NULL, &buf, &status)) {
        return false;
    }

    if (status == 404) {
        logError(client->log, NULL,
                 "version '{s}' of package '{s}' not found in registry",
                 (FormatArg[]){{.s = version}, {.s = name}});
        byteBufFree(&buf);
        return false;
    }

    if (status != 200) {
        logApiError(client, &buf, status, path);
        byteBufFree(&buf);
        return false;
    }

    cJSON *root = parseJsonBody(client, &buf, path);
    byteBufFree(&buf);
    if (!root) return false;

    parseRegistryVersion(client, root, out);
    cJSON_Delete(root);
    return true;
}

bool registryListVersions(RegistryClient *client,
                          const char *name,
                          RegistryVersionList *out)
{
    if (!client || !name || !out) return false;
    memset(out, 0, sizeof(*out));

    char path[512];
    snprintf(path, sizeof(path), "/packages/%s/versions", name);

    ByteBuf buf;
    long    status = 0;
    if (!httpRequest(client, HTTP_GET, path, NULL, &buf, &status)) {
        return false;
    }

    if (status == 404) {
        logError(client->log, NULL,
                 "package '{s}' not found in registry",
                 (FormatArg[]){{.s = name}});
        byteBufFree(&buf);
        return false;
    }

    if (status != 200) {
        logApiError(client, &buf, status, path);
        byteBufFree(&buf);
        return false;
    }

    cJSON *root = parseJsonBody(client, &buf, path);
    byteBufFree(&buf);
    if (!root) return false;

    /* Schema: { name: string, versions: [VersionSummary] } */
    const cJSON *nameItem = cJSON_GetObjectItem(root, "name");
    if (nameItem && (nameItem->type & cJSON_String))
        out->packageName = makeString(client->strings, nameItem->valuestring);

    const cJSON *versArr = cJSON_GetObjectItem(root, "versions");
    if (versArr && (versArr->type & cJSON_Array)) {
        int count = cJSON_GetArraySize(versArr);
        if (count > 0) {
            out->versions = calloc((size_t)count, sizeof(RegistryVersion));
            out->count    = (u32)count;
            int idx = 0;
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, versArr) {
                parseRegistryVersion(client, item, &out->versions[idx++]);
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

void registryVersionListFree(RegistryVersionList *list)
{
    if (!list) return;
    free(list->versions);
    list->versions = NULL;
    list->count    = 0;
}

/* -------------------------------------------------------------------------
 * Version resolution
 * ---------------------------------------------------------------------- */

bool registryResolveVersion(RegistryClient *client,
                            const char *name,
                            const char *constraint,
                            RegistryVersion *out)
{
    if (!client || !name || !out) return false;
    memset(out, 0, sizeof(*out));

    /* Fetch all versions (already sorted newest-first by the server) */
    RegistryVersionList list;
    if (!registryListVersions(client, name, &list))
        return false;

    if (list.count == 0) {
        logError(client->log, NULL,
                 "package '{s}' has no published versions",
                 (FormatArg[]){{.s = name}});
        registryVersionListFree(&list);
        return false;
    }

    /* Wildcard / no constraint → return latest non-yanked */
    bool useLatest = (!constraint || constraint[0] == '\0' || strcmp(constraint, "*") == 0);

    if (useLatest) {
        for (u32 i = 0; i < list.count; i++) {
            if (!list.versions[i].yanked) {
                *out = list.versions[i];
                /* strings owned by pool — no free needed */
                free(list.versions); /* free the array, not the strings */
                list.versions = NULL;
                return true;
            }
        }
        logError(client->log, NULL,
                 "all versions of package '{s}' are yanked",
                 (FormatArg[]){{.s = name}});
        registryVersionListFree(&list);
        return false;
    }

    /* Parse the constraint */
    VersionConstraint vc;
    if (!parseVersionConstraint(constraint, &vc, client->log)) {
        logError(client->log, NULL,
                 "invalid version constraint '{s}' for package '{s}'",
                 (FormatArg[]){{.s = constraint}, {.s = name}});
        registryVersionListFree(&list);
        return false;
    }

    /* Iterate versions (newest first) and pick the first satisfying one */
    for (u32 i = 0; i < list.count; i++) {
        RegistryVersion *rv = &list.versions[i];
        if (rv->yanked || !rv->version)
            continue;

        SemanticVersion sv;
        if (!parseSemanticVersion(rv->version, &sv, NULL))
            continue;

        if (versionSatisfiesConstraint(&sv, &vc)) {
            *out = *rv;
            free(list.versions);
            list.versions = NULL;
            return true;
        }
    }

    logError(client->log, NULL,
             "no version of package '{s}' satisfies constraint '{s}'",
             (FormatArg[]){{.s = name}, {.s = constraint}});
    registryVersionListFree(&list);
    return false;
}

/* -------------------------------------------------------------------------
 * Download tracking
 * ---------------------------------------------------------------------- */

bool registryRecordDownload(RegistryClient *client,
                            const char *name,
                            const char *version)
{
    if (!client || !name || !version) return false;

    char path[512];
    snprintf(path, sizeof(path), "/packages/%s/%s/download", name, version);

    ByteBuf buf;
    long    status = 0;

    if (!httpRequest(client, HTTP_POST, path, NULL, &buf, &status)) {
        /* Non-fatal: log warning and carry on */
        logWarning(client->log, NULL,
                   "failed to record download for '{s}@{s}' (network error)",
                   (FormatArg[]){{.s = name}, {.s = version}});
        return false;
    }

    byteBufFree(&buf);

    if (status != 200 && status != 204) {
        /* Non-fatal */
        logWarning(client->log, NULL,
                   "failed to record download for '{s}@{s}' (HTTP {i64})",
                   (FormatArg[]){{.s = name}, {.s = version}, {.i64 = status}});
        return false;
    }

    return true;
}

/* -------------------------------------------------------------------------
 * Yanking
 * ---------------------------------------------------------------------- */

bool registryYank(RegistryClient *client, const char *name, const char *version, bool undo)
{
    if (!client || !name || !version) return false;

    if (!registryClientHasApiKey(client)) {
        logError(client->log, NULL,
                 "yanking requires an API key. "
                 "Set CXY_API_KEY or run 'cxy package login'.",
                 NULL);
        return false;
    }

    /* POST /api/v1/admin/packages/{name}/{version}/yank
     * POST /api/v1/admin/packages/{name}/{version}/unyank
     * No request body required.
     */
    char endpoint[512];
    snprintf(endpoint, sizeof(endpoint),
             "/packages/%s/%s/%s", name, version, undo ? "unyank" : "yank");

    ByteBuf buf;
    long    status = 0;
    bool    ok = httpRequest(client, HTTP_POST, endpoint, NULL, &buf, &status);

    if (!ok)
        return false;

    if (status != 200) {
        logApiError(client, &buf, status,
                    undo ? "POST /packages/{name}/{version}/unyank"
                         : "POST /packages/{name}/{version}/yank");
        byteBufFree(&buf);
        return false;
    }

    byteBufFree(&buf);
    return true;
}

/* -------------------------------------------------------------------------
 * Publishing
 * ---------------------------------------------------------------------- */

bool registryPublish(RegistryClient *client,
                     const RegistryPublishRequest *req,
                     const char **outUrl)
{
    if (!client || !req) return false;

    if (!registryClientHasApiKey(client)) {
        logError(client->log, NULL,
                 "publishing requires an API key. "
                 "Set CXY_API_KEY or run 'cxy package login'.",
                 NULL);
        return false;
    }

    /* Build the JSON body */
    cJSON *root = cJSON_CreateObject();

    if (req->repository)
        cJSON_AddStringToObject(root, "repository", req->repository);
    if (req->tag)
        cJSON_AddStringToObject(root, "tag", req->tag);
    if (req->commit)
        cJSON_AddStringToObject(root, "commit", req->commit);
    if (req->checksum)
        cJSON_AddStringToObject(root, "checksum", req->checksum);
    if (req->readme)
        cJSON_AddStringToObject(root, "readme", req->readme);

    /* metadata sub-object */
    cJSON *meta = cJSON_CreateObject();
    if (req->name)        cJSON_AddStringToObject(meta, "name",        req->name);
    if (req->version)     cJSON_AddStringToObject(meta, "version",     req->version);
    if (req->description) cJSON_AddStringToObject(meta, "description", req->description);
    if (req->author)      cJSON_AddStringToObject(meta, "author",      req->author);
    if (req->license)     cJSON_AddStringToObject(meta, "license",     req->license);
    if (req->repository)  cJSON_AddStringToObject(meta, "repository",  req->repository);
    if (req->homepage)    cJSON_AddStringToObject(meta, "homepage",    req->homepage);
    cJSON_AddItemToObject(root, "metadata", meta);

    /* dependencies array */
    if (req->depCount > 0 && req->deps) {
        cJSON *depsArr = cJSON_CreateArray();
        for (u32 i = 0; i < req->depCount; i++) {
            cJSON *dep = cJSON_CreateObject();
            if (req->deps[i].name)
                cJSON_AddStringToObject(dep, "name", req->deps[i].name);
            if (req->deps[i].repository)
                cJSON_AddStringToObject(dep, "repository", req->deps[i].repository);
            if (req->deps[i].versionConstraint)
                cJSON_AddStringToObject(dep, "version_constraint", req->deps[i].versionConstraint);
            cJSON_AddBoolToObject(dep, "is_dev", req->deps[i].isDev);
            cJSON_AddItemToArray(depsArr, dep);
        }
        cJSON_AddItemToObject(root, "dependencies", depsArr);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!body) {
        logError(client->log, NULL, "failed to serialise publish request", NULL);
        return false;
    }

    ByteBuf buf;
    long    status = 0;
    bool    ok = httpRequest(client, HTTP_POST, "/packages", body, &buf, &status);
    free(body);

    if (!ok) {
        return false;
    }

    if (status != 200 && status != 201) {
        logApiError(client, &buf, status, "POST /packages");
        byteBufFree(&buf);
        return false;
    }

    if (outUrl) {
        cJSON *resp = parseJsonBody(client, &buf, "publish response");
        if (resp) {
            const cJSON *urlItem = cJSON_GetObjectItem(resp, "url");
            if (urlItem && (urlItem->type & cJSON_String))
                *outUrl = makeString(client->strings, urlItem->valuestring);
            cJSON_Delete(resp);
        }
    }

    byteBufFree(&buf);
    return true;
}

/* -------------------------------------------------------------------------
 * Search
 * ---------------------------------------------------------------------- */

bool registrySearchPackages(RegistryClient *client,
                            const char *query,
                            int limit,
                            int offset,
                            const char *sort,
                            RegistrySearchResult *out)
{
    if (!client || !out) return false;
    memset(out, 0, sizeof(*out));

    /* Build query string */
    char path[512];
    char queryEnc[256] = "";

    /* Simple percent-encoding for the query string */
    if (query && query[0] != '\0') {
        const char *hex = "0123456789ABCDEF";
        char *dst = queryEnc;
        char *end = queryEnc + sizeof(queryEnc) - 4;
        for (const char *src = query; *src && dst < end; src++) {
            unsigned char c = (unsigned char)*src;
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                *dst++ = (char)c;
            } else if (c == ' ') {
                *dst++ = '+';
            } else {
                *dst++ = '%';
                *dst++ = hex[c >> 4];
                *dst++ = hex[c & 0xF];
            }
        }
        *dst = '\0';
    }

    snprintf(path, sizeof(path),
             "/packages?q=%s&limit=%d&offset=%d&sort=%s",
             queryEnc,
             limit  > 0   ? limit  : 20,
             offset >= 0  ? offset : 0,
             (sort && sort[0] != '\0') ? sort : "relevance");

    ByteBuf buf;
    long    status = 0;
    if (!httpRequest(client, HTTP_GET, path, NULL, &buf, &status))
        return false;

    if (status != 200) {
        logApiError(client, &buf, status, path);
        byteBufFree(&buf);
        return false;
    }

    cJSON *root = parseJsonBody(client, &buf, path);
    byteBufFree(&buf);
    if (!root) return false;

    /* Schema: { total: int, packages: [PackageSummary] } */
    out->total = dupJsonInt(root, "total");

    const cJSON *pkgsArr = cJSON_GetObjectItem(root, "packages");
    if (pkgsArr && (pkgsArr->type & cJSON_Array)) {
        int count = cJSON_GetArraySize(pkgsArr);
        if (count > 0) {
            out->packages = calloc((size_t)count, sizeof(PackageSummary));
            out->count    = (u32)count;
            int idx = 0;
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, pkgsArr) {
                PackageSummary *s = &out->packages[idx++];
                s->name           = dupJsonString(client, item, "name");
                s->description    = dupJsonString(client, item, "description");
                s->author         = dupJsonString(client, item, "author");
                s->license        = dupJsonString(client, item, "license");
                s->repository     = dupJsonString(client, item, "repository");
                s->latestVersion  = dupJsonString(client, item, "latest_version");
                s->totalDownloads = dupJsonInt(item, "total_downloads");
                s->updatedAt      = dupJsonString(client, item, "updated_at");
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

void registrySearchResultFree(RegistrySearchResult *result)
{
    if (!result) return;
    free(result->packages);
    result->packages = NULL;
    result->count    = 0;
}

/* -------------------------------------------------------------------------
 * Authentication
 * ---------------------------------------------------------------------- */

bool registryLogin(RegistryClient *client, RegistryLoginResponse *out)
{
    if (!client || !out) return false;
    memset(out, 0, sizeof(*out));

    if (!registryClientHasApiKey(client)) {
        logError(client->log, NULL,
                 "no API key configured — provide a key to validate",
                 NULL);
        return false;
    }

    ByteBuf buf;
    long    status = 0;
    if (!httpRequest(client, HTTP_GET, "/login", NULL, &buf, &status)) {
        return false;
    }

    if (status == 401) {
        logError(client->log, NULL,
                 "invalid API key — server rejected the credentials",
                 NULL);
        byteBufFree(&buf);
        return false;
    }

    if (status != 200) {
        logApiError(client, &buf, status, "GET /login");
        byteBufFree(&buf);
        return false;
    }

    cJSON *root = parseJsonBody(client, &buf, "GET /login");
    byteBufFree(&buf);
    if (!root) return false;

    out->id        = dupJsonInt(root, "id");
    out->login     = dupJsonString(client, root, "login");
    out->name      = dupJsonString(client, root, "name");
    out->email     = dupJsonString(client, root, "email");
    out->avatarUrl = dupJsonString(client, root, "avatar_url");
    out->isAdmin   = dupJsonBool(root, "is_admin");

    cJSON_Delete(root);
    return true;
}