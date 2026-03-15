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

#include "core/array.h"
#include "core/utils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;
typedef struct PackageScript PackageScript;
struct StrPool;

/**
 * Check if a script should be skipped based on its cache status
 * 
 * A script is considered cached (and can be skipped) if:
 * - It has both inputs and outputs defined
 * - All output files exist
 * - All output files are newer than all input files
 * 
 * @param script Script to check
 * @param packageDir Base directory for resolving relative paths
 * @param strings String pool for temporary allocations
 * @param log Logger for warnings and errors
 * @param isCached Output parameter - set to true if script is cached and can be skipped
 * @return true if check succeeded, false on error
 */
bool checkScriptCache(const PackageScript *script,
                      const char *packageDir,
                      struct StrPool *strings,
                      Log *log,
                      bool *isCached);

/**
 * Check script cache with environment variable substitution
 * 
 * Same as checkScriptCache but substitutes {{VAR}} templates in inputs/outputs
 * 
 * @param script Script to check
 * @param packageDir Base directory for resolving relative paths
 * @param envVars Environment variables for substitution
 * @param builtins Built-in environment variables for substitution
 * @param strings String pool for temporary allocations
 * @param log Logger for warnings and errors
 * @param isCached Output parameter - set to true if script is cached and can be skipped
 * @return true if check succeeded, false on error
 */
bool checkScriptCacheWithEnv(const PackageScript *script,
                             const char *packageDir,
                             const DynArray *envVars,
                             const DynArray *builtins,
                             struct StrPool *strings,
                             Log *log,
                             bool *isCached);

/**
 * Expand glob patterns in inputs array to actual file paths
 * 
 * Supports ** for recursive directory traversal
 * 
 * @param inputs Array of glob patterns (cstring)
 * @param packageDir Base directory for resolving relative paths
 * @param expandedFiles Output array that will be filled with expanded file paths (cstring)
 * @param strings String pool for allocating file paths
 * @param log Logger for error reporting
 * @return true if expansion succeeded, false on error
 */
bool expandInputGlobs(const DynArray *inputs,
                      const char *packageDir,
                      DynArray *expandedFiles,
                      struct StrPool *strings,
                      Log *log);

/**
 * Get the modification time of a file
 * 
 * @param path File path
 * @param mtime Output parameter for modification time in nanoseconds
 * @param log Logger for error reporting
 * @return true if succeeded, false if file doesn't exist or error
 */
bool getFileModTime(const char *path, u64 *mtime, Log *log);

#ifdef __cplusplus
}
#endif