# Phase 0: Preparation for Parallel Compilation

## Goal

Prepare the Cxy codebase for parallel compilation by auditing global state, documenting thread-safety, and implementing feature flags. This phase makes NO functional changes - existing code continues to work exactly as before.

## Architecture Summary

### Memory Management Strategy

**Two-Tier MemPool Approach**:
1. **Global MemPool** (shared, no mutex needed):
   - Owned by CompilerDriver
   - Used by StrPool for string allocations
   - Used before parsing (initialization)
   - Used after parsing (compilation stages)
   - Single-threaded access during these phases

2. **Worker MemPools** (per-thread, no mutex needed):
   - Each worker thread gets its own MemPool
   - Used only for AST node allocations during parsing
   - Workers own their pools exclusively
   - No synchronization required
   - ASTs reference their origin worker pool

### String Pool Strategy

**Shared StrPool with Conditional Mutex**:
- ONE StrPool shared by all workers (backed by global MemPool)
- Mutex protection ENABLED during parallel parsing phase
- Mutex protection DISABLED after parsing (sequential compilation)
- Ensures string uniqueness across entire compilation
- String comparison by pointer equality works correctly

**Why shared?** Cxy relies on string pointer equality for comparisons. All strings must be interned in the same pool to maintain uniqueness.

**Lock contention?** Yes, but acceptable because:
- String interning is infrequent (identifiers, literals only)
- Lock held briefly (hash lookup + insert)
- Correctness (uniqueness) is more important than max speed
- Still expect 2-3x speedup from parallel parsing

### Worker Thread Model

```
WorkerContext {
    localPool: MemPool       // For AST allocations (no lock, worker-owned)
    sharedStrings: StrPool   // Points to driver->strings (with mutex)
    driver: CompilerDriver   // Reference to shared state
}
```

Each worker:
- Allocates AST nodes from its local pool (fast, no contention)
- Interns strings via shared StrPool (locks, but infrequent)
- Reports parsed files back to controller

## Current State Analysis

### Lexer (src/cxy/lang/frontend/lexer.c)

**Status**: ✅ Already thread-safe

**Analysis**:
- All state stored in `Lexer` struct (instance-based)
- No global variables found
- Keywords stored in HashTable, but it's read-only after initialization
- Each worker can have its own Lexer instance

**Conclusion**: No changes needed for parallel compilation.

### Parser (src/cxy/lang/frontend/parser.c)

**Status**: ✅ Already thread-safe

**Analysis**:
- All state stored in `Parser` struct (instance-based)
- Uses local MemPool and StrPool (passed in)
- No global variables found
- Look-ahead buffer is per-instance

**Conclusion**: No changes needed for parallel compilation.

### MemPool (src/cxy/core/mempool.c)

**Status**: ✅ Thread-safe (instance-based)

**Analysis**:
- All state in `MemPool` struct
- Block-based allocator with internal state
- No global variables
- Currently one pool per CompilerDriver

**Plan**: Two-tier memory pool strategy
1. **Global MemPool** (shared):
   - Used for StrPool allocations (strings must be in shared memory)
   - Used before parsing starts (initialization)
   - Used after parsing completes (compilation stages)
   - No synchronization needed (StrPool has its own lock)

2. **Worker MemPool** (per-thread):
   - Each worker thread gets its own MemPool instance
   - Used for AST node allocations during parsing
   - No synchronization needed (each worker owns its pool)
   - Pools remain separate after parsing (ASTs reference their origin pool)

This approach minimizes lock contention (only StrPool is shared).

### StrPool (src/cxy/core/strpool.c)

**Status**: ⚠️ Needs synchronization

**Analysis**:
- StrPool contains a HashTable for string interning
- Strings in AST are just `const char*` pointers
- String comparison in Cxy is done by POINTER EQUALITY (not strcmp)
- Therefore: strings MUST be unique across entire compilation

**Problem**: Multiple threads inserting into same HashTable = race condition.

**Solution**: Shared StrPool with mutex protection during parsing
- One global StrPool shared by all workers (backed by global MemPool)
- Add mutex to StrPool for thread-safe insertion
- Mutex enabled ONLY during parallel parsing phase
- After parsing complete: disable mutex (sequential compilation)
- String uniqueness maintained across entire compilation

**Implementation**:
```c
typedef struct StrPool {
    MemPool *pool;        // Points to global pool
    HashTable table;
    Mutex *lock;          // NULL when not needed, non-NULL during parallel parsing
} StrPool;

// During parsing setup
strPool->lock = createMutex();  // Enable protection

// During string insertion (makeString, etc.)
if (strPool->lock) lockMutex(strPool->lock);
// ... insert into hash table
if (strPool->lock) unlockMutex(strPool->lock);

// After parsing complete
destroyMutex(strPool->lock);
strPool->lock = NULL;  // Disable for sequential compilation
```

**Lock Contention**: Yes, workers will contend on StrPool lock, but:
- String interning is relatively infrequent (identifiers, literals)
- Lock is only held briefly (hash lookup + insert)
- Correctness (string uniqueness) is more important than max speed
- Still expect 2-3x speedup from parallel parsing

### Global Variables Found

#### 1. gProfileDepth (src/cxy/driver/stats.c)

```c
int gProfileDepth = 0;
```

**Purpose**: Tracks import nesting depth for profiling indentation output.

**Problem**: Shared mutable state across threads.

**Solution**: Make thread-local
```c
#ifdef CXY_PARALLEL_COMPILE
__thread int gProfileDepth = 0;
#else
int gProfileDepth = 0;
#endif
```

**Files to modify**:
- src/cxy/driver/stats.h (extern declaration)
- src/cxy/driver/stats.c (definition)

#### 2. CXY_pluginsDir (src/cxy/driver/plugin.c)

```c
static cstring CXY_pluginsDir = NULL;
```

**Purpose**: Cached plugin directory path.

**Problem**: Lazy initialization pattern not thread-safe.

**Solution**: Move to CompilerDriver struct
```c
// In driver.h
typedef struct CompilerDriver {
    // ... existing fields
    cstring pluginsDir;  // Add this
} CompilerDriver;

// In plugin.c
cstring getPluginsDir(CompilerDriver *driver) {
    if (!driver->pluginsDir) {
        driver->pluginsDir = computePluginsDir();
    }
    return driver->pluginsDir;
}
```

**Files to modify**:
- src/cxy/driver/driver.h (add field)
- src/cxy/driver/plugin.c (refactor function)
- All callsites of getPluginsDir() (pass driver)

### CompilerDriver Structure

```c
typedef struct CompilerDriver {
    MemPool *pool;           // Global MemPool (for StrPool + compilation)
    StrPool *strings;        // Shared StrPool (with mutex during parsing)
    HashTable moduleCache;   // ⚠️ Will need mutex protection
    HashTable nativeSources; // ⚠️ Will need mutex protection
    HashTable linkLibraries; // ⚠️ Will need mutex protection
    Log *L;                  // ✅ Already thread-safe
    // ... other fields
} CompilerDriver;

// Worker threads will have:
typedef struct WorkerContext {
    int workerId;
    MemPool *localPool;      // Worker-local for AST allocations
    StrPool *sharedStrings;  // Reference to driver->strings (shared, locked)
    Log *log;                // Reference to driver->L (shared, thread-safe)
    CompilerDriver *driver;  // Reference to main driver
} WorkerContext;
```

**Analysis**:
- `pool`: Global MemPool remains in CompilerDriver, used by StrPool
- `strings`: Shared across all workers, protected by internal mutex during parsing
- `moduleCache`: Will become parsed files map, needs lock for concurrent access
- `nativeSources`: Populated during parsing, needs lock
- `linkLibraries`: Populated during parsing, needs lock
- Workers allocate AST nodes from their local pool, strings from shared pool

**Phase 0**: Document this, no changes yet.

### Compilation Stages (src/cxy/driver/stages.c)

**Status**: ✅ Sequential (not parallelized in Phase 0)

**Analysis**:
- All compilation stages run sequentially
- Symbol tables, type checking, codegen are NOT thread-safe
- These are ONLY used during compilation, not parsing

**Conclusion**: Phase 0 doesn't touch these. Compilation remains sequential.

## Feature Flag Design

### File: src/cxy/core/features.h

```c
#pragma once

/**
 * CXY_PARALLEL_COMPILE - Enable parallel compilation architecture
 * 
 * When enabled:
 * - Parsing happens in parallel across worker threads
 * - Each worker has its own MemPool for AST allocations
 * - All workers share the global StrPool (with mutex protection)
 * - Compilation remains sequential (builtins → leaves → root)
 * 
 * Memory Pool Strategy:
 * - Global MemPool: Used by StrPool and compilation stages
 * - Worker MemPools: One per worker, for AST allocations
 * 
 * String Pool Strategy:
 * - Single shared StrPool with mutex during parsing
 * - Ensures string uniqueness (pointer equality works)
 * - Mutex disabled after parsing (sequential compilation)
 * 
 * Default: OFF (to be enabled incrementally in Phase 4)
 */
// #define CXY_PARALLEL_COMPILE 1

#ifdef CXY_PARALLEL_COMPILE
  #define CXY_THREAD_SAFE 1
  #define CXY_USE_WORKER_POOLS 1
#else
  #define CXY_THREAD_SAFE 0
  #define CXY_USE_WORKER_POOLS 0
#endif

// Conditional compilation helpers
#if CXY_PARALLEL_COMPILE
  #define IF_PARALLEL(code) code
  #define IF_SEQUENTIAL(code)
#else
  #define IF_PARALLEL(code)
  #define IF_SEQUENTIAL(code) code
#endif
```

### Usage Pattern

```c
#include "core/features.h"

// Example 1: Thread-local storage
#ifdef CXY_PARALLEL_COMPILE
__thread int gProfileDepth = 0;
#else
int gProfileDepth = 0;
#endif

// Example 2: Conditional code paths
void compileEntryFile(const char *path, CompilerDriver *driver) {
    IF_PARALLEL(
        // New: parallel parse all files first
        ParseController *pc = createParseController(driver);
        parseAllFiles(pc, path);
        ImportGraph *graph = buildImportGraph(pc);
        compileInOrder(graph);
    )
    
    IF_SEQUENTIAL(
        // Old: depth-first recursive
        compileFile(path, driver);
    )
}
```

### CMake Integration (Optional)

```cmake
option(CXY_PARALLEL_COMPILE "Enable parallel compilation" OFF)

if(CXY_PARALLEL_COMPILE)
    target_compile_definitions(cxy PRIVATE CXY_PARALLEL_COMPILE=1)
endif()
```

## Thread-Safety Annotation Standard

### Format

```c
/**
 * Thread-Safety: [SAFE | UNSAFE | CONDITIONAL]
 * Context: [Brief description]
 * Parallel-Phase: [PARSING | COMPILATION | N/A]
 * Global-State: [YES | NO]
 * 
 * Notes: [Additional context if needed]
 */
```

### Examples

```c
/**
 * Thread-Safety: SAFE
 * Context: Each thread uses separate Lexer instance
 * Parallel-Phase: PARSING
 * Global-State: NO
 */
Token advanceLexer(Lexer *lexer);

/**
 * Thread-Safety: UNSAFE
 * Context: Modifies global gProfileDepth
 * Parallel-Phase: PARSING
 * Global-State: YES
 * 
 * Notes: TODO(parallel) Make thread-local in Phase 0
 */
void incrementProfileDepth(void);

/**
 * Thread-Safety: CONDITIONAL
 * Context: Safe if each thread has separate MemPool instance
 * Parallel-Phase: PARSING
 * Global-State: NO
 */
void *allocFromMemPool(MemPool *pool, size_t size);
```

## Implementation Plan

### Task 0: Create Profiling Framework (4 hours) ✅ COMPLETED

**Files**:
- `src/cxy/driver/profiling.h` (new file) ✅
- `src/cxy/driver/profiling.c` (new file) ✅
- `docs/PROFILING-EXAMPLES.md` (new file) ✅

**Steps**:
1. ✅ Created comprehensive profiling API with manual timers
2. ✅ Implemented PROFILE_SCOPE and PROFILE_SECTION macros
3. ✅ Added PROFILE_LOCK for lock contention analysis
4. ✅ Thread-local storage for parallel compilation support
5. ✅ Hierarchical profiling with automatic nesting
6. ✅ Added profiling.c to CMakeLists.txt
7. ✅ Build succeeds with new profiling framework
8. ✅ Added ProfileMode enum (NONE|STDOUT|JSON) to options.h
9. ✅ Implemented profilePrintToJSON() for custom visualizers
10. ✅ Updated --profile CLI option to accept modes
11. ✅ Integrated JSON export into driver.c compilation flow
12. ✅ Added path abbreviation for console output (max 35 chars)
13. ✅ Rewrote profile_analyze.rb to consume JSON with 4 visualization modes

**Features**:
- Manual timers: profileStart/profileStop
- Automatic scope profiling: PROFILE_SCOPE(name)
- Code block profiling: PROFILE_SECTION(name)
- Lock profiling: PROFILE_LOCK(mutex, name)
- Structured output with call counts, averages, min/max
- Lock contention analysis (wait vs hold time)
- Thread-aware profiling
- Export to file or programmatic access
- JSON export: profilePrintToJSON() for custom visualizers
- CLI integration: --profile=NONE|STDOUT|JSON
- Path abbreviation: Long paths abbreviated for console (e.g., `.../cxy/examples/hello.cxy`)
- Full paths preserved in JSON output for tooling
- Analysis tool: profile_analyze.rb with 4 modes (table, heatmap, timeline, locks)
- Heat maps: Visual representation of compilation hotspots
- Timeline view: Hierarchical visualization of compilation flow
- Lock analysis: Deep dive into parallel compilation contention

**Verification**: 
```bash
cmake --build build  # ✅ PASSED
```

### Task 1: Create Feature Flag Infrastructure (2 hours) ✅ COMPLETED

**Files**:
- `src/cxy/core/features.h` (new file) ✅
- `src/cxy/core/synchronization.h` (new file) ✅
- `src/cxy/core/synchronization.c` (new file) ✅

**Steps**:
1. ✅ Created features.h with CXY_PARALLEL_COMPILE flag (default OFF)
2. ✅ Created synchronization primitives (Mutex, CondVar, Thread)
3. ✅ Added synchronization.c to CMakeLists.txt
4. ✅ Build succeeds with new files

**Verification**: 
```bash
cmake --build build  # ✅ PASSED
```

**Notes**: 
- Synchronization API uses POSIX threads (pthread)
- All primitives guarded by `#if CXY_PARALLEL_COMPILE`
- Includes: createMutex/lockMutex, createCondVar/waitCondVar, createThread/joinThread
- getCpuCount() helper for determining worker count

### Task 2: Remove gProfileDepth (2 hours) ✅ COMPLETED

**Files**:
- `src/cxy/driver/stats.h` ✅
- `src/cxy/driver/stats.c` ✅
- `src/cxy/driver/driver.c` ✅
- `src/cxy/driver/stages.c` ✅

**Changes**:

Instead of making `gProfileDepth` thread-local, we **removed it entirely** and replaced it with the new profiling framework:

In `stats.h`:
```c
// Removed: extern int gProfileDepth;
```

In `stats.c`:
```c
// Removed: int gProfileDepth = 0;
```

In `driver.c`:
```c
// Old approach:
gProfileDepth++;
// ... code ...
gProfileDepth--;

// New approach:
PROFILE_SCOPE(fileName);  // Automatic depth tracking
```

In `stages.c`:
```c
// Old approach:
printf("%*s  %-14s  %" PRIu64 " ms\n", gProfileDepth * 2, "", stageName, ms);

// New approach:
PROFILE_SECTION(stageName) {
    node = executor(driver, node);
}  // Automatic hierarchical profiling
```

**Benefits**:
- No global state anymore
- Automatic depth tracking via profiling framework
- Thread-safe by design (uses thread-local storage internally)
- Better structured profiling with PROFILE_SCOPE/PROFILE_SECTION

**Verification**: 
```bash
cmake --build build  # ✅ PASSED
```

### Task 3: Move CXY_pluginsDir to CompilerDriver (3 hours)

**Files**:
- `src/cxy/driver/driver.h`
- `src/cxy/driver/plugin.c`
- Any callsites (search for `getPluginsDir` or similar)

**Changes**:

In `driver.h`:
```c
typedef struct CompilerDriver {
    // ... existing fields
    cstring pluginsDir;  // Cached plugin directory path
} CompilerDriver;
```

In `plugin.c`:
```c
// Remove: static cstring CXY_pluginsDir = NULL;

cstring getPluginsDir(CompilerDriver *driver) {
    if (!driver->pluginsDir) {
        // Compute plugin directory
        driver->pluginsDir = /* computation logic */;
    }
    return driver->pluginsDir;
}
```

Update all callsites to pass `driver` parameter.

**Verification**: Build and test.

### Task 4: Add StrPool Mutex Support (3 hours)

**Files**:
- `src/cxy/core/strpool.h`
- `src/cxy/core/strpool.c`

**Goal**: Add mutex field to StrPool and protect string interning operations.

**Changes**:

In `strpool.h`:
```c
typedef struct StrPool {
    MemPool *pool;
    HashTable table;
    Mutex *lock;  // NULL when not needed, non-NULL during parallel parsing
} StrPool;

// Enable/disable mutex protection
void strPoolEnableThreadSafety(StrPool *pool);
void strPoolDisableThreadSafety(StrPool *pool);
```

In `strpool.c`:
```c
const char *makeString(StrPool *pool, const char *str) {
    if (pool->lock) lockMutex(pool->lock);
    
    // ... existing hash lookup and insertion logic
    
    if (pool->lock) unlockMutex(pool->lock);
    return result;
}

// Apply same pattern to:
// - makeTrimmedString
// - makeStringSized
// - makeAnonymousVariable
// - makeStringf
// - makeStringConcat_

void strPoolEnableThreadSafety(StrPool *pool) {
    if (!pool->lock) {
        pool->lock = createMutex();
    }
}

void strPoolDisableThreadSafety(StrPool *pool) {
    if (pool->lock) {
        destroyMutex(pool->lock);
        pool->lock = NULL;
    }
}
```

**Guard with feature flag**:
```c
#ifdef CXY_PARALLEL_COMPILE
    if (pool->lock) lockMutex(pool->lock);
#endif
    // ... string operation
#ifdef CXY_PARALLEL_COMPILE
    if (pool->lock) unlockMutex(pool->lock);
#endif
```

**Verification**: Build with and without CXY_PARALLEL_COMPILE, ensure no deadlocks.

### Task 5: Add Thread-Safety Annotations (6 hours)

**Files** (in priority order):
1. `src/cxy/lang/frontend/lexer.h` + `.c`
2. `src/cxy/lang/frontend/parser.h` + `.c`
3. `src/cxy/core/strpool.h` + `.c`
4. `src/cxy/core/mempool.h` + `.c`
5. `src/cxy/driver/driver.h` + `.c`
6. `src/cxy/driver/stats.h` + `.c`

**Process per file**:
1. Review all public functions
2. Determine thread-safety status
3. Add documentation comment above each function
4. Note any assumptions or conditions

**Example for lexer.h**:
```c
/**
 * Thread-Safety: SAFE
 * Context: Creates independent lexer instance
 * Parallel-Phase: PARSING
 * Global-State: NO
 */
Lexer newLexer(const char *fileName, const char *fileData, 
               size_t fileSize, Log *log);

/**
 * Thread-Safety: SAFE
 * Context: Operates only on provided lexer instance
 * Parallel-Phase: PARSING
 * Global-State: NO
 */
Token advanceLexer(Lexer *lexer);
```

### Task 6: Document Shared Data Structures (2 hours)

Create inline documentation in driver.h explaining what needs protection:

```c
typedef struct CompilerDriver {
    Options options;
    MemPool *pool;              // Global pool (for StrPool + compilation)
    StrPool *strings;           // TODO(parallel): Add internal mutex for parsing phase
    HashTable moduleCache;      // TODO(parallel): Protect with mutex
    HashTable nativeSources;    // TODO(parallel): Protect with mutex
    HashTable linkLibraries;    // TODO(parallel): Protect with mutex
    Log *L;                     // Thread-safe (internal mutex)
    TypeTable *types;           // Read-only after initialization
    // ...
} CompilerDriver;

/**
 * Worker threads use WorkerContext (Phase 1+):
 * - localPool: MemPool for AST allocations (worker-owned, no sync needed)
 * - sharedStrings: Reference to driver->strings (shared, mutex-protected)
 * - driver: Reference to main driver (for accessing shared state)
 */
```

### Task 7: Write Current Flow Documentation (2 hours)

Document how compilation currently works in comments at top of driver.c:

```c
/**
 * CURRENT COMPILATION FLOW (Sequential)
 * ======================================
 * 
 * 1. compileFile(main.cxy)
 *    - parseFile() → AST
 *    - compileProgram()
 *      - Stage: Import Resolution
 *        → For each import: compileModule() [RECURSIVE]
 *          → parseFile(import.cxy)
 *          → compileProgram(import) [NESTED]
 *      - Stage: Symbol Check
 *      - Stage: Type Check
 *      - Stage: Code Generation
 * 
 * Key characteristics:
 * - Depth-first import resolution
 * - Parse and compile are interleaved
 * - Single-threaded execution
 * - Module cache prevents duplicate work
 * 
 * PARALLEL COMPILATION FLOW (Future, Phase 4+)
 * ============================================
 * 
 * Phase 1: PARSE ALL FILES (Parallel)
 * - Parse entry file, collect imports
 * - Dispatch imports to worker pool
 * - Workers parse and return AST + more imports
 *   - Each worker uses its own MemPool for AST nodes
 *   - All workers share StrPool (with mutex) for string interning
 * - Repeat until all imports discovered
 * 
 * Phase 2: COMPILE IN ORDER (Sequential)
 * - Disable StrPool mutex (no more parallel access)
 * - Build import dependency graph
 * - Topological sort (builtins → leaves → root)
 * - Compile each file in order
 */
```

### Task 8: Testing and Verification (4 hours)

**Test Plan**:

1. **Baseline Tests** (no feature flag)
   ```bash
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```
   All tests must pass.

2. **With Feature Flag** (stub enabled)
   ```bash
   # Edit features.h, uncomment CXY_PARALLEL_COMPILE
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```
   All tests must still pass (no functional changes yet).

3. **Global Variable Tests**
   - Verify gProfileDepth works in both modes
   - Verify plugins load correctly with new code
   - Check profiling output still correct

4. **Documentation Review**
   - All annotations added
   - No functions missed
   - Documentation is accurate

## Deliverables Checklist

- [x] `src/cxy/driver/profiling.h` created with JSON export support
- [x] `src/cxy/driver/profiling.c` created with profilePrintToJSON()
- [x] `docs/PROFILING-EXAMPLES.md` created with JSON examples
- [x] Profiling framework added to CMakeLists.txt
- [x] ProfileMode enum added (NONE|STDOUT|JSON)
- [x] --profile CLI option updated to accept modes
- [x] JSON export integrated into compilation flow
- [x] `src/tools/profile_analyze.rb` rewritten for JSON input
- [x] Analysis tool supports 4 visualization modes (table/heatmap/timeline/locks)
- [x] `src/cxy/core/features.h` created
- [x] `src/cxy/core/synchronization.h` created
- [x] `src/cxy/core/synchronization.c` created
- [x] Synchronization primitives added to CMakeLists.txt
- [x] `gProfileDepth` removed entirely (replaced with PROFILE_SCOPE)
- [ ] `CXY_pluginsDir` moved to CompilerDriver
- [ ] StrPool mutex support added (enable/disable functions)
- [ ] Thread-safety annotations added to 6 key files
- [ ] CompilerDriver fields documented with TODO(parallel) notes
- [ ] Current compilation flow documented in driver.c
- [ ] All tests pass with feature flag OFF
- [ ] All tests pass with feature flag ON
- [ ] Zero functional regressions

## Success Criteria

Phase 0 is complete when:

1. ✅ All global state identified and documented
2. ✅ Global variables eliminated or made thread-local
3. ✅ Thread-safety status known for all parsing components
4. ✅ Feature flag infrastructure allows parallel development
5. ✅ No regressions in current functionality
6. ✅ Clear path to Phase 1 (data structures)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Missed global state | High | Thorough grep/search, code review |
| Thread-local storage not supported | High | Check compiler (GCC/Clang support `__thread`) |
| Feature flags break build | Medium | Test both paths, add to CI |
| Annotations incomplete | Low | Systematic file-by-file review |
| Plugin directory refactor breaks code | Medium | Thorough testing of plugin loading |

## Estimated Time

- Task 0 (Profiling framework): 4 hours ✅ COMPLETED
- Task 1 (Feature flags): 2 hours ✅ COMPLETED
- Task 2 (Remove gProfileDepth): 2 hours ✅ COMPLETED
- Task 3 (CXY_pluginsDir): 3 hours
- Task 4 (StrPool mutex): 3 hours
- Task 5 (Annotations): 6 hours
- Task 6 (Documentation): 2 hours
- Task 7 (Flow docs): 2 hours
- Task 8 (Testing): 4 hours

**Total: ~28 hours (~3.5 days)**
**Completed: ~8 hours**
**Remaining: ~20 hours**

## Next Phase Preview

Once Phase 0 is complete, Phase 1 will:
- Define data structures (ParseController, ImportGraph, WorkerPool)
- Implement graph building (sequential, no threads yet)
- Refactor compileFile() to separate parse from compile
- Keep everything working (stubs for worker pool)

Phase 0 lays the foundation. Phase 1 builds the structure. Phase 4 adds parallelism.