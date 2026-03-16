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
#include "core/array.h"
#include "core/mempool.h"
#include "package/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;

/**
 * Represents a Git tag with associated metadata
 */
typedef struct GitTag {
    cstring name;           // Tag name (e.g., "v1.2.3")
    cstring commit;         // Full commit hash
    SemanticVersion version; // Parsed semantic version (tags that don't parse are filtered out)
} GitTag;

/**
 * Represents a Git commit
 */
typedef struct GitCommit {
    cstring hash;           // Full commit hash
    cstring shortHash;      // Short commit hash (first 7 chars)
    cstring message;        // Commit message
    cstring author;         // Author name and email
    cstring date;           // Commit date (ISO 8601 format)
} GitCommit;

/**
 * Check if a Git repository is accessible (exists and is reachable)
 * Uses git ls-remote to verify connectivity without cloning
 *
 * Phase 3: Use this before adding/installing dependencies
 *
 * @param repositoryUrl Git repository URL (https://, git://, git@, etc.)
 * @param log Logger for error reporting
 * @return true if repository is accessible, false otherwise
 */
bool gitIsRepositoryAccessible(cstring repositoryUrl, Log *log);

/**
 * Fetch all tags from a remote repository and parse as semantic versions
 * Tags that don't conform to semantic versioning are ignored.
 * Results are sorted by semantic version in ascending order.
 *
 * Phase 3: Use this to get available versions for dependency resolution
 *
 * @param repositoryUrl Git repository URL
 * @param tags Output array of GitTag structures (allocated from pool), sorted by version
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if tags were fetched successfully, false otherwise
 */
bool gitFetchTags(cstring repositoryUrl, DynArray *tags, MemPool *pool, Log *log);

/**
 * Get the latest tag from a remote repository (highest semantic version)
 * Fetches all tags and returns the one with the highest semantic version.
 *
 * Phase 3: Use for resolving "latest" version or finding best match
 *
 * @param repositoryUrl Git repository URL
 * @param pattern Optional version constraint pattern (e.g., "^1.2.0", NULL for any) [TODO: not yet implemented]
 * @param tag Output parameter for the latest matching tag (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if a matching tag was found, false otherwise
 */
bool gitGetLatestTag(cstring repositoryUrl, cstring pattern, GitTag *tag, MemPool *pool, Log *log);

/**
 * Get the latest tag from a remote repository that matches a version pattern
 *
 * Phase 3: Use for resolving "latest" version or finding best match
 *
 * @param repositoryUrl Git repository URL
 * @param pattern Optional pattern to match (e.g., "v1.*", NULL for any)
 * @param tag Output parameter for the latest matching tag (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if a matching tag was found, false otherwise
 */
bool gitGetLatestTag(cstring repositoryUrl, cstring pattern, GitTag *tag, MemPool *pool, Log *log);

/**
 * Clone a Git repository to a local directory
 *
 * Phase 3: Use for downloading dependencies to .cxy/packages/
 *
 * @param repositoryUrl Git repository URL
 * @param destination Local directory path to clone into
 * @param shallow If true, perform shallow clone (--depth 1)
 * @param log Logger for error reporting
 * @return true if clone succeeded, false otherwise
 */
bool gitClone(cstring repositoryUrl, cstring destination, bool shallow, Log *log, bool verbose);

/**
 * Clone a specific branch from a Git repository
 *
 * Phase 3: Use for branch-pinned dependencies
 *
 * @param repositoryUrl Git repository URL
 * @param branch Branch name to clone
 * @param destination Local directory path to clone into
 * @param shallow If true, perform shallow clone (--depth 1)
 * @param log Logger for error reporting
 * @return true if clone succeeded, false otherwise
 */
bool gitCloneBranch(cstring repositoryUrl, cstring branch, cstring destination, bool shallow, Log *log, bool verbose);

/**
 * Clone a specific tag from a Git repository
 *
 * Phase 3: Use for tag/version-pinned dependencies
 *
 * @param repositoryUrl Git repository URL
 * @param tag Tag name to checkout
 * @param destination Local directory path to clone into
 * @param log Logger for error reporting
 * @return true if clone succeeded, false otherwise
 */
bool gitCloneTag(cstring repositoryUrl, cstring tag, cstring destination, Log *log, bool verbose);

/**
 * Checkout a specific commit in an existing repository
 *
 * Phase 3: Use for lockfile-based installs with exact commit hashes
 *
 * @param repoPath Path to local Git repository
 * @param commitHash Commit hash to checkout
 * @param log Logger for error reporting
 * @return true if checkout succeeded, false otherwise
 */
bool gitCheckoutCommit(cstring repoPath, cstring commitHash, Log *log, bool verbose);

/**
 * Get the current commit hash of a local repository
 *
 * Phase 3: Use when generating lockfile entries
 *
 * @param repoPath Path to local Git repository
 * @param commitHash Output parameter for commit hash (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if commit hash was retrieved, false otherwise
 */
bool gitGetCurrentCommit(cstring repoPath, cstring *commitHash, MemPool *pool, Log *log);

/**
 * Get the current branch name of a local repository
 *
 * @param repoPath Path to local Git repository
 * @param branchName Output parameter for branch name (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if branch name was retrieved, false otherwise
 */
bool gitGetCurrentBranch(cstring repoPath, cstring *branchName, MemPool *pool, Log *log);

/**
 * Pull latest changes from remote in an existing repository
 *
 * Phase 3: Use for update command
 *
 * @param repoPath Path to local Git repository
 * @param log Logger for error reporting
 * @return true if pull succeeded, false otherwise
 */
bool gitPull(cstring repoPath, Log *log, bool verbose);

/**
 * Check if a local directory is a Git repository
 *
 * @param path Directory path to check
 * @return true if directory contains a .git folder, false otherwise
 */
bool gitIsRepository(cstring path);

/**
 * Get information about a specific commit
 *
 * Phase 3: Use for displaying commit info in list/info commands
 *
 * @param repoPath Path to local Git repository
 * @param commitHash Commit hash to query (NULL for HEAD)
 * @param commit Output parameter for commit info (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if commit info was retrieved, false otherwise
 */
bool gitGetCommitInfo(cstring repoPath, cstring commitHash, GitCommit *commit, MemPool *pool, Log *log);

/**
 * Get the remote URL of a local repository
 *
 * @param repoPath Path to local Git repository
 * @param remoteName Remote name (e.g., "origin"), NULL for default
 * @param url Output parameter for remote URL (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if URL was retrieved, false otherwise
 */
bool gitGetRemoteUrl(cstring repoPath, cstring remoteName, cstring *url, MemPool *pool, Log *log);

/**
 * Create and push a Git tag (for publish command)
 *
 * Phase 4: Use for package publish command
 *
 * @param repoPath Path to local Git repository
 * @param tagName Tag name to create
 * @param message Tag annotation message (NULL for lightweight tag)
 * @param push If true, push tag to remote
 * @param log Logger for error reporting
 * @return true if tag was created (and pushed if requested), false otherwise
 */
bool gitCreateTag(cstring repoPath, cstring tagName, cstring message, bool push, Log *log, bool verbose);

/**
 * Check if repository has uncommitted changes
 *
 * Phase 4: Use for publish command to ensure clean working directory
 *
 * @param repoPath Path to local Git repository
 * @param hasChanges Output parameter indicating if there are uncommitted changes
 * @param log Logger for error reporting
 * @return true if check succeeded, false otherwise
 */
bool gitHasUncommittedChanges(cstring repoPath, bool *hasChanges, Log *log);

/**
 * Calculate checksum (SHA-256) of repository contents
 *
 * Phase 3: Use for lockfile checksum verification
 *
 * @param repoPath Path to local Git repository
 * @param checksum Output parameter for checksum string (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if checksum was calculated, false otherwise
 */
bool gitCalculateChecksum(cstring repoPath, cstring *checksum, MemPool *pool, Log *log);

/**
 * Normalize a Git repository URL to canonical form
 * Handles various formats: https://, git://, git@, github:user/repo shorthand, etc.
 *
 * Phase 2/3: Use for consistent repository identification
 *
 * @param repositoryUrl Input repository URL or shorthand
 * @param normalized Output parameter for normalized URL (allocated from pool)
 * @param pool Memory pool for string allocations
 * @param log Logger for error reporting
 * @return true if normalization succeeded, false otherwise
 */
bool gitNormalizeRepositoryUrl(cstring repositoryUrl, cstring *normalized, MemPool *pool, Log *log);

#ifdef __cplusplus
}
#endif
