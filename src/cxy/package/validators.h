/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#pragma once

#include "core/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Validate a package name.
 * 
 * Rules:
 * - Length: 1-214 characters
 * - Allowed characters: lowercase letters, numbers, hyphens, underscores
 * - Must start with a letter
 * - No consecutive hyphens or underscores
 * - No leading/trailing hyphens or underscores
 * - Cannot be just "." or ".."
 * 
 * @param name Package name to validate
 * @return NULL if valid, error message string if invalid
 */
cstring validatePackageName(cstring name);

/**
 * Validate a semantic version string.
 * 
 * Rules:
 * - Format: MAJOR.MINOR.PATCH (e.g., "1.0.0")
 * - Optional pre-release: "1.0.0-alpha.1"
 * - Optional build metadata: "1.0.0+20130313144700"
 * - Each component must be a non-negative integer
 * - No leading zeros except standalone zero
 * 
 * @param version Version string to validate
 * @return NULL if valid, error message string if invalid
 */
cstring validateSemanticVersion(cstring version);

/**
 * Validate a Git repository identifier.
 * 
 * Accepts multiple formats:
 * - HTTPS/HTTP URLs: "https://github.com/user/repo.git"
 * - Git URLs: "git://github.com/user/repo.git"
 * - SSH URLs: "git@github.com:user/repo.git"
 * - Short format: "github:user/repo", "gitlab:user/repo", "bitbucket:user/repo"
 * 
 * @param repository Repository identifier to validate
 * @return NULL if valid, error message string if invalid
 */
cstring validateGitRepository(cstring repository);

/**
 * Validate a license identifier (lenient SPDX validation).
 * 
 * Accepts:
 * - Common SPDX identifiers: "MIT", "Apache-2.0", "GPL-3.0", etc.
 * - Special values: "UNLICENSED", "PROPRIETARY"
 * - File reference: "SEE LICENSE IN <filename>"
 * - Empty/NULL for optional fields
 * 
 * @param license License identifier to validate
 * @return NULL if valid, error message string if invalid
 */
cstring validateLicenseIdentifier(cstring license);

#ifdef __cplusplus
}
#endif