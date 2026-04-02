/**
 * Per-File Profiling System for Cxy Compiler
 * 
 * Design:
 * - Each file has its own ProfileData tracking stages and times
 * - ProfilingContext contains HashMap of fileName -> FileProfileData
 * - No global state or locking (each context is independent)
 * - Parse stage can be paused/resumed for recursive imports
 * - C importer time tracked separately
 * 
 * Usage:
 *   ProfilingContext ctx;
 *   profileInitContext(&ctx, pool);
 *   profileEnable(&ctx);
 *   
 *   // Start compiling a file
 *   PROFILE_FILE(&ctx, "main.cxy") {
 *       // Record stages
 *       PROFILE_STAGE(&ctx, ccsTypeCheck) {
 *           typeCheckProgram(driver, node);
 *       }
 *   }
 *   
 *   // C import tracking
 *   PROFILE_CIMPORT(&ctx) {
 *       importCHeader(driver, source);
 *   }
 *   
 *   // Print results
 *   profilePrint(&ctx);
 *   profileDeinitContext(&ctx);
 */

#pragma once

#include <driver/stages.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct FileProfileData FileProfileData;
typedef struct ProfilingContext ProfilingContext;

// ============================================================================
// Per-File Profile Data
// ============================================================================

/**
 * Profiling data for a single file.
 * Tracks parse time, stage times, and total compilation time.
 */
struct FileProfileData {
    const char *fileName;            // File name (key in hash map)
    uint64_t parseTimeNs;            // Total parse time (can pause/resume)
    uint64_t stageTimesNs[ccsCOUNT]; // Time per stage
    uint64_t totalTimeNs;            // Total time compiling this file
    struct timespec parseStart;      // For pause/resume
    uint64_t parseAccumNs;           // Accumulated parse time during pauses
    bool parsePaused;                // Is parse timer paused?
    struct timespec fileStart;       // When file compilation started
};

// ============================================================================
// Profiling Context
// ============================================================================

/**
 * Profiling context containing all profile data.
 * Each CompilerDriver has its own context (no shared state).
 */
struct ProfilingContext {
    HashTable fileData;              // fileName (cstring) -> FileProfileData*
    FileProfileData *activeFile;     // Currently active file
    uint64_t cImportTimeNs;          // Total time importing C headers
    uint64_t wallStartNs;            // Wall-clock start (set when profiling enabled)
    bool enabled;                    // Is profiling enabled?
    MemPool *pool;                   // Memory pool for allocations
};

// ============================================================================
// Context Management
// ============================================================================

/**
 * Initialize profiling context.
 * Must be called before using the context.
 * 
 * @param ctx Context to initialize
 * @param pool Memory pool for allocations
 */
void profileInitContext(ProfilingContext *ctx, MemPool *pool);

/**
 * Deinitialize profiling context and free resources.
 * 
 * @param ctx Context to deinitialize
 */
void profileDeinitContext(ProfilingContext *ctx);

/**
 * Enable profiling for this context.
 * 
 * @param ctx Profiling context
 */
void profileEnable(ProfilingContext *ctx);

/**
 * Disable profiling for this context.
 * 
 * @param ctx Profiling context
 */
void profileDisable(ProfilingContext *ctx);

/**
 * Check if profiling is enabled.
 * 
 * @param ctx Profiling context
 * @return true if enabled
 */
bool profileIsEnabled(const ProfilingContext *ctx);

// ============================================================================
// File Tracking
// ============================================================================

/**
 * Start tracking a file's compilation.
 * Creates a new FileProfileData entry and sets it as active.
 * 
 * @param ctx Profiling context
 * @param fileName File name
 */
void profileStartFile(ProfilingContext *ctx, const char *fileName);

/**
 * End tracking a file's compilation.
 * Records total time and clears active file.
 * 
 * @param ctx Profiling context
 */
void profileEndFile(ProfilingContext *ctx);

/**
 * Get the currently active file profile data.
 * 
 * @param ctx Profiling context
 * @return Active file data, or NULL if none
 */
FileProfileData *profileGetActiveFile(ProfilingContext *ctx);

// ============================================================================
// Parse Time Tracking (with Pause/Resume)
// ============================================================================

/**
 * Start parse timer for active file.
 * 
 * @param ctx Profiling context
 */
void profileParseStart(ProfilingContext *ctx);

/**
 * Pause parse timer for active file.
 * Used when starting a recursive import.
 * 
 * @param ctx Profiling context
 */
void profileParsePause(ProfilingContext *ctx);

/**
 * Resume parse timer for active file.
 * Used after completing a recursive import.
 * 
 * @param ctx Profiling context
 */
void profileParseResume(ProfilingContext *ctx);

/**
 * Stop parse timer and record final parse time.
 * 
 * @param ctx Profiling context
 */
void profileParseStop(ProfilingContext *ctx);

/**
 * Directly record parse time for a file.
 * Used by worker threads that parse files independently.
 * 
 * @param ctx Profiling context
 * @param fileName File name
 * @param durationNs Parse duration in nanoseconds
 */
void profileRecordParse(ProfilingContext *ctx, const char *fileName, uint64_t durationNs);

// ============================================================================
// Stage Time Tracking
// ============================================================================

/**
 * Record time spent in a compiler stage for the active file.
 * 
 * @param ctx Profiling context
 * @param stage Compiler stage
 * @param durationNs Duration in nanoseconds
 */
void profileRecordStage(ProfilingContext *ctx, CompilerStage stage, uint64_t durationNs);

// ============================================================================
// C Importer Tracking
// ============================================================================

/**
 * Record time spent importing C headers.
 * This is accumulated across all files.
 * 
 * @param ctx Profiling context
 * @param durationNs Duration in nanoseconds
 */
void profileRecordCImport(ProfilingContext *ctx, uint64_t durationNs);

// ============================================================================
// Macros for Convenient Profiling
// ============================================================================

/**
 * Profile a file's compilation.
 * Sets the file as active, executes the block, then ends the file.
 * 
 * Usage:
 *   PROFILE_FILE(&ctx, "main.cxy") {
 *       compileProgram(driver, program, "main.cxy");
 *   }
 */
#define PROFILE_FILE(ctx, fileName) \
    for (int _pf_once_ = (profileStartFile(ctx, fileName), 1); \
         _pf_once_; \
         _pf_once_ = 0, profileEndFile(ctx))

/**
 * Profile a compiler stage.
 * Measures the time spent in the block and records it.
 * 
 * Usage:
 *   PROFILE_STAGE(&ctx, ccsTypeCheck) {
 *       typeCheckProgram(driver, node);
 *   }
 */
#define PROFILE_STAGE(ctx, stage) \
    for (struct timespec _ps_start_ = ((struct timespec){0}), \
                         _ps_end_ = ((struct timespec){0}); \
         (_ps_start_.tv_sec == 0 ? (clock_gettime(CLOCK_MONOTONIC, &_ps_start_), 1) : 0); \
         (clock_gettime(CLOCK_MONOTONIC, &_ps_end_), \
          profileRecordStage(ctx, stage, \
                           (_ps_end_.tv_sec - _ps_start_.tv_sec) * 1000000000ULL + \
                           (_ps_end_.tv_nsec - _ps_start_.tv_nsec)), \
          0))

/**
 * Profile C import operation.
 * Measures the time spent importing C headers.
 * 
 * Usage:
 *   PROFILE_CIMPORT(&ctx) {
 *       program = importCHeader(driver, source);
 *   }
 */
#define PROFILE_CIMPORT(ctx) \
    for (struct timespec _pc_start_ = ((struct timespec){0}), \
                         _pc_end_ = ((struct timespec){0}); \
         (_pc_start_.tv_sec == 0 ? (clock_gettime(CLOCK_MONOTONIC, &_pc_start_), 1) : 0); \
         (clock_gettime(CLOCK_MONOTONIC, &_pc_end_), \
          profileRecordCImport(ctx, \
                             (_pc_end_.tv_sec - _pc_start_.tv_sec) * 1000000000ULL + \
                             (_pc_end_.tv_nsec - _pc_start_.tv_nsec)), \
          0))

// ============================================================================
// Output
// ============================================================================

/**
 * Print profiling results to stdout.
 * Shows per-file breakdown and aggregated totals.
 * 
 * @param ctx Profiling context
 */
void profilePrint(ProfilingContext *ctx);

/**
 * Print profiling results to a JSON file.
 * 
 * JSON format:
 * {
 *   "profilingEnabled": bool,
 *   "totalFiles": int,
 *   "cImportTimeNs": uint64,
 *   "files": [
 *     {
 *       "fileName": string,
 *       "parseTimeNs": uint64,
 *       "totalTimeNs": uint64,
 *       "stages": {
 *         "Parse": uint64,
 *         "TypeCheck": uint64,
 *         ...
 *       }
 *     }
 *   ],
 *   "totals": {
 *     "parseTimeNs": uint64,
 *     "stageTimesNs": { "Parse": uint64, ... },
 *     "totalTimeNs": uint64
 *   }
 * }
 * 
 * @param ctx Profiling context
 * @param filePath Output file path
 * @return true on success
 */
bool profilePrintToJSON(ProfilingContext *ctx, const char *filePath);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Get current time in nanoseconds (monotonic clock).
 * 
 * @return Nanoseconds since arbitrary point
 */
uint64_t profileGetCurrentNs(void);

/**
 * Format nanoseconds as human-readable string.
 * 
 * @param ns Nanoseconds
 * @param buffer Output buffer (at least 32 bytes)
 * @return Pointer to buffer
 */
const char *profileFormatNs(uint64_t ns, char *buffer);

#ifdef __cplusplus
}
#endif