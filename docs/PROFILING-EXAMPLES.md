# Profiling Framework Usage Examples

This document demonstrates how to use the Cxy compiler's profiling framework to measure and optimize compilation performance.

## Quick Start

### Enable Profiling

```c
#include "driver/profiling.h"

int main() {
    // Enable profiling at startup
    profileEnable();
    
    // ... do work ...
    
    // Print results
    profilePrint(false);  // false = aggregate view only
    
    return 0;
}
```

### Disable Profiling

```c
profileDisable();  // Stop collecting profiling data
```

## Profiling Modes

The profiling framework supports three output modes via the `--profile` flag:

### NONE (Default)
No profiling data is collected or output.

```bash
cxy dev myfile.cxy --profile=NONE
# OR
cxy dev myfile.cxy  # NONE is default
```

### STDOUT
Prints profiling results to standard output in human-readable format.

```bash
cxy dev myfile.cxy --profile=STDOUT
```

Output:
```
════════════════════════════════════════════════════════════════════════════
  Profiling Results
════════════════════════════════════════════════════════════════════════════

Section                              Calls       Total         Avg         Max
────────────────────────────────────────────────────────────────────────────
.../examples/myfile.cxy              1           523ms         523ms       523ms
__builtins.cxy                       1           5ms           5ms         5ms
  parse                              2           312ms         156ms       312ms
  type-check                         2           89ms          44.5ms      89ms
  codegen                            2           122ms         61ms        122ms
════════════════════════════════════════════════════════════════════════════
```

**Path Abbreviation**: Long file paths are automatically abbreviated to fit the console width (max 35 characters). For example:
- `/Users/dc/projects/mono-cxy/cxy/examples/hello.cxy` → `.../cxy/examples/hello.cxy`
- `/tmp/very/long/nested/directory/structure/for/testing/hello.cxy` → `.../for/testing/hello.cxy`

The abbreviation keeps the last 3 path components for context while staying readable.

### JSON
Exports profiling data to `profiling.json` in machine-readable format.

```bash
cxy dev myapp.cxy --profile=JSON
```

This creates `profiling.json` with structured profiling data suitable for:
- Custom visualizers
- Performance analysis tools
- Automated performance regression testing
- Comparison across builds

**Note**: JSON output preserves full, unabbreviated file paths for accurate tooling integration.

## JSON Profiling Format

When using `--profile=JSON`, the compiler outputs a JSON file with the following schema:

```json
{
  "profilingEnabled": true,
  "threadId": 0,
  "entryCount": 5,
  "entries": [
    {
      "name": "compileModule",
      "totalNs": 523000000,
      "count": 1,
      "minNs": 523000000,
      "maxNs": 523000000,
      "avgNs": 523000000,
      "depth": 0,
      "isLockProfile": false
    },
    {
      "name": "parse",
      "totalNs": 312000000,
      "count": 1,
      "minNs": 312000000,
      "maxNs": 312000000,
      "avgNs": 312000000,
      "depth": 1,
      "isLockProfile": false
    },
    {
      "name": "strpool-lock",
      "totalNs": 0,
      "count": 1243,
      "minNs": 0,
      "maxNs": 0,
      "avgNs": 0,
      "depth": 2,
      "isLockProfile": true,
      "waitTimeNs": 42000000,
      "holdTimeNs": 156000000
    }
  ]
}
```

### JSON Field Descriptions

- `profilingEnabled`: Boolean indicating if profiling was active
- `threadId`: Thread identifier (0 for main thread)
- `entryCount`: Total number of profiling entries
- `entries`: Array of profiling entries, each containing:
  - `name`: Human-readable name of the profiled section
  - `totalNs`: Total time spent in nanoseconds
  - `count`: Number of times this section was executed
  - `minNs`: Minimum duration in nanoseconds
  - `maxNs`: Maximum duration in nanoseconds
  - `avgNs`: Average duration in nanoseconds
  - `depth`: Nesting level (0 = top level, 1+ = nested)
  - `isLockProfile`: Boolean indicating if this is lock contention data
  - `waitTimeNs`: (Lock profiles only) Time waiting to acquire lock
  - `holdTimeNs`: (Lock profiles only) Time holding the lock

### Using JSON Output for Visualization

Example Python script to analyze profiling data:

```python
import json

with open('profiling.json', 'r') as f:
    data = json.load(f)

# Find slowest operations
entries = sorted(data['entries'], key=lambda x: x['totalNs'], reverse=True)

print("Top 10 Slowest Operations:")
for entry in entries[:10]:
    total_ms = entry['totalNs'] / 1_000_000
    avg_ms = entry['avgNs'] / 1_000_000
    indent = "  " * entry['depth']
    print(f"{indent}{entry['name']}: {total_ms:.2f}ms total, {avg_ms:.2f}ms avg ({entry['count']} calls)")

# Analyze lock contention
locks = [e for e in data['entries'] if e['isLockProfile']]
if locks:
    print("\nLock Contention Analysis:")
    for lock in locks:
        wait_ms = lock['waitTimeNs'] / 1_000_000
        hold_ms = lock['holdTimeNs'] / 1_000_000
        print(f"{lock['name']}: {wait_ms:.2f}ms waiting, {hold_ms:.2f}ms holding ({lock['count']} acquisitions)")
```

Example Node.js script for web visualization:

```javascript
const fs = require('fs');
const data = JSON.parse(fs.readFileSync('profiling.json', 'utf8'));

// Generate HTML report
const html = `
<!DOCTYPE html>
<html>
<head>
  <title>Profiling Report</title>
  <style>
    .entry { margin-left: calc(var(--depth) * 20px); }
    .slow { color: red; }
  </style>
</head>
<body>
  <h1>Profiling Results</h1>
  ${data.entries.map(e => {
    const totalMs = (e.totalNs / 1000000).toFixed(2);
    const className = e.totalNs > 100000000 ? 'slow' : '';
    return `<div class="entry ${className}" style="--depth: ${e.depth}">
      ${e.name}: ${totalMs}ms (${e.count} calls)
    </div>`;
  }).join('\n')}
</body>
</html>
`;

fs.writeFileSync('profile-report.html', html);
console.log('Report written to profile-report.html');
```

## Basic Usage Examples

### Example 1: Profile Entire Function

```c
#include "driver/profiling.h"

void parseFile(const char *path) {
    PROFILE_SCOPE("parseFile");
    
    // All code here is timed
    Lexer lexer = newLexer(path, data, size, log);
    Parser parser = makeParser(&lexer, driver, false);
    AstNode *ast = parseProgram(&parser);
    
    freeLexer(&lexer);
    return ast;
}
// Timer automatically stops when function returns
```

### Example 2: Profile Code Sections

```c
void compileModule(const char *path) {
    PROFILE_SCOPE("compileModule");
    
    AstNode *ast = NULL;
    
    PROFILE_SECTION("parse") {
        ast = parseFile(path);
    }
    
    PROFILE_SECTION("type-check") {
        typeCheckProgram(ast);
    }
    
    PROFILE_SECTION("codegen") {
        generateCode(ast);
    }
}
```

Output:
```
Section                              Calls       Total         Avg         Max
────────────────────────────────────────────────────────────────────────────
compileModule                        1           523ms         523ms       523ms
  parse                              1           312ms         312ms       312ms
  type-check                         1           89ms          89ms        89ms
  codegen                            1           122ms         122ms       122ms
```

### Example 3: Manual Timer Control

```c
void processImports(AstNode *program) {
    ProfileTimer *total = profileStart("processImports-total");
    
    for (AstNode *import = program->imports; import; import = import->next) {
        ProfileTimer *t = profileStart("single-import");
        
        resolveImport(import);
        
        uint64_t ns = profileStop(t);
        if (ns > 10000000) {  // > 10ms
            printf("Slow import: %s took %lluns\n", import->path, ns);
        }
    }
    
    profileStop(total);
}
```

### Example 4: Conditional Profiling

```c
void parseAllFiles(const char **paths, int count) {
    PROFILE_SCOPE("parseAllFiles");
    
    for (int i = 0; i < count; i++) {
        // Only profile files that match certain criteria
        if (shouldProfileFile(paths[i])) {
            ProfileTimer *t = profileStart("suspicious-file");
            parseFile(paths[i]);
            profileStop(t);
        } else {
            parseFile(paths[i]);  // Not profiled
        }
    }
}
```

## Lock Profiling (Parallel Compilation)

When `CXY_PARALLEL_COMPILE` is enabled, you can profile lock contention.

### Example 5: Profile StrPool Lock

```c
const char *makeString(StrPool *pool, const char *str) {
    PROFILE_LOCK(pool->lock, "strpool-insert") {
        // Measures:
        // 1. Time waiting to acquire lock
        // 2. Time holding the lock
        
        // Hash lookup and insert
        u32 hash = hashStr(hashInit(), str);
        const char *result = findInHashTable(&pool->table, str, hash, ...);
        
        if (!result) {
            result = allocFromMemPool(pool->pool, strlen(str) + 1);
            strcpy((char *)result, str);
            insertInHashTable(&pool->table, result, hash, ...);
        }
        
        return result;
    } // Lock automatically released here
}
```

Output:
```
Lock Contention Analysis
────────────────────────────────────────────────────────────────────────────
Lock                         Wait Time    Hold Time    Acq.
────────────────────────────────────────────────────────────────────────────
strpool-insert              42ms         156ms        1,243
moduleCache-lock            8ms          12ms         142
```

### Example 6: Manual Lock Profiling

For complex scenarios where you need the lock across multiple statements:

```c
void batchInsert(StrPool *pool, const char **strings, int count) {
    profileLockWait("batch-insert", pool->lock);
    
    // Lock is now held
    for (int i = 0; i < count; i++) {
        // ... insert operations
    }
    
    profileLockRelease("batch-insert", pool->lock);
    // Lock released
}
```

## Nested Profiling

The framework automatically handles nested profiling with proper indentation:

```c
void compileProject() {
    PROFILE_SCOPE("compileProject");
    
    PROFILE_SECTION("parse-all") {
        for (int i = 0; i < fileCount; i++) {
            parseFile(files[i]);  // Has its own PROFILE_SCOPE
        }
    }
    
    PROFILE_SECTION("compile-all") {
        for (int i = 0; i < fileCount; i++) {
            compileFile(files[i]);  // Has its own PROFILE_SCOPE
        }
    }
}

void parseFile(const char *path) {
    PROFILE_SCOPE("parseFile");
    // ... parsing logic
}

void compileFile(const char *path) {
    PROFILE_SCOPE("compileFile");
    // ... compilation logic
}
```

Output:
```
Section                              Calls       Total         Avg         Max
────────────────────────────────────────────────────────────────────────────
compileProject                       1           2.5s          2.5s        2.5s
  parse-all                          1           800ms         800ms       800ms
    parseFile                        50          750ms         15ms        42ms
  compile-all                        1           1.7s          1.7s        1.7s
    compileFile                      50          1.65s         33ms        89ms
```

## Real-World Compiler Examples

### Example 7: Profile Import Resolution

```c
const Type *compileModule(CompilerDriver *driver, 
                         const AstNode *source,
                         AstNode *entities,
                         AstNode *alias,
                         bool testMode) {
    PROFILE_SCOPE("compileModule");
    
    AstNode *program = NULL;
    cstring path = source->stringLiteral.value;
    
    PROFILE_SECTION("module-lookup") {
        program = findCachedModule(driver, path);
    }
    
    if (program == NULL) {
        PROFILE_SECTION("module-parse") {
            program = parseFile(driver, path, testMode);
        }
        
        PROFILE_SECTION("module-compile") {
            compileProgram(driver, program, path, false);
        }
        
        PROFILE_SECTION("cache-insert") {
            addCachedModule(driver, path, program);
        }
    }
    
    PROFILE_SECTION("entity-resolution") {
        // Resolve imported entities
        for (AstNode *entity = entities; entity; entity = entity->next) {
            resolveEntity(entity, program);
        }
    }
    
    return program->type;
}
```

### Example 8: Profile Worker Thread

```c
void *workerThread(void *arg) {
    WorkerContext *ctx = (WorkerContext *)arg;
    
    char threadName[32];
    sprintf(threadName, "worker-%d", ctx->workerId);
    PROFILE_SCOPE(threadName);
    
    while (true) {
        ParseJob *job = NULL;
        
        PROFILE_SECTION("get-job") {
            job = getNextJob(ctx->jobQueue);
        }
        
        if (!job) break;
        
        PROFILE_SECTION("parse-job") {
            parseFile(job->path);
        }
        
        PROFILE_SECTION("report-result") {
            reportResult(ctx, job);
        }
    }
    
    return NULL;
}
```

## Output and Analysis

### Print to Console

```c
profilePrint(false);  // Aggregate view
```

### Print with Details

```c
profilePrint(true);   // Show individual call details
```

### Print to File

```c
profilePrintToFile("profile-results.txt", false);
```

### Programmatic Access

```c
const ProfileEntry *entries = profileGetEntries();

for (const ProfileEntry *e = entries; e->name != NULL; e++) {
    printf("%s: %llu calls, %lluns total\n", 
           e->name, e->count, e->totalNs);
    
    if (e->isLockProfile) {
        printf("  Wait: %lluns, Hold: %lluns\n",
               e->waitTimeNs, e->holdTimeNs);
    }
}
```

## Profiling in Different Build Modes

### Development Build (Profiling On)

```c
#ifdef DEBUG
    profileEnable();
#endif

compileProject();

#ifdef DEBUG
    profilePrint(true);
#endif
```

### Release Build (Profiling Off)

```c
// Profiling calls become no-ops when disabled
profileDisable();

compileProject();  // PROFILE_SCOPE macros do nothing
```

### Conditional Compilation

```c
#if CXY_PARALLEL_COMPILE
    PROFILE_LOCK(pool->lock, "operation") {
        // ... code
    }
#else
    // No lock profiling in sequential mode
    operation();
#endif
```

## Performance Tips

1. **Use PROFILE_SCOPE for functions**: Automatic cleanup, no manual profileStop needed
2. **Use PROFILE_SECTION for blocks**: Clear boundaries, automatic nesting
3. **Use manual timers for conditional profiling**: Only profile what you need
4. **Profile locks to find contention**: Helps identify parallelization bottlenecks
5. **Reset between runs**: Call `profileReset()` to clear accumulated data

## Common Patterns

### Pattern 1: Profile Top-Level Operations Only

```c
void compile(const char *path) {
    PROFILE_SCOPE("compile-toplevel");
    
    // Don't profile internal details
    parseFile(path);        // Not profiled
    typeCheck();            // Not profiled
    generateCode();         // Not profiled
}
```

### Pattern 2: Profile Everything (Hierarchical)

```c
void compile(const char *path) {
    PROFILE_SCOPE("compile");
    parseFile(path);        // Has PROFILE_SCOPE inside
    typeCheck();            // Has PROFILE_SCOPE inside
    generateCode();         // Has PROFILE_SCOPE inside
}
```

### Pattern 3: Profile Only Slow Operations

```c
void processFiles(const char **paths, int count) {
    for (int i = 0; i < count; i++) {
        ProfileTimer *t = profileStart("file-process");
        processFile(paths[i]);
        uint64_t ns = profileStop(t);
        
        // Only report if slow
        if (ns > 100000000) {  // > 100ms
            char buf[32];
            formatNanoseconds(ns, buf);
            printf("Slow file: %s (%s)\n", paths[i], buf);
        }
    }
}
```

## Integration with Existing Stats

The profiling framework complements the existing `stats.c` system:

```c
// Old stats (high-level)
compilerStatsSnapshot(driver);
parseFile(path);
compilerStatsRecord(driver, ccsParse);

// New profiling (fine-grained)
PROFILE_SCOPE("parseFile") {
    parseFile(path);
}
```

Both can be used together for comprehensive performance analysis.

## Profiling Analysis Tool

The Cxy compiler includes a powerful Ruby-based analyzer that generates reports, heat maps, and timeline visualizations from JSON profiling data.

### Installation

The analyzer is located at `src/tools/profile_analyze.rb` and requires Ruby (no additional gems needed).

```bash
chmod +x src/tools/profile_analyze.rb
```

### Basic Usage

```bash
# Generate profiling data
cxy dev myapp.cxy --profile=JSON

# Analyze with default table view
ruby src/tools/profile_analyze.rb profiling.json

# Or specify the JSON file explicitly
ruby src/tools/profile_analyze.rb profiling.json
```

### Analysis Modes

The analyzer supports four different visualization modes:

#### 1. Table Mode (Default)

Shows a comprehensive table with compilation units, stages, and lock contention:

```bash
ruby src/tools/profile_analyze.rb profiling.json --mode table
```

Output:
```
Cxy Profiling Report
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Compilation Units

  File                                                Total         Avg         Max   Calls  Heat
  ─────────────────────────────────────────────  ──────────  ──────────  ──────────  ──────  ────────────────────
  myapp.cxy                                          83.3ms      83.3ms      83.3ms       1  ████████████████████
  __builtins.cxy                                     4.76ms      4.76ms      4.76ms       1  █░░░░░░░░░░░░░░░░░░░

Compilation Stages

  Stage                      Total         Avg         Max   Calls     Pct  Heat
  ────────────────────  ──────────  ──────────  ──────────  ──────  ──────  ──────────────────────────────
  Compile                  81.91ms     40.96ms     81.91ms       2   95.3%  ██████████████████████████████
  TypeCheck                 1.62ms      0.81ms      1.59ms       2    1.9%  █░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
```

#### 2. Heatmap Mode

Shows a matrix of files vs stages with color-coded timing:

```bash
ruby src/tools/profile_analyze.rb profiling.json --mode heatmap
```

Output:
```
Compilation Heatmap
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  File                                Bind   Codegen   Compile  MemoryMg  Preproce     Shake  Simplify  TypeChec
  ──────────────────────────────  ────────  ────────  ────────  ────────  ────────  ────────  ────────  ────────
  __builtins.cxy                      0.19      0.53     40.96       0.0      0.16      0.08      0.26      0.81
  myapp.cxy                           0.19      0.53     40.96       0.0      0.16      0.08      0.26      0.81

Legend: █ hot  █ warm  █ cool
```

#### 3. Timeline Mode

Shows hierarchical timeline of compilation events:

```bash
ruby src/tools/profile_analyze.rb profiling.json --mode timeline
```

Output:
```
Compilation Timeline
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

myapp.cxy                                      83.3ms  ████████████████████████████████████████
__builtins.cxy                                 4.76ms  ██░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
  Bind                                         0.39ms  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
  Codegen                                      1.06ms  █░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
  Compile                                     81.91ms  ███████████████████████████████████████░
  TypeCheck                                    1.62ms  █░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
```

#### 4. Locks Mode

Deep dive into lock contention (requires parallel compilation):

```bash
ruby src/tools/profile_analyze.rb profiling.json --mode locks
```

Output:
```
Lock Contention Deep Dive
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Summary
  Total wait time:    42.5ms
  Total hold time:    156.8ms
  Total acquisitions: 1243

Lock Details

  strpool-lock
    Acquisitions:  1243
    Wait time:     42.5ms (avg: 34.2μs)
    Hold time:     156.8ms (avg: 126.1μs)
    Wait %:        73.2%
    Contention:    ████████████████████████████████████████
```

### Filtering and Sorting

```bash
# Show only top 10 entries
ruby src/tools/profile_analyze.rb profiling.json --top 10

# Filter to a specific stage
ruby src/tools/profile_analyze.rb profiling.json --stage TypeCheck

# Sort by different metrics
ruby src/tools/profile_analyze.rb profiling.json --sort total   # Total time
ruby src/tools/profile_analyze.rb profiling.json --sort avg     # Average time
ruby src/tools/profile_analyze.rb profiling.json --sort max     # Max time
ruby src/tools/profile_analyze.rb profiling.json --sort count   # Call count
```

### Save to File

```bash
# Save analysis to a file
ruby src/tools/profile_analyze.rb profiling.json --output report.txt

# Generate heatmap and save
ruby src/tools/profile_analyze.rb profiling.json --mode heatmap --output heatmap.txt
```

### Complete Example Workflow

```bash
# 1. Compile with JSON profiling
cxy dev myapp.cxy --profile=JSON

# 2. View table summary
ruby src/tools/profile_analyze.rb profiling.json

# 3. Check which stage is slowest
ruby src/tools/profile_analyze.rb profiling.json --mode heatmap

# 4. Drill into specific stage
ruby src/tools/profile_analyze.rb profiling.json --stage Compile --sort max

# 5. Check lock contention (if parallel compilation enabled)
ruby src/tools/profile_analyze.rb profiling.json --mode locks

# 6. Save full report
ruby src/tools/profile_analyze.rb profiling.json --output full-report.txt
```

### Integration with CI/CD

```bash
#!/bin/bash
# ci/profile.sh

# Compile with profiling
cxy dev src/main.cxy --profile=JSON

# Generate report
ruby src/tools/profile_analyze.rb profiling.json --output build-profile.txt

# Check if compilation is getting slower
# (compare with previous build's profiling.json)
CURRENT_TIME=$(jq '.entries[] | select(.depth == 0) | .totalNs' profiling.json | awk '{s+=$1} END {print s}')
THRESHOLD=100000000000  # 100 seconds in nanoseconds

if [ "$CURRENT_TIME" -gt "$THRESHOLD" ]; then
  echo "⚠️  Compilation time exceeded threshold"
  exit 1
fi
```

## Command-Line Examples

```bash
# No profiling (default)
cxy dev myapp.cxy

# Profile and print to stdout
cxy dev myapp.cxy --profile=STDOUT

# Profile and export to JSON
cxy dev myapp.cxy --profile=JSON

# Analyze JSON output
cxy dev myapp.cxy --profile=JSON
ruby src/tools/profile_analyze.rb profiling.json

# Build and profile with analysis
cxy build myapp.cxy --profile=JSON
ruby src/tools/profile_analyze.rb profiling.json --mode heatmap
```

## Profiling Output Location

- **STDOUT mode**: Prints to console after compilation completes
- **JSON mode**: Creates `profiling.json` in current working directory
- Custom location: Use `profilePrintToJSON("path/to/output.json")` in code