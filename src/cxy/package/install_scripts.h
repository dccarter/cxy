/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-20
 */

#pragma once

#include "core/array.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PackageMetadata PackageMetadata;
typedef struct StrPool StrPool;
typedef struct Log Log;

/**
 * Execute install scripts and generate .install.yaml
 * 
 * @param meta Package metadata containing install scripts
 * @param packageDir Package directory (where Cxyfile.yaml is located)
 * @param buildDir Build directory (where .install.yaml will be created)
 * @param includeDev Whether to include install-dev scripts
 * @param strings String pool for allocations
 * @param log Logger for output
 * @param verbose Whether to show detailed output
 * @return true if all required scripts succeeded, false otherwise
 */
bool executeInstallScripts(const PackageMetadata *meta,
                           const char *packageDir,
                           const char *packagesDir,
                           const char *buildDir,
                           bool includeDev,
                           StrPool *strings,
                           Log *log,
                           bool verbose);

/**
 * Read flags from .install.yaml or .install.dev.yaml for use during build/test
 * 
 * @param buildDir Build directory containing .install.yaml or .install.dev.yaml
 * @param flags Output array to be filled with flags (cstring elements)
 * @param useDev If true, try .install.dev.yaml first, then fall back to .install.yaml
 * @param strings String pool for allocations
 * @param log Logger for errors
 * @return true on success, false on error
 */
bool readInstallYamlFlags(const char *buildDir,
                         DynArray *flags,
                         bool useDev,
                         StrPool *strings,
                         Log *log);

#ifdef __cplusplus
}
#endif