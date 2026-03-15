/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/resolver.h"
#include "package/cxyfile.h"
#include "package/gitops.h"
#include "core/log.h"
#include "core/strpool.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <yaml.h>

/**
 * Initialize a resolver context
 */
void initResolverContext(ResolverContext *ctx, MemPool *pool, Log *log)
{
    ctx->resolved = newDynArray(sizeof(ResolvedDependency));
    ctx->conflicts = newDynArray(sizeof(VersionConflict));
    ctx->pool = pool;
    ctx->log = log;
    ctx->allowPrerelease = false;
    ctx->allowDevDeps = true;
}

/**
 * Free resources associated with a resolver context
 */
void freeResolverContext(ResolverContext *ctx)
{
    // Free nested arrays in resolved dependencies
    for (u32 i = 0; i < ctx->resolved.size; i++) {
        ResolvedDependency *dep = &((ResolvedDependency *)ctx->resolved.elems)[i];
        freeDynArray(&dep->dependencies);
    }
    freeDynArray(&ctx->resolved);

    // Free nested arrays in conflicts
    for (u32 i = 0; i < ctx->conflicts.size; i++) {
        VersionConflict *conflict = &((VersionConflict *)ctx->conflicts.elems)[i];
        freeDynArray(&conflict->constraints);
        freeDynArray(&conflict->requestedBy);
    }
    freeDynArray(&ctx->conflicts);
}

/**
 * Check if a dependency is already resolved
 */
bool isDependencyResolved(const ResolverContext *ctx,
                         cstring name,
                         ResolvedDependency **resolved)
{
    for (u32 i = 0; i < ctx->resolved.size; i++) {
        ResolvedDependency *dep = &((ResolvedDependency *)ctx->resolved.elems)[i];
        if (strcmp(dep->name, name) == 0) {
            if (resolved) {
                *resolved = dep;
            }
            return true;
        }
    }
    return false;
}

/**
 * Find the best matching version for a dependency given constraints
 */
bool findBestMatchingVersion(cstring repository,
                            const DynArray *constraints,
                            GitTag *bestTag,
                            MemPool *pool,
                            Log *log)
{
    // Fetch all available tags from repository
    DynArray tags = newDynArray(sizeof(GitTag));
    if (!gitFetchTags(repository, &tags, pool, log)) {
        freeDynArray(&tags);
        return false;
    }

    if (tags.size == 0) {
        logError(log, NULL, "no semantic version tags found in repository '{s}'",
                (FormatArg[]){{.s = repository}});
        freeDynArray(&tags);
        return false;
    }

    // Find the highest version that satisfies all constraints
    GitTag *selected = NULL;
    for (u32 i = tags.size; i > 0; i--) {
        GitTag *tag = &((GitTag *)tags.elems)[i - 1];
        bool satisfiesAll = true;

        // Check against all constraints
        for (u32 j = 0; j < constraints->size; j++) {
            VersionConstraint *constraint = &((VersionConstraint *)constraints->elems)[j];
            
            // vcAny accepts any version
            if (constraint->type == vcAny) {
                continue;
            }

            if (!versionSatisfiesConstraint(&tag->version, constraint)) {
                satisfiesAll = false;
                break;
            }
        }

        if (satisfiesAll) {
            selected = tag;
            break;
        }
    }

    if (!selected) {
        freeDynArray(&tags);
        return false;
    }

    // Copy the selected tag
    *bestTag = *selected;
    freeDynArray(&tags);
    return true;
}

/**
 * Add a version conflict to the resolver context
 */
void addVersionConflict(ResolverContext *ctx,
                       cstring packageName,
                       const VersionConstraint *constraint,
                       cstring requestedBy)
{
    // Check if conflict already exists for this package
    VersionConflict *existing = NULL;
    for (u32 i = 0; i < ctx->conflicts.size; i++) {
        VersionConflict *conflict = &((VersionConflict *)ctx->conflicts.elems)[i];
        if (strcmp(conflict->packageName, packageName) == 0) {
            existing = conflict;
            break;
        }
    }

    if (existing) {
        // Add to existing conflict
        pushOnDynArray(&existing->constraints, constraint);
        pushOnDynArray(&existing->requestedBy, &requestedBy);
    } else {
        // Create new conflict
        VersionConflict conflict;
        conflict.packageName = packageName;
        conflict.constraints = newDynArray(sizeof(VersionConstraint));
        conflict.requestedBy = newDynArray(sizeof(cstring));
        
        pushOnDynArray(&conflict.constraints, constraint);
        pushOnDynArray(&conflict.requestedBy, &requestedBy);
        
        pushOnDynArray(&ctx->conflicts, &conflict);
    }
}

/**
 * Resolve transitive dependencies from a Cxyfile.yaml
 */
static bool resolveTransitiveDependencies(ResolverContext *ctx,
                                         cstring packagePath,
                                         cstring packageName,
                                         ResolvedDependency *parent)
{
    // Load Cxyfile.yaml from the package
    char cxyfilePath[1024];
    snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packagePath);

    PackageMetadata meta;
    StrPool strings = newStrPool(ctx->pool);
    initPackageMetadata(&meta, &strings);

    if (!findAndLoadCxyfile(packagePath, &meta, &strings, ctx->log, NULL)) {
        freePackageMetadata(&meta);
        freeStrPool(&strings);
        return true; // Not an error if package has no dependencies
    }

    // Resolve each dependency
    for (u32 i = 0; i < meta.dependencies.size; i++) {
        PackageDependency *dep = &((PackageDependency *)meta.dependencies.elems)[i];
        
        // Parse version constraint
        VersionConstraint constraint;
        if (!parseVersionConstraint(dep->version, &constraint, ctx->log)) {
            logError(ctx->log, NULL, "invalid version constraint '{s}' for dependency '{s}'",
                    (FormatArg[]){{.s = dep->version}, {.s = dep->name}});
            freePackageMetadata(&meta);
            freeStrPool(&strings);
            return false;
        }

        if (!resolveDependency(ctx, dep, packageName, &constraint)) {
            freePackageMetadata(&meta);
            freeStrPool(&strings);
            return false;
        }
    }

    freePackageMetadata(&meta);
    freeStrPool(&strings);
    return true;
}

/**
 * Resolve a single dependency and its transitive dependencies
 */
bool resolveDependency(ResolverContext *ctx,
                      const PackageDependency *dep,
                      cstring requestedBy,
                      const VersionConstraint *constraint)
{
    // Check if already resolved
    ResolvedDependency *existing = NULL;
    if (isDependencyResolved(ctx, dep->name, &existing)) {
        // Check if existing version satisfies the new constraint
        if (constraint->type != vcAny && 
            !versionSatisfiesConstraint(&existing->version, constraint)) {
            // Version conflict!
            addVersionConflict(ctx, dep->name, constraint, requestedBy);
            return false;
        }
        // Already resolved and compatible
        return true;
    }

    // Handle local path dependencies
    if (dep->path && dep->path[0] != '\0') {
        ResolvedDependency resolved;
        resolved.name = dep->name;
        resolved.repository = NULL;
        resolved.version = (SemanticVersion){0, 0, 0, NULL, NULL};
        resolved.tag = NULL;
        resolved.commit = NULL;
        resolved.checksum = NULL;
        resolved.dependencies = newDynArray(sizeof(ResolvedDependency));
        resolved.isDev = dep->isDev;
        
        pushOnDynArray(&ctx->resolved, &resolved);
        
        // Resolve transitive dependencies from local path
        return resolveTransitiveDependencies(ctx, dep->path, dep->name, &resolved);
    }

    // Remote dependency - need to resolve version
    if (!dep->repository || dep->repository[0] == '\0') {
        logError(ctx->log, NULL, "dependency '{s}' has no repository specified",
                (FormatArg[]){{.s = dep->name}});
        return false;
    }

    // If a specific tag is requested, use it
    if (dep->tag && dep->tag[0] != '\0') {
        // TODO: Fetch the tag and parse its version
        // For now, just accept the tag as-is
        ResolvedDependency resolved;
        resolved.name = dep->name;
        resolved.repository = dep->repository;
        resolved.tag = dep->tag;
        resolved.commit = NULL; // Will be filled during install
        resolved.checksum = NULL; // Will be filled during install
        resolved.dependencies = newDynArray(sizeof(ResolvedDependency));
        resolved.isDev = dep->isDev;
        
        // Parse version from tag if possible
        if (!parseSemanticVersion(dep->tag, &resolved.version, ctx->log)) {
            // Tag doesn't parse as semver, use 0.0.0
            resolved.version = (SemanticVersion){0, 0, 0, NULL, NULL};
        }
        
        pushOnDynArray(&ctx->resolved, &resolved);
        return true;
    }

    // Resolve version using constraint
    DynArray constraints = newDynArray(sizeof(VersionConstraint));
    pushOnDynArray(&constraints, constraint);

    GitTag bestTag;
    if (!findBestMatchingVersion(dep->repository, &constraints, &bestTag, ctx->pool, ctx->log)) {
        logError(ctx->log, NULL, 
                "no version of '{s}' satisfies constraint '{s}' (requested by '{s}')",
                (FormatArg[]){{.s = dep->name}, {.s = constraint->raw}, {.s = requestedBy}});
        freeDynArray(&constraints);
        return false;
    }
    freeDynArray(&constraints);

    // Create resolved dependency
    ResolvedDependency resolved;
    resolved.name = dep->name;
    resolved.repository = dep->repository;
    resolved.version = bestTag.version;
    resolved.tag = bestTag.name;
    resolved.commit = bestTag.commit;
    resolved.checksum = NULL; // Will be filled during install
    resolved.dependencies = newDynArray(sizeof(ResolvedDependency));
    resolved.isDev = dep->isDev;

    pushOnDynArray(&ctx->resolved, &resolved);

    printStatus(ctx->log, "Resolved %s@%u.%u.%u (%s)",
               resolved.name,
               resolved.version.major,
               resolved.version.minor,
               resolved.version.patch,
               resolved.tag);

    return true;
}

/**
 * Resolve all dependencies for a package
 */
bool resolveDependencies(ResolverContext *ctx, const PackageMetadata *meta)
{
    printStatus(ctx->log, "Resolving dependencies...");

    // Resolve regular dependencies
    for (u32 i = 0; i < meta->dependencies.size; i++) {
        PackageDependency *dep = &((PackageDependency *)meta->dependencies.elems)[i];
        
        // Parse version constraint
        VersionConstraint constraint;
        if (!parseVersionConstraint(dep->version, &constraint, ctx->log)) {
            logError(ctx->log, NULL, "invalid version constraint '{s}' for dependency '{s}'",
                    (FormatArg[]){{.s = dep->version}, {.s = dep->name}});
            return false;
        }

        if (!resolveDependency(ctx, dep, meta->name, &constraint)) {
            return false;
        }
    }

    // Resolve dev dependencies if enabled
    if (ctx->allowDevDeps) {
        for (u32 i = 0; i < meta->devDependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)meta->devDependencies.elems)[i];
            
            VersionConstraint constraint;
            if (!parseVersionConstraint(dep->version, &constraint, ctx->log)) {
                logError(ctx->log, NULL, "invalid version constraint '{s}' for dev dependency '{s}'",
                        (FormatArg[]){{.s = dep->version}, {.s = dep->name}});
                return false;
            }

            if (!resolveDependency(ctx, dep, meta->name, &constraint)) {
                return false;
            }
        }
    }

    // Check if there are any conflicts
    if (ctx->conflicts.size > 0) {
        printVersionConflicts(ctx);
        return false;
    }

    printStatus(ctx->log, "Resolved %u dependencies", ctx->resolved.size);
    return true;
}

/**
 * Print version conflicts to the log
 */
void printVersionConflicts(const ResolverContext *ctx)
{
    logError(ctx->log, NULL, "Version conflicts detected:", NULL);
    
    for (u32 i = 0; i < ctx->conflicts.size; i++) {
        VersionConflict *conflict = &((VersionConflict *)ctx->conflicts.elems)[i];
        
        logError(ctx->log, NULL, "\n  Package: {s}", (FormatArg[]){{.s = conflict->packageName}});
        
        for (u32 j = 0; j < conflict->constraints.size; j++) {
            VersionConstraint *constraint = &((VersionConstraint *)conflict->constraints.elems)[j];
            cstring *requester = &((cstring *)conflict->requestedBy.elems)[j];
            
            logError(ctx->log, NULL, "    - {s} requires {s}",
                    (FormatArg[]){{.s = *requester}, {.s = constraint->raw}});
        }
    }
}

#define EMIT_EVENT(emitter, event, ctx) \
    if (!yaml_emitter_emit(emitter, event)) { \
        logError((ctx)->log, NULL, "YAML emitter error", NULL); \
        return false; \
    }

#define EMIT_SCALAR(emitter, value, style, ctx) \
    do { \
        yaml_event_t event; \
        yaml_scalar_event_initialize(&event, NULL, NULL, \
            (yaml_char_t *)(value), strlen(value), 1, 1, style); \
        EMIT_EVENT(emitter, &event, ctx); \
    } while(0)

#define EMIT_SCALAR_KEY(emitter, key, ctx) \
    EMIT_SCALAR(emitter, key, YAML_PLAIN_SCALAR_STYLE, ctx)

#define EMIT_SCALAR_VALUE(emitter, value, ctx) \
    EMIT_SCALAR(emitter, value, YAML_ANY_SCALAR_STYLE, ctx)

/**
 * Helper to emit a resolved dependency to YAML
 */
static bool emitResolvedDependency(yaml_emitter_t *emitter, 
                                   const ResolvedDependency *dep,
                                   ResolverContext *ctx)
{
    yaml_event_t event;
    char versionBuf[64];

    // Start dependency map
    yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
    EMIT_EVENT(emitter, &event, ctx);

    // name
    EMIT_SCALAR_KEY(emitter, "name", ctx);
    EMIT_SCALAR_VALUE(emitter, dep->name, ctx);

    // repository (if present)
    if (dep->repository) {
        EMIT_SCALAR_KEY(emitter, "repository", ctx);
        EMIT_SCALAR_VALUE(emitter, dep->repository, ctx);
    }

    // version
    EMIT_SCALAR_KEY(emitter, "version", ctx);
    snprintf(versionBuf, sizeof(versionBuf), "%u.%u.%u",
             dep->version.major, dep->version.minor, dep->version.patch);
    EMIT_SCALAR_VALUE(emitter, versionBuf, ctx);

    // tag (if present)
    if (dep->tag) {
        EMIT_SCALAR_KEY(emitter, "tag", ctx);
        EMIT_SCALAR_VALUE(emitter, dep->tag, ctx);
    }

    // commit (if present)
    if (dep->commit) {
        EMIT_SCALAR_KEY(emitter, "commit", ctx);
        EMIT_SCALAR_VALUE(emitter, dep->commit, ctx);
    }

    // checksum (if present)
    if (dep->checksum) {
        EMIT_SCALAR_KEY(emitter, "checksum", ctx);
        EMIT_SCALAR_VALUE(emitter, dep->checksum, ctx);
    }

    // End dependency map
    yaml_mapping_end_event_initialize(&event);
    EMIT_EVENT(emitter, &event, ctx);
    
    return true;
}

/**
 * Generate a lock file from resolved dependencies
 */
bool generateLockFile(const ResolverContext *ctx, cstring lockFilePath)
{
    FILE *file = fopen(lockFilePath, "w");
    if (!file) {
        logError(ctx->log, NULL, "failed to create lock file: {s}",
                (FormatArg[]){{.s = lockFilePath}});
        return false;
    }

    yaml_emitter_t emitter;
    yaml_event_t event;

    // Initialize emitter
    if (!yaml_emitter_initialize(&emitter)) {
        logError(ctx->log, NULL, "failed to initialize YAML emitter", NULL);
        fclose(file);
        return false;
    }

    yaml_emitter_set_output_file(&emitter, file);
    yaml_emitter_set_canonical(&emitter, 0);
    yaml_emitter_set_unicode(&emitter, 1);

    // Stream start
    yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
    EMIT_EVENT(&emitter, &event, ctx);

    // Document start
    yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 0);
    EMIT_EVENT(&emitter, &event, ctx);

    // Root mapping start
    yaml_mapping_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);
    EMIT_EVENT(&emitter, &event, ctx);

    // dependencies section
    EMIT_SCALAR_KEY(&emitter, "dependencies", ctx);
    
    // Start dependencies sequence
    yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE);
    EMIT_EVENT(&emitter, &event, ctx);

    // Emit each non-dev dependency
    for (u32 i = 0; i < ctx->resolved.size; i++) {
        ResolvedDependency *dep = &((ResolvedDependency *)ctx->resolved.elems)[i];
        if (!dep->isDev) {
            if (!emitResolvedDependency(&emitter, dep, (ResolverContext *)ctx)) {
                yaml_emitter_delete(&emitter);
                fclose(file);
                return false;
            }
        }
    }

    // End dependencies sequence
    yaml_sequence_end_event_initialize(&event);
    EMIT_EVENT(&emitter, &event, ctx);

    // Check if we have dev dependencies
    bool hasDevDeps = false;
    for (u32 i = 0; i < ctx->resolved.size; i++) {
        ResolvedDependency *dep = &((ResolvedDependency *)ctx->resolved.elems)[i];
        if (dep->isDev) {
            hasDevDeps = true;
            break;
        }
    }

    // devDependencies section (if any)
    if (hasDevDeps) {
        EMIT_SCALAR_KEY(&emitter, "devDependencies", ctx);
        
        // Start devDependencies sequence
        yaml_sequence_start_event_initialize(&event, NULL, NULL, 1, YAML_BLOCK_SEQUENCE_STYLE);
        EMIT_EVENT(&emitter, &event, ctx);

        // Emit each dev dependency
        for (u32 i = 0; i < ctx->resolved.size; i++) {
            ResolvedDependency *dep = &((ResolvedDependency *)ctx->resolved.elems)[i];
            if (dep->isDev) {
                if (!emitResolvedDependency(&emitter, dep, (ResolverContext *)ctx)) {
                    yaml_emitter_delete(&emitter);
                    fclose(file);
                    return false;
                }
            }
        }

        // End devDependencies sequence
        yaml_sequence_end_event_initialize(&event);
        EMIT_EVENT(&emitter, &event, ctx);
    }

    // Root mapping end
    yaml_mapping_end_event_initialize(&event);
    EMIT_EVENT(&emitter, &event, ctx);

    // Document end
    yaml_document_end_event_initialize(&event, 1);
    EMIT_EVENT(&emitter, &event, ctx);

    // Stream end
    yaml_stream_end_event_initialize(&event);
    EMIT_EVENT(&emitter, &event, ctx);

    // Cleanup
    yaml_emitter_delete(&emitter);
    fclose(file);

    printStatus(ctx->log, "Generated lock file: %s", lockFilePath);
    return true;
}

#undef EMIT_EVENT
#undef EMIT_SCALAR
#undef EMIT_SCALAR_KEY
#undef EMIT_SCALAR_VALUE

/**
 * Load and validate a lock file
 */
bool loadLockFile(cstring lockFilePath, ResolverContext *ctx)
{
    FILE *file = fopen(lockFilePath, "r");
    if (!file) {
        // Lock file doesn't exist - not an error, just needs fresh resolution
        return false;
    }

    yaml_parser_t parser;
    yaml_event_t event;

    if (!yaml_parser_initialize(&parser)) {
        logError(ctx->log, NULL, "failed to initialize YAML parser", NULL);
        fclose(file);
        return false;
    }

    yaml_parser_set_input_file(&parser, file);

    bool inDependencies = false;
    bool inDevDependencies = false;
    bool inDependencyMap = false;
    ResolvedDependency currentDep = {0};
    bool success = true;
    cstring currentKey = NULL;

    while (success) {
        if (!yaml_parser_parse(&parser, &event)) {
            logError(ctx->log, NULL, "YAML parse error in lock file", NULL);
            success = false;
            break;
        }

        switch (event.type) {
            case YAML_STREAM_END_EVENT:
                yaml_event_delete(&event);
                goto done;

            case YAML_MAPPING_START_EVENT:
                if (inDependencies || inDevDependencies) {
                    // Starting a dependency map
                    inDependencyMap = true;
                    memset(&currentDep, 0, sizeof(ResolvedDependency));
                    currentDep.dependencies = newDynArray(sizeof(ResolvedDependency));
                    currentDep.isDev = inDevDependencies;
                }
                break;

            case YAML_MAPPING_END_EVENT:
                if (inDependencyMap) {
                    // Finished a dependency, add it to resolved list
                    pushOnDynArray(&ctx->resolved, &currentDep);
                    inDependencyMap = false;
                }
                break;

            case YAML_SEQUENCE_START_EVENT:
                // Starting dependencies or devDependencies sequence
                break;

            case YAML_SEQUENCE_END_EVENT:
                if (inDependencies) {
                    inDependencies = false;
                } else if (inDevDependencies) {
                    inDevDependencies = false;
                }
                break;

            case YAML_SCALAR_EVENT: {
                cstring value = (cstring)event.data.scalar.value;

                if (!inDependencies && !inDevDependencies) {
                    // Top-level key
                    if (strcmp(value, "dependencies") == 0) {
                        inDependencies = true;
                    } else if (strcmp(value, "devDependencies") == 0) {
                        inDevDependencies = true;
                    }
                } else if (inDependencyMap) {
                    if (currentKey == NULL) {
                        // This is a key
                        currentKey = allocFromMemPool(ctx->pool, strlen(value) + 1);
                        strcpy((char*)currentKey, value);
                    } else {
                        // This is a value for the current key
                        if (strcmp(currentKey, "name") == 0) {
                            currentDep.name = allocFromMemPool(ctx->pool, strlen(value) + 1);
                            strcpy((char*)currentDep.name, value);
                        } else if (strcmp(currentKey, "repository") == 0) {
                            currentDep.repository = allocFromMemPool(ctx->pool, strlen(value) + 1);
                            strcpy((char*)currentDep.repository, value);
                        } else if (strcmp(currentKey, "version") == 0) {
                            // Parse semantic version
                            if (!parseSemanticVersion(value, &currentDep.version, ctx->log)) {
                                logWarning(ctx->log, NULL, "invalid version in lock file: {s}", 
                                         (FormatArg[]){{.s = value}});
                            }
                        } else if (strcmp(currentKey, "tag") == 0) {
                            currentDep.tag = allocFromMemPool(ctx->pool, strlen(value) + 1);
                            strcpy((char*)currentDep.tag, value);
                        } else if (strcmp(currentKey, "commit") == 0) {
                            currentDep.commit = allocFromMemPool(ctx->pool, strlen(value) + 1);
                            strcpy((char*)currentDep.commit, value);
                        } else if (strcmp(currentKey, "checksum") == 0) {
                            currentDep.checksum = allocFromMemPool(ctx->pool, strlen(value) + 1);
                            strcpy((char*)currentDep.checksum, value);
                        }
                        currentKey = NULL;
                    }
                }
                break;
            }

            default:
                break;
        }

        yaml_event_delete(&event);
    }

done:
    yaml_parser_delete(&parser);
    fclose(file);

    if (success) {
        printStatus(ctx->log, "Loaded %u dependencies from lock file", ctx->resolved.size);
    }

    return success;
}

/**
 * Verify a single installed package against lock file entry
 */
bool verifyInstalledPackage(const ResolvedDependency *resolved,
                           cstring packagesDir,
                           MemPool *pool,
                           Log *log)
{
    char packagePath[1024];
    snprintf(packagePath, sizeof(packagePath), "%s/%s", packagesDir, resolved->name);

    // Check if package directory exists
    struct stat st;
    if (stat(packagePath, &st) != 0 || !S_ISDIR(st.st_mode)) {
        logError(log, NULL, "package '{s}' not found in {s}",
                (FormatArg[]){{.s = resolved->name}, {.s = packagesDir}});
        return false;
    }

    // Verify Cxyfile.yaml exists
    char cxyfilePath[1024];
    snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", packagePath);
    if (stat(cxyfilePath, &st) != 0 || !S_ISREG(st.st_mode)) {
        logError(log, NULL, "package '{s}' missing Cxyfile.yaml",
                (FormatArg[]){{.s = resolved->name}});
        return false;
    }

    bool verified = true;

    // Verify commit if specified in lock file
    if (resolved->commit && resolved->repository) {
        cstring actualCommit = NULL;
        if (gitGetCurrentCommit(packagePath, &actualCommit, pool, log)) {
            if (strcmp(resolved->commit, actualCommit) != 0) {
                logError(log, NULL, "package '{s}' commit mismatch:\n  Expected: {s}\n  Actual:   {s}",
                        (FormatArg[]){{.s = resolved->name}, 
                                     {.s = resolved->commit}, 
                                     {.s = actualCommit}});
                verified = false;
            }
        } else {
            logWarning(log, NULL, "could not verify commit for package '{s}'",
                      (FormatArg[]){{.s = resolved->name}});
        }
    }

    // Verify checksum if specified in lock file
    if (resolved->checksum) {
        cstring actualChecksum = NULL;
        if (gitCalculateChecksum(packagePath, &actualChecksum, pool, log)) {
            if (strcmp(resolved->checksum, actualChecksum) != 0) {
                logError(log, NULL, "package '{s}' checksum mismatch:\n  Expected: {s}\n  Actual:   {s}",
                        (FormatArg[]){{.s = resolved->name}, 
                                     {.s = resolved->checksum}, 
                                     {.s = actualChecksum}});
                verified = false;
            }
        } else {
            logWarning(log, NULL, "could not verify checksum for package '{s}'",
                      (FormatArg[]){{.s = resolved->name}});
        }
    }

    if (verified) {
        printStatus(log, "✓ Package '{s}' verified (v%u.%u.%u)",
                   resolved->name,
                   resolved->version.major,
                   resolved->version.minor,
                   resolved->version.patch);
    }

    return verified;
}

/**
 * Verify that installed packages match the lock file
 */
bool verifyLockFile(cstring lockFilePath,
                   cstring packagesDir,
                   MemPool *pool,
                   Log *log)
{
    printStatus(log, "Verifying lock file...");

    // Load lock file
    ResolverContext ctx;
    initResolverContext(&ctx, pool, log);

    if (!loadLockFile(lockFilePath, &ctx)) {
        logError(log, NULL, "failed to load lock file: {s}",
                (FormatArg[]){{.s = lockFilePath}});
        freeResolverContext(&ctx);
        return false;
    }

    if (ctx.resolved.size == 0) {
        printStatus(log, "No dependencies to verify");
        freeResolverContext(&ctx);
        return true;
    }

    printStatus(log, "Verifying %u %s...",
               ctx.resolved.size,
               ctx.resolved.size == 1 ? "package" : "packages");

    bool allVerified = true;
    u32 verifiedCount = 0;
    u32 failedCount = 0;

    // Verify each package
    for (u32 i = 0; i < ctx.resolved.size; i++) {
        ResolvedDependency *resolved = &((ResolvedDependency *)ctx.resolved.elems)[i];
        
        if (verifyInstalledPackage(resolved, packagesDir, pool, log)) {
            verifiedCount++;
        } else {
            failedCount++;
            allVerified = false;
        }
    }

    // Print summary
    printStatus(log, "");
    if (allVerified) {
        printStatus(log, cBGRN "✔" cDEF " All %u %s verified successfully",
                   verifiedCount,
                   verifiedCount == 1 ? "package" : "packages");
    } else {
        logError(log, NULL, cBRED "✘" cDEF " %u %s, %u failed verification",
                (FormatArg[]){{.i = verifiedCount},
                             {.s = verifiedCount == 1 ? "succeeded" : "succeeded"},
                             {.i = failedCount}});
    }

    freeResolverContext(&ctx);
    return allVerified;
}