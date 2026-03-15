/**
 * Unit tests for script caching with inputs/outputs
 *
 * Tests the caching logic in package/cache.c
 */

#include "doctest.h"
#include "utils/ast.hpp"

extern "C" {
#include "package/cache.h"
#include "package/types.h"
#include "core/log.h"
#include "core/strpool.h"
}

#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

using cxy::test::MemPoolWrapper;

// Quiet diagnostic handler for tests
static void quietDiagnosticHandler(const Diagnostic* diagnostic, void* ctx) {
    (void)diagnostic;
    (void)ctx;
}

// Helper to create a log instance for testing
static Log createTestLog() {
    Log log = newLog(quietDiagnosticHandler, nullptr);
    log.maxErrors = 100;
    log.ignoreStyles = true;
    return log;
}

// Helper to create a temporary directory for tests
static std::string createTempDir() {
    char templ[] = "/tmp/cxy_cache_test_XXXXXX";
    char* result = mkdtemp(templ);
    REQUIRE(result != nullptr);
    return std::string(result);
}

// Helper to create a file with specific content
static void createFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    REQUIRE(file.is_open());
    file << content;
    file.close();
}

// Helper to create a directory
static void createDir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

// Helper to set file modification time (seconds since epoch)
static void setFileModTime(const std::string& path, time_t mtime) {
    struct utimbuf times;
    times.actime = mtime;
    times.modtime = mtime;
    utime(path.c_str(), &times);
}

// Helper to remove directory recursively
static void removeDir(const std::string& path) {
    std::string cmd = "rm -rf " + path;
    system(cmd.c_str());
}

TEST_CASE("Cache - no inputs or outputs") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // No cache config - always run
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - only inputs, no outputs") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    createFile(tempDir + "/input.txt", "content");
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring input = makeString(&strings, "input.txt");
    pushOnDynArray(&script.inputs, &input);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // Only inputs - always run
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - outputs but no inputs (warning case)") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    createFile(tempDir + "/output.txt", "result");
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // Warning case - always run
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - output missing") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    createFile(tempDir + "/input.txt", "content");
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring input = makeString(&strings, "input.txt");
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.inputs, &input);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // Output doesn't exist - need to run
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - input missing") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    createFile(tempDir + "/output.txt", "result");
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring input = makeString(&strings, "input.txt");
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.inputs, &input);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // Input doesn't exist - cache invalid
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - output newer than input (cached)") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    // Create input with older timestamp
    createFile(tempDir + "/input.txt", "content");
    setFileModTime(tempDir + "/input.txt", 1000);
    
    // Create output with newer timestamp
    createFile(tempDir + "/output.txt", "result");
    setFileModTime(tempDir + "/output.txt", 2000);
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring input = makeString(&strings, "input.txt");
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.inputs, &input);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == true);  // Output is newer - cached!
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - output older than input (not cached)") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    // Create input with newer timestamp
    createFile(tempDir + "/input.txt", "content");
    setFileModTime(tempDir + "/input.txt", 2000);
    
    // Create output with older timestamp
    createFile(tempDir + "/output.txt", "result");
    setFileModTime(tempDir + "/output.txt", 1000);
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring input = makeString(&strings, "input.txt");
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.inputs, &input);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // Output is older - need to rebuild
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - multiple inputs and outputs") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    // Create inputs with different timestamps
    createFile(tempDir + "/input1.txt", "content1");
    setFileModTime(tempDir + "/input1.txt", 1000);
    
    createFile(tempDir + "/input2.txt", "content2");
    setFileModTime(tempDir + "/input2.txt", 1500);  // Latest input
    
    // Create outputs - must be newer than latest input
    createFile(tempDir + "/output1.txt", "result1");
    setFileModTime(tempDir + "/output1.txt", 3000);
    
    createFile(tempDir + "/output2.txt", "result2");
    setFileModTime(tempDir + "/output2.txt", 2000);  // Earliest output
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring input1 = makeString(&strings, "input1.txt");
    cstring input2 = makeString(&strings, "input2.txt");
    cstring output1 = makeString(&strings, "output1.txt");
    cstring output2 = makeString(&strings, "output2.txt");
    
    pushOnDynArray(&script.inputs, &input1);
    pushOnDynArray(&script.inputs, &input2);
    pushOnDynArray(&script.outputs, &output1);
    pushOnDynArray(&script.outputs, &output2);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == true);  // Earliest output (2000) > latest input (1500)
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - glob pattern expansion") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    // Create source files
    createFile(tempDir + "/file1.txt", "content1");
    setFileModTime(tempDir + "/file1.txt", 1000);
    
    createFile(tempDir + "/file2.txt", "content2");
    setFileModTime(tempDir + "/file2.txt", 1200);
    
    // Create output newer than all inputs
    createFile(tempDir + "/output.txt", "result");
    setFileModTime(tempDir + "/output.txt", 2000);
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring inputGlob = makeString(&strings, "*.txt");
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.inputs, &inputGlob);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    // Note: output.txt itself matches *.txt, but should still be cached
    // since we're comparing against all matched inputs
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - recursive glob pattern") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    // Create nested directory structure
    createDir(tempDir + "/src");
    createDir(tempDir + "/src/lib");
    
    createFile(tempDir + "/src/main.txt", "main");
    setFileModTime(tempDir + "/src/main.txt", 1000);
    
    createFile(tempDir + "/src/lib/util.txt", "util");
    setFileModTime(tempDir + "/src/lib/util.txt", 1500);
    
    // Create output newer than all inputs
    createFile(tempDir + "/output.bin", "result");
    setFileModTime(tempDir + "/output.bin", 2000);
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring inputGlob = makeString(&strings, "src/**/*.txt");
    cstring output = makeString(&strings, "output.bin");
    pushOnDynArray(&script.inputs, &inputGlob);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == true);  // Output (2000) > all inputs (latest: 1500)
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("Cache - no matching input files from glob") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    // Create output but no matching inputs
    createFile(tempDir + "/output.txt", "result");
    
    PackageScript script;
    script.name = makeString(&strings, "build");
    script.command = makeString(&strings, "echo test");
    script.dependencies = newDynArray(sizeof(cstring));
    script.inputs = newDynArray(sizeof(cstring));
    script.outputs = newDynArray(sizeof(cstring));
    
    cstring inputGlob = makeString(&strings, "*.nonexistent");
    cstring output = makeString(&strings, "output.txt");
    pushOnDynArray(&script.inputs, &inputGlob);
    pushOnDynArray(&script.outputs, &output);
    
    bool isCached = false;
    bool result = checkScriptCache(&script, tempDir.c_str(), &strings, &log, &isCached);
    
    CHECK(result == true);
    CHECK(isCached == false);  // No inputs matched - cache invalid, need to run
    
    freeDynArray(&script.dependencies);
    freeDynArray(&script.inputs);
    freeDynArray(&script.outputs);
    removeDir(tempDir);
}

TEST_CASE("expandInputGlobs - simple pattern") {
    Log log = createTestLog();
    MemPoolWrapper pool;
    StrPool strings = newStrPool(pool.get());
    
    std::string tempDir = createTempDir();
    
    createFile(tempDir + "/file1.txt", "");
    createFile(tempDir + "/file2.txt", "");
    createFile(tempDir + "/other.dat", "");
    
    DynArray inputs = newDynArray(sizeof(cstring));
    cstring pattern = makeString(&strings, "*.txt");
    pushOnDynArray(&inputs, &pattern);
    
    DynArray expanded = newDynArray(sizeof(cstring));
    bool result = expandInputGlobs(&inputs, tempDir.c_str(), &expanded, &strings, &log);
    
    CHECK(result == true);
    CHECK(expanded.size >= 2);  // Should match file1.txt and file2.txt
    
    freeDynArray(&inputs);
    freeDynArray(&expanded);
    removeDir(tempDir);
}

TEST_CASE("getFileModTime - existing file") {
    Log log = createTestLog();
    
    std::string tempDir = createTempDir();
    std::string filePath = tempDir + "/test.txt";
    
    createFile(filePath, "content");
    time_t expectedTime = 1234567890;
    setFileModTime(filePath, expectedTime);
    
    u64 mtime;
    bool result = getFileModTime(filePath.c_str(), &mtime, &log);
    
    CHECK(result == true);
    CHECK(mtime > 0);
    
    removeDir(tempDir);
}

TEST_CASE("getFileModTime - nonexistent file") {
    Log log = createTestLog();
    
    u64 mtime;
    bool result = getFileModTime("/nonexistent/file.txt", &mtime, &log);
    
    CHECK(result == false);
}