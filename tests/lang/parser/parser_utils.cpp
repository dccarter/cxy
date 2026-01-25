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
    // Initialize log with quiet handler for tests
    log_ = newLog(quietDiagnosticHandler, this);

    // Initialize compiler driver with minimal setup
    memset(&driver_, 0, sizeof(driver_));
    driver_.pool = pool_.get();
    driver_.strings = &strings_;
    driver_.L = &log_;
}

void ParserTestFixture::quietDiagnosticHandler(const Diagnostic* diagnostic, void* ctx) {
    ParserTestFixture* fixture = static_cast<ParserTestFixture*>(ctx);

    // Capture diagnostic in memory instead of printing
    const char* kind = diagnostic->kind == dkError ? "error" :
                      diagnostic->kind == dkWarning ? "warning" : "note";

    // Simple format: "kind: message at file:line:col"
    fixture->captured_diagnostics_ += kind;
    fixture->captured_diagnostics_ += ": ";

    // Format the diagnostic message (simplified)
    if (diagnostic->fmt) {
        fixture->captured_diagnostics_ += diagnostic->fmt;
    }

    if (diagnostic->loc.fileName) {
        fixture->captured_diagnostics_ += " in ";
        fixture->captured_diagnostics_ += diagnostic->loc.fileName;
        fixture->captured_diagnostics_ += ":";
        fixture->captured_diagnostics_ += std::to_string(diagnostic->loc.begin.row);
        fixture->captured_diagnostics_ += ":";
        fixture->captured_diagnostics_ += std::to_string(diagnostic->loc.begin.col);
    }

    fixture->captured_diagnostics_ += "\n";
}

ParserTestFixture::~ParserTestFixture() {
    freeStrPool(&strings_);
    freeLog(&log_);
}

Lexer ParserTestFixture::createLexer(const std::string& source, const std::string& filename) {
    // Clear previous diagnostics for each new parse
    captured_diagnostics_.clear();
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
