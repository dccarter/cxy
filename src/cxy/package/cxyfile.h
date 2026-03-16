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

#include "package/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;
typedef struct MemPool MemPool;
struct StrPool;

/**
 * Parse a Cxyfile.yaml file and populate PackageMetadata structure
 *
 * @param path Path to Cxyfile.yaml
 * @param meta Output parameter for parsed metadata
 * @param strings String pool for allocating strings
 * @param log Logger for error reporting
 * @return true if parsing succeeded, false otherwise
 */
bool parseCxyfile(const char *path,
                  PackageMetadata *meta,
                  struct StrPool *strings,
                  Log *log);

/**
 * Write PackageMetadata to a Cxyfile.yaml file
 *
 * @param path Path to Cxyfile.yaml to write
 * @param meta Metadata to write
 * @param log Logger for error reporting
 * @return true if writing succeeded, false otherwise
 */
bool writeCxyfile(const char *path,
                  const PackageMetadata *meta,
                  Log *log);

/**
 * Validate that a PackageMetadata structure has all required fields
 *
 * @param meta Metadata to validate
 * @param log Logger for error reporting
 * @return true if valid, false otherwise
 */
bool validatePackageMetadata(const PackageMetadata *meta, Log *log);

/**
 * Find and load the Cxyfile.yaml in the current directory or parent directories
 *
 * @param startDir Directory to start searching from (NULL for current directory)
 * @param meta Output parameter for parsed metadata
 * @param strings String pool for allocating strings
 * @param log Logger for error reporting
 * @param foundPath Output parameter for the path where Cxyfile.yaml was found (optional)
 * @return true if found and parsed successfully, false otherwise
 */
bool findAndLoadCxyfile(const char *startDir,
                        PackageMetadata *meta,
                        struct StrPool *strings,
                        Log *log,
                        char **foundPath);

/**
 * Install a dependency to the packages directory
 *
 * Downloads/clones the dependency, verifies it contains a Cxyfile.yaml,
 * and checks out the appropriate version.
 *
 * @param dep Dependency to install
 * @param packagesDir Base directory for packages (e.g., ".cxy/packages")
 * @param pool Memory pool for allocations
 * @param log Logger for error reporting
 * @param noInstall If true, skip installation (only validate)
 * @return true if installation succeeded, false otherwise
 */
bool installDependency(const PackageDependency *dep,
                       const char *packagesDir,
                       MemPool *pool,
                       Log *log,
                       bool noInstall,
                       bool verbose);

#ifdef __cplusplus
}
#endif
