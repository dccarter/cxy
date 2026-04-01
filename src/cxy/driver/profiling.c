/**
 * Profiling Framework Implementation
 */

#include "profiling.h"
#include "core/alloc.h"
#include "core/utils.h"
#include "core/hash.h"

#if CXY_PARALLEL_COMPILE
#include "core/synchronization.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Configuration
// ============================================================================

#define MAX_PROFILE_ENTRIES 1024
#define MAX_PROFILE_DEPTH 32
#define NSEC_PER_SEC 1000000000ULL
#define NSEC_PER_MSEC 1000000ULL
#define NSEC_PER_USEC 1000ULL

// ============================================================================
// Internal Data Structures
// ============================================================================

struct ProfileTimer {
    const char *name;
    struct timespec start;
    int depth;
};

typedef struct ProfileEntryInternal {
    char name[128];
    uint64_t totalNs;
    uint64_t count;
    uint64_t minNs;
    uint64_t maxNs;
    int depth;
    uint64_t waitTimeNs;
    uint64_t holdTimeNs;
    bool isLockProfile;
    struct timespec lockAcquiredAt;  // For tracking hold time
} ProfileEntryInternal;

typedef struct ProfileContext {
    bool enabled;
    int depth;
    ProfileTimer *timerStack[MAX_PROFILE_DEPTH];
    int stackTop;
    ProfileEntryInternal entries[MAX_PROFILE_ENTRIES];
    size_t entryCount;
    int threadId;
} ProfileContext;

// ============================================================================
// Global State
// ============================================================================

static bool gProfilingEnabled = false;

#if CXY_PARALLEL_COMPILE
__thread ProfileContext gProfileContext = {0};
#else
ProfileContext gProfileContext = {0};
#endif

// ============================================================================
// Utility Functions
// ============================================================================

uint64_t getCurrentNanoseconds(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * NSEC_PER_SEC + (uint64_t)ts.tv_nsec;
}

static uint64_t timespecDiffNs(const struct timespec *start, const struct timespec *end)
{
    uint64_t startNs = (uint64_t)start->tv_sec * NSEC_PER_SEC + (uint64_t)start->tv_nsec;
    uint64_t endNs = (uint64_t)end->tv_sec * NSEC_PER_SEC + (uint64_t)end->tv_nsec;
    return endNs - startNs;
}

const char *formatNanoseconds(uint64_t ns, char *buffer)
{
    if (ns >= NSEC_PER_SEC) {
        sprintf(buffer, "%.2fs", (double)ns / NSEC_PER_SEC);
    } else if (ns >= NSEC_PER_MSEC) {
        sprintf(buffer, "%.2fms", (double)ns / NSEC_PER_MSEC);
    } else if (ns >= NSEC_PER_USEC) {
        sprintf(buffer, "%.2fus", (double)ns / NSEC_PER_USEC);
    } else {
        sprintf(buffer, "%lluns", (unsigned long long)ns);
    }
    return buffer;
}

static const char *abbreviatePath(const char *path, char *buffer, size_t maxLen)
{
    if (!path) return "";
    
    size_t pathLen = strlen(path);
    if (pathLen <= maxLen) {
        return path;
    }
    
    // Find the last few path components that fit within maxLen - 4 (for "...")
    const char *p = path + pathLen;
    size_t componentLen = 0;
    int components = 0;
    
    // Walk backwards to find enough components to fit
    while (p > path && componentLen < (maxLen - 4)) {
        p--;
        componentLen++;
        if (*p == '/' || *p == '\\') {
            components++;
            if (components >= 3) break;  // Keep last 3 components
        }
    }
    
    // Skip the leading slash if we found one
    if (p > path && (*p == '/' || *p == '\\')) {
        p++;
    }
    
    snprintf(buffer, maxLen, ".../%s", p);
    return buffer;
}

// ============================================================================
// Profile Entry Management
// ============================================================================

static ProfileEntryInternal *findOrCreateEntry(const char *name, int depth, bool isLockProfile)
{
    ProfileContext *ctx = &gProfileContext;
    
    // Search for existing entry
    for (size_t i = 0; i < ctx->entryCount; i++) {
        if (strcmp(ctx->entries[i].name, name) == 0 && 
            ctx->entries[i].depth == depth) {
            return &ctx->entries[i];
        }
    }
    
    // Create new entry
    if (ctx->entryCount >= MAX_PROFILE_ENTRIES) {
        return NULL;  // Too many entries
    }
    
    ProfileEntryInternal *entry = &ctx->entries[ctx->entryCount++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->totalNs = 0;
    entry->count = 0;
    entry->minNs = UINT64_MAX;
    entry->maxNs = 0;
    entry->depth = depth;
    entry->waitTimeNs = 0;
    entry->holdTimeNs = 0;
    entry->isLockProfile = isLockProfile;
    
    return entry;
}

static void recordDuration(const char *name, uint64_t durationNs, int depth)
{
    ProfileEntryInternal *entry = findOrCreateEntry(name, depth, false);
    if (!entry) return;
    
    entry->totalNs += durationNs;
    entry->count++;
    if (durationNs < entry->minNs) entry->minNs = durationNs;
    if (durationNs > entry->maxNs) entry->maxNs = durationNs;
}

// ============================================================================
// Manual Timer API
// ============================================================================

ProfileTimer *profileStart(const char *name)
{
    if (!gProfilingEnabled) return NULL;
    
    ProfileContext *ctx = &gProfileContext;
    if (!ctx->enabled) {
        ctx->enabled = true;
        ctx->depth = 0;
        ctx->stackTop = -1;
    }
    
    ProfileTimer *timer = mallocOrDie(sizeof(ProfileTimer));
    timer->name = name;
    timespec_get(&timer->start, TIME_UTC);
    timer->depth = ctx->depth;
    
    // Push to stack
    if (ctx->stackTop < MAX_PROFILE_DEPTH - 1) {
        ctx->timerStack[++ctx->stackTop] = timer;
    }
    
    ctx->depth++;
    
    return timer;
}

uint64_t profileStop(ProfileTimer *timer)
{
    if (!timer) return 0;
    if (!gProfilingEnabled) {
        free(timer);
        return 0;
    }
    
    struct timespec end;
    timespec_get(&end, TIME_UTC);
    
    uint64_t durationNs = timespecDiffNs(&timer->start, &end);
    
    ProfileContext *ctx = &gProfileContext;
    recordDuration(timer->name, durationNs, timer->depth);
    
    // Pop from stack
    if (ctx->stackTop >= 0 && ctx->timerStack[ctx->stackTop] == timer) {
        ctx->stackTop--;
    }
    
    ctx->depth--;
    free(timer);
    
    return durationNs;
}

// ============================================================================
// Block-Scoped Profiling
// ============================================================================

ProfileBlock profileBlockStart(const char *name)
{
    ProfileBlock block;
    block.name = name;
    block.active = gProfilingEnabled;
    block.savedDepth = gProfileContext.depth;
    
    if (block.active) {
        timespec_get(&block.start, TIME_UTC);
        gProfileContext.depth++;
    }
    
    return block;
}

bool profileBlockActive(ProfileBlock *block)
{
    return block && block->active;
}

void profileBlockEnd(ProfileBlock *block)
{
    if (!block || !block->active) return;
    
    struct timespec end;
    timespec_get(&end, TIME_UTC);
    
    uint64_t durationNs = timespecDiffNs(&block->start, &end);
    recordDuration(block->name, durationNs, block->savedDepth);
    
    gProfileContext.depth--;
    block->active = false;
}

void profileBlockCleanup(ProfileBlock *block)
{
    if (block && block->active) {
        profileBlockEnd(block);
    }
}

// ============================================================================
// Lock Profiling
// ============================================================================

#if CXY_PARALLEL_COMPILE

void profileLockWait(const char *name, struct Mutex *mutex)
{
    if (!gProfilingEnabled) {
        lockMutex(mutex);
        return;
    }
    
    struct timespec waitStart;
    timespec_get(&waitStart, TIME_UTC);
    
    lockMutex(mutex);
    
    struct timespec waitEnd;
    timespec_get(&waitEnd, TIME_UTC);
    
    uint64_t waitNs = timespecDiffNs(&waitStart, &waitEnd);
    
    // Record wait time
    ProfileEntryInternal *entry = findOrCreateEntry(name, gProfileContext.depth, true);
    if (entry) {
        entry->waitTimeNs += waitNs;
        entry->count++;
        entry->lockAcquiredAt = waitEnd;
    }
}

void profileLockRelease(const char *name, struct Mutex *mutex)
{
    if (!gProfilingEnabled) {
        unlockMutex(mutex);
        return;
    }
    
    struct timespec releaseTime;
    timespec_get(&releaseTime, TIME_UTC);
    
    // Record hold time
    ProfileContext *ctx = &gProfileContext;
    for (size_t i = 0; i < ctx->entryCount; i++) {
        if (strcmp(ctx->entries[i].name, name) == 0 && 
            ctx->entries[i].isLockProfile) {
            uint64_t holdNs = timespecDiffNs(&ctx->entries[i].lockAcquiredAt, &releaseTime);
            ctx->entries[i].holdTimeNs += holdNs;
            break;
        }
    }
    
    unlockMutex(mutex);
}

#endif // CXY_PARALLEL_COMPILE

// ============================================================================
// Profiling Control
// ============================================================================

void profileEnable(void)
{
    gProfilingEnabled = true;
}

void profileDisable(void)
{
    gProfilingEnabled = false;
}

bool profileIsEnabled(void)
{
    return gProfilingEnabled;
}

void profileReset(void)
{
    ProfileContext *ctx = &gProfileContext;
    ctx->entryCount = 0;
    ctx->depth = 0;
    ctx->stackTop = -1;
    memset(ctx->entries, 0, sizeof(ctx->entries));
}

void profileResetAll(void)
{
    profileReset();
    // In multi-threaded context, this would need to reset all thread contexts
    // For now, just reset current thread
}

// ============================================================================
// Profiling Output
// ============================================================================

static int compareEntriesByDepthAndName(const void *a, const void *b)
{
    const ProfileEntryInternal *ea = (const ProfileEntryInternal *)a;
    const ProfileEntryInternal *eb = (const ProfileEntryInternal *)b;
    
    if (ea->depth != eb->depth) {
        return ea->depth - eb->depth;
    }
    return strcmp(ea->name, eb->name);
}

void profilePrint(bool showDetails)
{
    if (!gProfilingEnabled) {
        printf("Profiling is disabled\n");
        return;
    }
    
    ProfileContext *ctx = &gProfileContext;
    if (ctx->entryCount == 0) {
        printf("No profiling data collected\n");
        return;
    }
    
    // Sort entries by depth and name
    qsort(ctx->entries, ctx->entryCount, sizeof(ProfileEntryInternal), 
          compareEntriesByDepthAndName);
    
    printf("\n");
    printf("════════════════════════════════════════════════════════════════════════════\n");
    printf("  Profiling Results\n");
    printf("════════════════════════════════════════════════════════════════════════════\n");
    printf("\n");
    printf("%-40s %8s %12s %12s %12s\n", "Section", "Calls", "Total", "Avg", "Max");
    printf("────────────────────────────────────────────────────────────────────────────\n");
    
    char totalBuf[32], avgBuf[32], maxBuf[32];
    
    for (size_t i = 0; i < ctx->entryCount; i++) {
        ProfileEntryInternal *entry = &ctx->entries[i];
        
        if (entry->isLockProfile) continue;  // Skip locks for now
        
        uint64_t avgNs = entry->count > 0 ? entry->totalNs / entry->count : 0;
        
        // Indent based on depth, abbreviate file names at depth 0
        char nameBuf[48];
        char pathBuf[48];
        int indent = entry->depth * 2;
        const char *displayName = entry->name;
        
        // Abbreviate long paths for files (depth 0)
        if (entry->depth == 0) {
            displayName = abbreviatePath(entry->name, pathBuf, 35);
        }
        
        snprintf(nameBuf, sizeof(nameBuf), "%*s%s", indent, "", displayName);
        
        formatNanoseconds(entry->totalNs, totalBuf);
        formatNanoseconds(avgNs, avgBuf);
        formatNanoseconds(entry->maxNs, maxBuf);
        
        printf("%-40s %8llu %12s %12s %12s\n",
               nameBuf,
               (unsigned long long)entry->count,
               totalBuf,
               avgBuf,
               maxBuf);
    }
    
    // Print lock contention analysis if any
    bool hasLockProfiles = false;
    for (size_t i = 0; i < ctx->entryCount; i++) {
        if (ctx->entries[i].isLockProfile) {
            hasLockProfiles = true;
            break;
        }
    }
    
    if (hasLockProfiles) {
        printf("\n");
        printf("Lock Contention Analysis\n");
        printf("────────────────────────────────────────────────────────────────────────────\n");
        printf("%-30s %12s %12s %10s\n", "Lock", "Wait Time", "Hold Time", "Acq.");
        printf("────────────────────────────────────────────────────────────────────────────\n");
        
        char waitBuf[32], holdBuf[32];
        
        for (size_t i = 0; i < ctx->entryCount; i++) {
            ProfileEntryInternal *entry = &ctx->entries[i];
            if (!entry->isLockProfile) continue;
            
            formatNanoseconds(entry->waitTimeNs, waitBuf);
            formatNanoseconds(entry->holdTimeNs, holdBuf);
            
            printf("%-30s %12s %12s %10llu\n",
                   entry->name,
                   waitBuf,
                   holdBuf,
                   (unsigned long long)entry->count);
        }
    }
    
    printf("════════════════════════════════════════════════════════════════════════════\n");
    printf("\n");
}

void profilePrintAll(bool showDetails)
{
    // In single-threaded mode, same as profilePrint
    // In multi-threaded mode, would iterate over all thread contexts
    profilePrint(showDetails);
}

bool profilePrintToFile(const char *filename, bool showDetails)
{
    FILE *oldStdout = stdout;
    FILE *file = fopen(filename, "w");
    if (!file) return false;
    
    stdout = file;
    profilePrint(showDetails);
    stdout = oldStdout;
    
    fclose(file);
    return true;
}

bool profilePrintToJSON(const char *filename)
{
    if (!gProfilingEnabled) {
        return false;
    }
    
    ProfileContext *ctx = &gProfileContext;
    if (ctx->entryCount == 0) {
        return false;
    }
    
    FILE *file = fopen(filename, "w");
    if (!file) return false;
    
    fprintf(file, "{\n");
    fprintf(file, "  \"profilingEnabled\": true,\n");
    fprintf(file, "  \"threadId\": %d,\n", ctx->threadId);
    fprintf(file, "  \"entryCount\": %zu,\n", ctx->entryCount);
    fprintf(file, "  \"entries\": [\n");
    
    for (size_t i = 0; i < ctx->entryCount; i++) {
        ProfileEntryInternal *entry = &ctx->entries[i];
        
        fprintf(file, "    {\n");
        fprintf(file, "      \"name\": \"%s\",\n", entry->name);
        fprintf(file, "      \"totalNs\": %llu,\n", (unsigned long long)entry->totalNs);
        fprintf(file, "      \"count\": %llu,\n", (unsigned long long)entry->count);
        fprintf(file, "      \"minNs\": %llu,\n", (unsigned long long)entry->minNs);
        fprintf(file, "      \"maxNs\": %llu,\n", (unsigned long long)entry->maxNs);
        
        uint64_t avgNs = entry->count > 0 ? entry->totalNs / entry->count : 0;
        fprintf(file, "      \"avgNs\": %llu,\n", (unsigned long long)avgNs);
        fprintf(file, "      \"depth\": %d,\n", entry->depth);
        fprintf(file, "      \"isLockProfile\": %s", entry->isLockProfile ? "true" : "false");
        
        if (entry->isLockProfile) {
            fprintf(file, ",\n");
            fprintf(file, "      \"waitTimeNs\": %llu,\n", (unsigned long long)entry->waitTimeNs);
            fprintf(file, "      \"holdTimeNs\": %llu\n", (unsigned long long)entry->holdTimeNs);
        } else {
            fprintf(file, "\n");
        }
        
        if (i < ctx->entryCount - 1) {
            fprintf(file, "    },\n");
        } else {
            fprintf(file, "    }\n");
        }
    }
    
    fprintf(file, "  ]\n");
    fprintf(file, "}\n");
    
    fclose(file);
    return true;
}

// ============================================================================
// Structured Data Access
// ============================================================================

static ProfileEntry gExportEntries[MAX_PROFILE_ENTRIES + 1];

const ProfileEntry *profileGetEntries(void)
{
    ProfileContext *ctx = &gProfileContext;
    
    for (size_t i = 0; i < ctx->entryCount; i++) {
        ProfileEntryInternal *internal = &ctx->entries[i];
        ProfileEntry *export = &gExportEntries[i];
        
        export->name = internal->name;
        export->totalNs = internal->totalNs;
        export->count = internal->count;
        export->minNs = internal->minNs;
        export->maxNs = internal->maxNs;
        export->avgNs = internal->count > 0 ? internal->totalNs / internal->count : 0;
        export->depth = internal->depth;
        export->waitTimeNs = internal->waitTimeNs;
        export->holdTimeNs = internal->holdTimeNs;
        export->isLockProfile = internal->isLockProfile;
    }
    
    // NULL-terminate
    gExportEntries[ctx->entryCount].name = NULL;
    
    return gExportEntries;
}

const ProfileEntry *profileGetEntriesForThread(int threadId)
{
    // For now, only support current thread
    if (threadId == gProfileContext.threadId) {
        return profileGetEntries();
    }
    return NULL;
}

size_t profileGetEntryCount(void)
{
    return gProfileContext.entryCount;
}