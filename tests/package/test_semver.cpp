/**
 * Unit tests for semantic version parsing and comparison
 *
 * Tests the semver implementation in package/types.c
 */

#include "doctest.h"
#include <string>

extern "C" {
#include "package/types.h"
#include "core/log.h"
#include "core/strpool.h"
}

// Quiet diagnostic handler for tests - suppresses output
static void quietDiagnosticHandler(const Diagnostic* diagnostic, void* ctx) {
    (void)diagnostic;
    (void)ctx;
    // Silently ignore - we're testing that errors are detected, not that they're printed
}

// Helper to create a log instance for testing
static Log createTestLog() {
    Log log = newLog(nullptr, quietDiagnosticHandler, nullptr);
    log.maxErrors = 100;
    log.ignoreStyles = true;
    return log;
}

TEST_CASE("Semantic version parsing - valid versions") {
    Log log = createTestLog();

    SUBCASE("Simple version") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.2.3", &ver, &log));
        CHECK(ver.major == 1);
        CHECK(ver.minor == 2);
        CHECK(ver.patch == 3);
        CHECK(ver.prerelease == nullptr);
        CHECK(ver.build == nullptr);
    }

    SUBCASE("Version with v prefix") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("v2.0.0", &ver, &log));
        CHECK(ver.major == 2);
        CHECK(ver.minor == 0);
        CHECK(ver.patch == 0);
    }

    SUBCASE("Version with V prefix (uppercase)") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("V1.5.9", &ver, &log));
        CHECK(ver.major == 1);
        CHECK(ver.minor == 5);
        CHECK(ver.patch == 9);
    }

    SUBCASE("Version with prerelease") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.0.0-alpha", &ver, &log));
        CHECK(ver.major == 1);
        CHECK(ver.minor == 0);
        CHECK(ver.patch == 0);
        CHECK(ver.prerelease != nullptr);
        CHECK(std::string(ver.prerelease) == "alpha");
    }

    SUBCASE("Version with prerelease and number") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.0.0-alpha.1", &ver, &log));
        CHECK(ver.major == 1);
        CHECK(ver.minor == 0);
        CHECK(ver.patch == 0);
        CHECK(std::string(ver.prerelease) == "alpha.1");
    }

    SUBCASE("Version with build metadata") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.0.0+20240115", &ver, &log));
        CHECK(ver.major == 1);
        CHECK(ver.minor == 0);
        CHECK(ver.patch == 0);
        CHECK(ver.prerelease == nullptr);
        CHECK(ver.build != nullptr);
        CHECK(std::string(ver.build) == "20240115");
    }

    SUBCASE("Version with prerelease and build") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.0.0-beta.2+exp.sha.5114f85", &ver, &log));
        CHECK(ver.major == 1);
        CHECK(ver.minor == 0);
        CHECK(ver.patch == 0);
        CHECK(std::string(ver.prerelease) == "beta.2+exp.sha.5114f85");
    }

    SUBCASE("Zero version") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("0.0.0", &ver, &log));
        CHECK(ver.major == 0);
        CHECK(ver.minor == 0);
        CHECK(ver.patch == 0);
    }

    SUBCASE("Large version numbers") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("999.888.777", &ver, &log));
        CHECK(ver.major == 999);
        CHECK(ver.minor == 888);
        CHECK(ver.patch == 777);
    }
}

TEST_CASE("Semantic version parsing - invalid versions") {
    Log log = createTestLog();

    SUBCASE("Empty string") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("", &ver, &log));
    }

    SUBCASE("NULL string") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion(nullptr, &ver, &log));
    }

    SUBCASE("Missing minor version") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("1", &ver, &log));
    }

    SUBCASE("Missing patch version") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("1.2", &ver, &log));
    }

    SUBCASE("Non-numeric major") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("a.2.3", &ver, &log));
    }

    SUBCASE("Non-numeric minor") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("1.b.3", &ver, &log));
    }

    SUBCASE("Non-numeric patch") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("1.2.c", &ver, &log));
    }

    SUBCASE("Missing dot separator") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("1-2-3", &ver, &log));
    }

    SUBCASE("Extra dots") {
        SemanticVersion ver;
        REQUIRE_FALSE(parseSemanticVersion("1.2.3.4", &ver, &log));
    }
}

TEST_CASE("Semantic version comparison") {
    SUBCASE("Equal versions") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.2.3", &a, &log));
        REQUIRE(parseSemanticVersion("1.2.3", &b, &log));
        CHECK(compareSemanticVersions(&a, &b) == 0);
    }

    SUBCASE("Major version difference") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("2.0.0", &a, &log));
        REQUIRE(parseSemanticVersion("1.9.9", &b, &log));
        CHECK(compareSemanticVersions(&a, &b) > 0);
        CHECK(compareSemanticVersions(&b, &a) < 0);
    }

    SUBCASE("Minor version difference") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.3.0", &a, &log));
        REQUIRE(parseSemanticVersion("1.2.9", &b, &log));
        CHECK(compareSemanticVersions(&a, &b) > 0);
        CHECK(compareSemanticVersions(&b, &a) < 0);
    }

    SUBCASE("Patch version difference") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.2.4", &a, &log));
        REQUIRE(parseSemanticVersion("1.2.3", &b, &log));
        CHECK(compareSemanticVersions(&a, &b) > 0);
        CHECK(compareSemanticVersions(&b, &a) < 0);
    }

    SUBCASE("Prerelease vs release") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.0.0", &a, &log));
        REQUIRE(parseSemanticVersion("1.0.0-alpha", &b, &log));
        // Release version is greater than prerelease
        CHECK(compareSemanticVersions(&a, &b) > 0);
        CHECK(compareSemanticVersions(&b, &a) < 0);
    }

    SUBCASE("Different prerelease versions") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.0.0-alpha", &a, &log));
        REQUIRE(parseSemanticVersion("1.0.0-beta", &b, &log));
        // Lexicographic comparison: "alpha" < "beta"
        CHECK(compareSemanticVersions(&a, &b) < 0);
        CHECK(compareSemanticVersions(&b, &a) > 0);
    }

    SUBCASE("Same prerelease") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.0.0-alpha.1", &a, &log));
        REQUIRE(parseSemanticVersion("1.0.0-alpha.1", &b, &log));
        CHECK(compareSemanticVersions(&a, &b) == 0);
    }

    SUBCASE("Zero vs non-zero") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("0.0.0", &a, &log));
        REQUIRE(parseSemanticVersion("0.0.1", &b, &log));
        CHECK(compareSemanticVersions(&a, &b) < 0);
    }

    SUBCASE("Build metadata ignored in comparison") {
        SemanticVersion a, b;
        Log log = createTestLog();
        REQUIRE(parseSemanticVersion("1.0.0+build.1", &a, &log));
        REQUIRE(parseSemanticVersion("1.0.0+build.2", &b, &log));
        // Build metadata should not affect comparison
        CHECK(compareSemanticVersions(&a, &b) == 0);
    }
}

TEST_CASE("Semantic version edge cases") {
    Log log = createTestLog();

    SUBCASE("Maximum reasonable version numbers") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("9999.9999.9999", &ver, &log));
        CHECK(ver.major == 9999);
        CHECK(ver.minor == 9999);
        CHECK(ver.patch == 9999);
    }

    SUBCASE("Version with complex prerelease") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.0.0-alpha.beta.1", &ver, &log));
        CHECK(std::string(ver.prerelease) == "alpha.beta.1");
    }

    SUBCASE("Version with complex build") {
        SemanticVersion ver;
        REQUIRE(parseSemanticVersion("1.0.0+20240115.abc123", &ver, &log));
        CHECK(std::string(ver.build) == "20240115.abc123");
    }
}
