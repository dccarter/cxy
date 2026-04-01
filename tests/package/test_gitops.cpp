/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-03-13
 */

#include "doctest.h"

extern "C" {
#include "package/gitops.h"
#include "core/log.h"
#include "core/mempool.h"
}

/**
 * Quiet diagnostic handler for tests - silently ignores diagnostics
 */
static void quietDiagnosticHandler(const Diagnostic* diagnostic, void* ctx) {
    (void)diagnostic;
    (void)ctx;
    // Silently ignore - we're testing that errors are detected, not that they're printed
}

/**
 * Create a test log with quiet handler
 */
static Log createTestLog() {
    Log log = newLog(nullptr, quietDiagnosticHandler, nullptr);
    log.maxErrors = 100;
    log.ignoreStyles = true;
    return log;
}

/**
 * Test suite for Git operations helper functions
 * 
 * These tests focus on:
 * - URL normalization (shorthand to full URL conversion)
 * - Repository path checking (local directory detection)
 * - Helper functions that don't require network access
 * 
 * Network-dependent functions (clone, fetch tags, etc.) are tested
 * separately or with mocked git commands.
 */

TEST_SUITE("Git Operations") {
    
    TEST_CASE("gitNormalizeRepositoryUrl - GitHub shorthand") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        cstring normalized = nullptr;
        
        SUBCASE("github:user/repo format") {
            bool success = gitNormalizeRepositoryUrl("github:user/my-repo", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "https://github.com/user/my-repo") == 0);
        }
        
        SUBCASE("github:org/project format") {
            bool success = gitNormalizeRepositoryUrl("github:nodejs/node", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "https://github.com/nodejs/node") == 0);
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("gitNormalizeRepositoryUrl - GitLab shorthand") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        cstring normalized = nullptr;
        
        SUBCASE("gitlab:user/repo format") {
            bool success = gitNormalizeRepositoryUrl("gitlab:user/my-project", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "https://gitlab.com/user/my-project") == 0);
        }
        
        SUBCASE("gitlab:group/subgroup/project format") {
            bool success = gitNormalizeRepositoryUrl("gitlab:group/subgroup/project", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "https://gitlab.com/group/subgroup/project") == 0);
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("gitNormalizeRepositoryUrl - Bitbucket shorthand") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        cstring normalized = nullptr;
        
        SUBCASE("bitbucket:user/repo format") {
            bool success = gitNormalizeRepositoryUrl("bitbucket:user/my-repo", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "https://bitbucket.org/user/my-repo") == 0);
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("gitNormalizeRepositoryUrl - full URLs are preserved") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        cstring normalized = nullptr;
        
        SUBCASE("https URL") {
            bool success = gitNormalizeRepositoryUrl("https://github.com/user/repo.git", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "https://github.com/user/repo.git") == 0);
        }
        
        SUBCASE("http URL") {
            bool success = gitNormalizeRepositoryUrl("http://example.com/repo.git", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "http://example.com/repo.git") == 0);
        }
        
        SUBCASE("git:// protocol") {
            bool success = gitNormalizeRepositoryUrl("git://github.com/user/repo.git", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "git://github.com/user/repo.git") == 0);
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("gitNormalizeRepositoryUrl - SSH URLs are preserved") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        cstring normalized = nullptr;
        
        SUBCASE("git@host:path format") {
            bool success = gitNormalizeRepositoryUrl("git@github.com:user/repo.git", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "git@github.com:user/repo.git") == 0);
        }
        
        SUBCASE("ssh://git@host/path format") {
            bool success = gitNormalizeRepositoryUrl("ssh://git@gitlab.com/user/repo.git", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "ssh://git@gitlab.com/user/repo.git") == 0);
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("gitNormalizeRepositoryUrl - edge cases") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        cstring normalized = nullptr;
        
        SUBCASE("empty string returns error") {
            bool success = gitNormalizeRepositoryUrl("", &normalized, &pool, &log);
            CHECK_FALSE(success);
        }
        
        SUBCASE("NULL returns error") {
            bool success = gitNormalizeRepositoryUrl(nullptr, &normalized, &pool, &log);
            CHECK_FALSE(success);
        }
        
        SUBCASE("relative path is preserved as-is") {
            bool success = gitNormalizeRepositoryUrl("../local/repo", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "../local/repo") == 0);
        }
        
        SUBCASE("absolute path is preserved as-is") {
            bool success = gitNormalizeRepositoryUrl("/home/user/repos/my-repo", &normalized, &pool, &log);
            REQUIRE(success);
            CHECK(normalized != nullptr);
            CHECK(strcmp(normalized, "/home/user/repos/my-repo") == 0);
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("gitIsRepository - detects git directories") {
        SUBCASE("non-existent path returns false") {
            bool isRepo = gitIsRepository("/nonexistent/path/to/nowhere");
            CHECK_FALSE(isRepo);
        }
        
        SUBCASE("empty string returns false") {
            bool isRepo = gitIsRepository("");
            CHECK_FALSE(isRepo);
        }
        
        SUBCASE("NULL returns false") {
            bool isRepo = gitIsRepository(nullptr);
            CHECK_FALSE(isRepo);
        }
        
        // Note: Testing actual git repository detection would require
        // creating a temporary git repo, which is better suited for
        // integration tests rather than unit tests
    }
    
    TEST_CASE("MemPool allocation for GitTag structures") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        
        SUBCASE("GitTag fields are allocated from pool") {
            GitTag tag = {0};
            
            // Simulate allocation like gitFetchTags does
            const char *testName = "v1.2.3";
            size_t nameLen = strlen(testName);
            char *nameCopy = (char *)allocFromMemPool(&pool, nameLen + 1);
            memcpy(nameCopy, testName, nameLen + 1);
            tag.name = nameCopy;
            
            const char *testCommit = "abc123def456";
            size_t commitLen = strlen(testCommit);
            char *commitCopy = (char *)allocFromMemPool(&pool, commitLen + 1);
            memcpy(commitCopy, testCommit, commitLen + 1);
            tag.commit = commitCopy;
            
            // Parse semantic version
            SemanticVersion version = {0};
            bool parsed = parseSemanticVersion("1.2.3", &version, &log);
            REQUIRE(parsed);
            tag.version = version;
            
            // Verify allocations
            CHECK(tag.name != nullptr);
            CHECK(strcmp(tag.name, "v1.2.3") == 0);
            CHECK(tag.commit != nullptr);
            CHECK(strcmp(tag.commit, "abc123def456") == 0);
            CHECK(tag.version.major == 1);
            CHECK(tag.version.minor == 2);
            CHECK(tag.version.patch == 3);
            
            // No individual frees needed - pool cleanup handles everything
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
    
    TEST_CASE("MemPool allocation for GitCommit structures") {
        MemPool pool = newMemPool();
        
        SUBCASE("GitCommit fields are allocated from pool") {
            GitCommit commit = {0};
            
            // Simulate allocation like gitGetCommitInfo does
            const char *testHash = "abc123def456789012345678901234567890abcd";
            size_t hashLen = strlen(testHash);
            char *hashCopy = (char *)allocFromMemPool(&pool, hashLen + 1);
            memcpy(hashCopy, testHash, hashLen + 1);
            commit.hash = hashCopy;
            
            const char *testShortHash = "abc123d";
            size_t shortHashLen = strlen(testShortHash);
            char *shortHashCopy = (char *)allocFromMemPool(&pool, shortHashLen + 1);
            memcpy(shortHashCopy, testShortHash, shortHashLen + 1);
            commit.shortHash = shortHashCopy;
            
            const char *testMessage = "Fix bug in parser";
            size_t messageLen = strlen(testMessage);
            char *messageCopy = (char *)allocFromMemPool(&pool, messageLen + 1);
            memcpy(messageCopy, testMessage, messageLen + 1);
            commit.message = messageCopy;
            
            const char *testAuthor = "John Doe <john@example.com>";
            size_t authorLen = strlen(testAuthor);
            char *authorCopy = (char *)allocFromMemPool(&pool, authorLen + 1);
            memcpy(authorCopy, testAuthor, authorLen + 1);
            commit.author = authorCopy;
            
            const char *testDate = "2024-03-13T10:30:00Z";
            size_t dateLen = strlen(testDate);
            char *dateCopy = (char *)allocFromMemPool(&pool, dateLen + 1);
            memcpy(dateCopy, testDate, dateLen + 1);
            commit.date = dateCopy;
            
            // Verify allocations
            CHECK(commit.hash != nullptr);
            CHECK(strcmp(commit.hash, "abc123def456789012345678901234567890abcd") == 0);
            CHECK(commit.shortHash != nullptr);
            CHECK(strcmp(commit.shortHash, "abc123d") == 0);
            CHECK(commit.message != nullptr);
            CHECK(strcmp(commit.message, "Fix bug in parser") == 0);
            CHECK(commit.author != nullptr);
            CHECK(strcmp(commit.author, "John Doe <john@example.com>") == 0);
            CHECK(commit.date != nullptr);
            CHECK(strcmp(commit.date, "2024-03-13T10:30:00Z") == 0);
            
            // No individual frees needed - pool cleanup handles everything
        }
        
        freeMemPool(&pool);
    }
    
    TEST_CASE("Multiple allocations from same pool") {
        MemPool pool = newMemPool();
        Log log = createTestLog();
        
        SUBCASE("Normalize multiple URLs from same pool") {
            cstring url1 = nullptr;
            cstring url2 = nullptr;
            cstring url3 = nullptr;
            
            bool success1 = gitNormalizeRepositoryUrl("github:user/repo1", &url1, &pool, &log);
            bool success2 = gitNormalizeRepositoryUrl("gitlab:user/repo2", &url2, &pool, &log);
            bool success3 = gitNormalizeRepositoryUrl("bitbucket:user/repo3", &url3, &pool, &log);
            
            REQUIRE(success1);
            REQUIRE(success2);
            REQUIRE(success3);
            
            CHECK(strcmp(url1, "https://github.com/user/repo1") == 0);
            CHECK(strcmp(url2, "https://gitlab.com/user/repo2") == 0);
            CHECK(strcmp(url3, "https://bitbucket.org/user/repo3") == 0);
            
            // All allocations are alive and valid
            // No individual frees needed - one pool cleanup handles all
        }
        
        freeMemPool(&pool);
        freeLog(&log);
    }
}