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
#include "package/gitops.h"
#include "core/array.h"
#include "core/mempool.h"
#include "core/strpool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;

/**
 * Represents a resolved dependency with its selected version
 */
typedef struct ResolvedDependency {
    cstring name;                   // Package name
    cstring repository;             // Git repository URL
    SemanticVersion version;        // Resolved version
    cstring tag;                    // Git tag for this version
    cstring commit;                 // Exact commit hash (from git)
    cstring checksum;               // SHA-256 checksum of package contents
    DynArray dependencies;          // Array of ResolvedDependency (transitive deps)
    bool isDev;                     // Whether this is a dev dependency
} ResolvedDependency;

/**
 * Represents a version conflict between dependencies
 */
typedef struct VersionConflict {
    cstring packageName;            // Name of conflicting package
    DynArray constraints;           // Array of VersionConstraint from different deps
    DynArray requestedBy;           // Array of cstring (package names requesting this)
} VersionConflict;

/**
 * Dependency resolution context
 */
typedef struct ResolverContext {
    DynArray resolved;              // Array of ResolvedDependency
    DynArray conflicts;             // Array of VersionConflict
    MemPool *pool;                  // Memory pool for allocations
    Log *log;                       // Logger
    bool allowPrerelease;           // Whether to consider prerelease versions
    bool allowDevDeps;              // Whether to resolve dev dependencies
} ResolverContext;

/**
 * Initialize a resolver context
 *
 * @param ctx Resolver context to initialize
 * @param pool Memory pool for allocations
 * @param log Logger for diagnostic messages
 */
void initResolverContext(ResolverContext *ctx, MemPool *pool, Log *log);

/**
 * Free resources associated with a resolver context
 *
 * @param ctx Resolver context to free
 */
void freeResolverContext(ResolverContext *ctx);

/**
 * Resolve all dependencies for a package
 * 
 * This builds a complete dependency graph, resolves version constraints,
 * detects conflicts, and produces a flat list of resolved dependencies.
 *
 * Phase 3 implementation:
 * - Fetch available versions from git repositories
 * - Match version constraints (exact, caret, tilde, range)
 * - Detect and report conflicts
 * - Produce deterministic resolution order
 *
 * @param ctx Resolver context
 * @param meta Package metadata containing dependencies
 * @return true if resolution succeeded, false if conflicts exist
 */
bool resolveDependencies(ResolverContext *ctx, const PackageMetadata *meta);

/**
 * Resolve a single dependency and its transitive dependencies
 *
 * Internal helper used by resolveDependencies to recursively resolve
 * the dependency tree.
 *
 * @param ctx Resolver context
 * @param dep Dependency to resolve
 * @param requestedBy Name of package requesting this dependency
 * @param constraint Version constraint to satisfy
 * @return true if resolution succeeded, false otherwise
 */
bool resolveDependency(ResolverContext *ctx,
                      const PackageDependency *dep,
                      cstring requestedBy,
                      const VersionConstraint *constraint);

/**
 * Find the best matching version for a dependency given constraints
 *
 * Fetches available versions from the repository and selects the highest
 * version that satisfies all given constraints.
 *
 * @param repository Git repository URL
 * @param constraints Array of VersionConstraint to satisfy
 * @param bestTag Output parameter for selected GitTag
 * @param pool Memory pool for allocations
 * @param log Logger
 * @return true if a matching version was found, false otherwise
 */
bool findBestMatchingVersion(cstring repository,
                            const DynArray *constraints,
                            GitTag *bestTag,
                            MemPool *pool,
                            Log *log);

/**
 * Check if a dependency is already resolved
 *
 * @param ctx Resolver context
 * @param name Package name
 * @param resolved Output parameter for existing resolved dependency (optional)
 * @return true if already resolved, false otherwise
 */
bool isDependencyResolved(const ResolverContext *ctx,
                         cstring name,
                         ResolvedDependency **resolved);

/**
 * Add a version conflict to the resolver context
 *
 * @param ctx Resolver context
 * @param packageName Name of conflicting package
 * @param constraint Version constraint that conflicts
 * @param requestedBy Package requesting this constraint
 */
void addVersionConflict(ResolverContext *ctx,
                       cstring packageName,
                       const VersionConstraint *constraint,
                       cstring requestedBy);

/**
 * Generate a lock file from resolved dependencies
 *
 * Creates a Cxyfile.lock with exact versions, commits, and checksums
 * for reproducible builds.
 *
 * @param ctx Resolver context with resolved dependencies
 * @param lockFilePath Path to write Cxyfile.lock
 * @return true if lock file was written successfully, false otherwise
 */
bool generateLockFile(const ResolverContext *ctx, cstring lockFilePath);

/**
 * Load and validate a lock file
 *
 * Reads Cxyfile.lock and populates the resolver context with locked dependencies.
 *
 * @param lockFilePath Path to Cxyfile.lock
 * @param ctx Resolver context to populate with locked dependencies
 * @return true if lock file was loaded successfully, false otherwise
 */
bool loadLockFile(cstring lockFilePath, ResolverContext *ctx);

/**
 * Verify that installed packages match the lock file
 *
 * Checks that:
 * - All packages in lock file are installed
 * - Installed commits match lock file commits
 * - Checksums match (if present in lock file)
 *
 * @param lockFilePath Path to Cxyfile.lock
 * @param packagesDir Path to .cxy/packages directory
 * @param pool Memory pool for allocations
 * @param log Logger for diagnostic messages
 * @return true if verification passed, false if mismatches found
 */
bool verifyLockFile(cstring lockFilePath,
                   cstring packagesDir,
                   MemPool *pool,
                   Log *log);

/**
 * Verify a single installed package against lock file entry
 *
 * @param resolved Expected package from lock file
 * @param packagesDir Path to .cxy/packages directory
 * @param pool Memory pool for allocations
 * @param log Logger
 * @return true if package matches, false otherwise
 */
bool verifyInstalledPackage(const ResolvedDependency *resolved,
                           cstring packagesDir,
                           MemPool *pool,
                           Log *log);

/**
 * Print version conflicts to the log
 *
 * Formats and displays all detected conflicts for user review.
 *
 * @param ctx Resolver context containing conflicts
 */
void printVersionConflicts(const ResolverContext *ctx);

#ifdef __cplusplus
}
#endif
