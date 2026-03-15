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
struct StrPool;

/**
 * Substitute {{VAR}} placeholders in a string with environment variable values
 * 
 * @param template String containing {{VAR}} placeholders
 * @param envVars Array of EnvVar to use for substitution
 * @param builtins Array of EnvVar containing built-in variables (SOURCE_DIR, etc.)
 * @param strings String pool for allocating result
 * @param log Logger for error reporting
 * @return Substituted string, or NULL on error
 */
cstring substituteEnvVars(const char *template,
                         const DynArray *envVars,
                         const DynArray *builtins,
                         struct StrPool *strings,
                         Log *log);

/**
 * Build environment variable array with built-ins
 * 
 * Creates an array containing:
 * - SOURCE_DIR: Directory containing Cxyfile.yaml
 * - PACKAGE_NAME: Package name from metadata
 * - PACKAGE_VERSION: Package version from metadata
 * - CXY_PACKAGES_DIR: Path to packages directory
 * 
 * @param meta Package metadata
 * @param packageDir Directory containing Cxyfile.yaml
 * @param packagesDir Directory for installed packages (e.g., ".cxy/packages")
 * @param strings String pool for allocating values
 * @return Array of EnvVar with built-in variables
 */
DynArray buildBuiltinEnvVars(const PackageMetadata *meta,
                             const char *packageDir,
                             const char *packagesDir,
                             struct StrPool *strings);

/**
 * Resolve environment variables in order, substituting references between them
 * 
 * Processes env vars in order, substituting {{VAR}} references to previously
 * defined variables. Detects circular references.
 * 
 * @param envVars Array of EnvVar from Cxyfile (will be modified in place)
 * @param builtins Array of built-in EnvVar
 * @param strings String pool for allocating resolved values
 * @param log Logger for error reporting
 * @return true if all variables resolved successfully, false on error
 */
bool resolveEnvVars(DynArray *envVars,
                   const DynArray *builtins,
                   struct StrPool *strings,
                   Log *log);

/**
 * Set environment variables for script execution
 * 
 * Sets all environment variables in the current process environment
 * so they're available to executed scripts as $VAR.
 * 
 * @param envVars Array of resolved EnvVar to set
 * @param builtins Array of built-in EnvVar to set
 * @param log Logger for error reporting
 * @return true if all variables set successfully, false on error
 */
bool setScriptEnvironment(const DynArray *envVars,
                         const DynArray *builtins,
                         Log *log);

/**
 * Clear/unset script environment variables
 * 
 * Unsets all environment variables that were set for script execution.
 * Should be called after script completes to clean up environment.
 * 
 * @param envVars Array of EnvVar to unset
 * @param builtins Array of built-in EnvVar to unset
 */
void clearScriptEnvironment(const DynArray *envVars,
                            const DynArray *builtins);

#ifdef __cplusplus
}
#endif