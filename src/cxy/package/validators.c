/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "validators.h"
#include "types.h"
#include <ctype.h>
#include <string.h>

cstring validatePackageName(cstring name)
{
    if (!name || name[0] == '\0') {
        return "package name cannot be empty";
    }

    size_t len = strlen(name);
    
    // Check length
    if (len > 214) {
        return "package name cannot exceed 214 characters";
    }

    // Check first character is a letter
    if (!islower(name[0])) {
        return "package name must start with a lowercase letter";
    }

    // Check for invalid names
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return "package name cannot be '.' or '..'";
    }

    // Validate characters and check for consecutive/trailing special chars
    char prev = name[0];
    for (size_t i = 1; i < len; i++) {
        char c = name[i];
        
        // Check allowed characters: lowercase letters, digits, hyphen, underscore
        if (!islower(c) && !isdigit(c) && c != '-' && c != '_') {
            return "package name can only contain lowercase letters, numbers, hyphens, and underscores";
        }

        // Check for consecutive hyphens or underscores
        if ((c == '-' && prev == '-') || (c == '_' && prev == '_')) {
            return "package name cannot contain consecutive hyphens or underscores";
        }

        prev = c;
    }

    // Check last character is not hyphen or underscore
    if (name[len - 1] == '-' || name[len - 1] == '_') {
        return "package name cannot end with a hyphen or underscore";
    }

    return NULL;
}

cstring validateSemanticVersion(cstring version)
{
    if (!version || version[0] == '\0') {
        return "version cannot be empty";
    }

    // Use existing semver parser from types.h
    SemanticVersion semver;
    
    // We need a minimal parser without Log dependency
    // Parse MAJOR.MINOR.PATCH manually
    const char *ptr = version;
    
    // Skip any leading 'v' (though we'll reject it)
    if (*ptr == 'v' || *ptr == 'V') {
        return "version must not start with 'v' prefix";
    }

    // Parse major
    if (!isdigit(*ptr)) {
        return "version must start with a number";
    }
    
    // Check for leading zeros
    if (*ptr == '0' && isdigit(*(ptr + 1))) {
        return "version components cannot have leading zeros";
    }

    unsigned long major = strtoul(ptr, (char **)&ptr, 10);
    if (*ptr != '.') {
        return "version must be in format MAJOR.MINOR.PATCH (e.g., '1.0.0')";
    }
    ptr++; // skip '.'

    // Parse minor
    if (!isdigit(*ptr)) {
        return "minor version must be a number";
    }
    
    if (*ptr == '0' && isdigit(*(ptr + 1))) {
        return "version components cannot have leading zeros";
    }

    unsigned long minor = strtoul(ptr, (char **)&ptr, 10);
    if (*ptr != '.') {
        return "version must be in format MAJOR.MINOR.PATCH (e.g., '1.0.0')";
    }
    ptr++; // skip '.'

    // Parse patch
    if (!isdigit(*ptr)) {
        return "patch version must be a number";
    }
    
    if (*ptr == '0' && isdigit(*(ptr + 1))) {
        return "version components cannot have leading zeros";
    }

    unsigned long patch = strtoul(ptr, (char **)&ptr, 10);

    // After MAJOR.MINOR.PATCH, we can have:
    // - end of string
    // - '-' followed by prerelease
    // - '+' followed by build metadata
    if (*ptr == '\0') {
        return NULL; // Valid simple version
    }

    if (*ptr == '-') {
        // Prerelease identifier
        ptr++;
        if (*ptr == '\0' || *ptr == '+') {
            return "prerelease identifier cannot be empty";
        }
        
        // Skip prerelease (alphanumeric, hyphens, dots)
        while (*ptr && *ptr != '+') {
            if (!isalnum(*ptr) && *ptr != '.' && *ptr != '-') {
                return "prerelease identifier can only contain alphanumeric characters, dots, and hyphens";
            }
            ptr++;
        }
    }

    if (*ptr == '+') {
        // Build metadata
        ptr++;
        if (*ptr == '\0') {
            return "build metadata cannot be empty";
        }
        
        // Skip build metadata (alphanumeric, hyphens, dots)
        while (*ptr) {
            if (!isalnum(*ptr) && *ptr != '.' && *ptr != '-') {
                return "build metadata can only contain alphanumeric characters, dots, and hyphens";
            }
            ptr++;
        }
    }

    if (*ptr != '\0') {
        return "invalid version format";
    }

    return NULL;
}

cstring validateGitRepository(cstring repository)
{
    if (!repository || repository[0] == '\0') {
        return "repository cannot be empty";
    }

    size_t len = strlen(repository);

    // Check for whitespace
    for (size_t i = 0; i < len; i++) {
        if (isspace(repository[i])) {
            return "repository identifier cannot contain whitespace";
        }
    }

    // Check for HTTPS/HTTP URLs
    if (strncmp(repository, "https://", 8) == 0 ||
        strncmp(repository, "http://", 7) == 0) {
        // Basic URL validation: must have something after protocol
        const char *host = strchr(repository, ':') + 3; // skip "://"
        if (!host || !*host || *host == '/') {
            return "invalid URL: missing host";
        }
        
        // Must have a path
        const char *path = strchr(host, '/');
        if (!path || !*(path + 1)) {
            return "invalid URL: missing path";
        }
        
        return NULL;
    }

    // Check for Git protocol URLs
    if (strncmp(repository, "git://", 6) == 0) {
        const char *host = repository + 6;
        if (!*host || *host == '/') {
            return "invalid git:// URL: missing host";
        }
        
        const char *path = strchr(host, '/');
        if (!path || !*(path + 1)) {
            return "invalid git:// URL: missing path";
        }
        
        return NULL;
    }

    // Check for SSH URLs (git@host:path)
    if (strncmp(repository, "git@", 4) == 0) {
        const char *colon = strchr(repository, ':');
        if (!colon || colon == repository + 4) {
            return "invalid SSH URL: must be in format git@host:path";
        }
        
        if (!*(colon + 1)) {
            return "invalid SSH URL: missing path after ':'";
        }
        
        return NULL;
    }

    // Check for SSH protocol URLs
    if (strncmp(repository, "ssh://", 6) == 0) {
        const char *host = repository + 6;
        if (!*host || *host == '/') {
            return "invalid ssh:// URL: missing host";
        }
        
        const char *path = strchr(host, '/');
        if (!path || !*(path + 1)) {
            return "invalid ssh:// URL: missing path";
        }
        
        return NULL;
    }

    // Check for short format (platform:user/repo)
    const char *colon = strchr(repository, ':');
    if (colon) {
        size_t prefix_len = colon - repository;
        const char *platforms[] = {"github", "gitlab", "bitbucket"};
        bool is_platform = false;
        
        for (size_t i = 0; i < sizeof(platforms) / sizeof(platforms[0]); i++) {
            if (strncmp(repository, platforms[i], prefix_len) == 0 &&
                strlen(platforms[i]) == prefix_len) {
                is_platform = true;
                break;
            }
        }
        
        if (is_platform) {
            const char *path = colon + 1;
            if (!*path) {
                return "invalid short format: missing user/repo after ':'";
            }
            
            // Check for user/repo format
            const char *slash = strchr(path, '/');
            if (!slash || slash == path || !*(slash + 1)) {
                return "invalid short format: must be platform:user/repo";
            }
            
            return NULL;
        }
    }

    return "repository must be a valid URL (https://, git://, ssh://, git@host:path) or short format (github:user/repo)";
}

cstring validateLicenseIdentifier(cstring license)
{
    // Empty/NULL is allowed for optional fields
    if (!license || license[0] == '\0') {
        return NULL;
    }

    // Check for special keywords
    if (strcasecmp(license, "UNLICENSED") == 0 ||
        strcasecmp(license, "PROPRIETARY") == 0) {
        return NULL;
    }

    // Check for "SEE LICENSE IN" pattern
    if (strncasecmp(license, "SEE LICENSE IN ", 15) == 0) {
        if (license[15] == '\0') {
            return "license file reference cannot be empty";
        }
        return NULL;
    }

    // Common SPDX identifiers (not exhaustive, but covers most used)
    const char *common_licenses[] = {
        "MIT",
        "Apache-2.0",
        "GPL-2.0",
        "GPL-2.0-only",
        "GPL-2.0-or-later",
        "GPL-3.0",
        "GPL-3.0-only",
        "GPL-3.0-or-later",
        "LGPL-2.1",
        "LGPL-2.1-only",
        "LGPL-2.1-or-later",
        "LGPL-3.0",
        "LGPL-3.0-only",
        "LGPL-3.0-or-later",
        "BSD-2-Clause",
        "BSD-3-Clause",
        "BSD-4-Clause",
        "ISC",
        "MPL-2.0",
        "AGPL-3.0",
        "AGPL-3.0-only",
        "AGPL-3.0-or-later",
        "Unlicense",
        "CC0-1.0",
        "CC-BY-4.0",
        "CC-BY-SA-4.0",
        "WTFPL",
        "Zlib",
        "Artistic-2.0",
        "EPL-2.0",
        "BSL-1.0",
        "PostgreSQL",
        "0BSD",
    };

    for (size_t i = 0; i < sizeof(common_licenses) / sizeof(common_licenses[0]); i++) {
        if (strcasecmp(license, common_licenses[i]) == 0) {
            return NULL;
        }
    }

    return "license must be a valid SPDX identifier (e.g., MIT, Apache-2.0, GPL-3.0), UNLICENSED, PROPRIETARY, or 'SEE LICENSE IN <file>'";
}