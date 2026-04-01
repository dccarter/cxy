/**
 * Feature Flags for Cxy Compiler
 * 
 * This file contains compile-time feature flags that control optional
 * functionality and experimental features in the Cxy compiler.
 * 
 * Flags are typically controlled via CMake options or can be manually
 * enabled/disabled by uncommenting the appropriate #define directives.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Parallel Compilation Feature
// ============================================================================

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
 * Default: OFF (will be enabled incrementally starting Phase 4)
 * 
 * To enable:
 * - Uncomment the line below, OR
 * - Pass -DCXY_PARALLEL_COMPILE=1 to the compiler, OR
 * - Set CMake option: -DCXY_PARALLEL_COMPILE=ON
 */
// #define CXY_PARALLEL_COMPILE 1

// ============================================================================
// Feature Flag Normalization
// ============================================================================

#ifndef CXY_PARALLEL_COMPILE
#define CXY_PARALLEL_COMPILE 0
#endif

// ============================================================================
// Derived Flags
// ============================================================================

#if CXY_PARALLEL_COMPILE
  #define CXY_THREAD_SAFE 1
  #define CXY_USE_WORKER_POOLS 1
#else
  #define CXY_THREAD_SAFE 0
  #define CXY_USE_WORKER_POOLS 0
#endif

// ============================================================================
// Conditional Compilation Helpers
// ============================================================================

/**
 * IF_PARALLEL(code) - Include code only when parallel compilation is enabled
 * IF_SEQUENTIAL(code) - Include code only when parallel compilation is disabled
 * 
 * Usage:
 *   void compile(const char *path) {
 *       IF_PARALLEL(
 *           parseAllFilesInParallel(path);
 *       )
 *       IF_SEQUENTIAL(
 *           parseFileRecursively(path);
 *       )
 *   }
 */
#if CXY_PARALLEL_COMPILE
  #define IF_PARALLEL(code) code
  #define IF_SEQUENTIAL(code)
#else
  #define IF_PARALLEL(code)
  #define IF_SEQUENTIAL(code) code
#endif

// ============================================================================
// Future Feature Flags (Placeholder)
// ============================================================================

/**
 * Additional feature flags can be added here as needed:
 * 
 * - CXY_INCREMENTAL_COMPILE: Incremental compilation support
 * - CXY_CACHE_ASTS: Cache parsed ASTs to disk
 * - CXY_PARALLEL_CODEGEN: Parallel code generation (after parallel parsing)
 * - CXY_PROFILE_MEMORY: Detailed memory profiling
 */

#ifdef __cplusplus
}
#endif