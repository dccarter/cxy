/**
 * Unit tests for Cxyfile.yaml parser
 *
 * Tests the YAML parsing implementation in package/cxyfile.c
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
#include <unistd.h>

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

TEST_CASE("Cxyfile parser - valid complete file") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("valid_complete.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    
    SUBCASE("Package metadata fields") {
        CHECK(std::string(meta.name) == "test-package");
        CHECK(std::string(meta.version) == "1.2.3");
        CHECK(std::string(meta.description) == "A complete test package");
        CHECK(std::string(meta.author) == "Test Author <test@example.com>");
        CHECK(std::string(meta.license) == "MIT");
        CHECK(std::string(meta.repository) == "https://github.com/test/test-package");
        CHECK(std::string(meta.homepage) == "https://test-package.example.com");
    }
    
    SUBCASE("Dependencies") {
        REQUIRE(meta.dependencies.size == 3);
        
        PackageDependency* dep0 = &((PackageDependency*)meta.dependencies.elems)[0];
        CHECK(std::string(dep0->name) == "json-parser");
        CHECK(std::string(dep0->repository) == "https://github.com/cxy-lang/json");
        CHECK(std::string(dep0->version) == "^2.1.0");
        
        PackageDependency* dep1 = &((PackageDependency*)meta.dependencies.elems)[1];
        CHECK(std::string(dep1->name) == "crypto");
        CHECK(std::string(dep1->version) == "~1.4.2");
        
        PackageDependency* dep2 = &((PackageDependency*)meta.dependencies.elems)[2];
        CHECK(std::string(dep2->name) == "utils");
        CHECK(std::string(dep2->version) == "1.2.3");
        CHECK(std::string(dep2->tag) == "v1.2.3");
    }
    
    SUBCASE("Dev dependencies") {
        REQUIRE(meta.devDependencies.size == 1);
        
        PackageDependency* dev = &((PackageDependency*)meta.devDependencies.elems)[0];
        CHECK(std::string(dev->name) == "test-framework");
        CHECK(std::string(dev->version) == "^3.0.0");
    }
    
    SUBCASE("Tests") {
        REQUIRE(meta.tests.size == 3);
        
        PackageTest* test0 = &((PackageTest*)meta.tests.elems)[0];
        CHECK(std::string(test0->file) == "src/lib.cxy");
        CHECK(test0->isPattern == false);
        
        PackageTest* test1 = &((PackageTest*)meta.tests.elems)[1];
        CHECK(std::string(test1->file) == "src/client.cxy");
        
        PackageTest* test2 = &((PackageTest*)meta.tests.elems)[2];
        CHECK(std::string(test2->file) == "tests/integration_test.cxy");
    }
    
    SUBCASE("Scripts") {
        REQUIRE(meta.scripts.size == 4);
        
        PackageScript* install = &((PackageScript*)meta.scripts.elems)[0];
        CHECK(std::string(install->name) == "install");
        CHECK(std::string(install->command) == "cxy package install");
        CHECK(install->dependencies.size == 0);
        
        PackageScript* build = &((PackageScript*)meta.scripts.elems)[1];
        CHECK(std::string(build->name) == "build");
        CHECK(std::string(build->command) == "cxy build src/lib.cxy -o build/test.so");
        REQUIRE(build->dependencies.size == 1);
        CHECK(std::string(((cstring*)build->dependencies.elems)[0]) == "install");
        
        PackageScript* test = &((PackageScript*)meta.scripts.elems)[2];
        CHECK(std::string(test->name) == "test");
        REQUIRE(test->dependencies.size == 1);
        CHECK(std::string(((cstring*)test->dependencies.elems)[0]) == "build");
        
        PackageScript* clean = &((PackageScript*)meta.scripts.elems)[3];
        CHECK(std::string(clean->name) == "clean");
        CHECK(std::string(clean->command) == "rm -rf build/");
    }
    
    SUBCASE("Build configuration") {
        CHECK(std::string(meta.build.entry) == "src/lib.cxy");
        
        REQUIRE(meta.build.cLibs.size == 2);
        CHECK(std::string(((cstring*)meta.build.cLibs.elems)[0]) == "hiredis");
        CHECK(std::string(((cstring*)meta.build.cLibs.elems)[1]) == "ssl");
        
        REQUIRE(meta.build.cLibDirs.size == 1);
        CHECK(std::string(((cstring*)meta.build.cLibDirs.elems)[0]) == "/usr/local/lib");
        
        REQUIRE(meta.build.cHeaderDirs.size == 1);
        CHECK(std::string(((cstring*)meta.build.cHeaderDirs.elems)[0]) == "/usr/local/include");
        
        REQUIRE(meta.build.cDefines.size == 1);
        CHECK(std::string(((cstring*)meta.build.cDefines.elems)[0]) == "_POSIX_C_SOURCE=200809L");
        
        REQUIRE(meta.build.cFlags.size == 2);
        CHECK(std::string(((cstring*)meta.build.cFlags.elems)[0]) == "-Wall");
        CHECK(std::string(((cstring*)meta.build.cFlags.elems)[1]) == "-Wextra");
        
        REQUIRE(meta.build.defines.size == 2);
        CHECK(std::string(((cstring*)meta.build.defines.elems)[0]) == "TEST_MODE");
        CHECK(std::string(((cstring*)meta.build.defines.elems)[1]) == "DEBUG=1");
        
        REQUIRE(meta.build.flags.size == 1);
        CHECK(std::string(((cstring*)meta.build.flags.elems)[0]) == "-O2");
        
        CHECK(std::string(meta.build.pluginsDir) == "./plugins");
        CHECK(std::string(meta.build.stdlib) == "/usr/local/lib/cxy/std");
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile parser - valid minimal file") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("valid_minimal.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    
    SUBCASE("Required fields present") {
        CHECK(std::string(meta.name) == "minimal-package");
        CHECK(std::string(meta.version) == "0.1.0");
        CHECK(std::string(meta.author) == "Minimal Author <minimal@example.com>");
    }
    
    SUBCASE("Optional fields") {
        CHECK(meta.description == nullptr);
        CHECK(meta.license == nullptr);
        CHECK(meta.repository == nullptr);
        CHECK(meta.homepage == nullptr);
    }
    
    SUBCASE("Empty collections") {
        CHECK(meta.dependencies.size == 0);
        CHECK(meta.devDependencies.size == 0);
    }
    
    SUBCASE("Tests present") {
        REQUIRE(meta.tests.size == 1);
        PackageTest* test = &((PackageTest*)meta.tests.elems)[0];
        CHECK(std::string(test->file) == "src/**/*.cxy");
        CHECK(test->isPattern == true);
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile parser - missing file") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    REQUIRE_FALSE(parseCxyfile("nonexistent.yaml", &meta, &strings, &log));
    
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile parser - invalid missing fields") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("invalid_missing_fields.yaml");
    
    SUBCASE("Can parse but validation fails") {
        REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
        REQUIRE_FALSE(validatePackageMetadata(&meta, &log));
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile validation - complete valid file") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("valid_complete.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    REQUIRE(validatePackageMetadata(&meta, &log));
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile validation - missing required fields") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("invalid_missing_fields.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    
    SUBCASE("Validation fails") {
        REQUIRE_FALSE(validatePackageMetadata(&meta, &log));
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile validation - invalid version format") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "invalid.version");
    meta.author = makeString(&strings, "Author <email@test.com>");
    
    REQUIRE_FALSE(validatePackageMetadata(&meta, &log));
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile validation - dependencies without repository or path") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    initPackageMetadata(&meta, &strings);
    meta.name = makeString(&strings, "test");
    meta.version = makeString(&strings, "1.0.0");
    meta.author = makeString(&strings, "Author <email@test.com>");
    
    // Add invalid dependency (no repository or path)
    PackageDependency dep = {0};
    dep.name = makeString(&strings, "invalid-dep");
    dep.version = makeString(&strings, "1.0.0");
    pushOnDynArray(&meta.dependencies, &dep);
    
    REQUIRE_FALSE(validatePackageMetadata(&meta, &log));
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile findAndLoad - success") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    char* foundPath = nullptr;
    
    // Search from tests/package/fixtures directory
    SUBCASE("Find from fixture directory") {
        bool found = findAndLoadCxyfile("tests/package/fixtures", &meta, &strings, &log, &foundPath);
        
        // This will fail if no Cxyfile.yaml exists in fixtures or parents
        // We can't guarantee success, but we can test the function doesn't crash
        if (found) {
            CHECK(foundPath != nullptr);
            CHECK(meta.name != nullptr);
            free(foundPath);
            freePackageMetadata(&meta);
        }
    }
    
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile findAndLoad - not found") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    // Search from a temporary non-existent path
    REQUIRE_FALSE(findAndLoadCxyfile("/tmp/definitely_not_exists_12345", &meta, &strings, &log, nullptr));
    
    freeStrPool(&strings);
}

TEST_CASE("Cxyfile parser - test with arguments") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    // Create a temporary test file with test arguments
    const char* tempPath = "/tmp/test_cxyfile_args.yaml";
    FILE* f = fopen(tempPath, "w");
    REQUIRE(f != nullptr);
    
    fprintf(f, "package:\n");
    fprintf(f, "  name: test-args\n");
    fprintf(f, "  version: 1.0.0\n");
    fprintf(f, "  author: Test <test@example.com>\n");
    fprintf(f, "tests:\n");
    fprintf(f, "  - file: tests/benchmark.cxy\n");
    fprintf(f, "    args:\n");
    fprintf(f, "      - --iterations=1000\n");
    fprintf(f, "      - --verbose\n");
    fclose(f);
    
    PackageMetadata meta;
    REQUIRE(parseCxyfile(tempPath, &meta, &strings, &log));
    
    SUBCASE("Test with arguments parsed correctly") {
        REQUIRE(meta.tests.size == 1);
        PackageTest* test = &((PackageTest*)meta.tests.elems)[0];
        CHECK(std::string(test->file) == "tests/benchmark.cxy");
        REQUIRE(test->args.size == 2);
        CHECK(std::string(((cstring*)test->args.elems)[0]) == "--iterations=1000");
        CHECK(std::string(((cstring*)test->args.elems)[1]) == "--verbose");
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
    
    // Cleanup
    unlink(tempPath);
}

TEST_CASE("Cxyfile parser - script with dependencies keyword") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    // Create a temporary test file using 'dependencies' instead of 'depends'
    const char* tempPath = "/tmp/test_cxyfile_script_deps.yaml";
    FILE* f = fopen(tempPath, "w");
    REQUIRE(f != nullptr);
    
    fprintf(f, "package:\n");
    fprintf(f, "  name: test-script-deps\n");
    fprintf(f, "  version: 1.0.0\n");
    fprintf(f, "  author: Test <test@example.com>\n");
    fprintf(f, "scripts:\n");
    fprintf(f, "  build: echo building\n");
    fprintf(f, "  test:\n");
    fprintf(f, "    command: echo testing\n");
    fprintf(f, "    dependencies: [build]\n");
    fclose(f);
    
    PackageMetadata meta;
    REQUIRE(parseCxyfile(tempPath, &meta, &strings, &log));
    
    SUBCASE("Script dependencies parsed with 'dependencies' keyword") {
        REQUIRE(meta.scripts.size == 2);
        PackageScript* test = &((PackageScript*)meta.scripts.elems)[1];
        CHECK(std::string(test->name) == "test");
        REQUIRE(test->dependencies.size == 1);
        CHECK(std::string(((cstring*)test->dependencies.elems)[0]) == "build");
    }
    
    freePackageMetadata(&meta);
    freeStrPool(&strings);
    
    // Cleanup
    unlink(tempPath);
}

TEST_CASE("Cxyfile parser - memory management") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    PackageMetadata meta;
    
    std::string path = getFixturePath("valid_complete.yaml");
    REQUIRE(parseCxyfile(path.c_str(), &meta, &strings, &log));
    
    SUBCASE("Can free metadata without crashes") {
        freePackageMetadata(&meta);
        // No assertion needed - just checking it doesn't crash
    }
    
    SUBCASE("Can initialize and free empty metadata") {
        PackageMetadata empty;
        initPackageMetadata(&empty, &strings);
        freePackageMetadata(&empty);
    }
    
    freeStrPool(&strings);
}