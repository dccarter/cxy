/**
 * Profiling Framework for Cxy Compiler
 * 
 * This file provides a comprehensive profiling API for measuring compilation
 * performance, including fine-grained timing, lock contention analysis, and
 * hierarchical profiling of nested operations.
 * 
 * Features:
 * - Manual timers (profileStart/profileStop)
 * - Automatic scope-based profiling (PROFILE_SCOPE)
 * - Code block profiling (PROFILE_SECTION)
 * - Lock contention profiling (PROFILE_LOCK)
 * - Thread-aware (works with parallel compilation)
 * - Hierarchical output with nesting
 * - Minimal overhead when disabled
 */

#pragma once

#include "core/features.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct ProfileTimer ProfileTimer;
typedef struct ProfileBlock ProfileBlock;

#if CXY_PARALLEL_COMPILE
struct Mutex;  // From core/synchronization.h
#endif

// ============================================================================
// Manual Timer API
// ============================================================================

/**
 * Thread-Safety: SAFE (uses thread-local storage)
 * 
 * Start a named profiling timer.
 * Returns a timer handle that must be passed to profileStop().
 * 
 * If profiling is disabled, returns NULL immediately.
 * 
 * @param name Human-readable name for this timer (should be string literal)
 * @return Timer handle, or NULL if profiling disabled
 * 
 * Example:
 *   ProfileTimer *t = profileStart("parseFile");
 *   parseFile(path);
 *   uint64_t ns = profileStop(t);
 */
ProfileTimer *profileStart(const char *name);

/**
 * Thread-Safety: SAFE (uses thread-local storage)
 * 
 * Stop a profiling timer and record the duration.
 * Safe to call with NULL timer (no-op).
 * 
 * @param timer The timer to stop (from profileStart), may be NULL
 * @return Duration in nanoseconds, or 0 if timer was NULL
 */
uint64_t profileStop(ProfileTimer *timer);

// ============================================================================
// Block-Scoped Profiling (RAII-style)
// ============================================================================

/**
 * PROFILE_SCOPE(name) - Profile the current scope
 * 
 * Uses GCC/Clang __attribute__((cleanup)) for automatic timer stop.
 * Timer starts immediately and stops when scope exits.
 * 
 * Usage:
 *   void myFunction() {
 *       PROFILE_SCOPE("myFunction");
 *       // ... code to profile
 *       if (error) return;  // Timer stops automatically
 *   } // Timer stops here too
 * 
 * @param name Timer name (should be string literal)
 */
#define PROFILE_SCOPE(name) \
    __attribute__((cleanup(profileBlockCleanup))) \
    ProfileBlock _profile_block_ = profileBlockStart(name)

/**
 * PROFILE_SECTION(name) - Profile a code block
 * 
 * Uses for-loop trick to create a scoped block.
 * Timer starts before block and stops after block.
 * 
 * Usage:
 *   PROFILE_SECTION("parsing") {
 *       parseFile();
 *       // ... more parsing
 *   } // Timer stops here
 * 
 * @param name Timer name (should be string literal)
 */
#define PROFILE_SECTION(name) \
    for (ProfileBlock _pb_ = profileBlockStart(name); \
         profileBlockActive(&_pb_); \
         profileBlockEnd(&_pb_))

// ============================================================================
// Lock Profiling
// ============================================================================

#if CXY_PARALLEL_COMPILE

/**
 * PROFILE_LOCK(mutex, name) - Profile lock acquisition and hold time
 * 
 * Measures two separate metrics:
 * 1. Wait time: How long we waited to acquire the lock
 * 2. Hold time: How long we held the lock
 * 
 * Creates a scoped block that holds the lock.
 * Lock is automatically released at end of block.
 * 
 * Usage:
 *   PROFILE_LOCK(pool->lock, "strpool-insert") {
 *       insertIntoHashTable(&pool->table, str);
 *   } // Lock released here
 * 
 * @param mutex The mutex to lock (struct Mutex*)
 * @param name Profile entry name (should be string literal)
 */
#define PROFILE_LOCK(mutex, name) \
    for (int _locked_ = (profileLockWait(name, mutex), 1); \
         _locked_; \
         _locked_ = (profileLockRelease(name, mutex), 0))

/**
 * Profile lock wait time and acquire the lock.
 * Measures time from call until lock acquired.
 * 
 * @param name Profile entry name
 * @param mutex Mutex to lock
 */
void profileLockWait(const char *name, struct Mutex *mutex);

/**
 * Profile lock hold time and release the lock.
 * Measures time since lock was acquired.
 * 
 * @param name Profile entry name (must match profileLockWait)
 * @param mutex Mutex to unlock
 */
void profileLockRelease(const char *name, struct Mutex *mutex);

#else

// Sequential compilation: no lock profiling, just use regular locks
#define PROFILE_LOCK(mutex, name) \
    for (int _locked_ = 1; _locked_; _locked_ = 0)

#endif // CXY_PARALLEL_COMPILE

// ============================================================================
// Profiling Control
// ============================================================================

/**
 * Enable profiling globally.
 * When disabled, all profiling functions become no-ops.
 * 
 * Default: Disabled
 */
void profileEnable(void);

/**
 * Disable profiling globally.
 * Existing timers will continue to work but no new data is collected.
 */
void profileDisable(void);

/**
 * Check if profiling is currently enabled.
 * 
 * @return true if enabled, false otherwise
 */
bool profileIsEnabled(void);

/**
 * Reset all profiling data for the current thread.
 * Clears accumulated timings and call counts.
 */
void profileReset(void);

/**
 * Reset profiling data for all threads.
 * Should only be called when no profiling is active.
 */
void profileResetAll(void);

// ============================================================================
// Profiling Output
// ============================================================================

/**
 * Print profiling results for current thread to stdout.
 * 
 * Output format:
 * - Hierarchical display with indentation
 * - Shows calls, total time, average time, max time
 * - Lock contention analysis (wait vs hold time)
 * 
 * @param showDetails If true, show individual calls; if false, aggregates only
 */
void profilePrint(bool showDetails);

/**
 * Print profiling results for all threads.
 * Useful for parallel compilation profiling.
 * 
 * @param showDetails If true, show individual calls; if false, aggregates only
 */
void profilePrintAll(bool showDetails);

/**
 * Print profiling results to a file.
 * 
 * @param filename Output file path
 * @param showDetails If true, show individual calls; if false, aggregates only
 * @return true on success, false on error
 */
bool profilePrintToFile(const char *filename, bool showDetails);

/**
 * Export profiling results to JSON format.
 * Creates a structured JSON file with all profiling data including:
 * - Entry names, timings (total, min, max, avg)
 * - Call counts
 * - Nesting depth
 * - Lock profiling data (wait time vs hold time)
 * 
 * JSON schema:
 * {
 *   "profilingEnabled": bool,
 *   "threadId": int,
 *   "entryCount": int,
 *   "entries": [
 *     {
 *       "name": string,
 *       "totalNs": uint64,
 *       "count": uint64,
 *       "minNs": uint64,
 *       "maxNs": uint64,
 *       "avgNs": uint64,
 *       "depth": int,
 *       "isLockProfile": bool,
 *       "waitTimeNs": uint64,  // only if isLockProfile
 *       "holdTimeNs": uint64   // only if isLockProfile
 *     }
 *   ]
 * }
 * 
 * @param filename Output JSON file path
 * @return true on success, false on error or if profiling disabled
 */
bool profilePrintToJSON(const char *filename);

// ============================================================================
// Structured Profiling Data (for analysis tools)
// ============================================================================

/**
 * Profiling entry containing aggregated statistics.
 */
typedef struct ProfileEntry {
    const char *name;       // Entry name (e.g., "parseFile")
    uint64_t totalNs;       // Total time spent in nanoseconds
    uint64_t count;         // Number of calls
    uint64_t minNs;         // Minimum duration
    uint64_t maxNs;         // Maximum duration
    uint64_t avgNs;         // Average duration (computed)
    int depth;              // Nesting level (0 = top level)
    
    // Lock-specific metrics (0 if not a lock profile)
    uint64_t waitTimeNs;    // Time waiting to acquire lock
    uint64_t holdTimeNs;    // Time holding lock
    bool isLockProfile;     // True if this is lock profiling data
} ProfileEntry;

/**
 * Get all profile entries for current thread.
 * Returns array of entries, terminated by entry with name=NULL.
 * 
 * The returned pointer is valid until next profileReset() or profileResetAll().
 * 
 * @return Array of ProfileEntry, NULL-terminated
 */
const ProfileEntry *profileGetEntries(void);

/**
 * Get profile entries for a specific thread.
 * 
 * @param threadId Thread ID (0 for main thread)
 * @return Array of ProfileEntry, NULL-terminated, or NULL if thread not found
 */
const ProfileEntry *profileGetEntriesForThread(int threadId);

/**
 * Get the number of profile entries for current thread.
 * 
 * @return Number of entries
 */
size_t profileGetEntryCount(void);

// ============================================================================
// Internal Structures (exposed for macro implementation)
// ============================================================================

/**
 * ProfileBlock - Used by PROFILE_SCOPE and PROFILE_SECTION macros.
 * Do not use directly; use the macros instead.
 */
struct ProfileBlock {
    const char *name;
    struct timespec start;
    bool active;
    int savedDepth;
};

/**
 * Internal: Start a profile block.
 * Used by PROFILE_SCOPE and PROFILE_SECTION macros.
 */
ProfileBlock profileBlockStart(const char *name);

/**
 * Internal: Check if profile block is active.
 * Used by PROFILE_SECTION macro.
 */
bool profileBlockActive(ProfileBlock *block);

/**
 * Internal: End a profile block.
 * Used by PROFILE_SECTION macro.
 */
void profileBlockEnd(ProfileBlock *block);

/**
 * Internal: Cleanup function for profile block.
 * Used by PROFILE_SCOPE macro with __attribute__((cleanup)).
 */
void profileBlockCleanup(ProfileBlock *block);

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * Convert nanoseconds to human-readable string.
 * 
 * @param ns Nanoseconds
 * @param buffer Output buffer (at least 32 bytes)
 * @return Pointer to buffer
 * 
 * Example: formatNanoseconds(1500000000, buf) -> "1.50s"
 */
const char *formatNanoseconds(uint64_t ns, char *buffer);

/**
 * Get current time in nanoseconds since epoch.
 * Used internally but exposed for custom timing.
 */
uint64_t getCurrentNanoseconds(void);

#ifdef __cplusplus
}
#endif