/**
 * Per-File Profiling System for Cxy Compiler
 * 
 * Implementation of per-file profiling with pause/resume support.
 */

#include "profiling.h"

#include <core/alloc.h>
#include <core/htable.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Debug profiling - set DEBUG_PROFILING=1 to enable timeline logging
static bool DEBUG_PROFILING_ENABLED = false;

#define DEBUG_LOG(...) \
    do { \
        if (DEBUG_PROFILING_ENABLED) { \
            struct timespec _ts_; \
            clock_gettime(CLOCK_MONOTONIC, &_ts_); \
            fprintf(stderr, "[%ld.%09ld] ", _ts_.tv_sec, _ts_.tv_nsec); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } \
    } while (0)

// ============================================================================
// Utility Functions
// ============================================================================

uint64_t profileGetCurrentNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t timespecDiffNs(const struct timespec *start, const struct timespec *end)
{
    uint64_t startNs = (uint64_t)start->tv_sec * 1000000000ULL + (uint64_t)start->tv_nsec;
    uint64_t endNs = (uint64_t)end->tv_sec * 1000000000ULL + (uint64_t)end->tv_nsec;
    return endNs > startNs ? endNs - startNs : 0;
}

const char *profileFormatNs(uint64_t ns, char *buffer)
{
    if (ns >= 1000000000ULL) {
        // Seconds
        double sec = (double)ns / 1000000000.0;
        snprintf(buffer, 32, "%.3fs", sec);
    } else if (ns >= 1000000ULL) {
        // Milliseconds
        double ms = (double)ns / 1000000.0;
        snprintf(buffer, 32, "%.2fms", ms);
    } else if (ns >= 1000ULL) {
        // Microseconds
        double us = (double)ns / 1000.0;
        snprintf(buffer, 32, "%.2fus", us);
    } else {
        // Nanoseconds
        snprintf(buffer, 32, "%lluns", (unsigned long long)ns);
    }
    return buffer;
}

// ============================================================================
// Context Management
// ============================================================================

static bool compareFileData(const void *a, const void *b)
{
    const FileProfileData *da = *(const FileProfileData **)a;
    const FileProfileData *db = *(const FileProfileData **)b;
    return strcmp(da->fileName, db->fileName) == 0;
}

void profileInitContext(ProfilingContext *ctx, MemPool *pool)
{
    if (!ctx) return;
    
    memset(ctx, 0, sizeof(ProfilingContext));
    ctx->pool = pool;
    ctx->enabled = false;
    ctx->activeFile = NULL;
    ctx->cImportTimeNs = 0;
    
    // Initialize hash table for file data (stores FileProfileData*)
    ctx->fileData = newHashTable(sizeof(FileProfileData *), pool);
    
    // Check for debug profiling env var
    const char *debug_env = getenv("DEBUG_PROFILING");
    DEBUG_PROFILING_ENABLED = (debug_env && atoi(debug_env) == 1);
    
    DEBUG_LOG("PROFILING: Context initialized");
}

void profileDeinitContext(ProfilingContext *ctx)
{
    if (!ctx) return;
    
    DEBUG_LOG("PROFILING: Context deinitialized");
    
    // Clear hash table (entries allocated from pool, so no need to free individually)
    clearHashTable(&ctx->fileData);
    
    memset(ctx, 0, sizeof(ProfilingContext));
}

void profileEnable(ProfilingContext *ctx)
{
    if (!ctx) return;
    ctx->enabled = true;
    ctx->wallStartNs = profileGetCurrentNs();
    DEBUG_LOG("PROFILING: Enabled");
}

void profileDisable(ProfilingContext *ctx)
{
    if (!ctx) return;
    ctx->enabled = false;
    DEBUG_LOG("PROFILING: Disabled");
}

bool profileIsEnabled(const ProfilingContext *ctx)
{
    return ctx && ctx->enabled;
}

// ============================================================================
// File Tracking
// ============================================================================

static FileProfileData *findOrCreateFileData(ProfilingContext *ctx, const char *fileName)
{
    if (!ctx || !fileName) return NULL;
    
    // Look up existing entry by fileName.
    // We store FileProfileData* in the table, so search with a dummy pointer
    // whose fileName field matches.
    HashCode hash = hashStr(hashInit(), fileName);
    FileProfileData dummy = {.fileName = fileName};
    FileProfileData *dummyPtr = &dummy;
    FileProfileData **dataPtr = findInHashTable(
        &ctx->fileData,
        &dummyPtr,
        hash,
        sizeof(FileProfileData *),
        compareFileData);

    if (dataPtr) {
        return *dataPtr;
    }

    // Create new entry (pool-allocated so the pointer is stable)
    FileProfileData *data = allocFromMemPool(ctx->pool, sizeof(FileProfileData));
    memset(data, 0, sizeof(FileProfileData));

    // Copy the file name into the pool so it outlives any stack frame
    size_t nameLen = strlen(fileName);
    char *nameCopy = allocFromMemPool(ctx->pool, nameLen + 1);
    memcpy(nameCopy, fileName, nameLen + 1);
    data->fileName = nameCopy;

    // Insert the pointer into the hash table
    insertInHashTable(
        &ctx->fileData,
        &data,
        hash,
        sizeof(FileProfileData *),
        compareFileData);
    
    DEBUG_LOG("PROFILING: Created file data for '%s'", fileName);
    
    return data;
}

void profileStartFile(ProfilingContext *ctx, const char *fileName)
{
    if (!profileIsEnabled(ctx) || !fileName) return;
    
    FileProfileData *data = findOrCreateFileData(ctx, fileName);
    if (!data) return;
    
    clock_gettime(CLOCK_MONOTONIC, &data->fileStart);
    data->parseAccumNs = 0;
    data->parsePaused = false;
    ctx->activeFile = data;
    
    DEBUG_LOG("PROFILING: START FILE '%s'", fileName);
}

void profileEndFile(ProfilingContext *ctx)
{
    if (!profileIsEnabled(ctx) || !ctx->activeFile) return;
    
    FileProfileData *data = ctx->activeFile;

    // Compute total as sum of what we actually measured for this file only
    // (not wall-clock, which would double-count time spent in recursive imports)
    uint64_t total = data->parseTimeNs;
    for (int s = 0; s < ccsCOUNT; s++)
        total += data->stageTimesNs[s];
    data->totalTimeNs = total;
    
    DEBUG_LOG("PROFILING: END FILE '%s' (total=%lluns)", 
              data->fileName, (unsigned long long)data->totalTimeNs);
    
    ctx->activeFile = NULL;
}

FileProfileData *profileGetActiveFile(ProfilingContext *ctx)
{
    if (!ctx) return NULL;
    return ctx->activeFile;
}

// ============================================================================
// Parse Time Tracking (with Pause/Resume)
// ============================================================================

void profileParseStart(ProfilingContext *ctx)
{
    if (!profileIsEnabled(ctx) || !ctx->activeFile) return;
    
    FileProfileData *data = ctx->activeFile;
    clock_gettime(CLOCK_MONOTONIC, &data->parseStart);
    data->parsePaused = false;
    
    DEBUG_LOG("PROFILING: PARSE START '%s'", data->fileName);
}

void profileParsePause(ProfilingContext *ctx)
{
    if (!profileIsEnabled(ctx) || !ctx->activeFile) return;
    
    FileProfileData *data = ctx->activeFile;
    if (data->parsePaused) return; // Already paused
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    uint64_t elapsed = timespecDiffNs(&data->parseStart, &now);
    data->parseAccumNs += elapsed;
    data->parsePaused = true;
    
    DEBUG_LOG("PROFILING: PARSE PAUSE '%s' (elapsed=%lluns, accum=%lluns)", 
              data->fileName, (unsigned long long)elapsed, (unsigned long long)data->parseAccumNs);
}

void profileParseResume(ProfilingContext *ctx)
{
    if (!profileIsEnabled(ctx) || !ctx->activeFile) return;
    
    FileProfileData *data = ctx->activeFile;
    if (!data->parsePaused) return; // Not paused
    
    clock_gettime(CLOCK_MONOTONIC, &data->parseStart);
    data->parsePaused = false;
    
    DEBUG_LOG("PROFILING: PARSE RESUME '%s'", data->fileName);
}

void profileParseStop(ProfilingContext *ctx)
{
    if (!profileIsEnabled(ctx) || !ctx->activeFile) return;
    
    FileProfileData *data = ctx->activeFile;
    
    if (!data->parsePaused) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        uint64_t elapsed = timespecDiffNs(&data->parseStart, &now);
        data->parseAccumNs += elapsed;
    }
    
    data->parseTimeNs = data->parseAccumNs;
    data->parsePaused = false;
    
    DEBUG_LOG("PROFILING: PARSE STOP '%s' (total=%lluns)", 
              data->fileName, (unsigned long long)data->parseTimeNs);
}

void profileRecordParse(ProfilingContext *ctx, const char *fileName, uint64_t durationNs)
{
    if (!profileIsEnabled(ctx) || !fileName) return;
    
    FileProfileData *data = findOrCreateFileData(ctx, fileName);
    if (!data) return;
    
    data->parseTimeNs = durationNs;
    
    DEBUG_LOG("PROFILING: RECORD PARSE '%s' (%lluns)", 
              fileName, (unsigned long long)durationNs);
}

// ============================================================================
// Stage Time Tracking
// ============================================================================

void profileRecordStage(ProfilingContext *ctx, CompilerStage stage, uint64_t durationNs)
{
    if (!profileIsEnabled(ctx) || !ctx->activeFile) return;
    if (stage < 0 || stage >= ccsCOUNT) return;
    
    FileProfileData *data = ctx->activeFile;
    data->stageTimesNs[stage] += durationNs;
    
    DEBUG_LOG("PROFILING: STAGE '%s' in '%s' (%lluns)", 
              getCompilerStageName(stage), data->fileName, (unsigned long long)durationNs);
}

// ============================================================================
// C Importer Tracking
// ============================================================================

void profileRecordCImport(ProfilingContext *ctx, uint64_t durationNs)
{
    if (!profileIsEnabled(ctx)) return;
    
    ctx->cImportTimeNs += durationNs;
    
    DEBUG_LOG("PROFILING: C IMPORT (%lluns, total=%lluns)", 
              (unsigned long long)durationNs, (unsigned long long)ctx->cImportTimeNs);
}

// ============================================================================
// Output
// ============================================================================

// Helper to iterate hash table entries
typedef struct {
    FileProfileData **entries;
    size_t count;
    size_t capacity;
} FileDataList;

static bool collectFileData(void *userData, const void *elem)
{
    FileDataList *list = (FileDataList *)userData;
    
    if (list->count >= list->capacity) return false;
    
    FileProfileData *data = *(FileProfileData **)elem;
    list->entries[list->count++] = data;
    return true;
}

static int compareFileDataByName(const void *a, const void *b)
{
    const FileProfileData *fa = *(const FileProfileData **)a;
    const FileProfileData *fb = *(const FileProfileData **)b;
    return strcmp(fa->fileName, fb->fileName);
}

// Return just the filename portion of a path (after last '/')
static const char *baseFileName(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

// Print a horizontal divider matching the given total width
static void printDivider(int width)
{
    for (int i = 0; i < width; i++) putchar('-');
    putchar('\n');
}

void profilePrint(ProfilingContext *ctx)
{
    if (!ctx) return;

    if (!ctx->enabled) {
        printf("Profiling is disabled\n");
        return;
    }

    // Collect and sort file data
    FileDataList list = {0};
    list.capacity = 1024;
    list.entries = malloc(sizeof(FileProfileData *) * list.capacity);
    enumerateHashTable(&ctx->fileData, &list, collectFileData, sizeof(FileProfileData *));
    qsort(list.entries, list.count, sizeof(FileProfileData *), compareFileDataByName);

    // Compute totals and find which stages have any data
    uint64_t totalParse = 0;
    uint64_t totalStages[ccsCOUNT] = {0};
    uint64_t totalTime = 0;
    bool stageActive[ccsCOUNT] = {false};

    for (size_t i = 0; i < list.count; i++) {
        FileProfileData *data = list.entries[i];
        totalParse += data->parseTimeNs;
        totalTime += data->totalTimeNs;
        for (int s = 0; s < ccsCOUNT; s++) {
            totalStages[s] += data->stageTimesNs[s];
            if (data->stageTimesNs[s] > 0)
                stageActive[s] = true;
        }
    }

    // Build ordered list of active stages (skip private _Dump/_DumpIR/_First)
    int activeStages[ccsCOUNT];
    int activeCount = 0;
    for (int s = ccs_First + 1; s < ccsCOUNT; s++) {
        if (stageActive[s])
            activeStages[activeCount++] = s;
    }

    // -------------------------------------------------------------------------
    // Table 1: File x Stage matrix
    // -------------------------------------------------------------------------

    // Compute column widths
    // Col 0: file name, Col 1: Parse, Col 2..N: active stages, last: Total
    char buf[32];

    // File name column width
    int fileColW = 4; // "File" header minimum
    for (size_t i = 0; i < list.count; i++) {
        int len = (int)strlen(baseFileName(list.entries[i]->fileName));
        if (len > fileColW) fileColW = len;
    }

    // Parse column width
    int parseColW = 5; // "Parse"
    for (size_t i = 0; i < list.count; i++) {
        int len = (int)strlen(profileFormatNs(list.entries[i]->parseTimeNs, buf));
        if (len > parseColW) parseColW = len;
    }
    {
        int len = (int)strlen(profileFormatNs(totalParse, buf));
        if (len > parseColW) parseColW = len;
    }

    // Stage column widths
    int stageColW[ccsCOUNT] = {0};
    for (int ci = 0; ci < activeCount; ci++) {
        int s = activeStages[ci];
        stageColW[ci] = (int)strlen(getCompilerStageName(s));
        for (size_t i = 0; i < list.count; i++) {
            int len = (int)strlen(profileFormatNs(list.entries[i]->stageTimesNs[s], buf));
            if (len > stageColW[ci]) stageColW[ci] = len;
        }
        int len = (int)strlen(profileFormatNs(totalStages[s], buf));
        if (len > stageColW[ci]) stageColW[ci] = len;
    }

    // Total column width
    int totalColW = 5; // "Total"
    for (size_t i = 0; i < list.count; i++) {
        int len = (int)strlen(profileFormatNs(list.entries[i]->totalTimeNs, buf));
        if (len > totalColW) totalColW = len;
    }
    {
        int len = (int)strlen(profileFormatNs(totalTime, buf));
        if (len > totalColW) totalColW = len;
    }

    // Compute total table width
    int tableW = fileColW + 3 + parseColW + 3; // file | parse |
    for (int ci = 0; ci < activeCount; ci++)
        tableW += stageColW[ci] + 3;
    tableW += totalColW;

    printf("\n");
    printf("  Per-File Stage Breakdown\n");
    printDivider(tableW + 2);

    // Header row
    printf("  %-*s | %-*s |", fileColW, "File", parseColW, "Parse");
    for (int ci = 0; ci < activeCount; ci++)
        printf(" %-*s |", stageColW[ci], getCompilerStageName(activeStages[ci]));
    printf(" %-*s\n", totalColW, "Total");

    printDivider(tableW + 2);

    // Data rows
    for (size_t i = 0; i < list.count; i++) {
        FileProfileData *data = list.entries[i];
        printf("  %-*s | %*s |", fileColW, baseFileName(data->fileName),
               parseColW, profileFormatNs(data->parseTimeNs, buf));
        for (int ci = 0; ci < activeCount; ci++) {
            int s = activeStages[ci];
            if (data->stageTimesNs[s] > 0)
                printf(" %*s |", stageColW[ci], profileFormatNs(data->stageTimesNs[s], buf));
            else
                printf(" %*s |", stageColW[ci], "-");
        }
        printf(" %*s\n", totalColW, profileFormatNs(data->totalTimeNs, buf));
    }

    // Totals row
    printDivider(tableW + 2);
    printf("  %-*s | %*s |", fileColW, "TOTAL", parseColW, profileFormatNs(totalParse, buf));
    for (int ci = 0; ci < activeCount; ci++) {
        int s = activeStages[ci];
        printf(" %*s |", stageColW[ci], profileFormatNs(totalStages[s], buf));
    }
    printf(" %*s\n", totalColW, profileFormatNs(totalTime, buf));
    printDivider(tableW + 2);

    // -------------------------------------------------------------------------
    // Table 2: Stage totals with wall-clock % column
    // -------------------------------------------------------------------------

    uint64_t wallNs = profileGetCurrentNs() - ctx->wallStartNs;

    // Sum of all measured time across all files + C imports
    uint64_t accountedNs = totalTime + ctx->cImportTimeNs;
    uint64_t unaccountedNs = wallNs > accountedNs ? wallNs - accountedNs : 0;

    // Column widths
    int stageNameW = 11; // "Unaccounted" is longest label
    int stageTotalW = 10; // "Total Time"
    int stageFilesW = 5;  // "Files"
    int stagePctW   = 5;  // "  %  " e.g. "99.9%"

    for (int ci = 0; ci < activeCount; ci++) {
        int s = activeStages[ci];
        int nlen = (int)strlen(getCompilerStageName(s));
        if (nlen > stageNameW) stageNameW = nlen;
        int tlen = (int)strlen(profileFormatNs(totalStages[s], buf));
        if (tlen > stageTotalW) stageTotalW = tlen;
    }
    {
        int tlen = (int)strlen(profileFormatNs(totalParse, buf));
        if (tlen > stageTotalW) stageTotalW = tlen;
        tlen = (int)strlen(profileFormatNs(wallNs, buf));
        if (tlen > stageTotalW) stageTotalW = tlen;
    }

    int totalsTableW = stageNameW + 3 + stageTotalW + 3 + stagePctW + 3 + stageFilesW;

    // Helper: format percentage of wall time
    char pctBuf[16];
    #define PCT(ns) \
        (wallNs > 0 \
            ? (snprintf(pctBuf, sizeof(pctBuf), "%.1f%%", 100.0 * (double)(ns) / (double)wallNs), pctBuf) \
            : "-")

    printf("\n");
    printf("  Stage Totals\n");
    printDivider(totalsTableW + 2);
    printf("  %-*s | %*s | %*s | %*s\n",
           stageNameW, "Stage",
           stageTotalW, "Total Time",
           stagePctW, "%",
           stageFilesW, "Files");
    printDivider(totalsTableW + 2);

    // Parse row
    printf("  %-*s | %*s | %*s | %*zu\n",
           stageNameW, "Parse",
           stageTotalW, profileFormatNs(totalParse, buf),
           stagePctW, PCT(totalParse),
           stageFilesW, list.count);

    for (int ci = 0; ci < activeCount; ci++) {
        int s = activeStages[ci];
        size_t fileCount = 0;
        for (size_t i = 0; i < list.count; i++)
            if (list.entries[i]->stageTimesNs[s] > 0) fileCount++;
        printf("  %-*s | %*s | %*s | %*zu\n",
               stageNameW, getCompilerStageName(s),
               stageTotalW, profileFormatNs(totalStages[s], buf),
               stagePctW, PCT(totalStages[s]),
               stageFilesW, fileCount);
    }

    if (ctx->cImportTimeNs > 0) {
        printf("  %-*s | %*s | %*s | %*s\n",
               stageNameW, "C Import",
               stageTotalW, profileFormatNs(ctx->cImportTimeNs, buf),
               stagePctW, PCT(ctx->cImportTimeNs),
               stageFilesW, "-");
    }

    printDivider(totalsTableW + 2);
    printf("  %-*s | %*s | %*s | %*zu\n",
           stageNameW, "Accounted",
           stageTotalW, profileFormatNs(accountedNs, buf),
           stagePctW, PCT(accountedNs),
           stageFilesW, list.count);
    printf("  %-*s | %*s | %*s | %*s\n",
           stageNameW, "Unaccounted",
           stageTotalW, profileFormatNs(unaccountedNs, buf),
           stagePctW, PCT(unaccountedNs),
           stageFilesW, "-");
    printDivider(totalsTableW + 2);
    printf("  %-*s | %*s | %*s\n",
           stageNameW, "Wall Time",
           stageTotalW, profileFormatNs(wallNs, buf),
           stagePctW, "100.0%");
    printDivider(totalsTableW + 2);

    #undef PCT
    printf("\n");

    free(list.entries);
}

bool profilePrintToJSON(ProfilingContext *ctx, const char *filePath)
{
    if (!ctx || !filePath) return false;
    
    FILE *fp = fopen(filePath, "w");
    if (!fp) return false;
    
    // Collect all file data
    FileDataList list = {0};
    list.capacity = 1024;
    list.entries = malloc(sizeof(FileProfileData *) * list.capacity);
    
    enumerateHashTable(&ctx->fileData, &list, collectFileData, sizeof(FileProfileData *));
    
    // Sort by file name
    qsort(list.entries, list.count, sizeof(FileProfileData *), compareFileDataByName);
    
    // Compute totals
    uint64_t totalParse = 0;
    uint64_t totalStages[ccsCOUNT] = {0};
    uint64_t totalTime = 0;
    
    for (size_t i = 0; i < list.count; i++) {
        FileProfileData *data = list.entries[i];
        totalParse += data->parseTimeNs;
        totalTime += data->totalTimeNs;
        
        for (int stage = 0; stage < ccsCOUNT; stage++) {
            totalStages[stage] += data->stageTimesNs[stage];
        }
    }
    
    // Write JSON
    fprintf(fp, "{\n");
    fprintf(fp, "  \"profilingEnabled\": %s,\n", ctx->enabled ? "true" : "false");
    fprintf(fp, "  \"totalFiles\": %zu,\n", list.count);
    fprintf(fp, "  \"cImportTimeNs\": %llu,\n", (unsigned long long)ctx->cImportTimeNs);
    fprintf(fp, "  \"files\": [\n");
    
    for (size_t i = 0; i < list.count; i++) {
        FileProfileData *data = list.entries[i];
        
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"fileName\": \"%s\",\n", data->fileName);
        fprintf(fp, "      \"parseTimeNs\": %llu,\n", (unsigned long long)data->parseTimeNs);
        fprintf(fp, "      \"totalTimeNs\": %llu,\n", (unsigned long long)data->totalTimeNs);
        fprintf(fp, "      \"stages\": {\n");
        
        bool firstStage = true;
        for (int stage = 0; stage < ccsCOUNT; stage++) {
            if (data->stageTimesNs[stage] > 0) {
                if (!firstStage) fprintf(fp, ",\n");
                fprintf(fp, "        \"%s\": %llu",
                       getCompilerStageName(stage),
                       (unsigned long long)data->stageTimesNs[stage]);
                firstStage = false;
            }
        }
        
        fprintf(fp, "\n      }\n");
        fprintf(fp, "    }%s\n", (i + 1 < list.count) ? "," : "");
    }
    
    fprintf(fp, "  ],\n");
    fprintf(fp, "  \"totals\": {\n");
    fprintf(fp, "    \"parseTimeNs\": %llu,\n", (unsigned long long)totalParse);
    fprintf(fp, "    \"totalTimeNs\": %llu,\n", (unsigned long long)totalTime);
    fprintf(fp, "    \"stageTimesNs\": {\n");
    
    bool firstStage = true;
    for (int stage = 0; stage < ccsCOUNT; stage++) {
        if (totalStages[stage] > 0) {
            if (!firstStage) fprintf(fp, ",\n");
            fprintf(fp, "      \"%s\": %llu",
                   getCompilerStageName(stage),
                   (unsigned long long)totalStages[stage]);
            firstStage = false;
        }
    }
    
    fprintf(fp, "\n    }\n");
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    free(list.entries);
    
    return true;
}