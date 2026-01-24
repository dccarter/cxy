/**
 * AST Validation Utility - C++ Version (Header)
 *
 * Modern C++ utilities for validating AST nodes against S-expression strings.
 * Uses the existing C S-expression dumper with RAII and exception handling.
 *
 * Usage:
 *   REQUIRE_AST_MATCHES(node, "(BinaryExpr + (Identifier a) (IntegerLit 10))");
 *   CHECK_AST_MATCHES_IGNORE_METADATA(node, "(BinaryExpr + (Identifier a) (IntegerLit 10))");
 */

#pragma once

#include "lang/frontend/ast.h"
#include "core/mempool.h"
#include "doctest.h"

#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cxy::test {

/**
 * @brief RAII wrapper for MemPool to ensure proper cleanup
 */
class MemPoolWrapper {
private:
    MemPool pool_;

public:
    MemPoolWrapper();
    ~MemPoolWrapper();

    // Non-copyable and non-moveable for safety
    MemPoolWrapper(const MemPoolWrapper&) = delete;
    MemPoolWrapper& operator=(const MemPoolWrapper&) = delete;
    MemPoolWrapper(MemPoolWrapper&&) = delete;
    MemPoolWrapper& operator=(MemPoolWrapper&&) = delete;

    MemPool* get() { return &pool_; }
    const MemPool* get() const { return &pool_; }
    MemPool& operator*() { return pool_; }
    const MemPool& operator*() const { return pool_; }
};

/**
 * @brief Comparison options for S-expression matching
 */
struct CompareOptions {
    bool ignoreMetadata = false;       // Skip (Metadata ...) blocks
    bool ignoreLocation = false;       // Skip location info within metadata
    bool ignoreFlags = false;          // Skip flags info within metadata
    bool normalizeWhitespace = true;   // Ignore whitespace differences

    // Fluent builder methods
    CompareOptions& withIgnoreMetadata(bool ignore = true) {
        ignoreMetadata = ignore;
        return *this;
    }

    CompareOptions& withIgnoreLocation(bool ignore = true) {
        ignoreLocation = ignore;
        return *this;
    }

    CompareOptions& withIgnoreFlags(bool ignore = true) {
        ignoreFlags = ignore;
        return *this;
    }

    CompareOptions& withNormalizeWhitespace(bool normalize = true) {
        normalizeWhitespace = normalize;
        return *this;
    }

    // Predefined configurations
    static CompareOptions defaults() {
        return CompareOptions{};
    }

    static CompareOptions withoutMetadata() {
        return CompareOptions{}.withIgnoreMetadata(true);
    }

    static CompareOptions strict() {
        return CompareOptions{}.withNormalizeWhitespace(false).withIgnoreMetadata();
    }
};

/**
 * @brief Represents a parsed S-expression for structural comparison
 */
struct SExpr {
    std::string atom;
    std::vector<SExpr> children;

    bool isAtom() const { return children.empty(); }

    bool operator==(const SExpr& other) const {
        if (isAtom() != other.isAtom())
            return false;
        if (isAtom())
            return atom == other.atom;

        if (children.size() != other.children.size())
            return false;
        for (size_t i = 0; i < children.size(); ++i) {
            if (children[i] != other.children[i])
                return false;
        }
        return true;
    }

    bool operator!=(const SExpr& other) const { return !(*this == other); }
};

/**
 * @brief Normalize S-expression string for whitespace-insensitive comparison
 */
inline std::string normalizeSerial(const std::string& sexpr) {
    std::string result;
    result.reserve(sexpr.size());

    bool inString = false;
    bool inEscape = false;
    bool prevWasSpace = false;

    for (size_t i = 0; i < sexpr.size(); ++i) {
        char c = sexpr[i];

        if (inEscape) {
            result += c;
            inEscape = false;
            prevWasSpace = false;
            continue;
        }

        if (c == '\\' && inString) {
            result += c;
            inEscape = true;
            prevWasSpace = false;
            continue;
        }

        if (c == '"') {
            inString = !inString;
            result += c;
            prevWasSpace = false;
            continue;
        }

        if (inString) {
            result += c;
            prevWasSpace = false;
            continue;
        }

        // Not in string - handle whitespace normalization
        if (std::isspace(c)) {
            if (!prevWasSpace && !result.empty() && result.back() != '(') {
                result += ' ';
            }
            prevWasSpace = true;
        } else {
            if (c == ')') {
                // Remove space before closing parens
                if (!result.empty() && result.back() == ' ') {
                    result.pop_back();
                }
            }
            result += c;
            prevWasSpace = false;
        }
    }

    // Remove trailing whitespace
    while (!result.empty() && std::isspace(result.back())) {
        result.pop_back();
    }

    return result;
}

/**
 * @brief Parse S-expression string into structured representation
 */
inline SExpr parseSerial(const std::string& sexpr) {
    std::string normalized = normalizeSerial(sexpr);
    size_t pos = 0;

    auto skipWhitespace = [&]() {
        while (pos < normalized.size() && std::isspace(normalized[pos])) {
            pos++;
        }
    };

    std::function<SExpr()> parseExpr = [&]() -> SExpr {
        skipWhitespace();

        if (pos >= normalized.size()) {
            throw std::runtime_error("Unexpected end of input");
        }

        if (normalized[pos] == '(') {
            // Parse list
            pos++; // Skip '('
            SExpr expr;

            while (true) {
                skipWhitespace();
                if (pos >= normalized.size()) {
                    throw std::runtime_error("Missing closing parenthesis");
                }

                if (normalized[pos] == ')') {
                    pos++; // Skip ')'
                    break;
                }

                expr.children.push_back(parseExpr());
            }

            return expr;
        } else {
            // Parse atom
            SExpr expr;
            std::string atom;

            if (normalized[pos] == '"') {
                // Parse string literal
                atom += normalized[pos++]; // Include opening quote

                while (pos < normalized.size() && normalized[pos] != '"') {
                    if (normalized[pos] == '\\' && pos + 1 < normalized.size()) {
                        atom += normalized[pos++]; // Backslash
                        atom += normalized[pos++]; // Escaped char
                    } else {
                        atom += normalized[pos++];
                    }
                }

                if (pos >= normalized.size()) {
                    throw std::runtime_error("Unterminated string literal");
                }

                atom += normalized[pos++]; // Include closing quote
            } else {
                // Parse regular atom
                while (pos < normalized.size() && !std::isspace(normalized[pos]) &&
                       normalized[pos] != '(' && normalized[pos] != ')') {
                    atom += normalized[pos++];
                }
            }

            expr.atom = atom;
            return expr;
        }
    };

    return parseExpr();
}

/**
 * @brief Test utility class for AST comparison and debugging
 */
class ASTTestUtils {
public:
    /**
     * @brief Convert AST node to S-expression string using C backend
     */
    static std::string toString(const AstNode* node, const CompareOptions& options = CompareOptions::defaults());

    /**
     * @brief Fast whitespace-insensitive comparison of AST and expected S-expression
     */
    static bool matches(const AstNode* ast, const std::string& expected,
                       const CompareOptions& options = CompareOptions::defaults());

    /**
     * @brief Robust structural comparison using S-expression parsing
     */
    static bool structurallyMatches(const AstNode* ast, const std::string& expected,
                                   const CompareOptions& options = CompareOptions::defaults());

    /**
     * @brief Generate diff information for debugging test failures
     */
    static std::string diff(const AstNode* ast, const std::string& expected,
                           const CompareOptions& options = CompareOptions::defaults());

    /**
     * @brief Quick debug print of AST
     */
    static std::string debug(const AstNode* ast, const CompareOptions& options = CompareOptions::defaults());

    /**
     * @brief Pretty print AST with location information
     */
    static std::string pretty(const AstNode* ast);
};

} // namespace cxy::test

// Convenient test macros using doctest
#define REQUIRE_AST_MATCHES_FLAGS(ast_node, expected_str, options) \
    do { \
        auto actual_result = cxy::test::ASTTestUtils::toString(ast_node, options); \
        auto normalized_actual = cxy::test::normalizeSerial(actual_result); \
        auto normalized_expected = cxy::test::normalizeSerial(expected_str); \
        INFO("AST diff: " << cxy::test::ASTTestUtils::diff(ast_node, expected_str, options)); \
        REQUIRE(normalized_actual == normalized_expected); \
    } while (0)

#define CHECK_AST_MATCHES_FLAGS(ast_node, expected_str, options) \
    do { \
        INFO("AST diff: " << cxy::test::ASTTestUtils::diff(ast_node, expected_str, options)); \
        CHECK(cxy::test::ASTTestUtils::matches(ast_node, expected_str, options)); \
    } while (0)

#define REQUIRE_AST_STRUCTURALLY_MATCHES_FLAGS(ast_node, expected_str, options) \
    do { \
        INFO("AST diff: " << cxy::test::ASTTestUtils::diff(ast_node, expected_str, options)); \
        REQUIRE(cxy::test::ASTTestUtils::structurallyMatches(ast_node, expected_str, options)); \
    } while (0)

#define CHECK_AST_STRUCTURALLY_MATCHES_FLAGS(ast_node, expected_str, options) \
    do { \
        INFO("AST diff: " << cxy::test::ASTTestUtils::diff(ast_node, expected_str, options)); \
        CHECK(cxy::test::ASTTestUtils::structurallyMatches(ast_node, expected_str, options)); \
    } while (0)

// Convenience macros with default options
#define REQUIRE_AST_MATCHES(ast_node, expected_str) \
    REQUIRE_AST_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::defaults())

#define CHECK_AST_MATCHES(ast_node, expected_str) \
    CHECK_AST_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::defaults())

#define REQUIRE_AST_STRUCTURALLY_MATCHES(ast_node, expected_str) \
    REQUIRE_AST_STRUCTURALLY_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::defaults())

#define CHECK_AST_STRUCTURALLY_MATCHES(ast_node, expected_str) \
    CHECK_AST_STRUCTURALLY_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::defaults())

// Macros with common option combinations
#define REQUIRE_AST_MATCHES_IGNORE_METADATA(ast_node, expected_str) \
    REQUIRE_AST_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::withoutMetadata())

#define CHECK_AST_MATCHES_IGNORE_METADATA(ast_node, expected_str) \
    CHECK_AST_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::withoutMetadata())

#define REQUIRE_AST_MATCHES_STRICT(ast_node, expected_str) \
    REQUIRE_AST_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::strict())

#define CHECK_AST_MATCHES_STRICT(ast_node, expected_str) \
    CHECK_AST_MATCHES_FLAGS(ast_node, expected_str, cxy::test::CompareOptions::strict())
