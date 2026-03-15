/**
 * Unit tests for version constraint parsing and satisfaction checking
 *
 * Tests the version constraint implementation in package/types.c
 */

#include "doctest.h"

extern "C" {
#include "package/types.h"
#include "core/log.h"
}

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

TEST_CASE("Version constraint parsing - caret ranges") {
    Log log = createTestLog();

    SUBCASE("Caret with full version") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("^1.2.3", &constraint, &log));
        CHECK(constraint.type == vcCaret);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
        CHECK(constraint.version.patch == 3);
    }

    SUBCASE("Caret with zero major") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("^0.2.3", &constraint, &log));
        CHECK(constraint.type == vcCaret);
        CHECK(constraint.version.major == 0);
        CHECK(constraint.version.minor == 2);
        CHECK(constraint.version.patch == 3);
    }

    SUBCASE("Caret with zero minor") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("^0.0.3", &constraint, &log));
        CHECK(constraint.type == vcCaret);
        CHECK(constraint.version.major == 0);
        CHECK(constraint.version.minor == 0);
        CHECK(constraint.version.patch == 3);
    }
}

TEST_CASE("Version constraint parsing - tilde ranges") {
    Log log = createTestLog();

    SUBCASE("Tilde with full version") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("~1.2.3", &constraint, &log));
        CHECK(constraint.type == vcTilde);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
        CHECK(constraint.version.patch == 3);
    }

    SUBCASE("Tilde with different versions") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("~2.4.7", &constraint, &log));
        CHECK(constraint.type == vcTilde);
        CHECK(constraint.version.major == 2);
        CHECK(constraint.version.minor == 4);
        CHECK(constraint.version.patch == 7);
    }
}

TEST_CASE("Version constraint parsing - comparison operators") {
    Log log = createTestLog();

    SUBCASE("Greater than") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint(">1.2.3", &constraint, &log));
        CHECK(constraint.type == vcGreater);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
        CHECK(constraint.version.patch == 3);
    }

    SUBCASE("Greater than or equal") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint(">=1.2.3", &constraint, &log));
        CHECK(constraint.type == vcGreaterEq);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
        CHECK(constraint.version.patch == 3);
    }

    SUBCASE("Less than") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("<2.0.0", &constraint, &log));
        CHECK(constraint.type == vcLess);
        CHECK(constraint.version.major == 2);
        CHECK(constraint.version.minor == 0);
        CHECK(constraint.version.patch == 0);
    }

    SUBCASE("Less than or equal") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("<=2.0.0", &constraint, &log));
        CHECK(constraint.type == vcLessEq);
        CHECK(constraint.version.major == 2);
        CHECK(constraint.version.minor == 0);
        CHECK(constraint.version.patch == 0);
    }

    SUBCASE("Operators with whitespace") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint(">= 1.2.3", &constraint, &log));
        CHECK(constraint.type == vcGreaterEq);
        CHECK(constraint.version.major == 1);
    }
}

TEST_CASE("Version constraint parsing - wildcards") {
    Log log = createTestLog();

    SUBCASE("Wildcard patch") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("1.2.x", &constraint, &log));
        CHECK(constraint.type == vcWildcard);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
    }

    SUBCASE("Wildcard minor") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("1.x", &constraint, &log));
        CHECK(constraint.type == vcWildcard);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 0);
    }

    SUBCASE("Wildcard with X (uppercase)") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("1.2.X", &constraint, &log));
        CHECK(constraint.type == vcWildcard);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
    }

    SUBCASE("Wildcard with asterisk") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("1.2.*", &constraint, &log));
        CHECK(constraint.type == vcWildcard);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
    }

    SUBCASE("Any version with asterisk") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("*", &constraint, &log));
        CHECK(constraint.type == vcAny);
    }
}

TEST_CASE("Version constraint parsing - exact versions") {
    Log log = createTestLog();

    SUBCASE("Exact version") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("1.2.3", &constraint, &log));
        CHECK(constraint.type == vcExact);
        CHECK(constraint.version.major == 1);
        CHECK(constraint.version.minor == 2);
        CHECK(constraint.version.patch == 3);
    }

    SUBCASE("Exact version with v prefix") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("v2.0.0", &constraint, &log));
        CHECK(constraint.type == vcExact);
        CHECK(constraint.version.major == 2);
    }
}

TEST_CASE("Version constraint parsing - empty and null") {
    Log log = createTestLog();

    SUBCASE("Empty string means any") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint("", &constraint, &log));
        CHECK(constraint.type == vcAny);
    }

    SUBCASE("NULL means any") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint(nullptr, &constraint, &log));
        CHECK(constraint.type == vcAny);
    }
}

TEST_CASE("Version satisfies constraint - exact") {
    Log log = createTestLog();

    SUBCASE("Exact match") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.3", &version, &log));
        REQUIRE(parseVersionConstraint("1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Exact mismatch") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.4", &version, &log));
        REQUIRE(parseVersionConstraint("1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }
}

TEST_CASE("Version satisfies constraint - caret") {
    Log log = createTestLog();

    SUBCASE("Caret allows patch updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.5", &version, &log));
        REQUIRE(parseVersionConstraint("^1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret allows minor updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.5.0", &version, &log));
        REQUIRE(parseVersionConstraint("^1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret blocks major updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("2.0.0", &version, &log));
        REQUIRE(parseVersionConstraint("^1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret blocks lower versions") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.2", &version, &log));
        REQUIRE(parseVersionConstraint("^1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret with 0.x.y allows only patch updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("0.2.5", &version, &log));
        REQUIRE(parseVersionConstraint("^0.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret with 0.x.y blocks minor updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("0.3.0", &version, &log));
        REQUIRE(parseVersionConstraint("^0.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret with 0.0.x allows only exact patch") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("0.0.3", &version, &log));
        REQUIRE(parseVersionConstraint("^0.0.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Caret with 0.0.x blocks different patch") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("0.0.4", &version, &log));
        REQUIRE(parseVersionConstraint("^0.0.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }
}

TEST_CASE("Version satisfies constraint - tilde") {
    Log log = createTestLog();

    SUBCASE("Tilde allows patch updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.5", &version, &log));
        REQUIRE(parseVersionConstraint("~1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Tilde blocks minor updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.3.0", &version, &log));
        REQUIRE(parseVersionConstraint("~1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Tilde blocks major updates") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("2.2.3", &version, &log));
        REQUIRE(parseVersionConstraint("~1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Tilde blocks lower versions") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.2", &version, &log));
        REQUIRE(parseVersionConstraint("~1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }
}

TEST_CASE("Version satisfies constraint - comparison operators") {
    Log log = createTestLog();

    SUBCASE("Greater than - satisfied") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.4", &version, &log));
        REQUIRE(parseVersionConstraint(">1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Greater than - not satisfied") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.3", &version, &log));
        REQUIRE(parseVersionConstraint(">1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Greater than or equal - satisfied exact") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.3", &version, &log));
        REQUIRE(parseVersionConstraint(">=1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Greater than or equal - satisfied greater") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.4", &version, &log));
        REQUIRE(parseVersionConstraint(">=1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Less than - satisfied") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.2", &version, &log));
        REQUIRE(parseVersionConstraint("<1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Less than - not satisfied") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.3", &version, &log));
        REQUIRE(parseVersionConstraint("<1.2.3", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Less than or equal - satisfied exact") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.3", &version, &log));
        REQUIRE(parseVersionConstraint("<=1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Less than or equal - satisfied less") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.2", &version, &log));
        REQUIRE(parseVersionConstraint("<=1.2.3", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }
}

TEST_CASE("Version satisfies constraint - wildcards") {
    Log log = createTestLog();

    SUBCASE("Wildcard patch - satisfied same minor") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.2.5", &version, &log));
        REQUIRE(parseVersionConstraint("1.2.x", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Wildcard patch - not satisfied different minor") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.3.0", &version, &log));
        REQUIRE(parseVersionConstraint("1.2.x", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Wildcard minor - satisfied") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.5.3", &version, &log));
        REQUIRE(parseVersionConstraint("1.x", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Wildcard minor - not satisfied different major") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("2.0.0", &version, &log));
        REQUIRE(parseVersionConstraint("1.x", &constraint, &log));
        CHECK_FALSE(versionSatisfiesConstraint(&version, &constraint));
    }
}

TEST_CASE("Version satisfies constraint - any version") {
    Log log = createTestLog();

    SUBCASE("Any constraint satisfied by any version") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("99.88.77", &version, &log));
        REQUIRE(parseVersionConstraint("*", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Any constraint from empty string") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.0.0", &version, &log));
        REQUIRE(parseVersionConstraint("", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }
}

TEST_CASE("Version constraint edge cases") {
    Log log = createTestLog();

    SUBCASE("Constraint with prerelease version") {
        VersionConstraint constraint;
        REQUIRE(parseVersionConstraint(">=1.0.0-alpha", &constraint, &log));
        CHECK(constraint.type == vcGreaterEq);
        CHECK(constraint.version.prerelease != nullptr);
    }

    SUBCASE("Version with prerelease satisfies exact constraint") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("1.0.0-alpha", &version, &log));
        REQUIRE(parseVersionConstraint("1.0.0-alpha", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }

    SUBCASE("Zero version constraints") {
        SemanticVersion version;
        VersionConstraint constraint;
        REQUIRE(parseSemanticVersion("0.0.1", &version, &log));
        REQUIRE(parseVersionConstraint(">=0.0.0", &constraint, &log));
        CHECK(versionSatisfiesConstraint(&version, &constraint));
    }
}