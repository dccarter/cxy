/**
 * Parser Tests: Primitive Types
 *
 * Tests for parsing primitive type expressions and declarations.
 * Covers all built-in primitive types including the new i128 and u128 types.
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

using namespace cxy::parser::test;
using namespace cxy::test;

TEST_CASE("Boolean Type") {
    ParserTestFixture fixture;

    SUBCASE("Bool cast expression") {
        auto node = fixture.parseExpression("1 as bool");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtBool);
    }

    SUBCASE("Bool variable declaration") {
        auto node = fixture.parseProgram("var x: bool;");
        CHECK(node != nullptr);
        // Will verify exact format based on dumper output
    }
}

TEST_CASE("Character Types") {
    ParserTestFixture fixture;

    SUBCASE("char cast expression") {
        auto node = fixture.parseExpression("65 as char");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtCChar);
    }

    SUBCASE("wchar cast expression") {
        auto node = fixture.parseExpression("65 as wchar");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtChar);
    }
}

TEST_CASE("8-bit Integer Types") {
    ParserTestFixture fixture;

    SUBCASE("i8 cast expression") {
        auto node = fixture.parseExpression("100 as i8");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtI8);
    }

    SUBCASE("u8 cast expression") {
        auto node = fixture.parseExpression("200 as u8");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtU8);
    }
}

TEST_CASE("16-bit Integer Types") {
    ParserTestFixture fixture;

    SUBCASE("i16 cast expression") {
        auto node = fixture.parseExpression("30000 as i16");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtI16);
    }

    SUBCASE("u16 cast expression") {
        auto node = fixture.parseExpression("60000 as u16");
        CHECK(node != nullptr);
        REQUIRE(node->tag == astCastExpr);
        CHECK(node->castExpr.to->tag == astPrimitiveType);
        CHECK(node->castExpr.to->primitiveType.id == prtU16);
    }
}

TEST_CASE("32-bit Integer Types") {
    ParserTestFixture fixture;

    SUBCASE("i32 cast expression") {
        auto node = fixture.parseExpression("2000000000 as i32");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (IntegerLit 2000000000) (PrimitiveType i32))");
    }

    SUBCASE("u32 cast expression") {
        auto node = fixture.parseExpression("4000000000 as u32");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (IntegerLit 4000000000) (PrimitiveType u32))");
    }
}

TEST_CASE("64-bit Integer Types") {
    ParserTestFixture fixture;

    SUBCASE("i64 cast expression") {
        auto node = fixture.parseExpression("9000000000000000000 as i64");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (IntegerLit 9000000000000000000) (PrimitiveType i64))");
    }

    SUBCASE("u64 cast expression") {
        auto node = fixture.parseExpression("18000000000000000000 as u64");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (IntegerLit 18000000000000000000) (PrimitiveType u64))");
    }
}

TEST_CASE("128-bit Integer Types") {
    ParserTestFixture fixture;

    SUBCASE("i128 cast expression") {
        auto node = fixture.parseExpression("12345 as i128");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (IntegerLit 12345) (PrimitiveType i128))");
    }

    SUBCASE("u128 cast expression") {
        auto node = fixture.parseExpression("12345 as u128");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (IntegerLit 12345) (PrimitiveType u128))");
    }

    SUBCASE("i128 variable declaration") {
        auto node = fixture.parseProgram("var bigInt: i128;");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Program (VarDecl (Identifier bigInt) (PrimitiveType i128)))");
    }

    SUBCASE("u128 variable declaration") {
        auto node = fixture.parseProgram("var bigUInt: u128;");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Program (VarDecl (Identifier bigUInt) (PrimitiveType u128)))");
    }

    SUBCASE("i128 function parameter") {
        auto node = fixture.parseProgram("func demo(x: i128) {}");
        CHECK(node != nullptr);
        // Will verify exact format based on dumper output
    }

    SUBCASE("u128 function return type") {
        auto node = fixture.parseProgram("func demo() : u128 { return 0; }");
        CHECK(node != nullptr);
        // Will verify exact format based on dumper output
    }
}

TEST_CASE("Floating-Point Types") {
    ParserTestFixture fixture;

    SUBCASE("f32 cast expression") {
        auto node = fixture.parseExpression("3.14 as f32");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (FloatLit 3.14) (PrimitiveType f32))");
    }

    SUBCASE("f64 cast expression") {
        auto node = fixture.parseExpression("3.14159265359 as f64");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CastExpr (FloatLit 3.14159265359) (PrimitiveType f64))");
    }
}

// TODO: Add error cases for invalid type combinations once error handling is implemented
TEST_CASE("Invalid Primitive Types - TODO") {
    // Will add tests for invalid type syntax once we understand error handling
}
