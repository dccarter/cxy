/**
 * Synchronization Primitives Implementation
 * 
 * POSIX threads (pthread) based implementation for cross-platform support.
 */

#include "synchronization.h"

#if CXY_PARALLEL_COMPILE

#include "alloc.h"
#include "utils.h"

#include <pthread.h>
#include <errno.h>
#include <unistd.h>

// ============================================================================
// Internal Structures
// ============================================================================

struct Mutex {
    pthread_mutex_t handle;
};

struct CondVar {
    pthread_cond_t handle;
};

struct Thread {
    pthread_t handle;
    bool detached;
};

// ============================================================================
// Mutex Implementation
// ============================================================================

Mutex *createMutex(void)
{
    Mutex *mutex = mallocOrDie(sizeof(Mutex));
    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
}

void destroyMutex(Mutex *mutex)
{
    if (mutex) {
        pthread_mutex_destroy(&mutex->handle);
        free(mutex);
    }
}

void lockMutex(Mutex *mutex)
{
    csAssert(mutex, "lockMutex called with NULL mutex");
    int result = pthread_mutex_lock(&mutex->handle);
    csAssert(result == 0, "pthread_mutex_lock failed");
}

void unlockMutex(Mutex *mutex)
{
    csAssert(mutex, "unlockMutex called with NULL mutex");
    int result = pthread_mutex_unlock(&mutex->handle);
    csAssert(result == 0, "pthread_mutex_unlock failed");
}

bool tryLockMutex(Mutex *mutex)
{
    csAssert(mutex, "tryLockMutex called with NULL mutex");
    return pthread_mutex_trylock(&mutex->handle) == 0;
}

// ============================================================================
// Condition Variable Implementation
// ============================================================================

CondVar *createCondVar(void)
{
    CondVar *cv = mallocOrDie(sizeof(CondVar));
    if (pthread_cond_init(&cv->handle, NULL) != 0) {
        free(cv);
        return NULL;
    }
    return cv;
}

void destroyCondVar(CondVar *cv)
{
    if (cv) {
        pthread_cond_destroy(&cv->handle);
        free(cv);
    }
}

void waitCondVar(CondVar *cv, Mutex *mutex)
{
    csAssert(cv, "waitCondVar called with NULL condition variable");
    csAssert(mutex, "waitCondVar called with NULL mutex");
    int result = pthread_cond_wait(&cv->handle, &mutex->handle);
    csAssert(result == 0, "pthread_cond_wait failed");
}

void signalCondVar(CondVar *cv)
{
    csAssert(cv, "signalCondVar called with NULL condition variable");
    int result = pthread_cond_signal(&cv->handle);
    csAssert(result == 0, "pthread_cond_signal failed");
}

void broadcastCondVar(CondVar *cv)
{
    csAssert(cv, "broadcastCondVar called with NULL condition variable");
    int result = pthread_cond_broadcast(&cv->handle);
    csAssert(result == 0, "pthread_cond_broadcast failed");
}

// ============================================================================
// Thread Implementation
// ============================================================================

Thread *createThread(ThreadFunc func, void *arg)
{
    csAssert(func, "createThread called with NULL function");
    
    Thread *thread = mallocOrDie(sizeof(Thread));
    thread->detached = false;
    
    if (pthread_create(&thread->handle, NULL, func, arg) != 0) {
        free(thread);
        return NULL;
    }
    
    return thread;
}

void *joinThread(Thread *thread)
{
    csAssert(thread, "joinThread called with NULL thread");
    csAssert(!thread->detached, "Cannot join a detached thread");
    
    void *result = NULL;
    int status = pthread_join(thread->handle, &result);
    csAssert(status == 0, "pthread_join failed");
    
    free(thread);
    return result;
}

void detachThread(Thread *thread)
{
    csAssert(thread, "detachThread called with NULL thread");
    csAssert(!thread->detached, "Thread already detached");
    
    int status = pthread_detach(thread->handle);
    csAssert(status == 0, "pthread_detach failed");
    
    thread->detached = true;
    free(thread);
}

int getCpuCount(void)
{
#ifdef _SC_NPROCESSORS_ONLN
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return (count > 0) ? (int)count : 1;
#else
    // Fallback to single core if detection not available
    return 1;
#endif
}

#endif // CXY_PARALLEL_COMPILE