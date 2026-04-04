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
#include "core/mempool.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

#define DEFAULT_REGISTRY_URL "https://registry.cxy-lang.org"

/**
 * Read a line from stdin without echoing characters to the terminal.
 * Used for reading the API key securely.
 *
 * @param buf    Output buffer.
 * @param bufLen Buffer size (including null terminator).
 * @return Number of characters read, or -1 on error.
 */
static int readSilent(char *buf, size_t bufLen)
{
    /* If stdin is not a TTY (e.g. piped input), just read normally */
    if (!isatty(STDIN_FILENO)) {
        if (fgets(buf, (int)bufLen, stdin) == NULL)
            return -1;
        int n = (int)strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
        return n;
    }

    struct termios old, noecho;

    if (tcgetattr(STDIN_FILENO, &old) != 0)
        return -1;

    noecho = old;
    noecho.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    noecho.c_lflag |= ICANON;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &noecho) != 0)
        return -1;

    int n = -1;
    if (fgets(buf, (int)bufLen, stdin) != NULL) {
        n = (int)strlen(buf);
        /* Strip trailing newline */
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
            buf[--n] = '\0';
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    printf("\n");
    return n;
}

/**
 * Expand a leading ~/ in a path to the user's home directory.
 * Writes the result into `out` (size `outLen`).
 */
static void expandPath(const char *path, char *out, size_t outLen)
{
    if (path[0] == '~' && path[1] == '/') {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            home = pw ? pw->pw_dir : "/tmp";
        }
        snprintf(out, outLen, "%s%s", home, path + 1);
    } else {
        strncpy(out, path, outLen - 1);
        out[outLen - 1] = '\0';
    }
}

/**
 * Write credentials to a JSON file, creating parent directories as needed.
 * Restricts file permissions to owner-only (0600).
 *
 * @param filePath  Destination file path (already expanded, no ~/).
 * @param url       Registry URL to save.
 * @param apiKey    API key to save.
 * @return true on success.
 */
static bool writeCredentialsFile(const char *filePath,
                                 const char *url,
                                 const char *apiKey,
                                 Log *log)
{
    /* Ensure parent directory exists */
    char dirPath[1024];
    strncpy(dirPath, filePath, sizeof(dirPath) - 1);
    dirPath[sizeof(dirPath) - 1] = '\0';

    char *lastSlash = strrchr(dirPath, '/');
    if (lastSlash && lastSlash != dirPath) {
        *lastSlash = '\0';
        struct stat st;
        if (stat(dirPath, &st) != 0) {
            if (mkdir(dirPath, 0700) != 0 && errno != EEXIST) {
                logError(log, NULL,
                         "failed to create directory '{s}': {s}",
                         (FormatArg[]){{.s = dirPath}, {.s = strerror(errno)}});
                return false;
            }
        }
    }

    /* Build JSON */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "url",     url);
    cJSON_AddStringToObject(root, "api_key", apiKey);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json) {
        logError(log, NULL, "failed to serialise credentials", NULL);
        return false;
    }

    FILE *f = fopen(filePath, "w");
    if (!f) {
        logError(log, NULL,
                 "failed to open '{s}' for writing: {s}",
                 (FormatArg[]){{.s = filePath}, {.s = strerror(errno)}});
        free(json);
        return false;
    }

    chmod(filePath, 0600);
    fputs(json, f);
    fputc('\n', f);
    fclose(f);
    free(json);
    return true;
}

/**
 * Validate the API key by calling GET /api/v1/login.
 * Temporarily sets env vars so registryClientInit picks up the right
 * url + key without touching the credentials file yet.
 *
 * @param url     Registry base URL.
 * @param apiKey  API key to validate.
 * @param strings String pool for allocations.
 * @param log     Logger.
 * @param out     Filled with the user profile on success.
 * @return true if the server accepted the key.
 */
static bool verifyApiKey(const char *url, const char *apiKey,
                         struct StrPool *strings, Log *log,
                         RegistryLoginResponse *out)
{
    char prevUrl[512] = "";
    char prevKey[256] = "";

    const char *existingUrl = getenv("CXY_REGISTRY_URL");
    const char *existingKey = getenv("CXY_API_KEY");

    if (existingUrl) strncpy(prevUrl, existingUrl, sizeof(prevUrl) - 1);
    if (existingKey) strncpy(prevKey, existingKey, sizeof(prevKey) - 1);

    setenv("CXY_REGISTRY_URL", url,    1);
    setenv("CXY_API_KEY",      apiKey, 1);

    RegistryClient *client = registryClientInit(strings, log, NULL);
    bool ok = false;

    if (client) {
        ok = registryLogin(client, out);
        registryClientFree(client);
    }

    /* Restore env */
    if (prevUrl[0]) setenv("CXY_REGISTRY_URL", prevUrl, 1);
    else            unsetenv("CXY_REGISTRY_URL");

    if (prevKey[0]) setenv("CXY_API_KEY", prevKey, 1);
    else            unsetenv("CXY_API_KEY");

    return ok;
}

bool packageLoginCommand(const Options *options, struct StrPool *strings, Log *log)
{
    const char *urlArg      = options->package.loginUrl;
    const char *registryArg = options->package.loginRegistryFile;

    /* ------------------------------------------------------------------ */
    /* 1. Resolve registry URL                                             */
    /* ------------------------------------------------------------------ */

    char url[512];
    if (urlArg && urlArg[0] != '\0') {
        strncpy(url, urlArg, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    } else {
        /* Check env var, then fall back to default */
        const char *envUrl = getenv("CXY_REGISTRY_URL");
        strncpy(url, envUrl ? envUrl : DEFAULT_REGISTRY_URL, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
    }

    /* Strip trailing slash */
    size_t ulen = strlen(url);
    if (ulen > 0 && url[ulen - 1] == '/')
        url[ulen - 1] = '\0';

    /* ------------------------------------------------------------------ */
    /* 2. Resolve credentials file path                                    */
    /* ------------------------------------------------------------------ */

    const char *filePath = (registryArg && registryArg[0] != '\0')
                               ? registryArg
                               : "~/.cxy-registry.json";

    char expandedPath[1024];
    expandPath(filePath, expandedPath, sizeof(expandedPath));

    /* ------------------------------------------------------------------ */
    /* 3. Prompt                                                           */
    /* ------------------------------------------------------------------ */

    printf("Logging in to " cBCYN "%s" cDEF "\n", url);
    printf("Credentials will be saved to " cBCYN "%s" cDEF "\n\n", expandedPath);
    printf("Enter API key: ");
    fflush(stdout);

    char apiKey[256];
    if (readSilent(apiKey, sizeof(apiKey)) < 0) {
        logError(log, NULL, "failed to read API key from terminal", NULL);
        return false;
    }

    if (apiKey[0] == '\0') {
        logError(log, NULL, "API key cannot be empty", NULL);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 4. Validate key against the registry                                */
    /* ------------------------------------------------------------------ */

    printStatusSticky(log, "Validating API key with %s...", url);

    RegistryLoginResponse user;
    if (!verifyApiKey(url, apiKey, strings, log, &user))
        return false;

    /* ------------------------------------------------------------------ */
    /* 5. Save credentials                                                 */
    /* ------------------------------------------------------------------ */

    if (!writeCredentialsFile(expandedPath, url, apiKey, log))
        return false;

    printStatusAlways(log, cBGRN "✔" cDEF " Logged in as %s%s\n",
                      user.login ? user.login : "unknown",
                      user.isAdmin ? " (admin)" : "");
    printStatusAlways(log, cBGRN "✔" cDEF " Credentials saved to %s\n", expandedPath);

    return true;
}