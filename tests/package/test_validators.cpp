/**
 * Unit tests for package validators
 *
 * Tests the validator functions in package/validators.c
 */

#include "doctest.h"
#include <string>

extern "C" {
#include "package/validators.h"
}

TEST_CASE("Package name validation - valid names") {
    SUBCASE("Simple lowercase name") {
        CHECK(validatePackageName("mypackage") == nullptr);
    }

    SUBCASE("Name with hyphen") {
        CHECK(validatePackageName("my-package") == nullptr);
    }

    SUBCASE("Name with underscore") {
        CHECK(validatePackageName("my_package") == nullptr);
    }

    SUBCASE("Name with numbers") {
        CHECK(validatePackageName("package123") == nullptr);
        CHECK(validatePackageName("my-package-2") == nullptr);
    }

    SUBCASE("Single character name") {
        CHECK(validatePackageName("a") == nullptr);
        CHECK(validatePackageName("z") == nullptr);
    }

    SUBCASE("Name with mixed separators") {
        CHECK(validatePackageName("my-package_name") == nullptr);
        CHECK(validatePackageName("package-name-2_test") == nullptr);
    }

    SUBCASE("Long name (at limit)") {
        // Create a 214-character valid name
        std::string longName(214, 'a');
        CHECK(validatePackageName(longName.c_str()) == nullptr);
    }
}

TEST_CASE("Package name validation - invalid names") {
    SUBCASE("Empty string") {
        CHECK(validatePackageName("") != nullptr);
    }

    SUBCASE("NULL string") {
        CHECK(validatePackageName(nullptr) != nullptr);
    }

    SUBCASE("Starts with number") {
        CHECK(validatePackageName("123package") != nullptr);
        CHECK(validatePackageName("2fast") != nullptr);
    }

    SUBCASE("Starts with hyphen") {
        CHECK(validatePackageName("-package") != nullptr);
    }

    SUBCASE("Starts with underscore") {
        CHECK(validatePackageName("_package") != nullptr);
    }

    SUBCASE("Contains uppercase letters") {
        CHECK(validatePackageName("MyPackage") != nullptr);
        CHECK(validatePackageName("myPackage") != nullptr);
        CHECK(validatePackageName("PACKAGE") != nullptr);
    }

    SUBCASE("Contains spaces") {
        CHECK(validatePackageName("my package") != nullptr);
        CHECK(validatePackageName("my  package") != nullptr);
    }

    SUBCASE("Contains special characters") {
        CHECK(validatePackageName("my@package") != nullptr);
        CHECK(validatePackageName("my.package") != nullptr);
        CHECK(validatePackageName("my/package") != nullptr);
        CHECK(validatePackageName("my\\package") != nullptr);
        CHECK(validatePackageName("my$package") != nullptr);
    }

    SUBCASE("Consecutive hyphens") {
        CHECK(validatePackageName("my--package") != nullptr);
        CHECK(validatePackageName("a--b") != nullptr);
    }

    SUBCASE("Consecutive underscores") {
        CHECK(validatePackageName("my__package") != nullptr);
        CHECK(validatePackageName("a__b") != nullptr);
    }

    SUBCASE("Trailing hyphen") {
        CHECK(validatePackageName("package-") != nullptr);
        CHECK(validatePackageName("my-package-") != nullptr);
    }

    SUBCASE("Trailing underscore") {
        CHECK(validatePackageName("package_") != nullptr);
        CHECK(validatePackageName("my_package_") != nullptr);
    }

    SUBCASE("Reserved names") {
        CHECK(validatePackageName(".") != nullptr);
        CHECK(validatePackageName("..") != nullptr);
    }

    SUBCASE("Too long (over 214 characters)") {
        std::string longName(215, 'a');
        CHECK(validatePackageName(longName.c_str()) != nullptr);
    }
}

TEST_CASE("Semantic version validation - valid versions") {
    SUBCASE("Simple version") {
        CHECK(validateSemanticVersion("0.1.0") == nullptr);
        CHECK(validateSemanticVersion("1.0.0") == nullptr);
        CHECK(validateSemanticVersion("1.2.3") == nullptr);
    }

    SUBCASE("Zero version") {
        CHECK(validateSemanticVersion("0.0.0") == nullptr);
    }

    SUBCASE("Large version numbers") {
        CHECK(validateSemanticVersion("999.888.777") == nullptr);
        CHECK(validateSemanticVersion("10.20.30") == nullptr);
    }

    SUBCASE("Version with prerelease") {
        CHECK(validateSemanticVersion("1.0.0-alpha") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-beta") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-rc.1") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-alpha.1") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-0.3.7") == nullptr);
    }

    SUBCASE("Version with build metadata") {
        CHECK(validateSemanticVersion("1.0.0+build") == nullptr);
        CHECK(validateSemanticVersion("1.0.0+20130313144700") == nullptr);
        CHECK(validateSemanticVersion("1.0.0+build.123") == nullptr);
    }

    SUBCASE("Version with prerelease and build") {
        CHECK(validateSemanticVersion("1.0.0-alpha+build") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-beta.2+exp.sha.5114f85") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-alpha.1+001") == nullptr);
    }

    SUBCASE("Complex prerelease identifiers") {
        CHECK(validateSemanticVersion("1.0.0-x.7.z.92") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-alpha.beta.1") == nullptr);
        CHECK(validateSemanticVersion("1.0.0-rc-1") == nullptr);
    }
}

TEST_CASE("Semantic version validation - invalid versions") {
    SUBCASE("Empty string") {
        CHECK(validateSemanticVersion("") != nullptr);
    }

    SUBCASE("NULL string") {
        CHECK(validateSemanticVersion(nullptr) != nullptr);
    }

    SUBCASE("Missing components") {
        CHECK(validateSemanticVersion("1") != nullptr);
        CHECK(validateSemanticVersion("1.0") != nullptr);
        CHECK(validateSemanticVersion("1.") != nullptr);
        CHECK(validateSemanticVersion("1.0.") != nullptr);
    }

    SUBCASE("With 'v' prefix") {
        CHECK(validateSemanticVersion("v1.0.0") != nullptr);
        CHECK(validateSemanticVersion("V1.0.0") != nullptr);
    }

    SUBCASE("Leading zeros") {
        CHECK(validateSemanticVersion("01.0.0") != nullptr);
        CHECK(validateSemanticVersion("1.02.0") != nullptr);
        CHECK(validateSemanticVersion("1.0.03") != nullptr);
        CHECK(validateSemanticVersion("00.0.0") != nullptr);
    }

    SUBCASE("Non-numeric components") {
        CHECK(validateSemanticVersion("a.0.0") != nullptr);
        CHECK(validateSemanticVersion("1.b.0") != nullptr);
        CHECK(validateSemanticVersion("1.0.c") != nullptr);
        CHECK(validateSemanticVersion("x.y.z") != nullptr);
    }

    SUBCASE("Extra components") {
        CHECK(validateSemanticVersion("1.0.0.0") != nullptr);
        CHECK(validateSemanticVersion("1.2.3.4") != nullptr);
    }

    SUBCASE("Empty prerelease") {
        CHECK(validateSemanticVersion("1.0.0-") != nullptr);
    }

    SUBCASE("Empty build metadata") {
        CHECK(validateSemanticVersion("1.0.0+") != nullptr);
    }

    SUBCASE("Invalid prerelease characters") {
        CHECK(validateSemanticVersion("1.0.0-alpha@beta") != nullptr);
        CHECK(validateSemanticVersion("1.0.0-alpha beta") != nullptr);
    }

    SUBCASE("Invalid build metadata characters") {
        CHECK(validateSemanticVersion("1.0.0+build@123") != nullptr);
        CHECK(validateSemanticVersion("1.0.0+build 123") != nullptr);
    }

    SUBCASE("Invalid separators") {
        CHECK(validateSemanticVersion("1-0-0") != nullptr);
        CHECK(validateSemanticVersion("1_0_0") != nullptr);
        CHECK(validateSemanticVersion("1:0:0") != nullptr);
    }
}

TEST_CASE("Git repository validation - valid repositories") {
    SUBCASE("HTTPS URLs") {
        CHECK(validateGitRepository("https://github.com/user/repo.git") == nullptr);
        CHECK(validateGitRepository("https://github.com/user/repo") == nullptr);
        CHECK(validateGitRepository("https://gitlab.com/group/project.git") == nullptr);
        CHECK(validateGitRepository("https://gitlab.example.com/user/repo.git") == nullptr);
    }

    SUBCASE("HTTP URLs") {
        CHECK(validateGitRepository("http://github.com/user/repo.git") == nullptr);
        CHECK(validateGitRepository("http://example.com/repo.git") == nullptr);
    }

    SUBCASE("Git protocol URLs") {
        CHECK(validateGitRepository("git://github.com/user/repo.git") == nullptr);
        CHECK(validateGitRepository("git://example.com/path/to/repo") == nullptr);
    }

    SUBCASE("SSH URLs") {
        CHECK(validateGitRepository("git@github.com:user/repo.git") == nullptr);
        CHECK(validateGitRepository("git@gitlab.com:group/project.git") == nullptr);
        CHECK(validateGitRepository("git@example.com:path/to/repo") == nullptr);
    }

    SUBCASE("SSH protocol URLs") {
        CHECK(validateGitRepository("ssh://git@github.com/user/repo") == nullptr);
        CHECK(validateGitRepository("ssh://user@host.com/repo.git") == nullptr);
    }

    SUBCASE("Short format - GitHub") {
        CHECK(validateGitRepository("github:user/repo") == nullptr);
        CHECK(validateGitRepository("github:organization/project") == nullptr);
    }

    SUBCASE("Short format - GitLab") {
        CHECK(validateGitRepository("gitlab:user/repo") == nullptr);
        CHECK(validateGitRepository("gitlab:group/project") == nullptr);
    }

    SUBCASE("Short format - Bitbucket") {
        CHECK(validateGitRepository("bitbucket:user/repo") == nullptr);
        CHECK(validateGitRepository("bitbucket:team/project") == nullptr);
    }
}

TEST_CASE("Git repository validation - invalid repositories") {
    SUBCASE("Empty string") {
        CHECK(validateGitRepository("") != nullptr);
    }

    SUBCASE("NULL string") {
        CHECK(validateGitRepository(nullptr) != nullptr);
    }

    SUBCASE("Plain text") {
        CHECK(validateGitRepository("not a url") != nullptr);
        CHECK(validateGitRepository("just some text") != nullptr);
    }

    SUBCASE("Invalid HTTPS URLs") {
        CHECK(validateGitRepository("https://") != nullptr);
        CHECK(validateGitRepository("https:///no-host") != nullptr);
        CHECK(validateGitRepository("https://github.com") != nullptr); // no path
        CHECK(validateGitRepository("https://github.com/") != nullptr); // empty path
    }

    SUBCASE("Invalid HTTP URLs") {
        CHECK(validateGitRepository("http://") != nullptr);
        CHECK(validateGitRepository("http://host") != nullptr); // no path
    }

    SUBCASE("Invalid git:// URLs") {
        CHECK(validateGitRepository("git://") != nullptr);
        CHECK(validateGitRepository("git:///no-host") != nullptr);
        CHECK(validateGitRepository("git://host") != nullptr); // no path
    }

    SUBCASE("Invalid SSH URLs") {
        CHECK(validateGitRepository("git@github.com") != nullptr); // missing colon and path
        CHECK(validateGitRepository("git@github.com:") != nullptr); // missing path
        CHECK(validateGitRepository("git@:path") != nullptr); // missing host
    }

    SUBCASE("Invalid ssh:// URLs") {
        CHECK(validateGitRepository("ssh://") != nullptr);
        CHECK(validateGitRepository("ssh://host") != nullptr); // no path
    }

    SUBCASE("Invalid short format") {
        CHECK(validateGitRepository("github:") != nullptr); // missing user/repo
        CHECK(validateGitRepository("github:user") != nullptr); // missing repo
        CHECK(validateGitRepository("github:user/") != nullptr); // empty repo
        CHECK(validateGitRepository("github:/repo") != nullptr); // empty user
        CHECK(validateGitRepository("gitlab:invalid") != nullptr); // no slash
        CHECK(validateGitRepository("unknown:user/repo") != nullptr); // unsupported platform
    }

    SUBCASE("Contains whitespace") {
        CHECK(validateGitRepository("https://github.com/user/repo with spaces") != nullptr);
        CHECK(validateGitRepository("github:user/repo name") != nullptr);
        CHECK(validateGitRepository("git@github.com:user/my repo") != nullptr);
    }

    SUBCASE("Invalid protocols") {
        CHECK(validateGitRepository("ftp://example.com/repo") != nullptr);
        CHECK(validateGitRepository("file:///local/path") != nullptr);
    }
}

TEST_CASE("License identifier validation - valid licenses") {
    SUBCASE("NULL and empty (optional)") {
        CHECK(validateLicenseIdentifier(nullptr) == nullptr);
        CHECK(validateLicenseIdentifier("") == nullptr);
    }

    SUBCASE("Common SPDX identifiers") {
        CHECK(validateLicenseIdentifier("MIT") == nullptr);
        CHECK(validateLicenseIdentifier("Apache-2.0") == nullptr);
        CHECK(validateLicenseIdentifier("GPL-2.0") == nullptr);
        CHECK(validateLicenseIdentifier("GPL-3.0") == nullptr);
        CHECK(validateLicenseIdentifier("LGPL-2.1") == nullptr);
        CHECK(validateLicenseIdentifier("LGPL-3.0") == nullptr);
        CHECK(validateLicenseIdentifier("BSD-2-Clause") == nullptr);
        CHECK(validateLicenseIdentifier("BSD-3-Clause") == nullptr);
        CHECK(validateLicenseIdentifier("ISC") == nullptr);
        CHECK(validateLicenseIdentifier("MPL-2.0") == nullptr);
    }

    SUBCASE("GPL variants") {
        CHECK(validateLicenseIdentifier("GPL-2.0-only") == nullptr);
        CHECK(validateLicenseIdentifier("GPL-2.0-or-later") == nullptr);
        CHECK(validateLicenseIdentifier("GPL-3.0-only") == nullptr);
        CHECK(validateLicenseIdentifier("GPL-3.0-or-later") == nullptr);
    }

    SUBCASE("LGPL variants") {
        CHECK(validateLicenseIdentifier("LGPL-2.1-only") == nullptr);
        CHECK(validateLicenseIdentifier("LGPL-2.1-or-later") == nullptr);
        CHECK(validateLicenseIdentifier("LGPL-3.0-only") == nullptr);
        CHECK(validateLicenseIdentifier("LGPL-3.0-or-later") == nullptr);
    }

    SUBCASE("AGPL variants") {
        CHECK(validateLicenseIdentifier("AGPL-3.0") == nullptr);
        CHECK(validateLicenseIdentifier("AGPL-3.0-only") == nullptr);
        CHECK(validateLicenseIdentifier("AGPL-3.0-or-later") == nullptr);
    }

    SUBCASE("Creative Commons") {
        CHECK(validateLicenseIdentifier("CC0-1.0") == nullptr);
        CHECK(validateLicenseIdentifier("CC-BY-4.0") == nullptr);
        CHECK(validateLicenseIdentifier("CC-BY-SA-4.0") == nullptr);
    }

    SUBCASE("Other common licenses") {
        CHECK(validateLicenseIdentifier("Unlicense") == nullptr);
        CHECK(validateLicenseIdentifier("WTFPL") == nullptr);
        CHECK(validateLicenseIdentifier("Zlib") == nullptr);
        CHECK(validateLicenseIdentifier("Artistic-2.0") == nullptr);
        CHECK(validateLicenseIdentifier("EPL-2.0") == nullptr);
        CHECK(validateLicenseIdentifier("BSL-1.0") == nullptr);
        CHECK(validateLicenseIdentifier("PostgreSQL") == nullptr);
        CHECK(validateLicenseIdentifier("0BSD") == nullptr);
    }

    SUBCASE("Case insensitive") {
        CHECK(validateLicenseIdentifier("mit") == nullptr);
        CHECK(validateLicenseIdentifier("Mit") == nullptr);
        CHECK(validateLicenseIdentifier("apache-2.0") == nullptr);
        CHECK(validateLicenseIdentifier("gpl-3.0") == nullptr);
    }

    SUBCASE("Special keywords") {
        CHECK(validateLicenseIdentifier("UNLICENSED") == nullptr);
        CHECK(validateLicenseIdentifier("unlicensed") == nullptr);
        CHECK(validateLicenseIdentifier("PROPRIETARY") == nullptr);
        CHECK(validateLicenseIdentifier("proprietary") == nullptr);
    }

    SUBCASE("File reference") {
        CHECK(validateLicenseIdentifier("SEE LICENSE IN LICENSE") == nullptr);
        CHECK(validateLicenseIdentifier("SEE LICENSE IN LICENSE.txt") == nullptr);
        CHECK(validateLicenseIdentifier("SEE LICENSE IN LICENSE.md") == nullptr);
        CHECK(validateLicenseIdentifier("see license in LICENSE") == nullptr);
    }
}

TEST_CASE("License identifier validation - invalid licenses") {
    SUBCASE("Unknown identifiers") {
        CHECK(validateLicenseIdentifier("My Custom License") != nullptr);
        CHECK(validateLicenseIdentifier("Unknown-License") != nullptr);
        CHECK(validateLicenseIdentifier("CustomLicense-1.0") != nullptr);
    }

    SUBCASE("Invalid SPDX format") {
        CHECK(validateLicenseIdentifier("MIT License") != nullptr);
        CHECK(validateLicenseIdentifier("Apache License 2.0") != nullptr);
        CHECK(validateLicenseIdentifier("GPL v3") != nullptr);
    }

    SUBCASE("Empty file reference") {
        CHECK(validateLicenseIdentifier("SEE LICENSE IN ") != nullptr);
        CHECK(validateLicenseIdentifier("SEE LICENSE IN") != nullptr);
    }

    SUBCASE("Random text") {
        CHECK(validateLicenseIdentifier("free to use") != nullptr);
        CHECK(validateLicenseIdentifier("public domain") != nullptr);
        CHECK(validateLicenseIdentifier("all rights reserved") != nullptr);
    }
}
