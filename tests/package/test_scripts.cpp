/**
 * Unit tests for script dependency resolution and validation
 *
 * Tests the script validation and dependency resolution in package/types.c
 */

#include "doctest.h"
#include "utils/ast.hpp"

extern "C" {
#include "package/cxyfile.h"
#include "package/types.h"
#include "core/log.h"
#include "core/strpool.h"
}

#include <string>

using cxy::test::MemPoolWrapper;

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

// Helper to get fixture path
static std::string getFixturePath(const char* filename) {
    return std::string(TEST_ROOT_DIR "/tests/package/fixtures/") + filename;
}

TEST_CASE("Script validation - valid scripts") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    SUBCASE("Simple script without dependencies") {
        PackageScript script = {0};
        script.name = makeString(&strings, "build");
        script.command = makeString(&strings, "cxy build");
        script.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &script);
        
        REQUIRE(validateScripts(&meta, &log));
    }
    
    SUBCASE("Script with valid dependency") {
        PackageScript build = {0};
        build.name = makeString(&strings, "build");
        build.command = makeString(&strings, "cxy build");
        build.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &build);
        
        PackageScript test = {0};
        test.name = makeString(&strings, "test");
        test.command = makeString(&strings, "cxy test");
        test.dependencies = newDynArray(sizeof(cstring));
        cstring buildName = makeString(&strings, "build");
        pushOnDynArray(&test.dependencies, &buildName);
        pushOnDynArray(&meta.scripts, &test);
        
        REQUIRE(validateScripts(&meta, &log));
    }
    
    SUBCASE("Multiple scripts with chain dependencies") {
        PackageScript install = {0};
        install.name = makeString(&strings, "install");
        install.command = makeString(&strings, "cxy package install");
        install.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &install);
        
        PackageScript build = {0};
        build.name = makeString(&strings, "build");
        build.command = makeString(&strings, "cxy build");
        build.dependencies = newDynArray(sizeof(cstring));
        cstring installName = makeString(&strings, "install");
        pushOnDynArray(&build.dependencies, &installName);
        pushOnDynArray(&meta.scripts, &build);
        
        PackageScript test = {0};
        test.name = makeString(&strings, "test");
        test.command = makeString(&strings, "cxy test");
        test.dependencies = newDynArray(sizeof(cstring));
        cstring buildName = makeString(&strings, "build");
        pushOnDynArray(&test.dependencies, &buildName);
        pushOnDynArray(&meta.scripts, &test);
        
        REQUIRE(validateScripts(&meta, &log));
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script validation - invalid scripts") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    SUBCASE("Script with missing name") {
        PackageScript script = {0};
        script.name = nullptr;
        script.command = makeString(&strings, "cxy build");
        script.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &script);
        
        REQUIRE_FALSE(validateScripts(&meta, &log));
    }
    
    SUBCASE("Script with empty name") {
        PackageScript script = {0};
        script.name = makeString(&strings, "");
        script.command = makeString(&strings, "cxy build");
        script.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &script);
        
        REQUIRE_FALSE(validateScripts(&meta, &log));
    }
    
    SUBCASE("Script with missing command") {
        PackageScript script = {0};
        script.name = makeString(&strings, "build");
        script.command = nullptr;
        script.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &script);
        
        REQUIRE_FALSE(validateScripts(&meta, &log));
    }
    
    SUBCASE("Script with empty command") {
        PackageScript script = {0};
        script.name = makeString(&strings, "build");
        script.command = makeString(&strings, "");
        script.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &script);
        
        REQUIRE_FALSE(validateScripts(&meta, &log));
    }
    
    SUBCASE("Script with undefined dependency") {
        PackageScript script = {0};
        script.name = makeString(&strings, "test");
        script.command = makeString(&strings, "cxy test");
        script.dependencies = newDynArray(sizeof(cstring));
        cstring undefinedDep = makeString(&strings, "nonexistent");
        pushOnDynArray(&script.dependencies, &undefinedDep);
        pushOnDynArray(&meta.scripts, &script);
        
        REQUIRE_FALSE(validateScripts(&meta, &log));
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script validation - circular dependencies") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("circular_scripts.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    
    SUBCASE("Circular dependency detected") {
        REQUIRE_FALSE(validateScripts(&meta, &log));
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - simple cases") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    SUBCASE("Script with no dependencies") {
        PackageScript script = {0};
        script.name = makeString(&strings, "build");
        script.command = makeString(&strings, "cxy build");
        script.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &script);
        
        DynArray executionOrder = newDynArray(sizeof(cstring));
        REQUIRE(resolveScriptDependencies(&meta, makeString(&strings, "build"), &executionOrder, &log));
        
        REQUIRE(executionOrder.size == 1);
        CHECK(std::string(((cstring*)executionOrder.elems)[0]) == "build");
        
        freeDynArray(&executionOrder);
    }
    
    SUBCASE("Script with single dependency") {
        PackageScript install = {0};
        install.name = makeString(&strings, "install");
        install.command = makeString(&strings, "cxy package install");
        install.dependencies = newDynArray(sizeof(cstring));
        pushOnDynArray(&meta.scripts, &install);
        
        PackageScript build = {0};
        build.name = makeString(&strings, "build");
        build.command = makeString(&strings, "cxy build");
        build.dependencies = newDynArray(sizeof(cstring));
        cstring installName = makeString(&strings, "install");
        pushOnDynArray(&build.dependencies, &installName);
        pushOnDynArray(&meta.scripts, &build);
        
        DynArray executionOrder = newDynArray(sizeof(cstring));
        REQUIRE(resolveScriptDependencies(&meta, makeString(&strings, "build"), &executionOrder, &log));
        
        REQUIRE(executionOrder.size == 2);
        CHECK(std::string(((cstring*)executionOrder.elems)[0]) == "install");
        CHECK(std::string(((cstring*)executionOrder.elems)[1]) == "build");
        
        freeDynArray(&executionOrder);
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - chain dependencies") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    // Create chain: install -> build -> test -> deploy
    PackageScript install = {0};
    install.name = makeString(&strings, "install");
    install.command = makeString(&strings, "cxy package install");
    install.dependencies = newDynArray(sizeof(cstring));
    pushOnDynArray(&meta.scripts, &install);
    
    PackageScript build = {0};
    build.name = makeString(&strings, "build");
    build.command = makeString(&strings, "cxy build");
    build.dependencies = newDynArray(sizeof(cstring));
    cstring installName = makeString(&strings, "install");
    pushOnDynArray(&build.dependencies, &installName);
    pushOnDynArray(&meta.scripts, &build);
    
    PackageScript test = {0};
    test.name = makeString(&strings, "test");
    test.command = makeString(&strings, "cxy test");
    test.dependencies = newDynArray(sizeof(cstring));
    cstring buildName = makeString(&strings, "build");
    pushOnDynArray(&test.dependencies, &buildName);
    pushOnDynArray(&meta.scripts, &test);
    
    PackageScript deploy = {0};
    deploy.name = makeString(&strings, "deploy");
    deploy.command = makeString(&strings, "./deploy.sh");
    deploy.dependencies = newDynArray(sizeof(cstring));
    cstring testName = makeString(&strings, "test");
    pushOnDynArray(&deploy.dependencies, &testName);
    pushOnDynArray(&meta.scripts, &deploy);
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    REQUIRE(resolveScriptDependencies(&meta, makeString(&strings, "deploy"), &executionOrder, &log));
    
    REQUIRE(executionOrder.size == 4);
    CHECK(std::string(((cstring*)executionOrder.elems)[0]) == "install");
    CHECK(std::string(((cstring*)executionOrder.elems)[1]) == "build");
    CHECK(std::string(((cstring*)executionOrder.elems)[2]) == "test");
    CHECK(std::string(((cstring*)executionOrder.elems)[3]) == "deploy");
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - multiple dependencies") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    // Create diamond: fmt, lint -> check -> deploy
    PackageScript fmt = {0};
    fmt.name = makeString(&strings, "fmt");
    fmt.command = makeString(&strings, "cxy fmt");
    fmt.dependencies = newDynArray(sizeof(cstring));
    pushOnDynArray(&meta.scripts, &fmt);
    
    PackageScript lint = {0};
    lint.name = makeString(&strings, "lint");
    lint.command = makeString(&strings, "cxy lint");
    lint.dependencies = newDynArray(sizeof(cstring));
    pushOnDynArray(&meta.scripts, &lint);
    
    PackageScript check = {0};
    check.name = makeString(&strings, "check");
    check.command = makeString(&strings, "echo checking");
    check.dependencies = newDynArray(sizeof(cstring));
    cstring fmtName = makeString(&strings, "fmt");
    cstring lintName = makeString(&strings, "lint");
    pushOnDynArray(&check.dependencies, &fmtName);
    pushOnDynArray(&check.dependencies, &lintName);
    pushOnDynArray(&meta.scripts, &check);
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    REQUIRE(resolveScriptDependencies(&meta, makeString(&strings, "check"), &executionOrder, &log));
    
    REQUIRE(executionOrder.size == 3);
    // Should execute fmt and lint before check
    CHECK(std::string(((cstring*)executionOrder.elems)[2]) == "check");
    
    // fmt and lint can be in either order
    bool hasFmt = false, hasLint = false;
    for (size_t i = 0; i < 2; i++) {
        std::string scriptName = ((cstring*)executionOrder.elems)[i];
        if (scriptName == "fmt") hasFmt = true;
        if (scriptName == "lint") hasLint = true;
    }
    CHECK(hasFmt);
    CHECK(hasLint);
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - undefined script") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    PackageScript script = {0};
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "cxy build");
    script.dependencies = newDynArray(sizeof(cstring));
    pushOnDynArray(&meta.scripts, &script);
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    
    SUBCASE("Resolve nonexistent script") {
        REQUIRE_FALSE(resolveScriptDependencies(&meta, makeString(&strings, "nonexistent"), &executionOrder, &log));
    }
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - circular detection") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("circular_scripts.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    
    SUBCASE("Circular dependency in resolution") {
        // Try to resolve script 'a' which has circular dependency
        REQUIRE_FALSE(resolveScriptDependencies(&meta, makeString(&strings, "a"), &executionOrder, &log));
    }
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - complex graph") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    // Create complex dependency graph:
    //   a -> b, c
    //   b -> d
    //   c -> d
    //   d -> e
    // Expected order: e, d, b, c, a (or e, d, c, b, a)
    
    PackageScript e = {0};
    e.name = makeString(&strings, "e");
    e.command = makeString(&strings, "echo e");
    e.dependencies = newDynArray(sizeof(cstring));
    pushOnDynArray(&meta.scripts, &e);
    
    PackageScript d = {0};
    d.name = makeString(&strings, "d");
    d.command = makeString(&strings, "echo d");
    d.dependencies = newDynArray(sizeof(cstring));
    cstring eName = makeString(&strings, "e");
    pushOnDynArray(&d.dependencies, &eName);
    pushOnDynArray(&meta.scripts, &d);
    
    PackageScript b = {0};
    b.name = makeString(&strings, "b");
    b.command = makeString(&strings, "echo b");
    b.dependencies = newDynArray(sizeof(cstring));
    cstring dName1 = makeString(&strings, "d");
    pushOnDynArray(&b.dependencies, &dName1);
    pushOnDynArray(&meta.scripts, &b);
    
    PackageScript c = {0};
    c.name = makeString(&strings, "c");
    c.command = makeString(&strings, "echo c");
    c.dependencies = newDynArray(sizeof(cstring));
    cstring dName2 = makeString(&strings, "d");
    pushOnDynArray(&c.dependencies, &dName2);
    pushOnDynArray(&meta.scripts, &c);
    
    PackageScript a = {0};
    a.name = makeString(&strings, "a");
    a.command = makeString(&strings, "echo a");
    a.dependencies = newDynArray(sizeof(cstring));
    cstring bName = makeString(&strings, "b");
    cstring cName = makeString(&strings, "c");
    pushOnDynArray(&a.dependencies, &bName);
    pushOnDynArray(&a.dependencies, &cName);
    pushOnDynArray(&meta.scripts, &a);
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    REQUIRE(resolveScriptDependencies(&meta, makeString(&strings, "a"), &executionOrder, &log));
    
    REQUIRE(executionOrder.size == 5);
    
    // Check execution order constraints
    CHECK(std::string(((cstring*)executionOrder.elems)[0]) == "e");
    CHECK(std::string(((cstring*)executionOrder.elems)[1]) == "d");
    CHECK(std::string(((cstring*)executionOrder.elems)[4]) == "a");
    
    // b and c should both come after d and before a
    // but their relative order doesn't matter
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script dependency resolution - self dependency") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Test <test@example.com>");
    
    // Create self-referencing script
    PackageScript script = {0};
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "cxy build");
    script.dependencies = newDynArray(sizeof(cstring));
    cstring selfRef = makeString(&strings, "build");
    pushOnDynArray(&script.dependencies, &selfRef);
    pushOnDynArray(&meta.scripts, &script);
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    
    SUBCASE("Self dependency is circular") {
        REQUIRE_FALSE(resolveScriptDependencies(&meta, makeString(&strings, "build"), &executionOrder, &log));
    }
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Script resolution - valid complete file") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("valid_complete.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    REQUIRE(validateScripts(&meta, &log));
    
    DynArray executionOrder = newDynArray(sizeof(cstring));
    
    SUBCASE("Resolve test script") {
        // Find the "test" script name from metadata
        PackageScript* testScript = nullptr;
        for (u32 i = 0; i < meta.scripts.size; i++) {
            PackageScript* script = &((PackageScript*)meta.scripts.elems)[i];
            if (std::string(script->name) == "test") {
                testScript = script;
                break;
            }
        }
        REQUIRE(testScript != nullptr);
        
        REQUIRE(resolveScriptDependencies(&meta, testScript->name, &executionOrder, &log));
        
        // Expected: install, build, test
        REQUIRE(executionOrder.size == 3);
        CHECK(std::string(((cstring*)executionOrder.elems)[0]) == "install");
        CHECK(std::string(((cstring*)executionOrder.elems)[1]) == "build");
        CHECK(std::string(((cstring*)executionOrder.elems)[2]) == "test");
    }
    
    freeDynArray(&executionOrder);
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}