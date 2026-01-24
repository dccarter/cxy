/**
 * Parser Tests: Literals and Identifiers (Phase 1)
 *
 * Tests for parsing basic literals and identifiers as defined in the grammar.
 * Phase 1 grammar productions cover:
 * - Integer literals
 * - Float literals
 * - String literals
 * - Character literals
 * - Boolean literals
 * - Identifiers
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

using namespace cxy::parser::test;
using namespace cxy::test;

TEST_CASE("Integer Literals") {
    ParserTestFixture fixture;

    SUBCASE("Simple positive integer") {
        auto node = fixture.parseExpression("42");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 42)");
    }

    SUBCASE("Zero") {
        auto node = fixture.parseExpression("0");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 0)");
    }

    SUBCASE("Large integer") {
        auto node = fixture.parseExpression("123456789");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 123456789)");
    }

    SUBCASE("Hexadecimal literals") {
        auto node = fixture.parseExpression("0xFF");
        // Expected format may vary - adjust based on actual dumper output
        CHECK(node != nullptr);
    }

    SUBCASE("Binary literals") {
        auto node = fixture.parseExpression("0b1010");
        // Expected format may vary - adjust based on actual dumper output
        CHECK(node != nullptr);
    }

    SUBCASE("Octal literals") {
        auto node = fixture.parseExpression("0o755");
        // Expected format may vary - adjust based on actual dumper output
        CHECK(node != nullptr);
    }
}

TEST_CASE("Float Literals") {
    ParserTestFixture fixture;

    SUBCASE("Simple decimal") {
        auto node = fixture.parseExpression("3.14");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(FloatLit 3.14)");
    }

    SUBCASE("Zero float") {
        auto node = fixture.parseExpression("0.0");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(FloatLit 0)");
    }

    SUBCASE("Float with exponent") {
        auto node = fixture.parseExpression("1e10");
        CHECK(node != nullptr);
    }

    SUBCASE("Float with negative exponent") {
        auto node = fixture.parseExpression("1.5e-3");
        CHECK(node != nullptr);
    }
}

TEST_CASE("String Literals") {
    ParserTestFixture fixture;

    SUBCASE("Simple string") {
        auto node = fixture.parseExpression("\"hello\"");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(StringLit \"hello\")");
    }

    SUBCASE("Empty string") {
        auto node = fixture.parseExpression("\"\"");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(StringLit \"\")");
    }

    SUBCASE("String with spaces") {
        auto node = fixture.parseExpression("\"hello world\"");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(StringLit \"hello world\")");
    }

    SUBCASE("String with escape sequences") {
        auto node = fixture.parseExpression("\"hello\\nworld\"");
        CHECK(node != nullptr);
        // Will need to verify exact format based on dumper output
    }

    SUBCASE("String with quotes") {
        auto node = fixture.parseExpression("\"say \\\"hello\\\"\"");
        CHECK(node != nullptr);
    }
}

TEST_CASE("Character Literals") {
    ParserTestFixture fixture;

    SUBCASE("Simple character") {
        auto node = fixture.parseExpression("'a'");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CharLit 'a')");
    }

    SUBCASE("Escaped character") {
        auto node = fixture.parseExpression("'\\n'");
        CHECK(node != nullptr);
    }

    SUBCASE("Escaped quote") {
        auto node = fixture.parseExpression("'\\''");
        CHECK(node != nullptr);
    }

    SUBCASE("Unicode character") {
        auto node = fixture.parseExpression("'\\u0041'");
        CHECK(node != nullptr);
    }
}

TEST_CASE("Boolean Literals") {
    ParserTestFixture fixture;

    SUBCASE("True literal") {
        auto node = fixture.parseExpression("true");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(BoolLit true)");
    }

    SUBCASE("False literal") {
        auto node = fixture.parseExpression("false");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(BoolLit false)");
    }
}

TEST_CASE("Null Literal") {
    ParserTestFixture fixture;

    SUBCASE("Null literal") {
        auto node = fixture.parseExpression("null");
        CHECK(node != nullptr);
        // Will verify exact format based on dumper output
    }
}

TEST_CASE("Single entry path") {
    ParserTestFixture fixture;

    SUBCASE("Simple identifier") {
        auto node = fixture.parseExpression("myVar");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Path myVar)");
    }

    SUBCASE("Identifier with underscore") {
        auto node = fixture.parseExpression("my_var");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Path my_var)");
    }

    SUBCASE("Identifier starting with underscore") {
        auto node = fixture.parseExpression("_private");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Path _private)");
    }

    SUBCASE("Identifier with numbers") {
        auto node = fixture.parseExpression("var123");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Path var123)");
    }

    SUBCASE("Single letter identifier") {
        auto node = fixture.parseExpression("x");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Path x)");
    }
}

TEST_CASE("Mixed Literal Expressions") {
    ParserTestFixture fixture;

    SUBCASE("Parenthesized literal") {
        auto node = fixture.parseExpression("(42)");
        // Should parse as grouped expression or just the literal
        CHECK(node != nullptr);
    }

    SUBCASE("Negative number") {
        auto node = fixture.parseExpression("-42");
        // Should parse as unary minus applied to literal
        CHECK(node != nullptr);
    }

    SUBCASE("Positive number") {
        auto node = fixture.parseExpression("+42");
        // Should parse as unary plus applied to literal
        CHECK(node != nullptr);
    }
}
