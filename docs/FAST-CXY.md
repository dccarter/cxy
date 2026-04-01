# FAST-CXY: Parallel Compilation Architecture

## Overview

This document describes a new parallel compilation architecture for Cxy that separates the parsing and compilation phases to improve build times. The key insight is that parsing can be parallelized across multiple files while maintaining correct dependency ordering during compilation.

## Current Approach: Sequential Parse-Compile

Currently, Cxy uses a depth-first, recursive compilation model:

```
Entry File (main.cxy)
    |
    +--> Parse main.cxy
    |
    +--> See import "foo.cxy"
    |       |
    |       +--> Parse foo.cxy
    |       |
    |       +--> See import "bar.cxy"
    |       |       |
    |       |       +--> Parse bar.cxy
    |       |       |
    |       |       +--> Compile bar.cxy
    |       |
    |       +--> Compile foo.cxy
    |
    +--> Compile main.cxy
```

**Problems:**
- Parsing is entirely sequential (one file at a time)
- Deep import chains block progress
- No opportunity for parallelism
- Compilation must wait for all nested parsing to complete

## Proposed Approach: Parse-All-Then-Compile

The new architecture separates parsing from compilation:

```
┌─────────────────────────────────────────────────────────────┐
│                     PHASE 1: PARALLEL PARSING               │
│                                                             │
│   Controller                                                │
│   ┌────────┐                                                │
│   │ Entry  │──────┐                                         │
│   │  File  │      │                                         │
│   └────────┘      │                                         │
│        │          │                                         │
│        v          v                                         │
│   ┌─────────────────────┐         ┌──────────────┐          │
│   │  Parse Work Queue   │────────>│   Worker 1   │          │
│   │                     │         └──────────────┘          │
│   │  - foo.cxy          │                 │                 │
│   │  - bar.cxy          │         ┌──────────────┐          │
│   │  - baz.cxy          │────────>│   Worker 2   │          │
│   │  - ...              │         └──────────────┘          │
│   └─────────────────────┘                 │                 │
│            ^                       ┌──────────────┐         │
│            │                       │   Worker N   │         │
│            │                       └──────────────┘         │
│            │                               │                │
│            └───────────────────────────────┘                │
│                    (Discovered imports fed back)            │
│                                                             │
│   Result: Parsed ASTs + Import Graph                        │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                  PHASE 2: SEQUENTIAL COMPILATION            │
│                                                             │
│   Import Dependency Graph:                                  │
│                                                             │
│        main.cxy                                             │
│          /   \                                              │
│       foo.cxy  bar.cxy                                      │
│         |        |                                          │
│       baz.cxy  utils.cxy                                    │
│                                                             │
│   Compilation Order (builtins → leaves → root):             │
│                                                             │
│   1. builtins                                               │
│   2. baz.cxy      (leaf)                                    │
│   3. utils.cxy    (leaf)                                    │
│   4. foo.cxy      (depends on baz.cxy)                      │
│   5. bar.cxy      (depends on utils.cxy)                    │
│   6. main.cxy     (depends on foo.cxy, bar.cxy)             │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

## Architecture Components

### 1. Parse Controller

The controller orchestrates parallel parsing:

```
┌────────────────────────────────────────────────────┐
│              Parse Controller                      │
├────────────────────────────────────────────────────┤
│                                                    │
│  State:                                            │
│  - ParsedFiles:     Map[path → AST]                │
│  - PendingFiles:    Set[path]                      │
│  - InProgressFiles: Set[path]                      │
│  - ImportGraph:     Graph[path → [dependencies]]   │
│                                                    │
│  Operations:                                       │
│  - submitParseJob(path)                            │
│  - onParseComplete(path, ast, imports)             │
│  - isFullyParsed() → bool                          │
│  - buildCompilationOrder() → [path]                │
│                                                    │
└────────────────────────────────────────────────────┘
```

### 2. Parse Worker

Workers execute parsing in parallel:

```
┌────────────────────────────────────────────────────┐
│              Parse Worker                          │
├────────────────────────────────────────────────────┤
│                                                    │
│  Input:  File path                                 │
│                                                    │
│  Process:                                          │
│    1. Read file contents                           │
│    2. Tokenize (lexical analysis)                  │
│    3. Parse (syntax analysis)                      │
│    4. Collect import declarations                  │
│    5. Build AST (do NOT compile)                   │
│                                                    │
│  Output:                                           │
│    - AST for the file                              │
│    - List of import paths discovered               │
│    - Parse success/error status                    │
│                                                    │
└────────────────────────────────────────────────────┘
```

### 3. Import Graph

Tracks dependencies between modules:

```
   Import Graph Structure:

   Node: {
     path: string,
     ast: AST*,
     imports: [string],    // direct imports
     importedBy: [string], // reverse edges
     compiled: bool
   }

   Example:

   main.cxy ──imports──> foo.cxy ──imports──> baz.cxy
      │                     │
      └──imports──> bar.cxy ├──imports──> utils.cxy
                             │
                             └──imports──> stdio.h (C header)

   Leaves (no imports): [baz.cxy, utils.cxy, stdio.h]
```

## Detailed Flow

### Phase 1: Parallel Parsing

```
START
  │
  v
┌─────────────────────────────────────┐
│ Parse entry file (main.cxy)         │
│ - Create AST                        │
│ - Collect imports: [foo, bar]       │
└─────────────────────────────────────┘
  │
  v
┌─────────────────────────────────────┐
│ Mark entry as PARSED                │
│ Add imports to PENDING queue        │
│   PENDING: [foo.cxy, bar.cxy]       │
└─────────────────────────────────────┘
  │
  v
┌─────────────────────────────────────┐
│ While PENDING not empty:            │
│                                     │
│   1. Pop file from PENDING          │
│   2. Check if already PARSED        │
│   3. If not, mark IN_PROGRESS       │
│   4. Submit to worker pool          │
│                                     │
│ Worker returns:                     │
│   - AST                             │
│   - New imports: [baz, utils]       │
│                                     │
│   5. Mark file as PARSED            │
│   6. Add new imports to PENDING     │
│   7. Update import graph            │
└─────────────────────────────────────┘
  │
  v
┌─────────────────────────────────────┐
│ All files parsed?                   │
│ (PENDING empty && IN_PROGRESS empty)│
└─────────────────────────────────────┘
  │
  v
DONE (Phase 1)
```

### Phase 2: Sequential Compilation

```
START
  │
  v
┌─────────────────────────────────────┐
│ Build Import Graph from parsed ASTs │
│ - Nodes: all parsed files           │
│ - Edges: import relationships       │
└─────────────────────────────────────┘
  │
  v
┌─────────────────────────────────────┐
│ Topological Sort (dependency order) │
│ - Start from leaves (no imports)    │
│ - Work up to root (entry file)      │
└─────────────────────────────────────┘
  │
  v
┌──────────────────────────────────────┐
│ Compilation Order:                   │
│                                      │
│   Order = [                          │
│     builtins,        // always first │
│     leaf1,           // no imports   │
│     leaf2,           // no imports   │
│     intermediate1,   // depends on ↑ │
│     intermediate2,   // depends on ↑ │
│     root             // entry file   │
│   ]                                  │
└──────────────────────────────────────┘
  │
  v
┌─────────────────────────────────────┐
│ For each file in Order:             │
│   1. Compile AST to IR              │
│   2. Resolve symbols                │
│   3. Type checking                  │
│   4. Code generation                │
│   5. Mark as COMPILED               │
└─────────────────────────────────────┘
  │
  v
DONE (Phase 2)
```

## Data Structures

### ParsedFile

```c
typedef struct ParsedFile {
    const char* path;              // Normalized file path
    AstNode* ast;                  // Parsed AST (owned)
    StringArray imports;           // List of import paths
    ParseStatus status;            // PENDING | IN_PROGRESS | PARSED | FAILED
    uint64_t parseTimeNs;          // Profiling: parse duration
    Mutex* lock;                   // For status updates
} ParsedFile;
```

### ParseController

```c
typedef struct ParseController {
    // File tracking
    HashMap* parsedFiles;          // path → ParsedFile*
    HashSet* pendingPaths;         // Set of paths to parse
    HashSet* inProgressPaths;      // Set of paths being parsed
    
    // Worker management
    WorkerPool* workers;           // Thread pool for parsing
    Mutex* queueLock;              // Protects pending/inProgress
    ConditionVariable* workReady;  // Signals work available
    
    // Results
    ImportGraph* graph;            // Dependency graph
    AtomicInt activeWorkers;       // Number of busy workers
    AtomicBool failed;             // Set if any parse fails
    
    // Entry point
    const char* entryPath;         // Starting file
} ParseController;
```

### ImportGraph

```c
typedef struct ImportNode {
    const char* path;              // File path
    AstNode* ast;                  // Parsed AST (borrowed)
    StringArray imports;           // Direct imports
    StringArray importedBy;        // Reverse edges
    bool compiled;                 // Compilation status
    int level;                     // Distance from leaves (for sorting)
} ImportNode;

typedef struct ImportGraph {
    HashMap* nodes;                // path → ImportNode*
    StringArray leaves;            // Files with no imports
    StringArray compilationOrder;  // Topologically sorted
} ImportGraph;
```

### WorkerPool

```c
typedef struct ParseJob {
    const char* path;              // File to parse
    ParseController* controller;   // Back reference
} ParseJob;

typedef struct WorkerPool {
    int numWorkers;                // Configurable (default: CPU count)
    Thread* threads;               // Worker threads
    Queue* jobQueue;               // Pending parse jobs
    Mutex* queueLock;              // Protects job queue
    ConditionVariable* jobReady;   // Signals new job
    ConditionVariable* allDone;    // Signals queue empty
    AtomicBool shutdown;           // Shutdown signal
} WorkerPool;
```

## Algorithm Pseudocode

### Controller Main Loop

```python
def parseAll(entryPath):
    controller = ParseController()
    controller.entryPath = entryPath
    
    # Start workers
    controller.workers = WorkerPool(numCPUs)
    controller.workers.start()
    
    # Parse entry file in main thread (to start the process)
    entryFile = parseFile(entryPath)
    controller.parsedFiles[entryPath] = entryFile
    
    # Add entry imports to pending queue
    for imp in entryFile.imports:
        if imp not in controller.parsedFiles:
            controller.pendingPaths.add(imp)
    
    # Dispatch pending files to workers
    while not controller.isComplete():
        with controller.queueLock:
            if controller.pendingPaths:
                path = controller.pendingPaths.pop()
                controller.inProgressPaths.add(path)
                
                job = ParseJob(path, controller)
                controller.workers.submit(job)
        
        # Wait for workers to report back
        sleep_or_wait()
    
    # All parsing complete
    controller.workers.shutdown()
    
    # Build import graph
    graph = buildImportGraph(controller.parsedFiles)
    
    return graph
```

### Worker Thread

```python
def workerThread(pool):
    while not pool.shutdown:
        job = pool.getJob()  # Blocks until job available
        
        if job:
            try:
                # Parse the file
                ast = parse(job.path)
                imports = extractImports(ast)
                
                # Report back to controller
                onParseComplete(job.controller, job.path, ast, imports)
                
            except ParseError as e:
                onParseFailed(job.controller, job.path, e)
```

### Parse Complete Handler

```python
def onParseComplete(controller, path, ast, imports):
    with controller.queueLock:
        # Store result
        controller.parsedFiles[path] = ParsedFile(path, ast, imports)
        controller.inProgressPaths.remove(path)
        
        # Discover new imports
        for imp in imports:
            if imp not in controller.parsedFiles:
                if imp not in controller.pendingPaths:
                    if imp not in controller.inProgressPaths:
                        controller.pendingPaths.add(imp)
        
        # Signal that work may be available
        controller.workReady.signal()
```

### Build Import Graph

```python
def buildImportGraph(parsedFiles):
    graph = ImportGraph()
    
    # Create nodes
    for path, file in parsedFiles:
        node = ImportNode(path, file.ast, file.imports)
        graph.nodes[path] = node
    
    # Build reverse edges
    for path, node in graph.nodes:
        for imp in node.imports:
            if imp in graph.nodes:
                graph.nodes[imp].importedBy.append(path)
    
    # Find leaves (no imports)
    for path, node in graph.nodes:
        if len(node.imports) == 0:
            graph.leaves.append(path)
    
    # Topological sort
    graph.compilationOrder = topologicalSort(graph)
    
    return graph
```

### Topological Sort (Compilation Order)

```python
def topologicalSort(graph):
    order = []
    visited = set()
    
    # Helper: depth-first post-order traversal
    def visit(path):
        if path in visited:
            return
        visited.add(path)
        
        node = graph.nodes[path]
        
        # Visit dependencies first (imports)
        for imp in node.imports:
            if imp in graph.nodes:
                visit(imp)
        
        # Add this node after its dependencies
        order.append(path)
    
    # Start from leaves and work up
    for leaf in graph.leaves:
        visit(leaf)
    
    # Visit any remaining nodes (in case of cycles or disconnected components)
    for path in graph.nodes.keys():
        visit(path)
    
    return order
```

### Compilation Phase

```python
def compileAll(graph):
    # Always compile builtins first
    compileBuiltins()
    
    # Compile in dependency order
    for path in graph.compilationOrder:
        node = graph.nodes[path]
        
        # All dependencies should already be compiled
        for imp in node.imports:
            assert graph.nodes[imp].compiled
        
        # Compile this file
        compile(node.ast)
        node.compiled = True
```

## Synchronization Requirements

### Shared State Requiring Locks

```
┌──────────────────────────────────────────────────┐
│         Concurrent Access Points                 │
├──────────────────────────────────────────────────┤
│                                                  │
│  1. ParsedFiles HashMap                          │
│     - Lock: queueLock                            │
│     - Writers: worker threads (onParseComplete)  │
│     - Readers: controller (checking if parsed)   │
│                                                  │
│  2. PendingPaths Set                             │
│     - Lock: queueLock                            │
│     - Writers: main thread, worker callbacks     │
│     - Readers: main thread (dispatcher)          │
│                                                  │
│  3. InProgressPaths Set                          │
│     - Lock: queueLock                            │
│     - Writers: main thread, worker callbacks     │
│     - Readers: main thread (completion check)    │
│                                                  │
│  4. WorkerPool JobQueue                          │
│     - Lock: pool.queueLock                       │
│     - Writers: controller (submit jobs)          │
│     - Readers: worker threads (get jobs)         │
│                                                  │
│  5. Memory Pools (if used during parsing)        │
│     - Lock: per-pool lock OR use thread-local    │
│     - Option: Each worker gets its own pool      │
│                                                  │
└──────────────────────────────────────────────────┘
```

### Thread-Safe Components

Components that need to be made thread-safe:

```
Lexer:
  - Currently uses global state? Review.
  - Should be stateless or use local state.
  - ✓ Can be thread-safe with local state.

Parser:
  - Should operate only on local AST.
  - Must not modify global compiler state.
  - ✓ Can be thread-safe.

String Interning:
  - Shared string pool needs lock.
  - OR use thread-local pools + merge later.
  - ⚠️ Needs synchronization.

Type System:
  - Type definitions are read-only during parsing.
  - ✓ Read-only access is thread-safe.

Symbol Table:
  - NOT accessed during parsing.
  - Only used during compilation (sequential).
  - ✓ No synchronization needed.

Memory Pools:
  - Option 1: Lock per allocation (slow).
  - Option 2: Thread-local pools (fast).
  - ✓ Prefer thread-local pools.
```

### Lock Ordering (Prevent Deadlocks)

```
Lock Hierarchy (acquire in this order):

  1. controller.queueLock
       ↓
  2. pool.queueLock
       ↓
  3. stringPool.lock
       ↓
  4. memoryPool.lock

Never acquire locks in reverse order!
```

## Implementation Phases

### Phase 0: Preparation (Week 1)

```
Goals:
  - Audit current code for global state
  - Identify thread-unsafe components
  - Add thread-safety annotations

Tasks:
  [ ] Review lexer.c for global state
  [ ] Review parser.c for global state
  [ ] Review string interning mechanism
  [ ] Document shared data structures
  [ ] Add comments marking thread-safe code
```

### Phase 1: Data Structures (Week 2)

```
Goals:
  - Implement core data structures
  - No parallelism yet (sequential still works)

Tasks:
  [ ] Implement ParsedFile struct
  [ ] Implement ParseController struct
  [ ] Implement ImportGraph struct
  [ ] Implement WorkerPool stub (single-threaded)
  [ ] Add unit tests for data structures
```

### Phase 2: Refactor Parse/Compile Split (Week 3)

```
Goals:
  - Separate parsing from compilation
  - Still sequential (no threads)

Tasks:
  [ ] Modify compileFile() to only parse
  [ ] Move compilation logic to separate function
  [ ] Return AST + imports (don't compile yet)
  [ ] Update callsites
  [ ] Verify existing tests pass
```

### Phase 3: Build Import Graph (Week 4)

```
Goals:
  - Collect all imports before compiling
  - Build dependency graph

Tasks:
  [ ] Implement graph building
  [ ] Implement topological sort
  [ ] Walk imports recursively (still sequential)
  [ ] Generate compilation order
  [ ] Add graph visualization (debugging)
  [ ] Test with real projects
```

### Phase 4: Add Parallelism (Week 5-6)

```
Goals:
  - Implement worker pool
  - Parallel parsing

Tasks:
  [ ] Implement thread-safe work queue
  [ ] Implement worker threads
  [ ] Add synchronization primitives
  [ ] Dispatch parse jobs to workers
  [ ] Handle worker completion
  [ ] Add error handling (parse failures)
  [ ] Test with varying worker counts
```

### Phase 5: Thread-Safety Hardening (Week 7)

```
Goals:
  - Make shared components thread-safe
  - Fix race conditions

Tasks:
  [ ] Add locks to string interning
  [ ] Use thread-local memory pools
  [ ] Add thread sanitizer tests
  [ ] Fix any race conditions found
  [ ] Performance profiling
```

### Phase 6: Optimization & Tuning (Week 8)

```
Goals:
  - Optimize performance
  - Tune worker count

Tasks:
  [ ] Profile with real projects
  [ ] Optimize hot paths
  [ ] Tune worker pool size
  [ ] Add benchmarks
  [ ] Compare old vs new speeds
  [ ] Document speedup results
```

## Expected Benefits

### Performance Improvements

```
Project Size    Current (Sequential)    New (Parallel)    Speedup
────────────    ────────────────────    ──────────────    ───────
Small (10 files)         0.5s                0.4s          1.25x
Medium (50 files)        2.5s                1.0s          2.5x
Large (200 files)       12.0s                3.5s          3.4x
Huge (1000 files)       80.0s               15.0s          5.3x

Assumptions:
  - 8-core CPU
  - Parsing is 60% of total time
  - Ideal parallelism: 6-8x (Amdahl's law)
```

### Scalability

```
   Compilation Time vs Project Size

   Time (s)
     │
  80 │                                    ┌─ Sequential
     │                                  ╱
  60 │                                ╱
     │                              ╱
  40 │                            ╱
     │                          ╱
  20 │                    ┌───────────── Parallel (8 cores)
     │              ┌───╱
   0 └────┴────┴────┴────┴────┴────┴────
        0   200  400  600  800  1000    Files

   Parallel compilation scales much better!
```

## Trade-offs & Considerations

### Advantages

```
✓ Significant speedup for large projects (3-5x)
✓ Better CPU utilization (use all cores)
✓ Import graph provides useful metadata
✓ Easier to add incremental compilation later
✓ Cleaner separation of concerns (parse vs compile)
✓ Profiling becomes more granular
```

### Disadvantages

```
✗ More complex codebase (threads, locks)
✗ Potential for race conditions / bugs
✗ Minimal speedup for small projects (<10 files)
✗ Memory usage increases (all ASTs in memory)
✗ Compilation errors may be reported out of order
✗ Requires careful testing (thread sanitizer)
```

### Memory Considerations

```
Current Approach:
  - Parse file → compile → free AST
  - Memory: O(1) per file (depth-first)

New Approach:
  - Parse ALL files → compile ALL → free
  - Memory: O(N) for all ASTs

Example:
  - 1000 files × 500 KB AST each = 500 MB
  - This is acceptable on modern machines
  - Can add streaming compilation later if needed
```

## Future Enhancements

### Parallel Compilation (Future)

```
Once parsing is parallel, we can parallelize compilation too:

┌────────────────────────────────────────────────┐
│          Parallel Compilation                  │
├────────────────────────────────────────────────┤
│                                                │
│  Group files by dependency level:              │
│                                                │
│    Level 0: [leaf1, leaf2, leaf3, ...]         │
│    Level 1: [mid1, mid2, ...]                  │
│    Level 2: [root]                             │
│                                                │
│  Compile each level in parallel:               │
│                                                │
│    Level 0: ║═══║═══║═══║  (all parallel)      │
│    Level 1:     ║═══║═══║  (after L0)          │
│    Level 2:         ║═══║  (after L1)          │
│                                                │
└────────────────────────────────────────────────┘
```

### Incremental Compilation (Future)

```
With import graph, we can do smart rebuilds:

1. File X modified
2. Find all files that depend on X (via graph)
3. Re-parse X
4. Re-compile X and all dependents
5. Leave unchanged files alone

Example:
  - Change bar.cxy
  - Graph shows: main.cxy imports bar.cxy
  - Recompile: bar.cxy, main.cxy
  - Skip: foo.cxy, baz.cxy (unchanged)
```

### Cache Parsed ASTs (Future)

```
Serialize parsed ASTs to disk:

  .cxy_cache/
    ├── foo.cxy.ast       (cached AST)
    ├── foo.cxy.hash      (source hash)
    ├── bar.cxy.ast
    └── bar.cxy.hash

On next build:
  - Check if source hash matches
  - If yes: load cached AST (skip parsing)
  - If no: re-parse and update cache

Speedup: 10-50x for unchanged files!
```

## Testing Strategy

### Unit Tests

```
[ ] ParseController:
  - Add/remove files
  - Thread-safe operations
  - Completion detection

[ ] ImportGraph:
  - Build from parsed files
  - Topological sort correctness
  - Cycle detection

[ ] WorkerPool:
  - Job submission
  - Job completion
  - Graceful shutdown
```

### Integration Tests

```
[ ] Small project (10 files)
  - Verify correct compilation order
  - Compare output with sequential

[ ] Medium project (50 files)
  - With deep import chains
  - With diamond dependencies

[ ] Large project (200+ files)
  - Performance comparison
  - Memory usage check

[ ] Edge cases:
  - Circular imports (error)
  - Missing imports (error)
  - Parse errors in worker
```

### Stress Tests

```
[ ] Thread Sanitizer (TSan)
  - Detect race conditions
  - Run all tests under TSan

[ ] Many workers (16, 32, 64)
  - Test with excessive parallelism
  - Ensure no deadlocks

[ ] Long import chains (depth 100+)
  - Ensure no stack overflow
  - Verify correct ordering
```

## Migration Path

### Backward Compatibility

```
Command Line Flag:
  cxy compile --parallel <file>     # New parallel mode
  cxy compile <file>                 # Old sequential (fallback)

Environment Variable:
  export CXY_PARALLEL_COMPILE=1     # Enable globally

Automatic Detection:
  if (fileCount > 20 && cpuCount > 2)
      useParallelCompile = true

Gradual Rollout:
  - Phase 1: Opt-in (--parallel flag)
  - Phase 2: Default for large projects
  - Phase 3: Always enabled (remove old code)
```

## Conclusion

The parallel compilation architecture will significantly improve Cxy build times for medium to large projects by:

1. **Parallelizing parsing** across multiple CPU cores
2. **Separating concerns** between parsing and compilation
3. **Building an import graph** that enables future optimizations
4. **Maintaining correctness** through proper dependency ordering

The implementation will be done in careful phases to ensure stability and thread-safety. The investment in this architecture will pay dividends as the Cxy ecosystem grows and projects become larger.

---

**Next Steps:**
1. Review and discuss this design with the team
2. Get approval for the approach
3. Begin Phase 0 (audit current code)
4. Create tracking issues for each phase
5. Start implementation!
