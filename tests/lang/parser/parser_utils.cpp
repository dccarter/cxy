/**
 * Parser Test Utilities Implementation
 *
 * Implementation of common utilities for testing the Cxy parser.
 */

#include "parser_utils.hpp"
#include "lang/frontend/strings.h"
#include "lang/frontend/lexer.h"
#include "core/alloc.h"

namespace cxy::parser::test {

ParserTestFixture::ParserTestFixture()
    : pool_(), strings_(newStrPool(pool_.get()))
{
    // Initialize log with null handler for tests
    log_ = newLog(nullptr, nullptr);

    // Initialize compiler driver with minimal setup
    memset(&driver_, 0, sizeof(driver_));
    driver_.pool = pool_.get();
    driver_.strings = &strings_;
    driver_.L = &log_;
}

ParserTestFixture::~ParserTestFixture() {
    freeStrPool(&strings_);
    freeLog(&log_);
}

Lexer ParserTestFixture::createLexer(const std::string& source, const std::string& filename) {
    return newLexer(filename.c_str(), source.c_str(), source.length(), &log_);
}

AstNode* ParserTestFixture::parseExpression(const std::string& source, const std::string& filename) {
    Lexer lexer = createLexer(source, filename);
    Parser parser = makeParser(&lexer, &driver_, false);

    AstNode* result = ::parseExpression(&parser);

    if (!result) {
        throw ParseError("Failed to parse expression: '" + source + "'");
    }

    return result;
}

AstNode* ParserTestFixture::parseProgram(const std::string& source, const std::string& filename) {
    Lexer lexer = createLexer(source, filename);
    Parser parser = makeParser(&lexer, &driver_, false);

    AstNode* result = ::parseProgram(&parser);

    if (!result) {
        throw ParseError("Failed to parse program: '" + source + "'");
    }

    return result;
}

} // namespace cxy::parser::test
