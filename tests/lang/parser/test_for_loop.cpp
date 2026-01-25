/**
 * Parser Tests: For-Loop Parsing Improvements
 *
 * Tests for parsing for-loop statements with:
 * - Support for 'in' keyword alongside ':' in headers
 * - Optional 'var'/'const' keywords with context-dependent defaults
 * - Various combinations and edge cases
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

using namespace cxy::parser::test;
using namespace cxy::test;

TEST_CASE("For Loops with 'in' keyword") {
    ParserTestFixture fixture;

    SUBCASE("Basic for loop with 'in' and explicit const") {
        auto node = fixture.parseProgram("func demo() { for const i in 0..10 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (IntegerLit 10))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }

    SUBCASE("Basic for loop with 'in' and explicit var") {
        auto node = fixture.parseProgram("func demo() { for var i in 0..10 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (IntegerLit 10))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }

    SUBCASE("For loop with 'in' and multiple variables") {
        auto node = fixture.parseProgram("func demo() { for const idx, value in items { use(idx, value); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier idx) (Identifier value))
                            (Path items)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path use) (Path idx) (Path value))))))))
        )");
    }

    SUBCASE("For loop with 'in' and parentheses") {
        auto node = fixture.parseProgram("func demo() { for (const i in 0..10) println(i); }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr ..
                                (IntegerLit 0)
                                (IntegerLit 10))
                            (ExprStmt
                                (CallExpr (Path println) (Path i)))))))
        )");
    }

    SUBCASE("For loop with 'in' without parentheses - single statement should fail") {
        // This should fail to parse - when parentheses are omitted, braces are required
        auto node = fixture.parseProgram("func demo() { for const i in 0..10 println(i); }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about missing braces
    }
}

TEST_CASE("For Loops with Optional Keywords - Runtime Context") {
    ParserTestFixture fixture;

    SUBCASE("For loop with omitted keyword in runtime context should default to 'var'") {
        auto node = fixture.parseProgram("func demo() { for i in 0..10 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (IntegerLit 10))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }

    SUBCASE("For loop with omitted keyword and multiple variables in runtime context") {
        auto node = fixture.parseProgram("func demo() { for idx, value in items { use(idx, value); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier idx) (Identifier value))
                            (Path items)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path use) (Path idx) (Path value))))))))
        )");
    }

    SUBCASE("For loop with omitted keyword using colon syntax in runtime context") {
        auto node = fixture.parseProgram("func demo() { for i: 0..10 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (IntegerLit 10))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }
}

TEST_CASE("For Loops - Mixed Syntax Compatibility") {
    ParserTestFixture fixture;

    SUBCASE("Traditional colon syntax with explicit const still works") {
        auto node = fixture.parseProgram("func demo() { for const i: 0..10 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (IntegerLit 10))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }

    SUBCASE("Traditional colon syntax with explicit var still works") {
        auto node = fixture.parseProgram("func demo() { for var i: 0..10 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (IntegerLit 10))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }

    SUBCASE("Traditional colon syntax with multiple variables") {
        auto node = fixture.parseProgram("func demo() { for const idx, value: items { use(idx, value); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier idx) (Identifier value))
                            (Path items)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path use) (Path idx) (Path value))))))))
        )");
    }
}

TEST_CASE("For Loops - Edge Cases and Wildcards") {
    ParserTestFixture fixture;

    SUBCASE("For loop with wildcard using 'in' syntax") {
        auto node = fixture.parseProgram("func demo() { for _ in items { process(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier _))
                            (Path items)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path process))))))))
        )");
    }

    SUBCASE("For loop with partial wildcards using 'in' syntax") {
        auto node = fixture.parseProgram("func demo() { for value, _ in arr { process(value); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl
                                (Identifier value)
                                (Identifier _))
                            (Path arr)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path process) (Path value))))))))
        )");
    }

    SUBCASE("For loop with trailing comma using 'in' syntax") {
        auto node = fixture.parseProgram("func demo() { for value, idx, in arr { println(value, idx); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier value) (Identifier idx))
                            (Path arr)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path value) (Path idx))))))))
        )");
    }
}

TEST_CASE("For Loops - Complex Expressions") {
    ParserTestFixture fixture;

    SUBCASE("For loop with complex range expression") {
        auto node = fixture.parseProgram("func demo() { for i in start..end+1 { println(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier i))
                            (BinaryExpr ..
                                (Path start)
                                (BinaryExpr + (Path end) (IntegerLit 1)))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path println) (Path i))))))))
        )");
    }

    SUBCASE("For loop with function call as iterable") {
        auto node = fixture.parseProgram("func demo() { for item in getItems() { process(item); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier item))
                            (CallExpr (Path getItems))
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path process) (Path item))))))))
        )");
    }

    SUBCASE("For loop with array access as iterable") {
        auto node = fixture.parseProgram("func demo() { for elem in array { use(elem); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt (VarDecl (Identifier elem))
                            (Path array)
                            (BlockStmt
                                (ExprStmt
                                    (CallExpr (Path use) (Path elem))))))))
        )");
    }
}

TEST_CASE("For Loops - Error Cases") {
    ParserTestFixture fixture;

    SUBCASE("For loop mixing 'in' and ':' should fail") {
        auto node = fixture.parseProgram("func demo() { for i in: 0..10 { println(i); } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about invalid syntax
    }

    SUBCASE("For loop with invalid keyword should fail") {
        auto node = fixture.parseProgram("func demo() { for let i in 0..10 { println(i); } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about invalid keyword
    }

    SUBCASE("For loop without iterable should fail") {
        auto node = fixture.parseProgram("func demo() { for i in { println(i); } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about missing iterable expression
    }

    SUBCASE("For loop without variable name should fail") {
        auto node = fixture.parseProgram("func demo() { for in items { process(); } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about missing variable declaration
    }
}
