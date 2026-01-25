/**
 * Parser Tests: Simplified Switch Statement Syntax
 *
 * Tests for parsing switch statements with:
 * - Optional 'case' keywords - direct pattern matching
 * - '...' as default case indicator instead of 'default' keyword
 * - Backward compatibility with existing 'case'/'default' syntax
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

using namespace cxy::parser::test;
using namespace cxy::test;

TEST_CASE("Switch Statements with Simplified Syntax") {
    ParserTestFixture fixture;

    SUBCASE("Basic switch with direct value matching") {
        auto node = fixture.parseProgram("func demo() { switch (value) { 0 => println(\"zero\"); 1 => println(\"one\"); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (IntegerLit 0)
                                (ExprStmt (CallExpr (Path println) (StringLit "zero"))))
                            (CaseStmt (IntegerLit 1)
                                (ExprStmt (CallExpr (Path println) (StringLit "one"))))))))
        )");
    }

    SUBCASE("Switch with default case using ...") {
        auto node = fixture.parseProgram("func demo() { switch (status) { 0 => success(); 1 => warning(); ... => error(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path status)
                            (CaseStmt (IntegerLit 0)
                                (ExprStmt (CallExpr (Path success))))
                            (CaseStmt (IntegerLit 1)
                                (ExprStmt (CallExpr (Path warning))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path error))))))))
        )");
    }

    SUBCASE("Switch with multiple values per case") {
        auto node = fixture.parseProgram("func demo() { switch (code) { 200, 201, 202 => handleSuccess(); 404, 500 => handleError(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path code)
                            (CaseStmt (IntegerLit 200))
                            (CaseStmt (IntegerLit 201))
                            (CaseStmt (IntegerLit 202)
                                (ExprStmt (CallExpr (Path handleSuccess))))
                            (CaseStmt (IntegerLit 404))
                            (CaseStmt (IntegerLit 500)
                                (ExprStmt (CallExpr (Path handleError))))))))
        )");
    }

    SUBCASE("Switch with block statements") {
        auto node = fixture.parseProgram("func demo() { switch (op) { \"add\" => { result = a + b; return result; } \"sub\" => { result = a - b; return result; } } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path op)
                            (CaseStmt (StringLit "add")
                                (BlockStmt
                                    (ExprStmt (AssignExpr = (Path result)
                                        (BinaryExpr + (Path a) (Path b))))
                                    (ReturnStmt (Path result))))
                            (CaseStmt (StringLit "sub")
                                (BlockStmt
                                    (ExprStmt (AssignExpr = (Path result)
                                        (BinaryExpr - (Path a) (Path b))))
                                    (ReturnStmt (Path result))))))))
        )");
    }
}

TEST_CASE("Switch Statements without Parentheses") {
    ParserTestFixture fixture;

    SUBCASE("Bare switch without parentheses") {
        auto node = fixture.parseProgram("func demo() { switch value { true => doTrue(); false => doFalse(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (BoolLit true)
                                (ExprStmt (CallExpr (Path doTrue))))
                            (CaseStmt (BoolLit false)
                                (ExprStmt (CallExpr (Path doFalse))))))))
        )");
    }

    SUBCASE("Switch without parentheses with default case") {
        auto node = fixture.parseProgram("func demo() { switch grade { \"A\" => excellent(); \"B\" => good(); ... => needsImprovement(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path grade)
                            (CaseStmt (StringLit "A")
                                (ExprStmt (CallExpr (Path excellent))))
                            (CaseStmt (StringLit "B")
                                (ExprStmt (CallExpr (Path good))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path needsImprovement))))))))
        )");
    }
}

TEST_CASE("Switch Statements with Range Expressions") {
    ParserTestFixture fixture;

    SUBCASE("Switch with range patterns") {
        auto node = fixture.parseProgram("func demo() { switch (score) { 0..59 => fail(); 60..79 => pass(); 80..100 => excel(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path score)
                            (CaseStmt (BinaryExpr .. (IntegerLit 0) (IntegerLit 59))
                                (ExprStmt (CallExpr (Path fail))))
                            (CaseStmt (BinaryExpr .. (IntegerLit 60) (IntegerLit 79))
                                (ExprStmt (CallExpr (Path pass))))
                            (CaseStmt (BinaryExpr .. (IntegerLit 80) (IntegerLit 100))
                                (ExprStmt (CallExpr (Path excel))))))))
        )");
    }

    SUBCASE("Switch with mixed patterns and default") {
        auto node = fixture.parseProgram("func demo() { switch (input) { 0 => zero(); 1..10 => small(); 100..1000 => large(); ... => other(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path input)
                            (CaseStmt (IntegerLit 0)
                                (ExprStmt (CallExpr (Path zero))))
                            (CaseStmt (BinaryExpr .. (IntegerLit 1) (IntegerLit 10))
                                (ExprStmt (CallExpr (Path small))))
                            (CaseStmt (BinaryExpr .. (IntegerLit 100) (IntegerLit 1000))
                                (ExprStmt (CallExpr (Path large))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path other))))))))
        )");
    }
}

TEST_CASE("Switch Statements - Backward Compatibility") {
    ParserTestFixture fixture;

    SUBCASE("Traditional switch with case keywords still works") {
        auto node = fixture.parseProgram("func demo() { switch (value) { case 0 => println(\"zero\"); case 1 => println(\"one\"); default => println(\"other\"); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (IntegerLit 0)
                                (ExprStmt (CallExpr (Path println) (StringLit "zero"))))
                            (CaseStmt (IntegerLit 1)
                                (ExprStmt (CallExpr (Path println) (StringLit "one"))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path println) (StringLit "other"))))))))
        )");
    }

    SUBCASE("Traditional switch with multiple case values") {
        auto node = fixture.parseProgram("func demo() { switch (status) { case 200, 201 => success(); case 404, 500 => error(); default => unknown(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path status)
                            (CaseStmt (IntegerLit 200))
                            (CaseStmt (IntegerLit 201)
                                (ExprStmt (CallExpr (Path success))))
                            (CaseStmt (IntegerLit 404))
                            (CaseStmt (IntegerLit 500)
                                (ExprStmt (CallExpr (Path error))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path unknown))))))))
        )");
    }
}

TEST_CASE("Switch Statements with Complex Expressions") {
    ParserTestFixture fixture;

    SUBCASE("Switch with function call as discriminant") {
        auto node = fixture.parseProgram("func demo() { switch getValue() { 1 => handleOne(); 2 => handleTwo(); ... => handleDefault(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (CallExpr (Path getValue))
                            (CaseStmt (IntegerLit 1)
                                (ExprStmt (CallExpr (Path handleOne))))
                            (CaseStmt (IntegerLit 2)
                                (ExprStmt (CallExpr (Path handleTwo))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path handleDefault))))))))
        )");
    }

    SUBCASE("Switch with computed expressions in cases") {
        auto node = fixture.parseProgram("func demo() { switch (value) { x + 1 => first(); y * 2 => second(); ... => other(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (BinaryExpr + (Path x) (IntegerLit 1))
                                (ExprStmt (CallExpr (Path first))))
                            (CaseStmt (BinaryExpr * (Path y) (IntegerLit 2))
                                (ExprStmt (CallExpr (Path second))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path other))))))))
        )");
    }
}

TEST_CASE("Switch Statements - Error Cases") {
    ParserTestFixture fixture;

    SUBCASE("Switch without cases should fail") {
        auto node = fixture.parseProgram("func demo() { switch (value) { } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about empty switch statement
    }

    SUBCASE("Switch with missing fat arrow should fail") {
        auto node = fixture.parseProgram("func demo() { switch (value) { 1 println(\"one\"); } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about missing '=>'
    }

    SUBCASE("Switch with missing expression should fail") {
        auto node = fixture.parseProgram("func demo() { switch () { 1 => println(\"one\"); } }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
        // Should have diagnostic about missing switch expression
    }
}

TEST_CASE("Switch Statements - Mixed Syntax Edge Cases") {
    ParserTestFixture fixture;

    SUBCASE("Switch mixing case keyword and direct patterns") {
        auto node = fixture.parseProgram("func demo() { switch (value) { case 1 => one(); 2 => two(); ... => other(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (IntegerLit 1)
                                (ExprStmt (CallExpr (Path one))))
                            (CaseStmt (IntegerLit 2)
                                (ExprStmt (CallExpr (Path two))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path other))))))))
        )");
    }

    SUBCASE("Empty case with only comma continuation") {
        auto node = fixture.parseProgram("func demo() { switch (value) { 1, 2, 3 => multi(); ... => other(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (IntegerLit 1))
                            (CaseStmt (IntegerLit 2))
                            (CaseStmt (IntegerLit 3)
                                (ExprStmt (CallExpr (Path multi))))
                            (CaseStmt
                                (ExprStmt (CallExpr (Path other))))))))
        )");
    }
}
