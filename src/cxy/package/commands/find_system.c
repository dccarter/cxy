/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-20
 */

#include "package/commands/commands.h"
#include "core/log.h"
#include "core/format.h"
#include "core/strpool.h"
#include "core/utils.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * System package information - built-in knowledge base
 */
typedef struct {
    const char *name;
    const char **aliases;           // Alternative names
    const char **pkgConfigNames;    // pkg-config names to try
    const char **brewNames;         // Homebrew formula names
    const char **systemPaths;       // Common installation paths
    const char **libs;              // Library names to link
    const char **headers;           // Key header files to verify
} SystemPackageInfo;

/**
 * Package discovery result
 */
typedef struct {
    bool found;
    cstring name;
    cstring version;
    cstring prefix;
    DynArray includeDirs;  // Array of cstring
    DynArray libDirs;      // Array of cstring
    DynArray libs;         // Array of cstring
    DynArray cflags;       // Array of cstring
    DynArray ldflags;      // Array of cstring
} PackageResult;

// Built-in package database
static SystemPackageInfo knownPackages[] = {
    {
        .name = "openssl",
        .aliases = (const char*[]){"ssl", "libressl", NULL},
        .pkgConfigNames = (const char*[]){"openssl", "libssl", NULL},
        .brewNames = (const char*[]){"openssl@3", "openssl@1.1", "openssl", NULL},
        .systemPaths = (const char*[]){
            "/opt/homebrew/opt/openssl@3",
            "/opt/homebrew/opt/openssl",
            "/usr/local/opt/openssl@3",
            "/usr/local/opt/openssl",
            "/usr/local",
            "/usr",
            NULL
        },
        .libs = (const char*[]){"ssl", "crypto", NULL},
        .headers = (const char*[]){"openssl/ssl.h", NULL}
    },
    {
        .name = "postgresql",
        .aliases = (const char*[]){"postgres", "libpq", NULL},
        .pkgConfigNames = (const char*[]){"libpq", "postgresql", NULL},
        .brewNames = (const char*[]){"postgresql@16", "postgresql@15", "postgresql", NULL},
        .systemPaths = (const char*[]){
            "/opt/homebrew/opt/postgresql@16",
            "/opt/homebrew/opt/postgresql",
            "/usr/local/opt/postgresql",
            "/usr/local",
            "/usr",
            NULL
        },
        .libs = (const char*[]){"pq", NULL},
        .headers = (const char*[]){"libpq-fe.h", NULL}
    },
    {
        .name = "sqlite",
        .aliases = (const char*[]){"sqlite3", NULL},
        .pkgConfigNames = (const char*[]){"sqlite3", NULL},
        .brewNames = (const char*[]){"sqlite", "sqlite3", NULL},
        .systemPaths = (const char*[]){"/usr/local", "/usr", NULL},
        .libs = (const char*[]){"sqlite3", NULL},
        .headers = (const char*[]){"sqlite3.h", NULL}
    },
    {
        .name = "zlib",
        .aliases = (const char*[]){NULL},
        .pkgConfigNames = (const char*[]){"zlib", NULL},
        .brewNames = (const char*[]){"zlib", NULL},
        .systemPaths = (const char*[]){"/usr/local", "/usr", NULL},
        .libs = (const char*[]){"z", NULL},
        .headers = (const char*[]){"zlib.h", NULL}
    },
    {
        .name = "curl",
        .aliases = (const char*[]){"libcurl", NULL},
        .pkgConfigNames = (const char*[]){"libcurl", NULL},
        .brewNames = (const char*[]){"curl", NULL},
        .systemPaths = (const char*[]){"/usr/local", "/usr", NULL},
        .libs = (const char*[]){"curl", NULL},
        .headers = (const char*[]){"curl/curl.h", NULL}
    },
    {
        .name = "pcre",
        .aliases = (const char*[]){"pcre2", NULL},
        .pkgConfigNames = (const char*[]){"libpcre", "libpcre2-8", NULL},
        .brewNames = (const char*[]){"pcre2", "pcre", NULL},
        .systemPaths = (const char*[]){"/usr/local", "/usr", NULL},
        .libs = (const char*[]){"pcre2-8", NULL},
        .headers = (const char*[]){"pcre2.h", NULL}
    }
};

static const int knownPackagesCount = sizeof(knownPackages) / sizeof(SystemPackageInfo);

/**
 * Initialize package result structure
 */
static void initPackageResult(PackageResult *result, StrPool *strings)
{
    result->found = false;
    result->name = NULL;
    result->version = NULL;
    result->prefix = NULL;
    result->includeDirs = newDynArray(sizeof(cstring));
    result->libDirs = newDynArray(sizeof(cstring));
    result->libs = newDynArray(sizeof(cstring));
    result->cflags = newDynArray(sizeof(cstring));
    result->ldflags = newDynArray(sizeof(cstring));
}

/**
 * Free package result structure
 */
static void freePackageResult(PackageResult *result)
{
    freeDynArray(&result->includeDirs);
    freeDynArray(&result->libDirs);
    freeDynArray(&result->libs);
    freeDynArray(&result->cflags);
    freeDynArray(&result->ldflags);
}

/**
 * Find package info by name or alias
 */
static SystemPackageInfo* findPackageInfo(const char *name)
{
    for (int i = 0; i < knownPackagesCount; i++) {
        SystemPackageInfo *pkg = &knownPackages[i];
        
        // Check main name
        if (strcmp(pkg->name, name) == 0) {
            return pkg;
        }
        
        // Check aliases
        if (pkg->aliases) {
            for (int j = 0; pkg->aliases[j]; j++) {
                if (strcmp(pkg->aliases[j], name) == 0) {
                    return pkg;
                }
            }
        }
    }
    
    return NULL;
}

/**
 * Check if a command exists in PATH
 */
static bool commandExists(const char *command)
{
    char checkCmd[256];
    snprintf(checkCmd, sizeof(checkCmd), "command -v %s >/dev/null 2>&1", command);
    return system(checkCmd) == 0;
}

/**
 * Try to find package using pkg-config
 */
static bool tryPkgConfig(const char *packageName, 
                         SystemPackageInfo *pkgInfo,
                         PackageResult *result,
                         StrPool *strings,
                         Log *log)
{
    // Skip if pkg-config is not available
    static int pkgConfigAvailable = -1;
    if (pkgConfigAvailable == -1) {
        pkgConfigAvailable = commandExists("pkg-config") ? 1 : 0;
    }
    if (!pkgConfigAvailable) {
        return false;
    }
    
    if (!pkgInfo || !pkgInfo->pkgConfigNames) {
        return false;
    }
    
    // Try each pkg-config name
    for (int i = 0; pkgInfo->pkgConfigNames[i]; i++) {
        const char *pcName = pkgInfo->pkgConfigNames[i];
        
        // Check if package exists
        char checkCmd[256];
        snprintf(checkCmd, sizeof(checkCmd), "pkg-config --exists %s 2>/dev/null", pcName);
        
        if (system(checkCmd) == 0) {
            // Get cflags
            char cflagsCmd[256];
            snprintf(cflagsCmd, sizeof(cflagsCmd), "pkg-config --cflags-only-I %s 2>/dev/null", pcName);
            
            FILE *fp = popen(cflagsCmd, "r");
            if (fp) {
                char line[2048];
                if (fgets(line, sizeof(line), fp)) {
                    // Parse -I flags
                    char *token = strtok(line, " \t\n");
                    while (token) {
                        if (strncmp(token, "-I", 2) == 0 && strlen(token) > 2) {
                            cstring path = makeString(strings, token + 2);
                            pushOnDynArray(&result->includeDirs, &path);
                        }
                        token = strtok(NULL, " \t\n");
                    }
                }
                pclose(fp);
            }
            
            // Get libs
            char libsCmd[256];
            snprintf(libsCmd, sizeof(libsCmd), "pkg-config --libs-only-L --libs-only-l %s 2>/dev/null", pcName);
            
            fp = popen(libsCmd, "r");
            if (fp) {
                char line[2048];
                if (fgets(line, sizeof(line), fp)) {
                    // Parse -L and -l flags
                    char *token = strtok(line, " \t\n");
                    while (token) {
                        if (strncmp(token, "-L", 2) == 0 && strlen(token) > 2) {
                            cstring path = makeString(strings, token + 2);
                            pushOnDynArray(&result->libDirs, &path);
                        } else if (strncmp(token, "-l", 2) == 0 && strlen(token) > 2) {
                            cstring lib = makeString(strings, token + 2);
                            pushOnDynArray(&result->libs, &lib);
                        }
                        token = strtok(NULL, " \t\n");
                    }
                }
                pclose(fp);
            }
            
            // Get version
            char versionCmd[256];
            snprintf(versionCmd, sizeof(versionCmd), "pkg-config --modversion %s 2>/dev/null", pcName);
            
            fp = popen(versionCmd, "r");
            if (fp) {
                char version[128];
                if (fgets(version, sizeof(version), fp)) {
                    // Remove trailing newline
                    size_t len = strlen(version);
                    if (len > 0 && version[len-1] == '\n') {
                        version[len-1] = '\0';
                    }
                    result->version = makeString(strings, version);
                }
                pclose(fp);
            }
            
            result->found = true;
            result->name = makeString(strings, packageName);
            return true;
        }
    }
    
    return false;
}

/**
 * Try to find package using Homebrew
 */
static bool tryHomebrew(const char *packageName,
                        SystemPackageInfo *pkgInfo,
                        PackageResult *result,
                        StrPool *strings,
                        Log *log)
{
    // Skip if brew is not available
    static int brewAvailable = -1;
    if (brewAvailable == -1) {
        brewAvailable = commandExists("brew") ? 1 : 0;
    }
    if (!brewAvailable) {
        return false;
    }
    
    const char **brewNamesToTry = NULL;
    const char *fallbackNames[2] = {packageName, NULL};
    
    // Use known brew names if available, otherwise try package name directly
    if (pkgInfo && pkgInfo->brewNames) {
        brewNamesToTry = pkgInfo->brewNames;
    } else {
        brewNamesToTry = fallbackNames;
    }
    
    // Try each brew formula name
    for (int i = 0; brewNamesToTry[i]; i++) {
        const char *brewName = brewNamesToTry[i];
        
        char prefixCmd[256];
        snprintf(prefixCmd, sizeof(prefixCmd), "brew --prefix %s 2>/dev/null", brewName);
        
        FILE *fp = popen(prefixCmd, "r");
        if (fp) {
            char prefix[1024];
            if (fgets(prefix, sizeof(prefix), fp)) {
                // Remove trailing newline
                size_t len = strlen(prefix);
                if (len > 0 && prefix[len-1] == '\n') {
                    prefix[len-1] = '\0';
                }
                
                pclose(fp);
                
                // Verify prefix exists
                struct stat st;
                if (stat(prefix, &st) == 0 && S_ISDIR(st.st_mode)) {
                    result->prefix = makeString(strings, prefix);
                    
                    // Add include directory
                    char includePath[1024];
                    snprintf(includePath, sizeof(includePath), "%s/include", prefix);
                    if (stat(includePath, &st) == 0 && S_ISDIR(st.st_mode)) {
                        cstring path = makeString(strings, includePath);
                        pushOnDynArray(&result->includeDirs, &path);
                    }
                    
                    // Add lib directory
                    char libPath[1024];
                    snprintf(libPath, sizeof(libPath), "%s/lib", prefix);
                    if (stat(libPath, &st) == 0 && S_ISDIR(st.st_mode)) {
                        cstring path = makeString(strings, libPath);
                        pushOnDynArray(&result->libDirs, &path);
                    }
                    
                    // Add library names from package info if available
                    if (pkgInfo && pkgInfo->libs) {
                        for (int j = 0; pkgInfo->libs[j]; j++) {
                            cstring lib = makeString(strings, pkgInfo->libs[j]);
                            pushOnDynArray(&result->libs, &lib);
                        }
                    } else {
                        // For unknown packages, try common library name patterns
                        // Try: libxml2 -> xml2, openssl -> ssl, etc.
                        cstring lib = NULL;
                        if (strncmp(packageName, "lib", 3) == 0 && strlen(packageName) > 3) {
                            // Strip "lib" prefix: libxml2 -> xml2
                            lib = makeString(strings, packageName + 3);
                        } else {
                            // Use package name as-is
                            lib = makeString(strings, packageName);
                        }
                        pushOnDynArray(&result->libs, &lib);
                    }
                    
                    result->found = true;
                    result->name = makeString(strings, packageName);
                    return true;
                }
            }
            pclose(fp);
        }
    }
    
    return false;
}

/**
 * Try to find package in system paths
 */
static bool trySystemPaths(const char *packageName,
                           SystemPackageInfo *pkgInfo,
                           const DynArray *searchRoots,
                           PackageResult *result,
                           StrPool *strings,
                           Log *log)
{
    // Build list of paths to search
    DynArray searchPaths = newDynArray(sizeof(cstring));
    
    // Add custom search roots first
    if (searchRoots) {
        for (u32 i = 0; i < searchRoots->size; i++) {
            cstring path = ((cstring *)searchRoots->elems)[i];
            pushOnDynArray(&searchPaths, &path);
        }
    }
    
    // Add system paths from package info if available
    if (pkgInfo && pkgInfo->systemPaths) {
        for (int i = 0; pkgInfo->systemPaths[i]; i++) {
            cstring path = makeString(strings, pkgInfo->systemPaths[i]);
            pushOnDynArray(&searchPaths, &path);
        }
    } else {
        // For unknown packages, add default system paths
        const char *defaultPaths[] = {
            "/usr/local",
            "/usr",
            NULL
        };
        for (int i = 0; defaultPaths[i]; i++) {
            cstring path = makeString(strings, defaultPaths[i]);
            pushOnDynArray(&searchPaths, &path);
        }
    }
    
    // Search each path
    for (u32 i = 0; i < searchPaths.size; i++) {
        cstring searchPath = ((cstring *)searchPaths.elems)[i];
        
        // Check if path exists
        struct stat st;
        if (stat(searchPath, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        
        // Check for header files if we have package info
        bool foundHeader = false;
        if (pkgInfo && pkgInfo->headers) {
            char headerPath[1024];
            for (int j = 0; pkgInfo->headers[j]; j++) {
                snprintf(headerPath, sizeof(headerPath), "%s/include/%s", searchPath, pkgInfo->headers[j]);
                if (stat(headerPath, &st) == 0 && S_ISREG(st.st_mode)) {
                    foundHeader = true;
                    break;
                }
            }
        } else {
            // For unknown packages, check if include and lib directories exist
            char includePath[1024];
            char libPath[1024];
            snprintf(includePath, sizeof(includePath), "%s/include", searchPath);
            snprintf(libPath, sizeof(libPath), "%s/lib", searchPath);
            
            struct stat incSt, libSt;
            if (stat(includePath, &incSt) == 0 && S_ISDIR(incSt.st_mode) &&
                stat(libPath, &libSt) == 0 && S_ISDIR(libSt.st_mode)) {
                foundHeader = true;
            }
        }
        
        if (foundHeader) {
            result->prefix = makeString(strings, searchPath);
            
            // Add include directory
            char includePath[1024];
            snprintf(includePath, sizeof(includePath), "%s/include", searchPath);
            if (stat(includePath, &st) == 0 && S_ISDIR(st.st_mode)) {
                cstring path = makeString(strings, includePath);
                pushOnDynArray(&result->includeDirs, &path);
            }
            
            // Add lib directory
            char libPath[1024];
            snprintf(libPath, sizeof(libPath), "%s/lib", searchPath);
            if (stat(libPath, &st) == 0 && S_ISDIR(st.st_mode)) {
                cstring path = makeString(strings, libPath);
                pushOnDynArray(&result->libDirs, &path);
            }
            
            // Add library names from package info if available
            if (pkgInfo && pkgInfo->libs) {
                for (int j = 0; pkgInfo->libs[j]; j++) {
                    cstring lib = makeString(strings, pkgInfo->libs[j]);
                    pushOnDynArray(&result->libs, &lib);
                }
            } else {
                // For unknown packages, try common library name patterns
                cstring lib = NULL;
                if (strncmp(packageName, "lib", 3) == 0 && strlen(packageName) > 3) {
                    // Strip "lib" prefix: libxml2 -> xml2
                    lib = makeString(strings, packageName + 3);
                } else {
                    // Use package name as-is
                    lib = makeString(strings, packageName);
                }
                pushOnDynArray(&result->libs, &lib);
            }
            
            result->found = true;
            result->name = makeString(strings, packageName);
            freeDynArray(&searchPaths);
            return true;
        }
    }
    
    freeDynArray(&searchPaths);
    return false;
}

/**
 * Output result in flags format
 */
static void outputFlags(const PackageResult *result,
                        bool includeDir,
                        bool libDir,
                        bool lib,
                        bool cflags,
                        bool ldflags)
{
    if (includeDir) {
        for (u32 i = 0; i < result->includeDirs.size; i++) {
            cstring path = ((cstring *)result->includeDirs.elems)[i];
            printf("--c-header-dir=%s\n", path);
        }
    }
    
    if (libDir) {
        for (u32 i = 0; i < result->libDirs.size; i++) {
            cstring path = ((cstring *)result->libDirs.elems)[i];
            printf("--c-lib-dir=%s\n", path);
        }
    }
    
    if (lib) {
        for (u32 i = 0; i < result->libs.size; i++) {
            cstring name = ((cstring *)result->libs.elems)[i];
            printf("--c-lib=%s\n", name);
        }
    }
    
    if (cflags) {
        for (u32 i = 0; i < result->cflags.size; i++) {
            cstring flag = ((cstring *)result->cflags.elems)[i];
            printf("--c-flag=%s\n", flag);
        }
    }
    
    if (ldflags) {
        for (u32 i = 0; i < result->ldflags.size; i++) {
            cstring flag = ((cstring *)result->ldflags.elems)[i];
            printf("--c-flag=%s\n", flag);
        }
    }
}

/**
 * Output result in JSON format
 */
static void outputJson(const PackageResult *result)
{
    printf("{\n");
    printf("  \"name\": \"%s\",\n", result->name ? result->name : "");
    printf("  \"version\": \"%s\",\n", result->version ? result->version : "");
    printf("  \"found\": %s,\n", result->found ? "true" : "false");
    printf("  \"prefix\": \"%s\",\n", result->prefix ? result->prefix : "");
    
    printf("  \"include_dirs\": [");
    for (u32 i = 0; i < result->includeDirs.size; i++) {
        cstring path = ((cstring *)result->includeDirs.elems)[i];
        printf("%s\"%s\"", i > 0 ? ", " : "", path);
    }
    printf("],\n");
    
    printf("  \"lib_dirs\": [");
    for (u32 i = 0; i < result->libDirs.size; i++) {
        cstring path = ((cstring *)result->libDirs.elems)[i];
        printf("%s\"%s\"", i > 0 ? ", " : "", path);
    }
    printf("],\n");
    
    printf("  \"libs\": [");
    for (u32 i = 0; i < result->libs.size; i++) {
        cstring name = ((cstring *)result->libs.elems)[i];
        printf("%s\"%s\"", i > 0 ? ", " : "", name);
    }
    printf("],\n");
    
    printf("  \"cflags\": [");
    for (u32 i = 0; i < result->cflags.size; i++) {
        cstring flag = ((cstring *)result->cflags.elems)[i];
        printf("%s\"%s\"", i > 0 ? ", " : "", flag);
    }
    printf("],\n");
    
    printf("  \"ldflags\": [");
    for (u32 i = 0; i < result->ldflags.size; i++) {
        cstring flag = ((cstring *)result->ldflags.elems)[i];
        printf("%s\"%s\"", i > 0 ? ", " : "", flag);
    }
    printf("]\n");
    
    printf("}\n");
}

/**
 * Output result in YAML format
 */
static void outputYaml(const PackageResult *result)
{
    printf("name: %s\n", result->name ? result->name : "");
    printf("version: %s\n", result->version ? result->version : "");
    printf("found: %s\n", result->found ? "true" : "false");
    printf("prefix: %s\n", result->prefix ? result->prefix : "");
    
    printf("include_dirs:\n");
    for (u32 i = 0; i < result->includeDirs.size; i++) {
        cstring path = ((cstring *)result->includeDirs.elems)[i];
        printf("  - %s\n", path);
    }
    
    printf("lib_dirs:\n");
    for (u32 i = 0; i < result->libDirs.size; i++) {
        cstring path = ((cstring *)result->libDirs.elems)[i];
        printf("  - %s\n", path);
    }
    
    printf("libs:\n");
    for (u32 i = 0; i < result->libs.size; i++) {
        cstring name = ((cstring *)result->libs.elems)[i];
        printf("  - %s\n", name);
    }
    
    printf("cflags:\n");
    for (u32 i = 0; i < result->cflags.size; i++) {
        cstring flag = ((cstring *)result->cflags.elems)[i];
        printf("  - %s\n", flag);
    }
    
    printf("ldflags:\n");
    for (u32 i = 0; i < result->ldflags.size; i++) {
        cstring flag = ((cstring *)result->ldflags.elems)[i];
        printf("  - %s\n", flag);
    }
}

/**
 * Package find-system command implementation
 */
bool packageFindSystemCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *packageName = options->package.findSystemPackage;
    const char *format = options->package.findSystemFormat;
    const DynArray *searchRoots = &options->package.findSystemSearchRoots;
    bool includeDir = options->package.findSystemIncludeDir;
    bool libDir = options->package.findSystemLibDir;
    bool lib = options->package.findSystemLib;
    bool version = options->package.findSystemVersion;
    bool cflags = options->package.findSystemCFlags;
    bool ldflags = options->package.findSystemLdFlags;
    
    if (!packageName || packageName[0] == '\0') {
        logError(log, NULL, "package name is required", NULL);
        return false;
    }
    
    // Find package info in knowledge base
    SystemPackageInfo *pkgInfo = findPackageInfo(packageName);
    
    // Initialize result
    PackageResult result;
    initPackageResult(&result, strings);
    
    // Try different discovery methods in order
    if (!tryPkgConfig(packageName, pkgInfo, &result, strings, log)) {
        if (!tryHomebrew(packageName, pkgInfo, &result, strings, log)) {
            if (!trySystemPaths(packageName, pkgInfo, searchRoots, &result, strings, log)) {
                // Package not found
                result.found = false;
                result.name = makeString(strings, packageName);
            }
        }
    }
    
    // Output based on format
    if (!format || strcmp(format, "flags") == 0) {
        if (result.found) {
            outputFlags(&result, includeDir, libDir, lib, cflags, ldflags);
        }
    } else if (strcmp(format, "json") == 0) {
        outputJson(&result);
    } else if (strcmp(format, "yaml") == 0) {
        outputYaml(&result);
    } else {
        logError(log, NULL, "invalid format: {s}", (FormatArg[]){{.s = format}});
        freePackageResult(&result);
        return false;
    }
    
    bool success = result.found;
    freePackageResult(&result);
    
    return success;
}