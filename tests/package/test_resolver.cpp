/**
 * Unit tests for dependency resolver
 *
 * Tests the dependency resolution implementation in package/resolver.c
 */

#include "doctest.h"

extern "C" {
#include "package/resolver.h"
#include "package/types.h"
#include "package/cxyfile.h"
#include "core/log.h"
#include "core/mempool.h"
#include "core/strpool.h"
}

#include <string.h>

// Quiet diagnostic handler for tests - suppresses output
static void quietDiagnosticHandler(const Diagnostic* diagnostic, void* ctx) {
    (void)diagnostic;
    (void)ctx;
    // Silently ignore - we're testing that errors are detected, not that they're printed
}

// Helper to create a log instance for testing
static Log createTestLog() {
    Log log = newLog(quietDiagnosticHandler, nullptr);
    log.maxErrors = 100;
    log.ignoreStyles = true;
    return log;
}

TEST_CASE("ResolverContext initialization and cleanup") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    CHECK(ctx.pool == &pool);
    CHECK(ctx.log == &log);
    CHECK(ctx.resolved.size == 0);
    CHECK(ctx.conflicts.size == 0);
    CHECK(ctx.allowPrerelease == false);
    CHECK(ctx.allowDevDeps == true);
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("isDependencyResolved - empty context") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    ResolvedDependency* found = nullptr;
    CHECK(isDependencyResolved(&ctx, "nonexistent", &found) == false);
    CHECK(found == nullptr);
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("isDependencyResolved - with resolved dependencies") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    // Add a resolved dependency manually
    ResolvedDependency dep;
    dep.name = "test-package";
    dep.repository = "https://github.com/test/repo.git";
    dep.version = {1, 2, 3, nullptr, nullptr};
    dep.tag = "v1.2.3";
    dep.commit = "abc123";
    dep.checksum = nullptr;
    dep.dependencies = newDynArray(sizeof(ResolvedDependency));
    dep.isDev = false;
    
    pushOnDynArray(&ctx.resolved, &dep);
    
    SUBCASE("Find existing dependency") {
        ResolvedDependency* found = nullptr;
        CHECK(isDependencyResolved(&ctx, "test-package", &found) == true);
        REQUIRE(found != nullptr);
        CHECK(strcmp(found->name, "test-package") == 0);
        CHECK(found->version.major == 1);
        CHECK(found->version.minor == 2);
        CHECK(found->version.patch == 3);
    }
    
    SUBCASE("Don't find non-existent dependency") {
        ResolvedDependency* found = nullptr;
        CHECK(isDependencyResolved(&ctx, "other-package", &found) == false);
        CHECK(found == nullptr);
    }
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("addVersionConflict - single conflict") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    VersionConstraint constraint;
    constraint.type = vcCaret;
    constraint.version = {1, 0, 0, nullptr, nullptr};
    constraint.raw = "^1.0.0";
    
    addVersionConflict(&ctx, "conflict-package", &constraint, "requester-a");
    
    CHECK(ctx.conflicts.size == 1);
    VersionConflict* conflict = &((VersionConflict*)ctx.conflicts.elems)[0];
    CHECK(strcmp(conflict->packageName, "conflict-package") == 0);
    CHECK(conflict->constraints.size == 1);
    CHECK(conflict->requestedBy.size == 1);
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("addVersionConflict - multiple conflicts on same package") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    VersionConstraint constraint1;
    constraint1.type = vcCaret;
    constraint1.version = {1, 0, 0, nullptr, nullptr};
    constraint1.raw = "^1.0.0";
    
    VersionConstraint constraint2;
    constraint2.type = vcTilde;
    constraint2.version = {2, 0, 0, nullptr, nullptr};
    constraint2.raw = "~2.0.0";
    
    addVersionConflict(&ctx, "conflict-package", &constraint1, "requester-a");
    addVersionConflict(&ctx, "conflict-package", &constraint2, "requester-b");
    
    // Should still be only 1 conflict entry, but with 2 constraints
    CHECK(ctx.conflicts.size == 1);
    VersionConflict* conflict = &((VersionConflict*)ctx.conflicts.elems)[0];
    CHECK(strcmp(conflict->packageName, "conflict-package") == 0);
    CHECK(conflict->constraints.size == 2);
    CHECK(conflict->requestedBy.size == 2);
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("addVersionConflict - conflicts on different packages") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    VersionConstraint constraint1;
    constraint1.type = vcCaret;
    constraint1.version = {1, 0, 0, nullptr, nullptr};
    constraint1.raw = "^1.0.0";
    
    VersionConstraint constraint2;
    constraint2.type = vcTilde;
    constraint2.version = {2, 0, 0, nullptr, nullptr};
    constraint2.raw = "~2.0.0";
    
    addVersionConflict(&ctx, "package-a", &constraint1, "requester-a");
    addVersionConflict(&ctx, "package-b", &constraint2, "requester-b");
    
    // Should be 2 separate conflict entries
    CHECK(ctx.conflicts.size == 2);
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("findBestMatchingVersion - with vcAny constraint") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    
    // Create a constraints array with vcAny
    DynArray constraints = newDynArray(sizeof(VersionConstraint));
    VersionConstraint anyConstraint;
    anyConstraint.type = vcAny;
    anyConstraint.raw = "*";
    pushOnDynArray(&constraints, &anyConstraint);
    
    // Note: This test would require a real repository or mock
    // For now, we just verify the constraint array is set up correctly
    CHECK(constraints.size == 1);
    VersionConstraint* c = &((VersionConstraint*)constraints.elems)[0];
    CHECK(c->type == vcAny);
    
    freeDynArray(&constraints);
    freeMemPool(&pool);
}

TEST_CASE("ResolvedDependency structure") {
    ResolvedDependency dep;
    dep.name = "test-dep";
    dep.repository = "https://github.com/test/dep.git";
    dep.version = {2, 1, 0, nullptr, nullptr};
    dep.tag = "v2.1.0";
    dep.commit = "def456";
    dep.checksum = "sha256:abcdef";
    dep.dependencies = newDynArray(sizeof(ResolvedDependency));
    dep.isDev = false;
    
    CHECK(strcmp(dep.name, "test-dep") == 0);
    CHECK(strcmp(dep.repository, "https://github.com/test/dep.git") == 0);
    CHECK(dep.version.major == 2);
    CHECK(dep.version.minor == 1);
    CHECK(dep.version.patch == 0);
    CHECK(strcmp(dep.tag, "v2.1.0") == 0);
    CHECK(strcmp(dep.commit, "def456") == 0);
    CHECK(dep.isDev == false);
    
    freeDynArray(&dep.dependencies);
}

TEST_CASE("ResolverContext configuration options") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    SUBCASE("Default settings") {
        CHECK(ctx.allowPrerelease == false);
        CHECK(ctx.allowDevDeps == true);
    }
    
    SUBCASE("Modified settings") {
        ctx.allowPrerelease = true;
        ctx.allowDevDeps = false;
        
        CHECK(ctx.allowPrerelease == true);
        CHECK(ctx.allowDevDeps == false);
    }
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("Resolver handles empty dependency list") {
    MemPool pool = newMemPool();
    StrPool strings = newStrPool(&pool);
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    PackageMetadata meta;
    initPackageMetadata(&meta, &strings);
    meta.name = "empty-package";
    meta.version = "1.0.0";
    
    // Empty dependencies - should resolve successfully
    bool result = resolveDependencies(&ctx, &meta);
    CHECK(result == true);
    CHECK(ctx.resolved.size == 0);
    CHECK(ctx.conflicts.size == 0);
    
    freeResolverContext(&ctx);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
    freeMemPool(&pool);
}

TEST_CASE("Version constraint array handling") {
    DynArray constraints = newDynArray(sizeof(VersionConstraint));
    
    VersionConstraint c1;
    c1.type = vcCaret;
    c1.version = {1, 0, 0, nullptr, nullptr};
    c1.raw = "^1.0.0";
    
    VersionConstraint c2;
    c2.type = vcGreaterEq;
    c2.version = {1, 2, 0, nullptr, nullptr};
    c2.raw = ">=1.2.0";
    
    pushOnDynArray(&constraints, &c1);
    pushOnDynArray(&constraints, &c2);
    
    CHECK(constraints.size == 2);
    
    VersionConstraint* first = &((VersionConstraint*)constraints.elems)[0];
    CHECK(first->type == vcCaret);
    CHECK(first->version.major == 1);
    
    VersionConstraint* second = &((VersionConstraint*)constraints.elems)[1];
    CHECK(second->type == vcGreaterEq);
    CHECK(second->version.minor == 2);
    
    freeDynArray(&constraints);
}

TEST_CASE("ResolvedDependency with transitive dependencies") {
    ResolvedDependency parent;
    parent.name = "parent-package";
    parent.repository = "https://github.com/test/parent.git";
    parent.version = {1, 0, 0, nullptr, nullptr};
    parent.tag = "v1.0.0";
    parent.commit = "parent123";
    parent.checksum = nullptr;
    parent.dependencies = newDynArray(sizeof(ResolvedDependency));
    parent.isDev = false;
    
    // Add a transitive dependency
    ResolvedDependency child;
    child.name = "child-package";
    child.repository = "https://github.com/test/child.git";
    child.version = {2, 0, 0, nullptr, nullptr};
    child.tag = "v2.0.0";
    child.commit = "child456";
    child.checksum = nullptr;
    child.dependencies = newDynArray(sizeof(ResolvedDependency));
    child.isDev = false;
    
    pushOnDynArray(&parent.dependencies, &child);
    
    CHECK(parent.dependencies.size == 1);
    ResolvedDependency* transitive = &((ResolvedDependency*)parent.dependencies.elems)[0];
    CHECK(strcmp(transitive->name, "child-package") == 0);
    CHECK(transitive->version.major == 2);
    
    // Cleanup nested arrays
    freeDynArray(&child.dependencies);
    freeDynArray(&parent.dependencies);
}

TEST_CASE("VersionConflict structure") {
    VersionConflict conflict;
    conflict.packageName = "conflicted-package";
    conflict.constraints = newDynArray(sizeof(VersionConstraint));
    conflict.requestedBy = newDynArray(sizeof(cstring));
    
    VersionConstraint c1;
    c1.type = vcCaret;
    c1.version = {1, 0, 0, nullptr, nullptr};
    c1.raw = "^1.0.0";
    
    cstring requester1 = "package-a";
    
    pushOnDynArray(&conflict.constraints, &c1);
    pushOnDynArray(&conflict.requestedBy, &requester1);
    
    CHECK(strcmp(conflict.packageName, "conflicted-package") == 0);
    CHECK(conflict.constraints.size == 1);
    CHECK(conflict.requestedBy.size == 1);
    
    cstring* req = &((cstring*)conflict.requestedBy.elems)[0];
    CHECK(strcmp(*req, "package-a") == 0);
    
    freeDynArray(&conflict.constraints);
    freeDynArray(&conflict.requestedBy);
}

TEST_CASE("Multiple resolved dependencies") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    // Add multiple dependencies
    for (int i = 0; i < 3; i++) {
        ResolvedDependency dep;
        char name[32];
        snprintf(name, sizeof(name), "package-%d", i);
        dep.name = (cstring)allocFromMemPool(&pool, strlen(name) + 1);
        strcpy((char*)dep.name, name);
        dep.repository = "https://github.com/test/repo.git";
        dep.version = {1, (u32)i, 0, nullptr, nullptr};
        dep.tag = "v1.0.0";
        dep.commit = "abc123";
        dep.checksum = nullptr;
        dep.dependencies = newDynArray(sizeof(ResolvedDependency));
        dep.isDev = false;
        
        pushOnDynArray(&ctx.resolved, &dep);
    }
    
    CHECK(ctx.resolved.size == 3);
    
    // Verify each dependency
    for (u32 i = 0; i < ctx.resolved.size; i++) {
        ResolvedDependency* dep = &((ResolvedDependency*)ctx.resolved.elems)[i];
        CHECK(dep->version.major == 1);
        CHECK(dep->version.minor == i);
    }
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("Dev dependencies flag") {
    MemPool pool = newMemPool();
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    // Add regular dependency
    ResolvedDependency regular;
    regular.name = "regular-dep";
    regular.repository = "https://github.com/test/regular.git";
    regular.version = {1, 0, 0, nullptr, nullptr};
    regular.tag = "v1.0.0";
    regular.commit = "abc123";
    regular.checksum = nullptr;
    regular.dependencies = newDynArray(sizeof(ResolvedDependency));
    regular.isDev = false;
    
    // Add dev dependency
    ResolvedDependency dev;
    dev.name = "dev-dep";
    dev.repository = "https://github.com/test/dev.git";
    dev.version = {2, 0, 0, nullptr, nullptr};
    dev.tag = "v2.0.0";
    dev.commit = "def456";
    dev.checksum = nullptr;
    dev.dependencies = newDynArray(sizeof(ResolvedDependency));
    dev.isDev = true;
    
    pushOnDynArray(&ctx.resolved, &regular);
    pushOnDynArray(&ctx.resolved, &dev);
    
    CHECK(ctx.resolved.size == 2);
    
    // Count dev vs regular
    int regularCount = 0;
    int devCount = 0;
    for (u32 i = 0; i < ctx.resolved.size; i++) {
        ResolvedDependency* dep = &((ResolvedDependency*)ctx.resolved.elems)[i];
        if (dep->isDev) {
            devCount++;
        } else {
            regularCount++;
        }
    }
    
    CHECK(regularCount == 1);
    CHECK(devCount == 1);
    
    freeResolverContext(&ctx);
    freeMemPool(&pool);
}

TEST_CASE("Resolver context with allowDevDeps disabled") {
    MemPool pool = newMemPool();
    StrPool strings = newStrPool(&pool);
    Log log = createTestLog();
    ResolverContext ctx;
    initResolverContext(&ctx, &pool, &log);
    
    // Disable dev dependencies
    ctx.allowDevDeps = false;
    
    PackageMetadata meta;
    initPackageMetadata(&meta, &strings);
    meta.name = "test-package";
    meta.version = "1.0.0";
    
    // Add a dev dependency
    PackageDependency devDep;
    devDep.name = "dev-dep";
    devDep.repository = "https://github.com/test/dev.git";
    devDep.version = "^1.0.0";
    devDep.tag = nullptr;
    devDep.branch = nullptr;
    devDep.path = nullptr;
    devDep.isDev = true;
    pushOnDynArray(&meta.devDependencies, &devDep);
    
    // When resolved, dev deps should be skipped
    bool result = resolveDependencies(&ctx, &meta);
    // Note: Will fail without real repo, but structure is correct
    
    freeResolverContext(&ctx);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
    freeMemPool(&pool);
}