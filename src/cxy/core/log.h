#pragma once

#include <stdint.h>

#include "format.h"
#include "htable.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off
#define CXY_COMPILER_WARNINGS(f)        \
    f(MissingStage,      0)             \
    f(UnusedVariable,    1)             \
    f(RedundantStmt,     2)             \
    f(CMacroRedefine,    3)             \
    f(CUnsupportedField, 4)             \
    f(MaybeUninitialized,5)

// clang-format on

enum {
#define f(NAME, IDX) wrn##NAME = IDX,
    CXY_COMPILER_WARNINGS(f)
#undef f
};

#define wrnNone (0ull)
#define wrnAll (~(0ull) >> 1)
#define wrn_Error (1ull << 63)
#define wrnDefault                                                             \
    wrnAll & ~(BIT(wrnMissingStage) | BIT(wrnCMacroRedefine) |                 \
               BIT(wrnMaybeUninitialized))

typedef u64 WarningId;

/*
 * The log object is used to
 * report messages from
 * various passes of the
 * compiler. It also caches
 * what files, to print
 * error diagnostics
 * efficiently.
 */
typedef struct {
    uint32_t row, col;
    size_t byteOffset;
} FilePos;

typedef struct FileLoc {
    const char *fileName;
    FilePos begin, end;
} FileLoc;

typedef enum { dkError, dkWarning, dkNote } DiagnosticKind;

typedef struct Diagnostic {
    DiagnosticKind kind;
    FileLoc loc;
    cstring fmt;
    const FormatArg *args;
} Diagnostic;

typedef void (*DiagnosticHandler)(const Diagnostic *, void *);

typedef struct Log {
    HashTable fileCache;
    DiagnosticHandler handler;
    void *handlerCtx;
    size_t errorCount;
    size_t warningCount;
    size_t maxErrors;
    bool ignoreStyles;
    FormatState *state;
    struct {
        cstring str;
        u64 num;
    } enabledWarnings;
    bool showDiagnostics;
    bool progress;
} Log;

typedef struct DiagnosticMemoryPrintCtx {
    FormatState *state;
    Log *L;
} DiagnosticMemoryPrintCtx;

Log newLog(DiagnosticHandler handler, void *ctx);
void freeLog(Log *);

void logError(Log *, const FileLoc *, const char *, const FormatArg *);
void logWarning(Log *, const FileLoc *, const char *, const FormatArg *);
void logWarningWithId(
    Log *, u8, const FileLoc *, const char *, const FormatArg *);
void logNote(Log *, const FileLoc *, const char *, const FormatArg *);
void printStatus(Log *L, const char *fmt, ...);
void printStatusAlways(Log *L, const char *fmt, ...);

void printDiagnosticToConsole(const Diagnostic *diag, void *ctx);
void printDiagnosticToMemory(const Diagnostic *diag, void *ctx);

static inline bool hasErrors(Log *L) { return L->errorCount > 0; }

const FileLoc *builtinLoc(void);
static inline FileLoc locSubRange(const FileLoc *start, const FileLoc *end)
{
    csAssert0(start->fileName == end->fileName);
    return (FileLoc){
        .fileName = start->fileName, .begin = start->begin, .end = end->begin};
}

static inline FileLoc *locExtend_(FileLoc *dst,
                                  const FileLoc *start,
                                  const FileLoc *end)
{
    if (start->fileName == end->fileName) {
        csAssert0(start->begin.byteOffset <= end->end.byteOffset);
        *dst = (FileLoc){.fileName = start->fileName,
                         .begin = start->begin,
                         .end = end->end};
    }
    else {
        *dst = *start;
    }
    return dst;
}

#define locExtend(start, end) locExtend_(&(FileLoc){}, (start), (end))

static inline FileLoc *locAfter(FileLoc *dst, const FileLoc *loc)
{
    *dst = (FileLoc){
        .fileName = loc->fileName, .begin = loc->end, .end = loc->end};
    return dst;
}

u64 parseWarningLevels(Log *L, cstring str);

static inline bool isWarningEnabled_(Log *L, WarningId id)
{
    return L->enabledWarnings.num & id;
}
#define isWarningEnabled(L, NAME) isWarningEnabled_((L), BIT(wrn##NAME))

#ifdef __cplusplus
}
#endif
