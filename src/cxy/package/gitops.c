/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/gitops.h"
#include "package/types.h"
#include "core/log.h"
#include "core/format.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

// Comparison function for qsort to sort GitTag by semantic version
static int compareGitTagsByVersion(const void *a, const void *b)
{
    const GitTag *tagA = (const GitTag *)a;
    const GitTag *tagB = (const GitTag *)b;
    return compareSemanticVersions(&tagA->version, &tagB->version);
}

// Helper to execute git commands and capture output
static bool executeGitCommand(const char *command, FormatState *output, Log *log)
{
    FILE *pipe = popen(command, "r");
    if (!pipe) {
        logError(log, NULL, "failed to execute git command: {s}",
                (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    if (output) {
        char buffer[4096];
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            appendString(output, buffer);
        }
    }

    int status = pclose(pipe);
    if (status != 0) {
        return false;
    }

    return true;
}

static bool _executeGitCommandQuiet(const char *command, Log *log, cstring header);

// Helper to execute git commands with no output (just check success)
// Uses the runCommandWithProgress helper to show a spinner and live output
// while the git command runs. The header shown to the user is a short
// excerpt of the command.
static bool executeGitCommandQuiet(const char *command, Log *log)
{
    return _executeGitCommandQuiet(command, log, NULL);
}

static bool _executeGitCommandQuiet(const char *command, Log *log, cstring msg)
{
    if (!command) {
        return false;
    }

    /* Build a header of the form:
     *   Running command '<command>'
     * The visible command portion is printed in cyan and italic.
     * We truncate the visible command if necessary and reserve room
     * in the buffer for the ANSI sequences and quoting.
     */
    char header[64];
    const char *prefix = msg ?: "Running command ";
    size_t prefixLen = strlen(prefix);
    /* Reserve extra room for quotes, ellipsis and ANSI sequences (approx 20 bytes). */
    size_t maxCmdLen = sizeof(header) - prefixLen - 1 - 20; /* room for NUL and escapes */

    if (strlen(command) <= maxCmdLen && maxCmdLen > 0) {
        /* fits entirely */
        /* Format: Running command '<cyan><italic><cmd><reset>' */
        snprintf(header, sizeof(header), "%s'%s\x1B[3m%s%s'", prefix, cCYN, command, cDEF);
    } else if (maxCmdLen > 3) {
        /* truncate and append ellipsis */
        size_t copyLen = maxCmdLen - 3;
        snprintf(header, sizeof(header), "%s'%s\x1B[3m%.*s...%s'", prefix, cCYN, (int)copyLen, command, cDEF);
    } else {
        /* fallback: just show prefix if there's no room for the command text */
        snprintf(header, sizeof(header), "%s", prefix);
    }

    /* Delegate to the spinner-aware runner which returns true on success. */
    return runCommandWithProgress(header, command, log);
}

bool gitIsRepositoryAccessible(cstring repositoryUrl, Log *log)
{
    if (!repositoryUrl || repositoryUrl[0] == '\0') {
        logError(log, NULL, "repository URL cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "git ls-remote --quiet --exit-code \"{s}\" HEAD >/dev/null 2>&1",
           (FormatArg[]){{.s = repositoryUrl}});

    char *cmdStr = formatStateToString(&cmd);
    bool accessible = executeGitCommandQuiet(cmdStr, log);

    free(cmdStr);
    freeFormatState(&cmd);

    if (!accessible) {
        logError(log, NULL, "repository '{s}' is not accessible",
                (FormatArg[]){{.s = repositoryUrl}});
    }

    return accessible;
}

bool gitFetchTags(cstring repositoryUrl, DynArray *tags, MemPool *pool, Log *log)
{
    if (!repositoryUrl || repositoryUrl[0] == '\0') {
        logError(log, NULL, "repository URL cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "git ls-remote --tags --refs \"{s}\" 2>/dev/null",
           (FormatArg[]){{.s = repositoryUrl}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to fetch tags from repository '{s}'",
                (FormatArg[]){{.s = repositoryUrl}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // Parse output: each line is "commit_hash\trefs/tags/tag_name"
    char *line = strtok(outputStr, "\n");
    while (line) {
        char *tab = strchr(line, '\t');
        if (tab) {
            *tab = '\0';
            char *commit = line;
            char *ref = tab + 1;

            // Extract tag name from refs/tags/name
            if (strncmp(ref, "refs/tags/", 10) == 0) {
                char *tagName = ref + 10;

                // Determine version string to parse (strip 'v' or 'V' prefix if present)
                const char *versionStr = tagName;
                if (tagName[0] == 'v' || tagName[0] == 'V') {
                    versionStr = tagName + 1;
                }

                // Try to parse as semantic version (pass NULL log to silently skip non-semver tags)
                SemanticVersion version = {0};
                if (parseSemanticVersion(versionStr, &version, NULL)) {
                    // Successfully parsed - add to array
                    GitTag tag = {0};

                    // Allocate tag name from pool
                    size_t nameLen = strlen(tagName);
                    char *nameCopy = allocFromMemPool(pool, nameLen + 1);
                    memcpy(nameCopy, tagName, nameLen + 1);
                    tag.name = nameCopy;

                    // Allocate commit hash from pool
                    size_t commitLen = strlen(commit);
                    char *commitCopy = allocFromMemPool(pool, commitLen + 1);
                    memcpy(commitCopy, commit, commitLen + 1);
                    tag.commit = commitCopy;

                    // Store parsed semantic version
                    tag.version = version;

                    pushOnDynArray(tags, &tag);
                }
                // If parsing fails, silently skip this tag (it's not a valid semver tag)
            }
        }
        line = strtok(NULL, "\n");
    }

    free(outputStr);

    // Sort tags by semantic version in ascending order
    if (tags->size > 1) {
        qsort(tags->elems, tags->size, tags->elemSize, compareGitTagsByVersion);
    }

    return true;
}

bool gitGetLatestTag(cstring repositoryUrl, cstring pattern, GitTag *tag, MemPool *pool, Log *log)
{
    DynArray tags = newDynArray(sizeof(GitTag));

    if (!gitFetchTags(repositoryUrl, &tags, pool, log)) {
        freeDynArray(&tags);
        return false;
    }

    if (tags.size == 0) {
        logError(log, NULL, "no tags found in repository", NULL);
        freeDynArray(&tags);
        return false;
    }

    // gitFetchTags returns sorted array in ascending order, so last element is latest
    GitTag *latestTag = &((GitTag *)tags.elems)[tags.size - 1];

    // Copy to output (already allocated from pool, so just assign)
    tag->name = latestTag->name;
    tag->commit = latestTag->commit;
    tag->version = latestTag->version;

    freeDynArray(&tags);
    return true;

    // TODO: Implement pattern matching for version constraints (e.g., "^1.2.0", "~1.0.0")
    // when pattern parameter is provided
}

bool gitClone(cstring repositoryUrl, cstring destination, bool shallow, Log *log)
{
    if (!repositoryUrl || repositoryUrl[0] == '\0') {
        logError(log, NULL, "repository URL cannot be empty", NULL);
        return false;
    }

    if (!destination || destination[0] == '\0') {
        logError(log, NULL, "destination path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    if (shallow) {
        format(&cmd, "git clone --depth 1 \"{s}\" \"{s}\" 2>&1",
               (FormatArg[]){{.s = repositoryUrl}, {.s = destination}});
    } else {
        format(&cmd, "git clone \"{s}\" \"{s}\" 2>&1",
               (FormatArg[]){{.s = repositoryUrl}, {.s = destination}});
    }

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    bool success = executeGitCommandQuiet(cmdStr, log);
    free(cmdStr);

    if (!success) {
        logError(log, NULL, "failed to clone repository '{s}'",
                (FormatArg[]){{.s = repositoryUrl}});
    }

    return success;
}

bool gitCloneBranch(cstring repositoryUrl, cstring branch, cstring destination, bool shallow, Log *log)
{
    if (!repositoryUrl || repositoryUrl[0] == '\0') {
        logError(log, NULL, "repository URL cannot be empty", NULL);
        return false;
    }

    if (!branch || branch[0] == '\0') {
        logError(log, NULL, "branch name cannot be empty", NULL);
        return false;
    }

    if (!destination || destination[0] == '\0') {
        logError(log, NULL, "destination path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    if (shallow) {
        format(&cmd, "git clone --depth 1 --branch \"{s}\" \"{s}\" \"{s}\" 2>&1",
               (FormatArg[]){{.s = branch}, {.s = repositoryUrl}, {.s = destination}});
    } else {
        format(&cmd, "git clone --branch \"{s}\" \"{s}\" \"{s}\" 2>&1",
               (FormatArg[]){{.s = branch}, {.s = repositoryUrl}, {.s = destination}});
    }

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    bool success = executeGitCommandQuiet(cmdStr, log);
    free(cmdStr);

    if (!success) {
        logError(log, NULL, "failed to clone branch '{s}' from repository '{s}'",
                (FormatArg[]){{.s = branch}, {.s = repositoryUrl}});
    }

    return success;
}

bool gitCloneTag(cstring repositoryUrl, cstring tagName, cstring destination, Log *log)
{
    if (!repositoryUrl || repositoryUrl[0] == '\0') {
        logError(log, NULL, "repository URL cannot be empty", NULL);
        return false;
    }

    if (!tagName || tagName[0] == '\0') {
        logError(log, NULL, "tag name cannot be empty", NULL);
        return false;
    }

    if (!destination || destination[0] == '\0') {
        logError(log, NULL, "destination path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "git clone --depth 1 --branch \"{s}\" \"{s}\" \"{s}\" 2>&1",
           (FormatArg[]){{.s = tagName}, {.s = repositoryUrl}, {.s = destination}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    bool success = executeGitCommandQuiet(cmdStr, log);
    free(cmdStr);

    if (!success) {
        logError(log, NULL, "failed to clone tag '{s}' from repository '{s}'",
                (FormatArg[]){{.s = tagName}, {.s = repositoryUrl}});
    }

    return success;
}

bool gitCheckoutCommit(cstring repoPath, cstring commitHash, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    if (!commitHash || commitHash[0] == '\0') {
        logError(log, NULL, "commit hash cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git checkout \"{s}\" 2>&1",
           (FormatArg[]){{.s = repoPath}, {.s = commitHash}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    bool success = executeGitCommandQuiet(cmdStr, log);
    free(cmdStr);

    if (!success) {
        logError(log, NULL, "failed to checkout commit '{s}' in repository '{s}'",
                (FormatArg[]){{.s = commitHash}, {.s = repoPath}});
    }

    return success;
}

bool gitGetCurrentCommit(cstring repoPath, cstring *commitHash, MemPool *pool, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git rev-parse HEAD 2>/dev/null",
           (FormatArg[]){{.s = repoPath}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to get current commit in repository '{s}'",
                (FormatArg[]){{.s = repoPath}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // Trim newline
    size_t len = strlen(outputStr);
    while (len > 0 && (outputStr[len - 1] == '\n' || outputStr[len - 1] == '\r')) {
        outputStr[--len] = '\0';
    }

    // Allocate from pool
    char *result = allocFromMemPool(pool, len + 1);
    memcpy(result, outputStr, len + 1);
    *commitHash = result;

    free(outputStr);
    return true;
}

bool gitGetCurrentBranch(cstring repoPath, cstring *branchName, MemPool *pool, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git rev-parse --abbrev-ref HEAD 2>/dev/null",
           (FormatArg[]){{.s = repoPath}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to get current branch in repository '{s}'",
                (FormatArg[]){{.s = repoPath}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // Trim newline
    size_t len = strlen(outputStr);
    while (len > 0 && (outputStr[len - 1] == '\n' || outputStr[len - 1] == '\r')) {
        outputStr[--len] = '\0';
    }

    // Allocate from pool
    char *result = allocFromMemPool(pool, len + 1);
    memcpy(result, outputStr, len + 1);
    *branchName = result;

    free(outputStr);
    return true;
}

bool gitPull(cstring repoPath, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git pull 2>&1",
           (FormatArg[]){{.s = repoPath}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    bool success = executeGitCommandQuiet(cmdStr, log);
    free(cmdStr);

    if (!success) {
        logError(log, NULL, "failed to pull in repository '{s}'",
                (FormatArg[]){{.s = repoPath}});
    }

    return success;
}

bool gitIsRepository(cstring path)
{
    if (!path || path[0] == '\0') {
        return false;
    }

    FormatState gitPath = newFormatState(NULL, true);
    format(&gitPath, "{s}/.git", (FormatArg[]){{.s = path}});
    char *gitPathStr = formatStateToString(&gitPath);
    freeFormatState(&gitPath);

    struct stat st;
    bool isRepo = (stat(gitPathStr, &st) == 0 && S_ISDIR(st.st_mode));

    free(gitPathStr);
    return isRepo;
}

bool gitGetCommitInfo(cstring repoPath, cstring commitHash, GitCommit *commit, MemPool *pool, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    const char *ref = commitHash ? commitHash : "HEAD";

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git show --no-patch --format=\"%%H%%n%%h%%n%%s%%n%%an <%%ae>%%n%%cI\" \"{s}\" 2>/dev/null",
           (FormatArg[]){{.s = repoPath}, {.s = ref}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to get commit info in repository '{s}'",
                (FormatArg[]){{.s = repoPath}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // Parse output: hash, shortHash, message, author, date (one per line)
    char *lines[5] = {NULL};
    int lineCount = 0;
    char *line = strtok(outputStr, "\n");
    while (line && lineCount < 5) {
        lines[lineCount++] = line;
        line = strtok(NULL, "\n");
    }

    if (lineCount < 5) {
        free(outputStr);
        logError(log, NULL, "invalid git show output format", NULL);
        return false;
    }

    // Allocate from pool
    size_t hashLen = strlen(lines[0]);
    char *hashCopy = allocFromMemPool(pool, hashLen + 1);
    memcpy(hashCopy, lines[0], hashLen + 1);
    commit->hash = hashCopy;

    size_t shortHashLen = strlen(lines[1]);
    char *shortHashCopy = allocFromMemPool(pool, shortHashLen + 1);
    memcpy(shortHashCopy, lines[1], shortHashLen + 1);
    commit->shortHash = shortHashCopy;

    size_t messageLen = strlen(lines[2]);
    char *messageCopy = allocFromMemPool(pool, messageLen + 1);
    memcpy(messageCopy, lines[2], messageLen + 1);
    commit->message = messageCopy;

    size_t authorLen = strlen(lines[3]);
    char *authorCopy = allocFromMemPool(pool, authorLen + 1);
    memcpy(authorCopy, lines[3], authorLen + 1);
    commit->author = authorCopy;

    size_t dateLen = strlen(lines[4]);
    char *dateCopy = allocFromMemPool(pool, dateLen + 1);
    memcpy(dateCopy, lines[4], dateLen + 1);
    commit->date = dateCopy;

    free(outputStr);
    return true;
}

bool gitGetRemoteUrl(cstring repoPath, cstring remoteName, cstring *url, MemPool *pool, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    const char *remote = remoteName ? remoteName : "origin";

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git remote get-url \"{s}\" 2>/dev/null",
           (FormatArg[]){{.s = repoPath}, {.s = remote}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to get remote URL for '{s}' in repository '{s}'",
                (FormatArg[]){{.s = remote}, {.s = repoPath}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // Trim newline
    size_t len = strlen(outputStr);
    while (len > 0 && (outputStr[len - 1] == '\n' || outputStr[len - 1] == '\r')) {
        outputStr[--len] = '\0';
    }

    // Allocate from pool
    char *result = allocFromMemPool(pool, len + 1);
    memcpy(result, outputStr, len + 1);
    *url = result;

    free(outputStr);
    return true;
}

bool gitCreateTag(cstring repoPath, cstring tagName, cstring message, bool push, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    if (!tagName || tagName[0] == '\0') {
        logError(log, NULL, "tag name cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    if (message && message[0] != '\0') {
        format(&cmd, "cd \"{s}\" && git tag -a \"{s}\" -m \"{s}\" 2>&1",
               (FormatArg[]){{.s = repoPath}, {.s = tagName}, {.s = message}});
    } else {
        format(&cmd, "cd \"{s}\" && git tag \"{s}\" 2>&1",
               (FormatArg[]){{.s = repoPath}, {.s = tagName}});
    }

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    bool success = executeGitCommandQuiet(cmdStr, log);
    free(cmdStr);

    if (!success) {
        logError(log, NULL, "failed to create tag '{s}' in repository '{s}'",
                (FormatArg[]){{.s = tagName}, {.s = repoPath}});
        return false;
    }

    if (push) {
        FormatState pushCmd = newFormatState(NULL, true);
        format(&pushCmd, "cd \"{s}\" && git push origin \"{s}\" 2>&1",
               (FormatArg[]){{.s = repoPath}, {.s = tagName}});

        char *pushCmdStr = formatStateToString(&pushCmd);
        freeFormatState(&pushCmd);

        success = executeGitCommandQuiet(pushCmdStr, log);
        free(pushCmdStr);

        if (!success) {
            logError(log, NULL, "failed to push tag '{s}' in repository '{s}'",
                    (FormatArg[]){{.s = tagName}, {.s = repoPath}});
            return false;
        }
    }

    return true;
}

bool gitHasUncommittedChanges(cstring repoPath, bool *hasChanges, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git status --porcelain 2>/dev/null",
           (FormatArg[]){{.s = repoPath}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to check status in repository '{s}'",
                (FormatArg[]){{.s = repoPath}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // If output is empty, no uncommitted changes
    *hasChanges = (outputStr[0] != '\0');

    free(outputStr);
    return true;
}

bool gitCalculateChecksum(cstring repoPath, cstring *checksum, MemPool *pool, Log *log)
{
    if (!repoPath || repoPath[0] == '\0') {
        logError(log, NULL, "repository path cannot be empty", NULL);
        return false;
    }

    FormatState cmd = newFormatState(NULL, true);
    format(&cmd, "cd \"{s}\" && git archive HEAD | shasum -a 256 | cut -d' ' -f1 2>/dev/null",
           (FormatArg[]){{.s = repoPath}});

    char *cmdStr = formatStateToString(&cmd);
    freeFormatState(&cmd);

    FormatState output = newFormatState(NULL, true);
    bool success = executeGitCommand(cmdStr, &output, log);
    free(cmdStr);

    if (!success) {
        freeFormatState(&output);
        logError(log, NULL, "failed to calculate checksum for repository '{s}'",
                (FormatArg[]){{.s = repoPath}});
        return false;
    }

    char *outputStr = formatStateToString(&output);
    freeFormatState(&output);

    // Trim newline
    size_t len = strlen(outputStr);
    while (len > 0 && (outputStr[len - 1] == '\n' || outputStr[len - 1] == '\r')) {
        outputStr[--len] = '\0';
    }

    // Allocate from pool
    char *result = allocFromMemPool(pool, len + 1);
    memcpy(result, outputStr, len + 1);
    *checksum = result;

    free(outputStr);
    return true;
}

bool gitNormalizeRepositoryUrl(cstring repositoryUrl, cstring *normalized, MemPool *pool, Log *log)
{
    if (!repositoryUrl || repositoryUrl[0] == '\0') {
        logError(log, NULL, "repository URL cannot be empty", NULL);
        return false;
    }

    // Handle shorthand formats
    if (strncmp(repositoryUrl, "github:", 7) == 0) {
        const char *path = repositoryUrl + 7;
        size_t len = strlen("https://github.com/") + strlen(path) + 1;
        char *result = allocFromMemPool(pool, len);
        snprintf(result, len, "https://github.com/%s", path);
        *normalized = result;
        return true;
    }

    if (strncmp(repositoryUrl, "gitlab:", 7) == 0) {
        const char *path = repositoryUrl + 7;
        size_t len = strlen("https://gitlab.com/") + strlen(path) + 1;
        char *result = allocFromMemPool(pool, len);
        snprintf(result, len, "https://gitlab.com/%s", path);
        *normalized = result;
        return true;
    }

    if (strncmp(repositoryUrl, "bitbucket:", 10) == 0) {
        const char *path = repositoryUrl + 10;
        size_t len = strlen("https://bitbucket.org/") + strlen(path) + 1;
        char *result = allocFromMemPool(pool, len);
        snprintf(result, len, "https://bitbucket.org/%s", path);
        *normalized = result;
        return true;
    }

    // Already in standard format, just copy
    size_t len = strlen(repositoryUrl);
    char *result = allocFromMemPool(pool, len + 1);
    memcpy(result, repositoryUrl, len + 1);
    *normalized = result;
    return true;
}
