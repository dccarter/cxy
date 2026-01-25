/**
 * Unit Tests: Core Utils
 *
 * Tests for core utility functions, specifically the strtou128 function
 * for parsing 128-bit unsigned integers from strings.
 */

#include "doctest.h"
#include "core/utils.h"
#include <errno.h>
#include <cstring>

TEST_CASE("strtou128 Basic Parsing") {
    char *endptr;
    __uint128_t result;

    SUBCASE("Simple decimal numbers") {
        result = strtou128("123", &endptr, 10);
        REQUIRE(result == 123);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Zero") {
        result = strtou128("0", &endptr, 10);
        REQUIRE(result == 0);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Single digit") {
        result = strtou128("7", &endptr, 10);
        REQUIRE(result == 7);
        REQUIRE(*endptr == '\0');
    }
}

TEST_CASE("strtou128 Different Bases") {
    char *endptr;
    __uint128_t result;

    SUBCASE("Hexadecimal") {
        result = strtou128("FF", &endptr, 16);
        REQUIRE(result == 255);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Hexadecimal with 0x prefix") {
        result = strtou128("0xFF", &endptr, 16);
        REQUIRE(result == 255);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Binary") {
        result = strtou128("1010", &endptr, 2);
        REQUIRE(result == 10);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Binary with 0b prefix") {
        result = strtou128("0b1010", &endptr, 2);
        REQUIRE(result == 10);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Octal") {
        result = strtou128("777", &endptr, 8);
        REQUIRE(result == 511);
        REQUIRE(*endptr == '\0');
    }

    // TODO: Fix auto-detect logic
    // SUBCASE("Auto-detect base (hex)") {
    //     result = strtou128("0xFF", &endptr, 0);
    //     CHECK(result == 255);
    //     CHECK(*endptr == '\0');
    // }

    // SUBCASE("Auto-detect base (binary)") {
    //     result = strtou128("0b1010", &endptr, 0);
    //     REQUIRE(result == 10);
    //     REQUIRE(*endptr == '\0');
    // }

    // SUBCASE("Auto-detect base (octal)") {
    //     result = strtou128("0777", &endptr, 0);
    //     REQUIRE(result == 511);
    //     REQUIRE(*endptr == '\0');
    // }
}

TEST_CASE("strtou128 Large Numbers") {
    char *endptr;
    __uint128_t result;

    SUBCASE("64-bit maximum") {
        result = strtou128("18446744073709551615", &endptr, 10);
        REQUIRE(result == 18446744073709551615ULL);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Large 128-bit number") {
        // 2^100 = 1267650600228229401496703205376
        result = strtou128("1267650600228229401496703205376", &endptr, 10);
        REQUIRE(result != 0); // Should parse successfully
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Very large hex number") {
        result = strtou128("123456789ABCDEF0", &endptr, 16);
        REQUIRE(result != 0); // Should parse successfully
        REQUIRE(*endptr == '\0');
    }
}

TEST_CASE("strtou128 Whitespace Handling") {
    char *endptr;
    __uint128_t result;

    SUBCASE("Leading whitespace") {
        result = strtou128("  123", &endptr, 10);
        REQUIRE(result == 123);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Leading tabs and spaces") {
        result = strtou128("\t  \n 456", &endptr, 10);
        REQUIRE(result == 456);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Plus sign") {
        result = strtou128("+789", &endptr, 10);
        REQUIRE(result == 789);
        REQUIRE(*endptr == '\0');
    }
}

TEST_CASE("strtou128 Error Handling") {
    char *endptr;
    __uint128_t result;

    SUBCASE("Invalid characters") {
        result = strtou128("abc", &endptr, 10);
        REQUIRE(result == 0);
        REQUIRE(endptr != nullptr);
        REQUIRE(*endptr == 'a'); // Points to first invalid character
    }

    SUBCASE("Mixed valid and invalid") {
        result = strtou128("123xyz", &endptr, 10);
        REQUIRE(result == 123);
        REQUIRE(*endptr == 'x'); // Points to first invalid character
    }

    SUBCASE("Negative number") {
        errno = 0;
        result = strtou128("-123", &endptr, 10);
        REQUIRE(errno == ERANGE);
        REQUIRE(result == 0);
    }

    SUBCASE("Empty string") {
        result = strtou128("", &endptr, 10);
        REQUIRE(result == 0);
        REQUIRE(endptr != nullptr);
    }

    SUBCASE("Invalid base") {
        errno = 0;
        result = strtou128("123", &endptr, 1); // Base 1 is invalid
        REQUIRE(errno == EINVAL);
        REQUIRE(result == 0);
    }

    SUBCASE("Base too large") {
        errno = 0;
        result = strtou128("123", &endptr, 37); // Base > 36 is invalid
        REQUIRE(errno == EINVAL);
        REQUIRE(result == 0);
    }
}

TEST_CASE("strtou128 Partial Parsing") {
    char *endptr;
    __uint128_t result;

    SUBCASE("Valid number followed by text") {
        result = strtou128("123abc", &endptr, 10);
        REQUIRE(result == 123);
        REQUIRE(std::strcmp(endptr, "abc") == 0);
    }

    SUBCASE("Hex number with invalid hex digits") {
        result = strtou128("123G", &endptr, 16);
        REQUIRE(result == 0x123);
        REQUIRE(*endptr == 'G');
    }

    SUBCASE("Binary with invalid binary digits") {
        result = strtou128("1012", &endptr, 2);
        REQUIRE(result == 5); // 101 in binary = 5 in decimal
        REQUIRE(*endptr == '2');
    }
}

TEST_CASE("formatu128 Basic Formatting") {
    char buffer[64];

    SUBCASE("Zero") {
        formatu128(0, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "0") == 0);
    }

    SUBCASE("Small numbers") {
        formatu128(123, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "123") == 0);
    }

    SUBCASE("Single digit") {
        formatu128(7, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "7") == 0);
    }

    SUBCASE("64-bit maximum") {
        formatu128(18446744073709551615ULL, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "18446744073709551615") == 0);
    }
}

TEST_CASE("formatu128 Large Numbers") {
    char buffer[64];

    SUBCASE("Medium number (2 chunks)") {
        // A number larger than 10^19 requiring 2 chunks
        __uint128_t medium = (__uint128_t)10000000000000000000ULL * 10; // 10^20
        formatu128(medium, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "100000000000000000000") == 0);
    }

    SUBCASE("Large number (3 chunks)") {
        // A number requiring 3 chunks: approximately 10^38
        __uint128_t large = (__uint128_t)1000000000000000000ULL * 1000000000000000000ULL * 100;
        formatu128(large, buffer, sizeof(buffer));
        REQUIRE(std::strlen(buffer) > 30); // Should be very long
        REQUIRE(buffer[0] == '1'); // Should start with 1
    }

    SUBCASE("Very large 128-bit number") {
        // 2^100 = 1267650600228229401496703205376
        __uint128_t big = (__uint128_t)1 << 100;
        formatu128(big, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "1267650600228229401496703205376") == 0);
    }
}

TEST_CASE("formatu128 Buffer Handling") {
    char buffer[64];

    SUBCASE("Small buffer") {
        char small[8];
        formatu128(1234567890, small, sizeof(small));
        REQUIRE(std::strlen(small) < sizeof(small));
    }

    SUBCASE("Zero buffer size") {
        formatu128(123, buffer, 0);
        // Should not crash and not write anything
    }

    SUBCASE("Exact fit") {
        char exact[5]; // For "1234" + null terminator
        formatu128(1234, exact, sizeof(exact));
        REQUIRE(std::strcmp(exact, "1234") == 0);
    }
}

TEST_CASE("formati128 Basic Formatting") {
    char buffer[64];

    SUBCASE("Zero") {
        formati128(0, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "0") == 0);
    }

    SUBCASE("Positive numbers") {
        formati128(123, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "123") == 0);
    }

    SUBCASE("Negative numbers") {
        formati128(-123, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "-123") == 0);
    }

    SUBCASE("Large positive") {
        formati128(9223372036854775807LL, buffer, sizeof(buffer)); // INT64_MAX
        REQUIRE(std::strcmp(buffer, "9223372036854775807") == 0);
    }

    SUBCASE("Large negative") {
        formati128(-9223372036854775807LL, buffer, sizeof(buffer));
        REQUIRE(std::strcmp(buffer, "-9223372036854775807") == 0);
    }
}

TEST_CASE("formati128 Edge Cases") {
    char buffer[64];

    SUBCASE("Most negative 128-bit value") {
        // -2^127
        __int128_t min_val = (__int128_t)((__uint128_t)1 << 127);
        formati128(min_val, buffer, sizeof(buffer));
        REQUIRE(buffer[0] == '-'); // Should start with minus
        REQUIRE(std::strlen(buffer) > 30); // Should be very long
    }

    SUBCASE("Maximum positive 128-bit value") {
        // 2^127 - 1
        __int128_t max_val = (__int128_t)(((__uint128_t)1 << 127) - 1);
        formati128(max_val, buffer, sizeof(buffer));
        REQUIRE(buffer[0] != '-'); // Should be positive
        REQUIRE(std::strlen(buffer) > 30); // Should be very long
    }
}

TEST_CASE("formati128 Buffer Handling") {
    char buffer[64];

    SUBCASE("Small buffer with negative") {
        char small[8];
        formati128(-1234, small, sizeof(small));
        REQUIRE(std::strlen(small) < sizeof(small));
        REQUIRE(small[0] == '-');
    }

    SUBCASE("Zero buffer size") {
        formati128(-123, buffer, 0);
        // Should not crash
    }

    SUBCASE("Buffer too small for negative") {
        char tiny[2]; // Only room for "-" + null
        formati128(-123, tiny, sizeof(tiny));
        // Should handle gracefully
    }
}

TEST_CASE("Format Round-trip Consistency") {
    char buffer[64];
    char *endptr;

    SUBCASE("Unsigned round-trip") {
        __uint128_t original = 123456789012345ULL;
        formatu128(original, buffer, sizeof(buffer));
        __uint128_t parsed = strtou128(buffer, &endptr, 10);
        REQUIRE(parsed == original);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Large unsigned round-trip") {
        __uint128_t original = (__uint128_t)1 << 100;
        formatu128(original, buffer, sizeof(buffer));
        __uint128_t parsed = strtou128(buffer, &endptr, 10);
        REQUIRE(parsed == original);
        REQUIRE(*endptr == '\0');
    }

    SUBCASE("Signed positive round-trip") {
        __int128_t original = 123456789012345LL;
        formati128(original, buffer, sizeof(buffer));
        __uint128_t parsed = strtou128(buffer, &endptr, 10);
        REQUIRE((__int128_t)parsed == original);
        REQUIRE(*endptr == '\0');
    }
}
