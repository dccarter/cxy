
#pragma once

#include <driver/options.h>
#include <driver/stats.h>

#include <core/strpool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MirContext;

typedef struct CompilerPreprocessor {
    MemPool *pool;
    HashTable symbols;
} CompilerPreprocessor;

typedef struct {
    cstring name;
    cstring main;
    DynArray tests;
    DynArray defines;
    DynArray cIncludeDirs;
    DynArray cLibDirs;
    DynArray cLibs;
    DynArray cSources;
    DynArray cDefines;
    DynArray cFlags;
} CxyBinary;

typedef struct {
    cstring name;
    cstring version;
    cstring description;
    cstring defaultRun;
    cstring repo;
    DynArray authors;
    HashTable binaries;
} CxyPackage;

typedef struct CompilerDriver {
    Options options;
    MemPool *pool;
    StrPool *strings;
    HashTable moduleCache;
    HashTable nativeSources;
    HashTable linkLibraries;
    cstring currentDir; // Directory that cxy was executed from
    u64 currentDirLen;
    cstring sourceDir; // Root directory for source files (derived from first
                       // compile unit)
    u64 sourceDirLen;
    cstring os;
    cstring cxyBinaryPath;
    CompilerStats stats;
    Log *L;
    TypeTable *types;
    AstNode *mainModule;
    AstNodeList startup;
    CompilerPreprocessor preprocessor;
    CxyPackage package;
    HashTable plugins;
    struct MirContext *mir;
    void *backend;
    void *cImporter;
    bool hasTestCases;
} CompilerDriver;

typedef struct {
    cstring name;
    cstring data;
    u64 len;
    u64 mtime;
} EmbeddedSource;

cstring getFilenameWithoutDirs(cstring fileName);
char *getGeneratedPath(CompilerDriver *driver,
                       cstring dir,
                       cstring filePath,
                       cstring ext);
void makeDirectoryForPath(CompilerDriver *driver, cstring path);
cstring getFilePathAsRelativeToCxySource(StrPool *strings,
                                         cstring cxySource,
                                         cstring file);
bool initCompilerDriver(CompilerDriver *compiler,
                        MemPool *pool,
                        StrPool *strings,
                        Log *log,
                        int argc,
                        char **argv);
void deinitCompilerDriver(CompilerDriver *driver);

bool compilePlugin(const char *fileName, CompilerDriver *driver);
bool compileFile(const char *fileName, CompilerDriver *driver);
bool generateBuiltinSources(CompilerDriver *driver);

bool compileString(CompilerDriver *driver,
                   cstring source,
                   u64 size,
                   cstring filename);

bool loadCxyfile(CompilerDriver *cc, cstring path);

const Type *compileModule(CompilerDriver *driver,
                          const AstNode *source,
                          AstNode *entities,
                          AstNode *alias,
                          bool testMode);

cstring getIncludeFileLocation(CompilerDriver *driver,
                               const FileLoc *loc,
                               cstring path);

void *initCompilerBackend(CompilerDriver *driver, int argc, char **argv);
void deinitCompilerBackend(CompilerDriver *driver);
bool compilerBackendMakeExecutable(CompilerDriver *driver);
bool compilerBackendExecuteTestCase(CompilerDriver *driver);
void initCompilerPreprocessor(struct CompilerDriver *driver);
void deinitCompilerPreprocessor(struct CompilerDriver *driver);
void initCImporter(struct CompilerDriver *driver);
void deinitCImporter(struct CompilerDriver *driver);
void driverAddNativeSource(struct CompilerDriver *driver, cstring source);

#ifdef __cplusplus
}
#endif