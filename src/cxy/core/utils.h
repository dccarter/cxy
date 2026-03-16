#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __APPLE__
#ifndef st_mtim
#define st_mtim st_mtimespec
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CXY_PASTE__(X, Y) X##Y
#define CXY_PASTE(X, Y) CXY_PASTE__(X, Y)
#define CXY_PASTE_XYZ__(X, Y, Z) X##Y##Z
#define CXY_PASTE_XYZ(X, Y, Z) CXY_PASTE_XYZ__(X, Y, Z)

#define LINE_VAR(name) CXY_PASTE(name, __LINE__)

#define CXY_STR__(V) #V
#define CXY_STR(V) CXY_STR__(V)

#ifndef CXY_VERSION_MAJOR
#define CXY_VERSION_MAJOR 0
#endif

#ifndef CXY_VERSION_MINOR
#define CXY_VERSION_MINOR 1
#endif

#ifndef CXY_VERSION_PATCH
#define CXY_VERSION_PATCH 0
#endif

#define CXY_VERSION_STR                                                        \
    CXY_STR(CXY_VERSION_MAJOR)                                                 \
    "." CXY_STR(CXY_VERSION_MINOR) "." CXY_STR(CXY_VERSION_PATCH)

#ifdef __BASE_FILE__
#define CXY_FILENAME __BASE_FILE__
#else
#define CXY_FILENAME ((strrchr(__FILE__, '/') ?: __FILE__ - 1) + 1)
#endif

// 128-bit integer constants
#define UINT128_C(u)     ((__uint128_t)u)
#define UINT128(h, l)    (UINT128_C(h)<<64 | l)

#define UINT128_MIN      UINT128_C(0)
#define UINT128_MAX      UINT128(UINT64_MAX, UINT64_MAX)
#define INT128_MIN       ((__int128_t)(UINT128_C(1) << 127))
#define INT128_MAX       ((__int128_t)(UINT128_MAX >> 1))

#define UINT128_HIGH(val) ((uint64_t)(((__uint128_t)(val)) >> 64))
#define UINT128_LOW(val)  ((uint64_t)((__uint128_t)(val)))

#define sizeof__(A) (sizeof(A) / sizeof(*(A)))

#ifndef BIT
#define BIT(N) (((u64)1) << (N))
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if __has_attribute(always_inline)
#define cxy_always_inline() inline __attribute__((always_inline))
#else
#define cxy_always_inline()
#endif

#if __has_attribute(unused)
#define cxy_unused() __attribute__((unused))
#else
#define cxy_unused()
#endif

#if __has_attribute(noreturn)
#define cxy_noreturn() __attribute__((noreturn))
#else
#define cxy_noreturn()
#endif

#if __has_attribute(pure)
#define cxy_pure() __attribute__((pure))
#else
#define cxy_pure()
#endif

#if __has_attribute(warn_unused_result)
#define cxy_nodiscard() __attribute__((warn_unused_result))
#else
#define cxy_discard()
#endif

#if __has_attribute(packed)
#define cxy_packed() __attribute__((packed))
#else
#define cxy_packed()
#endif

#if __has_attribute(aligned)
#define cxy_aligned(S) __attribute__((packed, (S)))
#else
#warning                                                                       \
    "Align attribute not available, attempt to use cxy_aligned will cause an error"
#define cxy_aligned(state)                                                     \
    struct cxy_aligned_not_supported_on_current_platform {};
#endif

#if __has_attribute(cleanup)
#define cxy_cleanup(func) __attribute__((cleanup(func)))
#elif __has_attribute(__cleanup__)
#define cxy_cleanup(enclosure) __attribute__((__cleanup__(enclosure)))
#else
#warning                                                                       \
    "Cleanup attribute not available, attempt to use cxy_cleanup will cause an error"
#define cxy_cleanup(state)                                                     \
    struct cxy_clean_not_supported_on_current_platform {}
#endif

#if __has_attribute(format)
#define cxy_format(...) __attribute__((format(__VA_ARGS__)))
#else
#define cxy_format(...)
#endif

#if __has_attribute(fallthrough)
#define cxy_fallthrough() __attribute__((fallthrough))
#else
#define cxy_fallthrough() /* fall through */
#endif

#if __has_attribute(__builtin_unreachable)
#define unreachable(...)                                                       \
    do {                                                                       \
        assert(!"Unreachable code reached");                                   \
        __builtin_unreachable();                                               \
    } while (0)
#else
#define unreachable(...) csAssert(false, "Unreachable code reached");
#endif

#define TODO(fmt, ...) csAssert(false, "TODO: " fmt, ##__VA_ARGS__)

#define attr(A, ...) CXY_PASTE(cxy_, A)(__VA_ARGS__)

#ifndef CXY_ALIGN
#define CXY_ALIGN(S, A) (((S) + ((A) - 1)) & ~((A) - 1))
#endif

typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;
typedef __uint128_t u128;
typedef __int128_t i128;
typedef float f32;
typedef double f64;
typedef uintptr_t uptr;
typedef const char *cstring;
typedef u32 wchar;

#define MIN(A, B)                                                              \
    ({                                                                         \
        __typeof__(A) _A = (A);                                                \
        __typeof__(B) _B = (B);                                                \
        _A < _B ? _A : _B;                                                     \
    })
#define MAX(A, B)                                                              \
    ({                                                                         \
        __typeof__(A) _A = (A);                                                \
        __typeof__(B) _B = (B);                                                \
        _A > _B ? _A : _B;                                                     \
    })

#define unalignedLoad(T, k)                                                    \
    ({                                                                         \
        T LINE_VAR(k);                                                         \
        memcpy(&LINE_VAR(k), k, sizeof(LINE_VAR(k)));                          \
        LINE_VAR(k);                                                           \
    })

#ifndef __cplusplus
#define __stack_str_t(N)                                                       \
    _Static_assert(((N) <= 32), "Stack string's must be small");               \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } Stack_str_##N
#else
#define __stack_str_t(N)                                                       \
    static_assert(((N) <= 32), "Stack string's must be small");                \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } Stack_str_##N
#endif

__stack_str_t(4);
__stack_str_t(8);
__stack_str_t(16);
__stack_str_t(32);

#define Stack_str(N) Stack_str_##N
#define Format_to_ss(SS, fmt, ...)                                             \
    snprintf((SS).str, sizeof((SS).str) - 1, fmt, __VA_ARGS__)

attr(noreturn) attr(format, printf, 1, 2) void cxyAbort(const char *fmt, ...);

#define cxyAssert(COND, FMT, ...)                                              \
    if (!(COND))                                                               \
    cxyAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define csAssert(cond, ...) cxyAssert((cond), ##__VA_ARGS__)
#define csAssert0(cond) cxyAssert((cond), "")

// Style attributes
#define cNONE "0"
#define cBOLD "1"
#define cITALIC "3"

// Color macros with optional style
#define cO(OPT, CODE) "\x1B[" OPT ";" CODE "m"

// Basic colors (no style)
#define cDEF "\x1B[0m"
#define cRED cO(cNONE, "31")
#define cGRN cO(cNONE, "32")
#define cYLW cO(cNONE, "33")
#define cBLU cO(cNONE, "34")
#define cMGN cO(cNONE, "35")
#define cMAG cO(cNONE, "35")
#define cCYN cO(cNONE, "36")
#define cWHT cO(cNONE, "37")

// Bold colors
#define cBRED cO(cBOLD, "31")
#define cBGRN cO(cBOLD, "32")
#define cBYLW cO(cBOLD, "33")
#define cBBLU cO(cBOLD, "34")
#define cBMGN cO(cBOLD, "35")
#define cBCYN cO(cBOLD, "36")
#define cBWHT cO(cBOLD, "37")

// Italic colors
#define cIRED cO(cITALIC, "31")
#define cIGRN cO(cITALIC, "32")
#define cIYLW cO(cITALIC, "33")
#define cIBLU cO(cITALIC, "34")
#define cIMGN cO(cITALIC, "35")
#define cICYN cO(cITALIC, "36")
#define cIWHT cO(cITALIC, "37")

#define CxyPair(T1, T2)                                                        \
    struct {                                                                   \
        T1 f;                                                                  \
        T2 s;                                                                  \
    }

#ifndef __cplusplus
#define make(T, ...) ((T){__VA_ARGS__})
#define __New(P, T, ...)                                                         \
    ({                                                                         \
        T *LINE_VAR(aNa) = callocFromMemPool((P), 1, sizeof(T));               \
        *LINE_VAR(aNa) = make(T, __VA_ARGS__);                                 \
        LINE_VAR(aNa);                                                         \
    })
#endif

struct FormatState;

static inline unsigned ilog2(uintmax_t i)
{
    unsigned p = 0;
    while (i > 0)
        p++, i >>= 1;
    return p;
}

int exec(const char *command, struct FormatState *output);

size_t convertEscapeSeq(const char *str, size_t n, u32 *res);
size_t escapeString(const char *str, size_t n, char *dst, size_t size);
size_t readChar(cstring str, size_t len, u32 *res);
__uint128_t strtou128(const char *str, char **endptr, int base);
i64 formati128(__int128_t value, char *buffer, size_t buffer_size);
i64 formatu128(__uint128_t value, char *buffer, size_t buffer_size);

bool isColorSupported(FILE *);
char *readFile(const char *fileName, size_t *file_size);

int binarySearch(const void *arr,
                 u64 len,
                 const void *x,
                 u64 size,
                 int (*compare)(const void *, const void *));

static inline int binarySearchWithRef(const void *arr,
                                      u64 len,
                                      const void *x,
                                      u64 size,
                                      int (*compare)(const void *,
                                                     const void *))
{
    return binarySearch(arr, len, &x, size, compare);
}

int compareStrings(const void *lhs, const void *rhs);
bool comparePointers(const void *lhs, const void *rhs);

static inline bool isIgnoreVar(cstring s)
{
    return s && s[0] == '_' && s[1] == '\0';
}

static inline u64 timespecToNanoSeconds(struct timespec *ts)
{
    return ts->tv_sec * 1000000000 + ts->tv_nsec;
}

static inline u64 timespecToMicroSeconds(struct timespec *ts)
{
    return ts->tv_sec * 1000000 + ts->tv_nsec / 1000;
}

static inline u64 timespecToMilliseconds(struct timespec *ts)
{
    return ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
}

typedef enum { triYes, triNo, triMaybe } TriState;

static inline TriState triState(TriState lhs, TriState rhs)
{
    return lhs != rhs ? triMaybe : lhs;
}

/**
 * Create a directory at the given path.
 *
 * @param path Directory path to create
 * @param createParents If true, creates parent directories as needed (like mkdir -p)
 * @return true on success, false on failure (errno is set)
 */
bool makeDirectory(const char *path, bool createParents);

/**
 * Write a string to a file, creating or truncating the file.
 *
 * @param path File path to write to
 * @param content String content to write
 * @param size Number of bytes to write from content
 * @return true on success, false on failure (errno is set)
 */
bool writeToFile(const char *path, const char *content, size_t size);

/**
 * Check if a directory is empty (contains no entries except . and ..).
 *
 * @param path Directory path to check
 * @return true if directory is empty, false if not empty or on error (errno is set)
 */
bool isDirectoryEmpty(const char *path);

/**
 * Run a command with progress indicator (spinner and live output).
 *
 * Shows a spinner with the header message while the command runs.
 * Displays the last 2 lines of command output indented by 2 spaces.
 * On success: clears output and shows ✓ with header.
 * On failure: shows ✗ with header and full output indented.
 *
 * @param header Message to display (e.g., "Cloning repository from X")
 * @param command Shell command to execute
 * @param log Logger for status messages
 * @param showOutput Whether to show the full command output on failure
 * @return true if command succeeded (exit code 0), false otherwise
 */
bool runCommandWithProgressFull(const char *header, const char *command, void *log, bool showOutput);

/**
 * Run a command with progress indicator (spinner and live output).
 *
 * Shows a spinner with the header message while the command runs.
 * Displays the last 2 lines of command output indented by 2 spaces.
 * On success: clears output and shows ✓ with header.
 * On failure: shows ✗ with header and full output indented.
 *
 * @param header Message to display (e.g., "Cloning repository from X")
 * @param command Shell command to execute
 * @param log Logger for status messages
 * @return true if command succeeded (exit code 0), false otherwise
 */
static inline bool runCommandWithProgress(const char *header, const char *command, void *log) {
    return runCommandWithProgressFull(header, command, log, false);
}

#ifdef __cplusplus
}
#endif
