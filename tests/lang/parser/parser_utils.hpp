/**
 * Parser Test Utilities
 *
 * Common utilities for testing the Cxy parser with different types of source code.
 * Provides helper functions to parse expressions and programs.
 *
 * Usage:
 *   auto expr = parseExpression("42 + 10");
 *   REQUIRE_AST_MATCHES_IGNORE_METADATA(expr, "(BinaryExpr + (IntegerLit 42) (IntegerLit 10))");
 */

#pragma once

#include "lang/frontend/parser.h"
#include "lang/frontend/lexer.h"
#include "utils/ast.hpp"
#include "core/mempool.h"
#include "core/strpool.h"
#include "core/log.h"
#include "driver/driver.h"

#include <string>
#include <stdexcept>

namespace cxy::parser::test {

/**
 * @brief Exception thrown when parsing fails
 */
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

/**
 * @brief RAII wrapper for parser test setup
 */
class ParserTestFixture {
private:
    cxy::test::MemPoolWrapper pool_;
    StrPool strings_;
    Log log_;
    CompilerDriver driver_;

public:
    ParserTestFixture();
    ~ParserTestFixture();

    // Non-copyable, non-moveable
    ParserTestFixture(const ParserTestFixture&) = delete;
    ParserTestFixture& operator=(const ParserTestFixture&) = delete;
    ParserTestFixture(ParserTestFixture&&) = delete;
    ParserTestFixture& operator=(ParserTestFixture&&) = delete;

    /**
     * @brief Parse source code as an expression
     */
    AstNode* parseExpression(const std::string& source, const std::string& filename = "test.cxy");

    /**
     * @brief Parse source code as a complete program/module
     */
    AstNode* parseProgram(const std::string& source, const std::string& filename = "test.cxy");

private:
    Lexer createLexer(const std::string& source, const std::string& filename);
};

} // namespace cxy::parser::test
