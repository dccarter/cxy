#ifndef CXY_SETUP_CODE
#define CXY_SETUP_CODE

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

typedef const char *string;
typedef u32 wchar;
typedef u8 bool;

#define true 1
#define false 0
#define nullptr NULL

#define CXY_PASTE__(X, Y) X##Y
#define CXY_PASTE(X, Y) CXY_PASTE__(X, Y)

#define LINE_VAR(name) CXY_PASTE(name, __LINE__)

#define CXY_STR__(V) #V
#define CXY_STR(V) CXY_STR__(V)

#define sizeof__(A) (sizeof(A) / sizeof(*(A)))

#ifndef BIT
#define BIT(N) (1 << (N))
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
#define cxy_cleanup(func) __attribute__((__cleanup__(func)))
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
        csAssert(false, "Unreachable code reached");                           \
        __builtin_unreachable();                                               \
    } while (0)
#else
#define unreachable(...) csAssert(false, "Unreachable code reached");
#endif

#define attr(A, ...) CXY_PASTE(cxy_, A)(__VA_ARGS__)

#ifndef cxy_ALIGN
#define cxy_ALIGN(S, A) (((S) + ((A)-1)) & ~((A)-1))
#endif

#ifndef ptr
#define ptr(X) ((uintptr_t)(X))
#endif

#ifdef CXY_GC_ENABLED
#define cxy_default_alloc(size, dctor)                                         \
    tgc_alloc_opt(&__cxy_builtins_gc, (size), 0, (dctor))
#define cxy_default_realloc(ptr, size, dctor)                                  \
    tgc_realloc(&__cxy_builtins_gc, (ptr), (size))
#define cxy_default_dealloc(ptr) tgc_free(&__cxy_builtins_gc, (ptr))
#define cxy_default_calloc(count, size, dctor)                                 \
    tgc_calloc_opt(&__cxy_builtins_gc, (count), 0, (size), (dctor))
#else
enum {
    CXY_ALLOC_STATIC = 0b001,
    CXY_ALLOC_HEAP = 0b010,
    CXY_ALLOC_STACK = 0b100
};

#define CXY_MEMORY_MAGIC(ALLOC) (0xbebebe00 | CXY_ALLOC_##ALLOC)

typedef struct cxy_memory_hdr_t {
    union {
        struct {
            u32 magic;
            u32 refs;
        };
        u64 hdr;
    };
    void *dctor;
} attr(packed) cxy_memory_hdr_t;

#define CXY_MEMORY_HEADER_SIZE sizeof(cxy_memory_hdr_t)
#define CXY_MEMORY_HEADER(PTR)                                                 \
    ((void *)(((u8 *)(PTR)) - CXY_MEMORY_HEADER_SIZE))
#define CXY_MEMORY_POINTER(HDR)                                                \
    ((void *)(((u8 *)(HDR)) + CXY_MEMORY_HEADER_SIZE))

static void *cxy_default_alloc(u64 size, void (*dctor)(void *))
{
    cxy_memory_hdr_t *hdr = malloc(size + CXY_MEMORY_HEADER_SIZE);
    hdr->magic = CXY_MEMORY_MAGIC(HEAP);
    hdr->refs = 1;
    hdr->dctor = dctor;
    return CXY_MEMORY_POINTER(hdr);
}

static void *cxy_default_calloc(u64 n, u64 size, void (*dctor)(void *))
{
    cxy_memory_hdr_t *hdr = calloc(n, size + CXY_MEMORY_HEADER_SIZE);
    hdr->magic = CXY_MEMORY_MAGIC(HEAP);
    hdr->refs = 1;
    hdr->dctor = dctor;
    return CXY_MEMORY_POINTER(hdr);
}

static void *cxy_default_realloc(void *ptr, u64 size, void (*dctor)(void *))
{
    if (ptr == NULL)
        return cxy_default_alloc(size, dctor);

    cxy_memory_hdr_t *hdr = CXY_MEMORY_HEADER(ptr);
    if (hdr->magic == CXY_MEMORY_MAGIC(HEAP)) {
        if (hdr->refs == 1) {
            hdr = realloc(hdr, size + CXY_MEMORY_HEADER_SIZE);
            return CXY_MEMORY_POINTER(hdr);
        }
        else {
            --hdr->refs;
        }
    }

    return cxy_default_alloc(size, dctor);
}

void cxy_default_dealloc(void *ctx)
{
    if (ctx) {
        cxy_memory_hdr_t *hdr = CXY_MEMORY_HEADER(ctx);
        if (hdr->magic == CXY_MEMORY_MAGIC(HEAP)) {
            if (hdr->refs == 1) {
                memset(hdr, 0, sizeof(*hdr));
                free(hdr);
            }
            else
                hdr->refs--;
        }
    }
}

#endif

#ifndef cxy_alloc
#define cxy_alloc cxy_default_alloc
#define cxy_free cxy_default_dealloc
#define cxy_realloc cxy_default_realloc
#define cxy_calloc cxy_default_calloc
#endif

#ifndef __builtin_alloc
#define __builtin_alloc(T, n, dctor) cxy_alloc((sizeof(T) * (n)), (dctor))
#endif

#ifndef __builtin_dealloc
#define __builtin_dealloc(P) cxy_default_dealloc((void *)(P))
#endif

#ifndef __builtin_realloc
#define __builtin_realloc(T, P, n, dctor)                                      \
    cxy_realloc((P), (sizeof(T) * (n)), (dctor))
#endif

typedef struct __cxy_builtin_slice_t {
    void *data;
    u64 len;
} __cxy_builtin_slice_t;

void *__builtin_alloc_slice_(u64 count, u64 size, void (*dctor)(void *))
{
    __cxy_builtin_slice_t *slice =
        cxy_alloc(sizeof(__cxy_builtin_slice_t), dctor);
    slice->len = count;
    slice->data = cxy_default_alloc((count * size), nullptr);
    return slice;
}

void *__builtin_realloc_slice_(void *ptr,
                               u64 count,
                               u64 size,
                               void (*dctor)(void *))
{
    __cxy_builtin_slice_t *slice = ptr;
    if (slice == NULL)
        slice = cxy_alloc(sizeof(__cxy_builtin_slice_t), dctor);

    slice->len = count;
    slice->data = cxy_realloc(slice->data, (count * size), nullptr);
    return slice;
}

#ifndef __builtin_alloc_slice
#define __builtin_alloc_slice(T, n, dctor)                                     \
    *((T *)__builtin_alloc_slice_((n), sizeof((*((T *)0)->data)), (dctor)))
#endif

#ifndef __builtin_realloc_slice
#define __builtin_realloc_slice(T, P, n, dctor)                                \
    *((T *)__builtin_realloc_slice_(                                           \
        &(P), (n), sizeof((*((T *)0)->data)), (dctor)))
#endif

#ifndef __builtin_memset_slice
#define __builtin_memset_slice(T, P, C)                                        \
    memset((P).data, (C), (sizeof(((T *)0)->data) * (P).len))
#endif

#ifndef __builtin_free_slice
#define __builtin_free_slice(P)                                                \
    if ((P).data)                                                              \
        cxy_free((P).data);                                                    \
    (P).data = nullptr;                                                        \
    (P).len = 0
#endif

#ifndef __builtin_init_slice
#define __builtin_init_slice(P)                                                \
    (P).data = nullptr;                                                        \
    (P).len = 0;
#endif

#ifndef __builtin_assert
#define __builtin_assert(cond, file, line, pos)                                \
    if (!(cond))                                                               \
    cxyAbort("assertion failed (" #cond ") : %s:%d:%d\n", file, line, pos)
#endif

#ifndef __builtin_sizeof
#define __builtin_sizeof(X) sizeof(X)
#endif

static attr(noreturn)
    attr(format, printf, 1, 2) void cxyAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}

#define cxyAssert(COND, FMT, ...)                                              \
    if (!(COND))                                                               \
    cxyAbort("%s:%d : (" #COND ") " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define csAssert(cond, ...) cxyAssert((cond), ##__VA_ARGS__)
#define csAssert0(cond) cxyAssert((cond), "")

#define cxy_stack_str_t(N)                                                     \
    _Static_assert(((N) <= 32), "Stack string's must be small");               \
    typedef struct Stack_str_##N##_t {                                         \
        char str[(N) + 1];                                                     \
    } cxy_stack_str_##N##_t

cxy_stack_str_t(4);
cxy_stack_str_t(8);
cxy_stack_str_t(16);
cxy_stack_str_t(32);

static cxy_stack_str_8_t cxy_wchar_str(wchar chr)
{
    if (chr < 0x80) {
        return (cxy_stack_str_8_t){
            .str = {[0] = (char)chr, [1] = '\0', [5] = 1}};
    }
    else if (chr < 0x800) {
        return (cxy_stack_str_8_t){.str = {[0] = (char)(0xC0 | (chr >> 6)),
                                           [1] = (char)(0x80 | (chr & 0x3F)),
                                           [3] = '\0',
                                           [5] = 2}};
    }
    else if (chr < 0x10000) {
        return (cxy_stack_str_8_t){
            .str = {[0] = (char)(0xE0 | (chr >> 12)),
                    [1] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                    [2] = (char)(0x80 | (chr & 0x3F)),
                    [3] = '\0',
                    [5] = 3}};
    }
    else if (chr < 0x200000) {
        return (cxy_stack_str_8_t){
            .str = {[0] = (char)(0xF0 | (chr >> 18)),
                    [1] = (char)(0x80 | ((chr >> 12) & 0x3F)),
                    [2] = (char)(0x80 | ((chr >> 6) & 0x3F)),
                    [3] = (char)(0x80 | (chr & 0x3F)),
                    [4] = '\0',
                    [5] = 4}};
    }
    else {
        unreachable("!!!Invalid UCS character: \\U%08x", chr);
    }
}

static inline u64 fwputc(wchar c, FILE *io)
{
    cxy_stack_str_8_t s = cxy_wchar_str(c);
    return fwrite(s.str, 1, s.str[5], io);
}

static inline u64 wputc(wchar c)
{
    cxy_stack_str_8_t s = cxy_wchar_str(c);
    s.str[4] = '\n';
    s.str[5] += 1;
    return fwrite(s.str, 1, s.str[5], stdout);
}

typedef struct {
    u64 size;
    char *data;
} __cxy_builtins_string_t;

attr(always_inline) static void __cxy_builtins_string_delete(void *str)
{
    __cxy_builtins_string_t *this = str;
    cxy_free(this->data);
    this->data = nullptr;
    this->data = 0;
}

static __cxy_builtins_string_t *__cxy_builtins_string_new0(const char *cstr,
                                                           u64 len)
{
    __cxy_builtins_string_t *str =
        cxy_alloc(sizeof(__cxy_builtins_string_t) + len + 1,
                  __cxy_builtins_string_delete);
    str->size = len;
    if (cstr != NULL)
        memcpy(str->data, cstr, len);
    str->data[len] = '\0';
    return str;
}

attr(always_inline) static __cxy_builtins_string_t *__cxy_builtins_string_new1(
    const char *cstr)
{
    return __cxy_builtins_string_new0(cstr, strlen(cstr));
}

static attr(always_inline) __cxy_builtins_string_t *__cxy_builtins_string_dup(
    const __cxy_builtins_string_t *str)
{
    return __cxy_builtins_string_new0(str->data, str->size);
}

static __cxy_builtins_string_t *__cxy_builtins_string_concat(
    const __cxy_builtins_string_t *s1, const __cxy_builtins_string_t *s2)
{
    __cxy_builtins_string_t *str =
        __cxy_builtins_string_new0(NULL, s1->size + s2->size);
    memcpy(str->data, s1->data, s1->size);
    memcpy(&str->data[s1->size], s2->data, s2->size);
    return str;
}

#ifndef __cxy_builtins_string_builder_DEFAULT_CAPACITY
#define __cxy_builtins_string_builder_DEFAULT_CAPACITY 32
#endif

typedef struct {
    u64 capacity;
    u64 size;
    char *data;
} __cxy_builtins_string_builder_t;

void __cxy_builtins_string_builder_grow(__cxy_builtins_string_builder_t *sb,
                                        u64 size)
{
    if (sb->data == NULL) {
        sb->data = cxy_alloc(size + 1, nullptr);
        sb->capacity = size;
    }
    else if (size > (sb->capacity - sb->size)) {
        while (sb->capacity < sb->size + size) {
            sb->capacity <<= 1;
        }
        sb->data = cxy_realloc(sb->data, sb->capacity + 1, nullptr);
    }
}

attr(always_inline) void __cxy_builtins_string_builder_init(
    __cxy_builtins_string_builder_t *sb)
{
    __cxy_builtins_string_builder_grow(
        sb, __cxy_builtins_string_builder_DEFAULT_CAPACITY);
}

static void __cxy_builtins_string_builder_delete_fwd(void *sb);

__cxy_builtins_string_builder_t *__cxy_builtins_string_builder_new()
{
    __cxy_builtins_string_builder_t *sb =
        cxy_calloc(1,
                   sizeof(__cxy_builtins_string_builder_t),
                   __cxy_builtins_string_builder_delete_fwd);
    __cxy_builtins_string_builder_init(sb);
    return sb;
}

void __cxy_builtins_string_builder_deinit(__cxy_builtins_string_builder_t *sb)
{
    if (sb->data)
        free(sb->data);
    memset(sb, 0, sizeof(*sb));
}

attr(always_inline) void __cxy_builtins_string_builder_delete(
    __cxy_builtins_string_builder_t *sb)
{
    if (sb)
        cxy_free(sb);
}

static void __cxy_builtins_string_builder_delete_fwd(void *sb)
{
    __cxy_builtins_string_builder_delete((__cxy_builtins_string_builder_t *)sb);
}

void __cxy_builtins_string_builder_append_cstr0(
    __cxy_builtins_string_builder_t *sb, const char *cstr, u64 len)
{
    __cxy_builtins_string_builder_grow(sb, len);
    memmove(&sb->data[sb->size], cstr, len);
    sb->size += len;
    sb->data[sb->size] = '\0';
}

attr(always_inline) void __cxy_builtins_string_builder_append_cstr1(
    __cxy_builtins_string_builder_t *sb, const char *cstr)
{
    __cxy_builtins_string_builder_append_cstr0(sb, cstr, strlen(cstr));
}

attr(always_inline) void __cxy_builtins_string_builder_append_int(
    __cxy_builtins_string_builder_t *sb, i64 num)
{
    char data[32];
    i64 len = sprintf(data, "%lld", num);
    __cxy_builtins_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) void __cxy_builtins_string_builder_append_float(
    __cxy_builtins_string_builder_t *sb, f64 num)
{
    char data[32];
    i64 len = sprintf(data, "%g", num);
    __cxy_builtins_string_builder_append_cstr0(sb, data, len);
}

attr(always_inline) void __cxy_builtins_string_builder_append_char(
    __cxy_builtins_string_builder_t *sb, wchar c)
{
    cxy_stack_str_8_t s = cxy_wchar_str(c);
    __cxy_builtins_string_builder_append_cstr0(sb, s.str, s.str[5]);
}

attr(always_inline) void __cxy_builtins_string_builder_append_bool(
    __cxy_builtins_string_builder_t *sb, bool v)
{
    if (v)
        __cxy_builtins_string_builder_append_cstr0(sb, "true", 4);
    else
        __cxy_builtins_string_builder_append_cstr0(sb, "false", 5);
}

char *__cxy_builtins_string_builder_release(__cxy_builtins_string_builder_t *sb)
{
    char *data = sb->data;
    sb->data = NULL;
    __cxy_builtins_string_builder_deinit(sb);
    return data;
}

int __cxy_builtins_binary_search(const void *arr,
                                 u64 len,
                                 const void *x,
                                 u64 size,
                                 int (*compare)(const void *, const void *))
{
    int lower = 0;
    int upper = (int)len - 1;
    const u8 *ptr = arr;
    while (lower <= upper) {
        int mid = lower + (upper - lower) / 2;
        int res = compare(x, ptr + (size * mid));
        if (res == 0)
            return mid;

        if (res > 0)
            lower = mid + 1;
        else
            upper = mid - 1;
    }
    return -1;
}

typedef struct {
    i64 value;
    const char *name;
} __cxy_builtins_enum_name_t;

static int __cxy_builtins_enum_name_compare(const void *lhs, const void *rhs)
{
    return (int)(((__cxy_builtins_enum_name_t *)lhs)->value -
                 ((__cxy_builtins_enum_name_t *)rhs)->value);
}

const char *__cxy_builtins_enum_find_name(
    const __cxy_builtins_enum_name_t *names, u64 count, u64 value)
{
    __cxy_builtins_enum_name_t name = {.value = value};
    int index = __cxy_builtins_binary_search(
        names, count, &name, sizeof(name), __cxy_builtins_enum_name_compare);
    if (index >= 0)
        return names[index].name;

    return "(Unknown)";
}

typedef uint32_t __cxy_builtins_hash_code_t;

attr(always_inline) __cxy_builtins_hash_code_t __cxy_builtins_fnv1a_init()
{
#define FNV_32_INIT UINT32_C(0x811c9dc5)
    return FNV_32_INIT;
#undef FNV_32_INIT
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_uint8(__cxy_builtins_hash_code_t h, uint8_t x)
{
#define FNV_32_PRIME 0x01000193
    return (h ^ x) * FNV_32_PRIME;
#undef FNV_32_PRIME
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_uint16(__cxy_builtins_hash_code_t h, uint16_t x)
{
    return __cxy_builtins_fnv1a_uint8(__cxy_builtins_fnv1a_uint8(h, x), x >> 8);
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_uint32(__cxy_builtins_hash_code_t h,
                                __cxy_builtins_hash_code_t x)
{
    return __cxy_builtins_fnv1a_uint16(__cxy_builtins_fnv1a_uint16(h, x),
                                       x >> 16);
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_uint64(__cxy_builtins_hash_code_t h, uint64_t x)
{
    return __cxy_builtins_fnv1a_uint32(__cxy_builtins_fnv1a_uint32(h, x),
                                       x >> 32);
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_ptr(__cxy_builtins_hash_code_t h, const void *ptr)
{
    return __cxy_builtins_fnv1a_uint64(h, (ptrdiff_t)ptr);
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_string(__cxy_builtins_hash_code_t h, const char *str)
{
    while (*str)
        h = __cxy_builtins_fnv1a_uint8(h, *(str++));
    return h;
}

attr(always_inline) __cxy_builtins_hash_code_t
    __cxy_builtins_fnv1a_bytes(__cxy_builtins_hash_code_t h,
                               const void *ptr,
                               u64 size)
{
    for (u64 i = 0; i < size; ++i)
        h = __cxy_builtins_fnv1a_uint8(h, ((char *)ptr)[i]);
    return h;
}

#define CXY_MIN_PRIME 7
#define CXY_MAX_PRIME 1048583
#define CXY_PRIMES(f)                                                          \
    f(CXY_MIN_PRIME) f(17) f(31) f(67) f(257) f(1031) f(4093) f(8191) f(16381) \
        f(32381) f(65539) f(131071) f(262147) f(524287) f(CXY_MAX_PRIME)

static const u64 cxy_primes[] = {
#define f(x) x,
    CXY_PRIMES(f)
#undef f
};

// Returns the prime that is strictly greater than the given value.
// If there is no such prime in the list, returns MAX_PRIME.
static u64 __cxy_builtins_next_prime(u64 i)
{
    u64 j = 0, k = sizeof__(cxy_primes);
    while (j < k) {
        u64 m = (j + k) / 2;
        u64 p = cxy_primes[m];
        if (p <= i)
            j = m + 1;
        else
            k = m;
    }
    return cxy_primes[k >= sizeof__(cxy_primes) ? sizeof__(cxy_primes) - 1 : k];
}

// Returns the modulus of a number i by a prime p.
static u64 __cxy_builtins_mod_prime(u64 i, u64 p)
{
    switch (p) {
#define f(x)                                                                   \
    case x:                                                                    \
        return i % x;
        CXY_PRIMES(f)
#undef f
    default:
        return i % p;
    }
}

#endif
