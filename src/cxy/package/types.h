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
struct StrPool;

/**
 * Represents a dependency on another package
 */
typedef struct PackageDependency {
    cstring name;           // Package name (e.g., "json-parser")
    cstring repository;     // Git repository URL
    cstring version;        // Version constraint (e.g., "^1.2.3", "~2.0.1")
    cstring tag;            // Optional: explicit Git tag (e.g., "v1.2.3")
    cstring branch;         // Optional: Git branch (e.g., "main", "develop")
    cstring path;           // Optional: local filesystem path for development
    bool isDev;             // True if this is a dev dependency
} PackageDependency;

/**
 * Represents a test configuration entry
 */
typedef struct PackageTest {
    cstring file;           // Test file path or glob pattern
    DynArray args;          // Array of cstring - arguments to pass to test
    bool isPattern;         // True if file is a glob pattern
} PackageTest;

/**
 * Build configuration for the package
 */
typedef struct PackageBuildConfig {
    cstring entry;          // Entry point file (e.g., "src/lib.cxy")
    DynArray cLibs;         // Array of cstring - C libraries to link (c-libs)
    DynArray cLibDirs;      // Array of cstring - C library search paths (c-lib-dirs)
    DynArray cHeaderDirs;   // Array of cstring - C header search paths (c-header-dirs)
    DynArray cDefines;      // Array of cstring - C preprocessor defines (c-defines)
    DynArray cFlags;        // Array of cstring - C compiler flags (c-flags)
    DynArray defines;       // Array of cstring - Cxy compiler defines (e.g., "TEST_MODE", "DEBUG=1")
    DynArray flags;         // Array of cstring - Cxy compiler flags
    cstring pluginsDir;     // Plugins directory path (plugins-dir)
    cstring stdlib;         // Standard library path (stdlib)
    cstring output;         // Output file path (e.g., "my-app", "my-lib")
} PackageBuildConfig;

/**
 * Named build target with configuration
 */
typedef struct PackageBuild {
    cstring name;           // Build target name (e.g., "lib", "bin", "debug")
    bool isDefault;         // True if this is the default build target
    PackageBuildConfig config;  // Build configuration
} PackageBuild;

/**
 * Represents an environment variable for scripts
 */
typedef struct EnvVar {
    cstring name;           // Variable name (e.g., "BUILD_DIR")
    cstring value;          // Variable value (may contain {{VAR}} templates)
} EnvVar;

/**
 * Represents a script command entry (name -> command mapping)
 */
typedef struct PackageScript {
    cstring name;           // Script name (e.g., "build", "test", "clean")
    cstring command;        // Shell command to execute
    DynArray dependencies;  // Array of cstring - script names that must run first
} PackageScript;

/**
 * Package metadata from Cxyfile.yaml
 */
typedef struct PackageMetadata {
    cstring name;           // Package name
    cstring version;        // Package version (semver)
    cstring description;    // Short description
    cstring author;         // Author name and email
    cstring license;        // License identifier (MIT, Apache-2.0, etc.)
    cstring repository;     // Git repository URL
    cstring homepage;       // Optional: package homepage URL

    DynArray dependencies;     // Array of PackageDependency
    DynArray devDependencies;  // Array of PackageDependency
    DynArray tests;            // Array of PackageTest
    DynArray scriptEnv;        // Array of EnvVar - environment variables for scripts
    DynArray scripts;          // Array of PackageScript

    // Build configuration (supports both single and multiple builds)
    PackageBuildConfig build;  // Single build (legacy, backwards compatible)
    DynArray builds;           // Array of PackageBuild - multiple named builds
    bool hasMultipleBuilds;    // True if using builds: section instead of build:
} PackageMetadata;

/**
 * Represents a locked dependency with exact version and commit
 */
typedef struct PackageLock {
    cstring name;           // Package name
    cstring repository;     // Git repository URL
    cstring version;        // Exact version (e.g., "1.2.3")
    cstring tag;            // Git tag (e.g., "v1.2.3")
    cstring commit;         // Full commit hash
    cstring checksum;       // SHA-256 checksum for verification
    DynArray dependencies;  // Array of cstring - names of dependencies
} PackageLock;

/**
 * Lock file structure
 */
typedef struct PackageLockFile {
    i32 version;            // Lock file format version
    cstring generated;      // ISO 8601 timestamp of generation
    DynArray packages;      // Array of PackageLock
    DynArray resolved;      // Array of resolved version mappings (name -> version)
} PackageLockFile;

/**
 * Version constraint types for dependency resolution
 */
typedef enum {
    vcExact,        // Exact version: "1.2.3"
    vcCaret,        // Caret range: "^1.2.3" (>=1.2.3 <2.0.0)
    vcTilde,        // Tilde range: "~1.2.3" (>=1.2.3 <1.3.0)
    vcGreater,      // Greater than: ">1.2.3"
    vcGreaterEq,    // Greater or equal: ">=1.2.3"
    vcLess,         // Less than: "<1.2.3"
    vcLessEq,       // Less or equal: "<=1.2.3"
    vcWildcard,     // Wildcard: "1.2.x" or "*"
    vcAny           // Any version
} VersionConstraintType;

/**
 * Semantic version structure
 */
typedef struct SemanticVersion {
    u32 major;
    u32 minor;
    u32 patch;
    cstring prerelease;     // Optional: prerelease identifier (e.g., "alpha.1")
    cstring build;          // Optional: build metadata (e.g., "001")
} SemanticVersion;

/**
 * Version constraint for dependency resolution
 */
typedef struct VersionConstraint {
    VersionConstraintType type;
    SemanticVersion version;
    cstring raw;            // Original constraint string
} VersionConstraint;

/**
 * Parse a semantic version string (e.g., "1.2.3", "2.0.0-alpha.1")
 */
bool parseSemanticVersion(cstring str, SemanticVersion *version, Log *log);

/**
 * Parse a version constraint string (e.g., "^1.2.3", "~2.0.1", ">=1.0.0")
 */
bool parseVersionConstraint(cstring str, VersionConstraint *constraint, Log *log);

/**
 * Compare two semantic versions
 * Returns: -1 if a < b, 0 if a == b, 1 if a > b
 */
i32 compareSemanticVersions(const SemanticVersion *a, const SemanticVersion *b);

/**
 * Check if a version satisfies a constraint
 */
bool versionSatisfiesConstraint(const SemanticVersion *version,
                               const VersionConstraint *constraint);

/**
 * Initialize a PackageMetadata structure
 */
void initPackageMetadata(PackageMetadata *meta, struct StrPool *strings);

/**
 * Free resources associated with PackageMetadata
 */
void freePackageMetadata(PackageMetadata *meta);

/**
 * Initialize a PackageLockFile structure
 */
void initPackageLockFile(PackageLockFile *lock);

/**
 * Free resources associated with PackageLockFile
 */
void freePackageLockFile(PackageLockFile *lock);

/**
 * Validate scripts for circular dependencies and undefined script references
 *
 * @param meta Package metadata containing scripts to validate
 * @param log Logger for error reporting
 * @return true if scripts are valid, false if cycles or undefined refs found
 */
bool validateScripts(const PackageMetadata *meta, Log *log);

/**
 * Get execution order for a script, resolving all dependencies
 *
 * @param meta Package metadata containing scripts
 * @param scriptName Name of the script to execute
 * @param executionOrder Output array that will be filled with script names in execution order
 * @param log Logger for error reporting
 * @return true if resolution succeeded, false if errors (cycles, undefined refs)
 */
bool resolveScriptDependencies(const PackageMetadata *meta,
                               cstring scriptName,
                               DynArray *executionOrder,
                               Log *log);

#ifdef __cplusplus
}
#endif
