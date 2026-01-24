/**
 * Basic tests for C++ AST test utilities
 *
 * Verifies that the new C++ AST validation utilities work correctly
 * with the existing C S-expression dumper backend.
 */

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "utils/ast.hpp"
#include "lang/frontend/ast.h"
#include "lang/frontend/operator.h"
#include "core/mempool.h"

using namespace cxy::test;

TEST_CASE("MemPoolWrapper RAII functionality") {
    SUBCASE("Basic construction and destruction") {
        MemPoolWrapper pool;
        REQUIRE(pool.get() != nullptr);
        // Destructor should be called automatically
    }

    SUBCASE("Access methods") {
        MemPoolWrapper pool;
        REQUIRE(pool.get() != nullptr);
        REQUIRE(&(*pool) == pool.get());
    }
}

TEST_CASE("CompareOptions fluent interface") {
    SUBCASE("Default options") {
        auto opts = CompareOptions::defaults();
        REQUIRE(opts.ignoreMetadata == false);
        REQUIRE(opts.ignoreLocation == false);
        REQUIRE(opts.ignoreFlags == false);
        REQUIRE(opts.normalizeWhitespace == true);
    }

    SUBCASE("Fluent builder methods") {
        auto opts = CompareOptions::defaults()
            .withIgnoreMetadata(true)
            .withIgnoreLocation(true)
            .withNormalizeWhitespace(false);

        REQUIRE(opts.ignoreMetadata == true);
        REQUIRE(opts.ignoreLocation == true);
        REQUIRE(opts.normalizeWhitespace == false);
    }

    SUBCASE("Predefined configurations") {
        auto withoutMeta = CompareOptions::withoutMetadata();
        REQUIRE(withoutMeta.ignoreMetadata == true);

        auto strict = CompareOptions::strict();
        REQUIRE(strict.normalizeWhitespace == false);
    }
}

TEST_CASE("S-expression normalization") {
    SUBCASE("Basic whitespace normalization") {
        std::string input = "( BinaryExpr   +   ( Identifier   a )   ( IntegerLit   10 ) )";
        std::string expected = "(BinaryExpr + (Identifier a) (IntegerLit 10))";
        REQUIRE(normalizeSerial(input) == expected);
    }

    SUBCASE("String literal preservation") {
        std::string input = "(StringLit \"hello  world\")";
        std::string expected = "(StringLit \"hello  world\")";
        REQUIRE(normalizeSerial(input) == expected);
    }

    SUBCASE("Escaped characters in strings") {
        std::string input = "(StringLit \"hello\\\"world\")";
        std::string expected = "(StringLit \"hello\\\"world\")";
        REQUIRE(normalizeSerial(input) == expected);
    }
}

TEST_CASE("S-expression parsing") {
    SUBCASE("Simple atom") {
        SExpr expr = parseSerial("atom");
        REQUIRE(expr.isAtom());
        REQUIRE(expr.atom == "atom");
    }

    SUBCASE("Simple list") {
        SExpr expr = parseSerial("(op a b)");
        REQUIRE_FALSE(expr.isAtom());
        REQUIRE(expr.children.size() == 3);
        REQUIRE(expr.children[0].atom == "op");
        REQUIRE(expr.children[1].atom == "a");
        REQUIRE(expr.children[2].atom == "b");
    }

    SUBCASE("Nested lists") {
        SExpr expr = parseSerial("(outer (inner x) y)");
        REQUIRE_FALSE(expr.isAtom());
        REQUIRE(expr.children.size() == 3);
        REQUIRE(expr.children[0].atom == "outer");
        REQUIRE_FALSE(expr.children[1].isAtom());
        REQUIRE(expr.children[1].children.size() == 2);
        REQUIRE(expr.children[2].atom == "y");
    }
}

TEST_CASE("SExpr equality comparison") {
    SUBCASE("Atom equality") {
        SExpr a = parseSerial("atom");
        SExpr b = parseSerial("atom");
        SExpr c = parseSerial("different");

        REQUIRE(a == b);
        REQUIRE(a != c);
    }

    SUBCASE("List equality") {
        SExpr a = parseSerial("(op x y)");
        SExpr b = parseSerial("(op x y)");
        SExpr c = parseSerial("(op x z)");

        REQUIRE(a == b);
        REQUIRE(a != c);
    }
}

TEST_CASE("ASTTestUtils null handling") {
    SUBCASE("toString with null node") {
        std::string result = ASTTestUtils::toString(nullptr);
        REQUIRE(result == "(null)");
    }

    SUBCASE("matches with null node") {
        bool result = ASTTestUtils::matches(nullptr, "anything");
        REQUIRE(result == false);
    }

    SUBCASE("structurallyMatches with null node") {
        bool result = ASTTestUtils::structurallyMatches(nullptr, "anything");
        REQUIRE(result == false);
    }

    SUBCASE("diff with null node") {
        std::string result = ASTTestUtils::diff(nullptr, "expected");
        REQUIRE(result == "AST is null, expected: expected");
    }
}

// Test helper for creating file location
FileLoc makeTestLoc() {
    FileLoc loc = {0};
    loc.fileName = "test.cxy";
    loc.begin = {1, 1};
    loc.end = {1, 10};
    return loc;
}

// Test creating nodes without location to avoid metadata

TEST_CASE("AST assertion macros with real nodes") {
    MemPoolWrapper pool;

    SUBCASE("Simple integer literal") {
        MemPoolWrapper pool;
        FileLoc loc = makeTestLoc();
        AstNode* node = makeIntegerLiteral(pool.get(), &loc, 42, nullptr, nullptr);
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 42)");
        CHECK_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 42)");
    }

    SUBCASE("Simple identifier") {
        MemPoolWrapper pool;
        FileLoc loc = makeTestLoc();
        AstNode* node = makeIdentifier(pool.get(),  &loc, "myVar", 0, nullptr, nullptr);
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(Identifier myVar)");
        CHECK_AST_MATCHES_IGNORE_METADATA(node, "(Identifier myVar)");
    }

    SUBCASE("String literal") {
        MemPoolWrapper pool;
        FileLoc loc = makeTestLoc();
        AstNode* node = makeStringLiteral(pool.get(), &loc, "hello", nullptr, nullptr);
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(StringLit \"hello\")");
        CHECK_AST_MATCHES_IGNORE_METADATA(node, "(StringLit \"hello\")");
    }

    SUBCASE("Boolean literals") {
        MemPoolWrapper pool;
        FileLoc loc = makeTestLoc();
        AstNode* trueNode = makeBoolLiteral(pool.get(), &loc, true, nullptr, nullptr);
        AstNode* falseNode = makeBoolLiteral(pool.get(), &loc, false, nullptr, nullptr);

        REQUIRE_AST_MATCHES_IGNORE_METADATA(trueNode, "(BoolLit true)");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(falseNode, "(BoolLit false)");
    }

    SUBCASE("Float literal") {
        MemPoolWrapper pool;
        FileLoc loc = makeTestLoc();
        AstNode* node = makeFloatLiteral(pool.get(), &loc, 3.14, nullptr, nullptr);
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(FloatLit 3.14)");
    }

    SUBCASE("Character literal") {
        MemPoolWrapper pool;
        FileLoc loc = makeTestLoc();
        AstNode* node = makeCharLiteral(pool.get(), &loc, 'A', nullptr, nullptr);
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(CharLit 'A')");
    }
}

TEST_CASE("AST assertion macros with composite nodes") {
    MemPoolWrapper pool;
    FileLoc loc = makeTestLoc();

    SUBCASE("Binary expression") {
        AstNode* lhs = makeIntegerLiteral(pool.get(), &loc, 5, nullptr, nullptr);
        AstNode* rhs = makeIntegerLiteral(pool.get(), &loc, 3, nullptr, nullptr);
        AstNode* binExpr = makeBinaryExpr(pool.get(), &loc, 0, lhs, opAdd, rhs, nullptr, nullptr);

        REQUIRE_AST_MATCHES_IGNORE_METADATA(binExpr, "(BinaryExpr + (IntegerLit 5) (IntegerLit 3))");
        CHECK_AST_MATCHES_IGNORE_METADATA(binExpr, "(BinaryExpr + (IntegerLit 5) (IntegerLit 3))");
    }

    SUBCASE("Member expression") {
        AstNode* target = makeIdentifier(pool.get(), &loc, "obj", 0, nullptr, nullptr);
        AstNode* member = makeIdentifier(pool.get(), &loc, "field", 0, nullptr, nullptr);
        AstNode* memberExpr = makeMemberExpr(pool.get(), &loc, 0, target, member, nullptr, nullptr);

        REQUIRE_AST_MATCHES_IGNORE_METADATA(memberExpr, "(MemberExpr (Identifier obj) (Identifier field))");
    }

    SUBCASE("Call expression") {
        AstNode* func = makeIdentifier(pool.get(), &loc, "print", 0, nullptr, nullptr);
        AstNode* arg = makeStringLiteral(pool.get(), &loc, "Hello", nullptr, nullptr);
        AstNode* callExpr = makeCallExpr(pool.get(), &loc, func, arg, 0, nullptr, nullptr);

        // Note: Exact S-expression format may vary based on implementation
        REQUIRE_AST_MATCHES_IGNORE_METADATA(callExpr, "(CallExpr (Identifier print) (StringLit \"Hello\"))");
    }

    SUBCASE("Nested expressions") {
        // Create: (a + b) * c
        AstNode* a = makeIdentifier(pool.get(), &loc, "a", 0, nullptr, nullptr);
        AstNode* b = makeIdentifier(pool.get(), &loc, "b", 0, nullptr, nullptr);
        AstNode* c = makeIdentifier(pool.get(), &loc, "c", 0, nullptr, nullptr);

        AstNode* add = makeBinaryExpr(pool.get(), &loc, 0, a, opAdd, b, nullptr, nullptr);
        AstNode* mul = makeBinaryExpr(pool.get(), &loc, 0, add, opMul, c, nullptr, nullptr);

        REQUIRE_AST_MATCHES_IGNORE_METADATA(mul, "(BinaryExpr * (BinaryExpr + (Identifier a) (Identifier b)) (Identifier c))");
    }
}

TEST_CASE("AST assertion macros with different comparison options") {
    MemPoolWrapper pool;
    FileLoc loc = makeTestLoc();

    SUBCASE("Ignore metadata option") {
        AstNode* node = makeIntegerLiteral(pool.get(), &loc, 100, nullptr, nullptr);

        // Test with metadata ignored
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 100)");
        CHECK_AST_MATCHES_IGNORE_METADATA(node, "(IntegerLit 100)");
    }

    SUBCASE("Strict comparison") {
        AstNode* node = makeStringLiteral(pool.get(), &loc, "test", nullptr, nullptr);

        // Test strict comparison (no whitespace normalization)
        REQUIRE_AST_MATCHES_STRICT(node, "(StringLit \"test\")");
        CHECK_AST_MATCHES_STRICT(node, "(StringLit \"test\")");
    }

    SUBCASE("Custom comparison options") {
        AstNode* node = makeIdentifier(pool.get(), &loc, "variable", 0, nullptr, nullptr);
        auto options = CompareOptions::defaults().withIgnoreLocation(true);

        REQUIRE_AST_MATCHES_FLAGS(node, "(Identifier variable)", options);
        CHECK_AST_MATCHES_FLAGS(node, "(Identifier variable)", options);
    }
}

TEST_CASE("AST structural matching") {
    MemPoolWrapper pool;
    FileLoc loc = makeTestLoc();

    SUBCASE("Structural vs simple matching") {
        AstNode* node = makeBinaryExpr(
            pool.get(), &loc, 0,
            makeIntegerLiteral(pool.get(), &loc, 10, nullptr, nullptr),
            opSub,
            makeIntegerLiteral(pool.get(), &loc, 5, nullptr, nullptr),
            nullptr, nullptr
        );

        // Both should work for well-formed expressions
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, "(BinaryExpr - (IntegerLit 10) (IntegerLit 5))");
        REQUIRE_AST_STRUCTURALLY_MATCHES_FLAGS(node, "(BinaryExpr - (IntegerLit 10) (IntegerLit 5))", CompareOptions::withoutMetadata());

        CHECK_AST_STRUCTURALLY_MATCHES_FLAGS(node, "(BinaryExpr - (IntegerLit 10) (IntegerLit 5))", CompareOptions::withoutMetadata());
    }

    SUBCASE("Structural matching with custom options") {
        AstNode* node = makeFloatLiteral(pool.get(), &loc, 2.5, nullptr, nullptr);
        auto options = CompareOptions::withoutMetadata();

        REQUIRE_AST_STRUCTURALLY_MATCHES_FLAGS(node, "(FloatLit 2.5)", options);
        CHECK_AST_STRUCTURALLY_MATCHES_FLAGS(node, "(FloatLit 2.5)", options);
    }
}

TEST_CASE("AST assertion macro error handling") {
    MemPoolWrapper pool;
    FileLoc loc = makeTestLoc();

    SUBCASE("Intentional mismatch for testing error messages") {
        AstNode* node = makeIntegerLiteral(pool.get(), &loc, 42, nullptr, nullptr);

        // This should fail, but we can't easily test failure in doctest
        // The macros include INFO() statements for debugging when they fail
        // In real usage, these would show helpful diff information

        // Just verify the node exists and can be converted
        std::string result = ASTTestUtils::toString(node);
        REQUIRE_FALSE(result.empty());
        REQUIRE(result.find("42") != std::string::npos);
    }
}
