#include "core/utils.h"
#include "core/alloc.h"
#include "format.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>

#ifdef WIN32
#define isatty _isatty
#define fileno _fileno
#include <io.h>
#else
#include <string.h>
#include <unistd.h>
#endif

#ifndef NDEBUG
#define CHUNK_SIZE 4
#else
#define CHUNK_SIZE 4096
#endif

typedef CxyPair(u32, u32) u32_u32_pair;

static inline bool isodigit(int c) { return c >= '0' && c <= '7'; }

static size_t convertStrToCharOrd(const char *ptr, int base, u32 *res)
{
    char *next = NULL;
    unsigned int ord = strtoul(ptr, &next, base);
    *res = ord;
    return ord <= 255 && !errno ? next - ptr : 0;
}

__uint128_t strtou128(const char *str, char **endptr, int base)
{
    if (str == NULL || base < 2 || base > 36) {
        if (endptr) *endptr = (char *)str;
        errno = EINVAL;
        return 0;
    }

    // Skip whitespace
    while (isspace((unsigned char)*str)) str++;

    // Handle empty string
    if (*str == '\0') {
        if (endptr) *endptr = (char *)str;
        return 0;
    }

    // Handle sign (128-bit unsigned, so no negative)
    if (*str == '+') str++;
    else if (*str == '-') {
        if (endptr) *endptr = (char *)str;
        errno = ERANGE;
        return 0;
    }

    // Auto-detect base if base is 0
    if (base == 0) {
        if (*str == '0') {
            if (str[1] == 'x' || str[1] == 'X') {
                base = 16;
                str += 2;
            } else if (str[1] == 'b' || str[1] == 'B') {
                base = 2;
                str += 2;
            } else {
                base = 8;
                str += 1;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    } else if (base == 2 && str[0] == '0' && (str[1] == 'b' || str[1] == 'B')) {
        str += 2;
    }

    __uint128_t result = 0;
    __uint128_t max_val = (__uint128_t)-1;
    __uint128_t cutoff = max_val / base;
    int cutlim = max_val % base;
    bool overflow = false;
    const char *start = str;

    while (*str) {
        int digit;
        char c = *str;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            digit = c - 'A' + 10;
        } else {
            break; // Invalid character, stop parsing
        }

        if (digit >= base) {
            break; // Invalid digit for this base
        }

        // Check for overflow
        if (result > cutoff || (result == cutoff && digit > cutlim)) {
            overflow = true;
            break;
        }

        result = result * base + digit;
        str++;
    }

    if (endptr) {
        *endptr = (char *)(str == start ? start : str);
    }

    if (overflow) {
        errno = ERANGE;
        return (__uint128_t)-1;
    }

    return result;
}

static void pu64_to_buffer(__uint64_t u, char **buf, size_t *remaining) {
    int written = snprintf(*buf, *remaining, "%"PRIu64, u);
    if (written > 0 && (size_t)written <= *remaining) {
        *buf += written;
        *remaining -= written;
    }
}

static void pu640_to_buffer(__uint64_t u, char **buf, size_t *remaining) {
    int written = snprintf(*buf, *remaining, "%019"PRIu64, u);
    if (written > 0 && (size_t)written <= *remaining) {
        *buf += written;
        *remaining -= written;
    }
}

#define D19_ UINT64_C(10000000000000000000)
const __uint128_t d19_ = D19_;
const __uint128_t d38_ = (UINT128_C(D19_)*D19_);

i64 formatu128(__uint128_t u, char *buffer, size_t buffer_size) {
    if (buffer_size == 0) return 0;

    char *buf = buffer;
    size_t remaining = buffer_size;

    if (u < d19_)      { pu64_to_buffer(u, &buf, &remaining); }
    else if (u < d38_) { pu64_to_buffer(u/d19_, &buf, &remaining); pu640_to_buffer(u%d19_, &buf, &remaining); }
    else               { pu64_to_buffer(u/d38_, &buf, &remaining); u%=d38_; pu640_to_buffer(u/d19_, &buf, &remaining); pu640_to_buffer(u%d19_, &buf, &remaining); }
    return buffer_size - remaining;
}

i64 formati128(__int128_t i, char *buffer, size_t buffer_size) {
    if (buffer_size == 0) return 0;

    if (i < 0) {
        if (buffer_size < 2) {
            buffer[0] = '\0';
            return 0;
        }
        buffer[0] = '-';

        // Handle the edge case: most negative i128 value cannot be negated
        if (i == (__int128_t)((__uint128_t)1 << 127)) {
            // This is -2^127, the most negative value
            return formatu128((__uint128_t)1 << 127, buffer + 1, buffer_size - 1) + 1;
        } else {
            return formatu128((__uint128_t)(-i), buffer + 1, buffer_size - 1) + 1;
        }
    } else {
        return formatu128((__uint128_t)i, buffer, buffer_size);
    }
}

static u32 inline countLeadingZeros(char c)
{
    for (int i = 7; i >= 0; i--) {
        if ((c & (1 << i)) == 0)
            return 7 - i;
    }
    return 8;
}

static u32_u32_pair convertStrToUtf32(const char *s, size_t count)
{
    u32 len = countLeadingZeros(s[0]);

    if (len == 0) {
        return (u32_u32_pair){1, (u32)s[0]};
    }
    csAssert(count <= len, "invalid UTF-8 character sequence");

    switch (len) {
    case 2:
        return (u32_u32_pair){2, ((s[0] & 0x1F) << 6) | (s[1] & 0x3F)};
    case 3:
        return (u32_u32_pair){
            3, ((s[0] & 0xF) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F)};
    case 4:
        return (u32_u32_pair){4,
                              ((s[0] & 0x7) << 18) | ((s[1] & 0x3F) << 12) |
                                  ((s[2] & 0x3F) << 6) | (s[3] & 0x3F)};
    default:
        csAssert(false, "invalid UTF-8 sequence");
    }

    unreachable("");
}

// Helper function to parse Unicode escape sequences
// Returns the number of characters consumed, or 0 on error
static size_t parseUnicodeEscape(const char *ptr, size_t n, int digits, u32 *res)
{
    if (n < digits + 2) // Need at least \u or \U plus the hex digits
        return 0;

    *res = 0;
    for (int i = 0; i < digits; i++) {
        char c = ptr[2 + i];
        if (!isxdigit(c))
            return 0;

        *res <<= 4;
        if (c >= '0' && c <= '9')
            *res += c - '0';
        else if (c >= 'a' && c <= 'f')
            *res += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            *res += c - 'A' + 10;
    }

    // Check if it's a valid Unicode code point
    if (*res > 0x10FFFF || (*res >= 0xD800 && *res <= 0xDFFF))
        return 0; // Invalid code point

    return 2 + digits; // \u/\U + hex digits
}

size_t convertEscapeSeq(const char *ptr, size_t n, u32 *res)
{
    if (n == 0)
        return 0;
    if (ptr[0] == '\\') {
        if (n <= 1)
            return 0;
        switch (ptr[1]) {
        case 'n':
            *res = '\n';
            return 2;
        case 't':
            *res = '\t';
            return 2;
        case 'v':
            *res = '\v';
            return 2;
        case 'r':
            *res = '\r';
            return 2;
        case 'a':
            *res = '\a';
            return 2;
        case 'b':
            *res = '\b';
            return 2;
        case 'f':
            *res = '\f';
            return 2;
        case '$':
            *res = '$';
            return 2;
        case '"':
            *res = '"';
            return 2;
        case '\'':
            *res = '\'';
            return 2;
        case '\\':
            *res = '\\';
            return 2;
        case 'x':
            if (n <= 2)
                return 0;
            return 2 + convertStrToCharOrd(ptr + 2, 16, res);
        case '0':
            if (!isodigit(ptr[2]) || !isodigit(ptr[3])) {
                *res = '\0';
                return 2;
            }
            return 1 + convertStrToCharOrd(ptr + 1, 8, res);
        case 'u':
            if (n < 6) // Need \uXXXX (6 chars total)
                return 0;
            return parseUnicodeEscape(ptr, n, 4, res);
        case 'U':
            if (n < 10) // Need \UXXXXXXXX (10 chars total)
                return 0;
            return parseUnicodeEscape(ptr, n, 8, res);
        default:
            if (isdigit(ptr[1]))
                return 1 + convertStrToCharOrd(ptr + 1, 8, res);
            return 0;
        }
    }
    if (((u8 *)ptr)[0] >= 0x80) {
        u32_u32_pair p = convertStrToUtf32(ptr, n);
        *res = p.s;
        return p.f;
    }

    *res = (u8)ptr[0];
    return 1;
}

size_t readChar(cstring str, size_t len, u32 *res)
{
    if (((u8 *)str)[0] >= 0x80) {
        size_t tmp = countLeadingZeros(str[0]);
        csAssert(tmp <= len, "invalid UTF-8 character sequence");
        len = tmp;
    }
    return convertEscapeSeq(str, len, res);
}

size_t escapeString(const char *str, size_t n, char *dst, size_t size)
{
    csAssert0(size >= n);

    u64 i = 0, j = 0;

    while (i < n) {
        if (str[i] != '\\') {
            dst[j++] = str[i++];
            continue;
        }

        i++;
        if ((n - i) < 1)
            continue;

        switch (str[i]) {
        case 'n':
            dst[j++] = '\n';
            i++;
            break;
        case 't':
            dst[j++] = '\t';
            i++;
            break;
        case 'v':
            dst[j++] = '\v';
            i++;
            break;
        case 'r':
            dst[j++] = '\r';
            i++;
            break;
        case 'a':
            dst[j++] = '\a';
            i++;
            break;
        case 'b':
            dst[j++] = '\b';
            i++;
            break;
        case '$':
            dst[j++] = '$';
            i++;
            break;
        case '"':
            dst[j++] = '"';
            i++;
            break;
        case '0':
            if (!isdigit(str[i + 1])) {
                dst[j++] = '\0';
                i++;
                break;
            }
        default:
            dst[j++] = '\\';
            break;
        }
    }

    dst[j] = 0;
    return j;
}

bool isColorSupported(FILE *file) { return isatty(fileno(file)); }

char *readFile(const char *fileName, size_t *file_size)
{
    FILE *file = fopen(fileName, "rb");
    if (!file)
        return NULL;
    size_t chunk_size = CHUNK_SIZE;
    char *file_data = NULL;
    *file_size = 0;
    while (true) {
        if (ferror(file)) {
            fclose(file);
            free(file_data);
            return NULL;
        }
        file_data = reallocOrDie(file_data, *file_size + chunk_size);
        size_t read_count = fread(file_data + *file_size, 1, chunk_size, file);
        *file_size += read_count;
        if (read_count < chunk_size)
            break;
        chunk_size *= 2;
    }
    fclose(file);

    // Add terminator
    file_data = reallocOrDie(file_data, *file_size + 1);
    file_data[*file_size] = 0;
    return file_data;
}

int binarySearch(const void *arr,
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

int compareStrings(const void *lhs, const void *rhs)
{
    return strcmp((cstring)lhs, (cstring)rhs);
}

bool comparePointers(const void *lhs, const void *rhs)
{
    return *((void **)lhs) == *((void **)rhs);
}

void cxyAbort(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    abort();
}

#ifndef NO_EXEC_UTIL

int exec(const char *command, FormatState *output)
{
    FILE *pipe = popen(command, "r");
    csAssert(
        pipe != NULL, "failed to execute '%s': %s\n", command, strerror(errno));

    char buffer[128];
    while (fgets(buffer, sizeof buffer, pipe)) {
        append(output, buffer, strlen(buffer));
    }

    int status = pclose(pipe);
    return WEXITSTATUS(status);
}

#endif
