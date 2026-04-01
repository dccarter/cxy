/**
 * Synchronization Primitives for Parallel Compilation
 * 
 * This file provides cross-platform wrappers around threading primitives.
 * Currently implemented using POSIX threads (pthread).
 * 
 * Only compiled when CXY_PARALLEL_COMPILE is enabled.
 */

#pragma once

#include "core/features.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CXY_PARALLEL_COMPILE

#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct Mutex Mutex;
typedef struct CondVar CondVar;
typedef struct Thread Thread;

// Thread function pointer type
typedef void *(*ThreadFunc)(void *arg);

// ============================================================================
// Mutex - Mutual Exclusion Lock
// ============================================================================

/**
 * Create a new mutex.
 * Returns NULL on failure.
 */
Mutex *createMutex(void);

/**
 * Destroy a mutex.
 * The mutex must not be locked when destroyed.
 */
void destroyMutex(Mutex *mutex);

/**
 * Lock the mutex. Blocks if already locked by another thread.
 */
void lockMutex(Mutex *mutex);

/**
 * Unlock the mutex.
 * Must only be called by the thread that locked it.
 */
void unlockMutex(Mutex *mutex);

/**
 * Try to lock the mutex without blocking.
 * Returns true if lock acquired, false if already locked.
 */
bool tryLockMutex(Mutex *mutex);

// ============================================================================
// Condition Variable - Thread Signaling
// ============================================================================

/**
 * Create a new condition variable.
 * Returns NULL on failure.
 */
CondVar *createCondVar(void);

/**
 * Destroy a condition variable.
 * No threads should be waiting on it when destroyed.
 */
void destroyCondVar(CondVar *cv);

/**
 * Wait on a condition variable.
 * The mutex must be locked before calling this.
 * The mutex is atomically unlocked while waiting, then re-locked on wakeup.
 * 
 * @param cv The condition variable to wait on
 * @param mutex The associated mutex (must be locked by caller)
 */
void waitCondVar(CondVar *cv, Mutex *mutex);

/**
 * Wake up one thread waiting on the condition variable.
 */
void signalCondVar(CondVar *cv);

/**
 * Wake up all threads waiting on the condition variable.
 */
void broadcastCondVar(CondVar *cv);

// ============================================================================
// Thread - OS Thread Management
// ============================================================================

/**
 * Create and start a new thread.
 * 
 * @param func The function to run in the new thread
 * @param arg The argument to pass to the function
 * @return Thread handle, or NULL on failure
 */
Thread *createThread(ThreadFunc func, void *arg);

/**
 * Wait for a thread to complete and retrieve its return value.
 * The thread handle is freed after joining.
 * 
 * @param thread The thread to wait for
 * @return The value returned by the thread function
 */
void *joinThread(Thread *thread);

/**
 * Detach a thread, allowing it to run independently.
 * Resources will be freed automatically when the thread exits.
 * Cannot join a detached thread.
 * 
 * @param thread The thread to detach
 */
void detachThread(Thread *thread);

/**
 * Get the number of CPU cores available on this system.
 * Useful for determining optimal worker thread count.
 * 
 * @return Number of CPU cores, or 1 if detection fails
 */
int getCpuCount(void);

// ============================================================================
// Atomic Operations (if needed in future)
// ============================================================================

// TODO: Add atomic int/bool operations if needed for lock-free structures

#endif // CXY_PARALLEL_COMPILE

#ifdef __cplusplus
}
#endif