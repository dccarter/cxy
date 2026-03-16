
#pragma once

#include <core/array.h>
#include <core/utils.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Log Log;
typedef struct StrPool StrPool;

typedef enum { _cmdHelp, _cmdCompletion, cmdDev, cmdBuild, cmdTest, cmdPackage } Command;
// clang-format off
#define DUMP_OPTIONS(ff)    \
    ff(NONE)                \
    ff(JSON)                \
    ff(YAML)                \
    ff(SEXP)                \
    ff(CXY)

typedef enum {
#define ff(N) dmp## N,
    DUMP_OPTIONS(ff)
    dmpCOUNT
#undef ff
} DumpModes;

#define DRIVER_STATS_MODE(f)    \
    f(NONE)                     \
    f(SUMMARY)                  \
    f(FULL)

typedef enum {
#define ff(N) dsm## N,
    DRIVER_STATS_MODE(ff)
    dsmCOUNT
#undef ff
} DumpStatsMode;

typedef enum OptimizationLevel {
    O0 = '0',
    Od = '0',
    O1 = '1',
    O2 = '2',
    O3 = '3',
    Os = 's'
} OptimizationLevel;

typedef struct CompilerDefine {
    cstring name;
    cstring value;
} CompilerDefine;

typedef enum {
    pkgSubCreate,
    pkgSubAdd,
    pkgSubInstall,
    pkgSubRemove,
    pkgSubUpdate,
    pkgSubTest,
    pkgSubBuild,
    pkgSubPublish,
    pkgSubList,
    pkgSubInfo,
    pkgSubClean,
    pkgSubRun
} PackageSubcommand;

// clang-format on

typedef struct Options {
    Command cmd;
    const char *output;
    const char *libDir;
    const char *buildDir;
    const char *pluginsDir;
    const char *depsDir;
    const char *rest;
    DynArray cflags;
    DynArray cDefines;
    DynArray librarySearchPaths;
    DynArray importSearchPaths;
    DynArray frameworkSearchPaths;
    DynArray libraries;
    DynArray defines;
    DynArray sources;
    bool withoutBuiltins;
    bool noPIE;
    bool withMemoryManager;
    bool withMemoryTrace;
    bool debug;
    OptimizationLevel optimizationLevel;
    bool debugPassManager;
    cstring passes;
    DynArray loadPassPlugins;
    DumpStatsMode dsmMode;
    cstring operatingSystem;
    bool buildPlugin;
    union {
        struct {
            bool printIR;
            bool emitBitCode;
            bool emitAssembly;
            bool cleanAst;
            bool withLocation;
            bool withoutAttrs;
            bool withNamedEnums;
            DumpModes dumpMode;
            u64 lastStage;
        } dev;
        struct {
            bool plugin;
        } build;
        struct {
            PackageSubcommand subcmd;
            const char *cxyfile;
            bool quiet;
            bool verbose;
            // create subcommand
            const char *name;
            const char *author;
            const char *description;
            const char *license;
            const char *version;
            bool interactive;
            const char *directory;
            bool bin;                // Create binary package (main.cxy instead of lib)
            // add subcommand
            const char *repository;  // Git repository URL or package identifier
            const char *packageName; // Custom package name (--name option)
            const char *constraint;  // Version constraint
            const char *tag;         // Specific Git tag
            const char *branch;      // Specific Git branch
            const char *path;        // Local filesystem path
            bool dev;                // Add as development dependency
            bool noInstall;          // Skip installation (validation only)
            // remove subcommand
            DynArray packages;       // Array of package names to remove
            // install subcommand
            bool includeDev;         // Include development dependencies
            bool clean;              // Ignore lock file and perform clean resolution
            const char *packagesDir; // Custom packages directory
            bool verify;             // Verify integrity of installed packages
            bool offline;            // Use only cached packages, no network access
            bool frozenLockfile;     // Fail if lock file is missing or outdated (CI mode)
            // update subcommand
            bool latest;             // Update to latest version, ignoring constraints
            bool dryRun;             // Show what would be updated without changing files
            // test subcommand
            const char *buildDir;    // Build directory for test binaries
            const char *filter;      // Run only tests matching pattern
            int parallel;            // Run tests in parallel
            DynArray testFiles;      // Specific test files to run
            // publish subcommand
            const char *bump;        // Version bump: major, minor, patch
            const char *tagName;     // Custom tag name
            const char *message;     // Tag annotation message
            // info subcommand
            const char *package;     // Package name to show info for
            bool json;               // Output in JSON format
            // build subcommand
            bool release;
            bool debug;
            const char *buildTarget; // Specific build target to build
            bool buildAll;           // Build all targets
            bool listBuilds;         // List available build targets
            DynArray rest;           // Positional arguments after build subcommand
            // run subcommand
            const char *scriptName;  // Script name to execute
            bool listScripts;        // List available scripts
            bool noCache;            // Disable script caching
            // clean subcommand
            bool cleanCache;         // Clean package cache (.cxy/packages)
            bool cleanBuild;         // Clean build directory
            bool cleanAll;           // Clean everything
            bool force;              // Skip confirmation prompts
        } package;
    };
} Options;

/// Parse command-line options, and remove those parsed options from the
/// argument list. After parsing, `argc` and `argv` are modified to only
/// contain the arguments that were not parsed.
bool parseCommandLineOptions(int *argc,
                             char **argv,
                             struct StrPool *strings,
                             Options *options,
                             Log *log);

void deinitCommandLineOptions(Options *options);

#ifdef __cplusplus
}
#endif
