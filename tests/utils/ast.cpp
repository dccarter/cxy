/**
 * AST Validation Utility - C++ Implementation
 *
 * Implementation of modern C++ utilities for validating AST nodes.
 * Interfaces with the existing C S-expression dumper.
 */

#include "ast.hpp"
#include "lang/middle/dump/sexp.h"
#include "core/format.h"
#include "driver/driver.h"
#include <sstream>

namespace cxy::test {

// MemPoolWrapper implementation
MemPoolWrapper::MemPoolWrapper() : pool_(newMemPool()) {}

MemPoolWrapper::~MemPoolWrapper() {
    freeMemPool(&pool_);
}



// ASTTestUtils implementation
namespace {
    /**
     * @brief Configure CompilerDriver with CompareOptions for C interface
     */
    void configureDriver(CompilerDriver& driver, const CompareOptions& options) {
        // Zero out the driver to ensure clean state
        memset(&driver, 0, sizeof(driver));

        // Configure the development options
        driver.options.dev.withLocation = !options.ignoreLocation;
        driver.options.dev.withoutAttrs = options.ignoreMetadata;
        driver.options.dev.withNamedEnums = false; // Keep simple for comparison

        // Important: Set the dump mode to exclude metadata when requested
        if (options.ignoreMetadata) {
            driver.options.dev.withoutAttrs = true;
            driver.options.dev.withLocation = false;
        }
    }
}

std::string ASTTestUtils::toString(const AstNode* node, const CompareOptions& options) {
    if (!node) {
        return "(null)";
    }

    MemPoolWrapper pool;

    // Create minimal CompilerDriver for dumper
    CompilerDriver driver;
    configureDriver(driver, options);
    driver.pool = pool.get();

    // Create FormatState for string output
    FormatState state = newFormatState("  ", true);

    try {
        // Use C S-expression dumper
        dumpAstToSexpState(&driver, const_cast<AstNode*>(node), &state);

        // Get result and convert to std::string
        char* cResult = formatStateToString(&state);
        std::string result = cResult ? cResult : "";

        freeFormatState(&state);
        return result;
    } catch (...) {
        freeFormatState(&state);
        throw;
    }
}

bool ASTTestUtils::matches(const AstNode* ast, const std::string& expected,
                          const CompareOptions& options) {
    if (!ast) {
        return false;
    }

    std::string actual = toString(ast, options);

    if (options.normalizeWhitespace) {
        return normalizeSerial(actual) == normalizeSerial(expected);
    } else {
        return actual == expected;
    }
}

bool ASTTestUtils::structurallyMatches(const AstNode* ast, const std::string& expected,
                                      const CompareOptions& options) {
    if (!ast) {
        return false;
    }

    try {
        std::string actual = toString(ast, options);

        SExpr actualExpr = parseSerial(actual);
        SExpr expectedExpr = parseSerial(expected);

        return actualExpr == expectedExpr;
    } catch (const std::exception&) {
        // Fall back to string comparison if parsing fails
        return matches(ast, expected, options);
    }
}

std::string ASTTestUtils::diff(const AstNode* ast, const std::string& expected,
                              const CompareOptions& options) {
    if (!ast) {
        return "AST is null, expected: " + expected;
    }

    std::string actual = toString(ast, options);

    std::ostringstream diff;
    diff << "Expected: " << expected << "\n";
    diff << "Actual:   " << actual << "\n";

    if (options.normalizeWhitespace) {
        std::string normActual = normalizeSerial(actual);
        std::string normExpected = normalizeSerial(expected);

        if (normActual != normExpected) {
            diff << "Normalized Expected: " << normExpected << "\n";
            diff << "Normalized Actual:   " << normActual << "\n";

            // Find first difference
            size_t minLen = std::min(normActual.size(), normExpected.size());
            for (size_t i = 0; i < minLen; ++i) {
                if (normActual[i] != normExpected[i]) {
                    diff << "First difference at position " << i << ": ";
                    diff << "expected '" << normExpected[i] << "', got '" << normActual[i] << "'\n";
                    break;
                }
            }

            if (normActual.size() != normExpected.size()) {
                diff << "Length difference: expected " << normExpected.size()
                     << ", got " << normActual.size() << "\n";
            }
        } else {
            diff << "Normalized strings match - possible configuration difference\n";
        }
    }

    return diff.str();
}

std::string ASTTestUtils::debug(const AstNode* ast, const CompareOptions& options) {
    return toString(ast, options);
}

std::string ASTTestUtils::pretty(const AstNode* ast) {
    auto options = CompareOptions::defaults().withIgnoreLocation(false);
    return toString(ast, options);
}

} // namespace cxy::test
