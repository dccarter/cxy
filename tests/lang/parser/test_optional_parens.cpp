/**
 * Parser Tests: Optional Parentheses in Control Flow Statements
 *
 * Tests for parsing control flow statements with optional parentheses.
 * When parentheses are omitted, braces around the body must be required.
 */

#include "doctest.h"
#include "parser_utils.hpp"
#include "utils/ast.hpp"

using namespace cxy::parser::test;
using namespace cxy::test;

TEST_CASE("If Statements with Optional Parentheses") {
    ParserTestFixture fixture;

    SUBCASE("Traditional if with parentheses and braces") {
        auto node = fixture.parseProgram("func demo() { if (true) { var x = 1; } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (BoolLit true)
                            (BlockStmt
                                (VarDecl (Identifier x) (IntegerLit 1)))))))
        )");
    }

    SUBCASE("Traditional if with parentheses and single statement") {
        auto node = fixture.parseProgram("func demo() { if (true) var x = 1; }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (BoolLit true)
                            (VarDecl (Identifier x) (IntegerLit 1))))))
        )");
    }

    SUBCASE("If without parentheses but with braces") {
        auto node = fixture.parseProgram("func demo() { if true { var x = 1; } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (BoolLit true)
                            (BlockStmt
                                (VarDecl (Identifier x) (IntegerLit 1)))))))
        )");
    }

    SUBCASE("If without parentheses with complex condition") {
        auto node = fixture.parseProgram("func demo() { if x > 0 && y < 10 { return x + y; } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt
                            (BinaryExpr &&
                                (BinaryExpr > (Path x) (IntegerLit 0))
                                (BinaryExpr < (Path y) (IntegerLit 10)))
                            (BlockStmt
                                (ReturnStmt
                                    (BinaryExpr + (Path x) (Path y))))))))
        )");
    }

    SUBCASE("If-else without parentheses") {
        auto node = fixture.parseProgram("func demo() { if condition { doSomething(); } else { doElse(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (Path condition)
                            (BlockStmt (ExprStmt (CallExpr (Path doSomething))))
                            (BlockStmt (ExprStmt (CallExpr (Path doElse))))))))
        )");
    }

    SUBCASE("Nested if without parentheses") {
        auto node = fixture.parseProgram("func demo() { if outer { if inner { action(); } } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (Path outer)
                            (BlockStmt
                                (IfStmt (Path inner)
                                    (BlockStmt
                                        (ExprStmt (CallExpr (Path action))))))))))
        )");
    }

    SUBCASE("If without parentheses and without braces should fail") {
        auto node = fixture.parseProgram("func demo() { if true var x = 1; }");
        CHECK(fixture.getCapturedDiagnostics().size() > 0);
    }
}

TEST_CASE("While Statements with Optional Parentheses") {
    ParserTestFixture fixture;

    SUBCASE("Traditional while with parentheses and braces") {
        auto node = fixture.parseProgram("func demo() { while (true) { continue; } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (WhileStmt (BoolLit true)
                            (BlockStmt (ContinueStmt))))))
        )");
    }

    SUBCASE("Traditional while with parentheses and single statement") {
        auto node = fixture.parseProgram("func demo() { while (x > 0) x--; }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (WhileStmt
                            (BinaryExpr > (Path x) (IntegerLit 0))
                            (ExprStmt (UnaryExpr -- (Path x)))))))
        )");
    }

    SUBCASE("While without parentheses but with braces") {
        auto node = fixture.parseProgram("func demo() { while condition { break; } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (WhileStmt (Path condition)
                            (BlockStmt (BreakStmt))))))
        )");
    }

    SUBCASE("While without parentheses with complex condition") {
        auto node = fixture.parseProgram("func demo() { while i < len && arr.[i] != target { i++; } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (WhileStmt
                            (BinaryExpr &&
                                (BinaryExpr < (Path i) (Path len))
                                (BinaryExpr !=
                                    (IndexExpr (Path arr) (Path i))
                                    (Path target)))
                            (BlockStmt
                                (ExprStmt (UnaryExpr ++ (Path i))))))))
        )");
    }

    SUBCASE("Infinite while without parentheses") {
        auto node = fixture.parseProgram("func demo() { while true { work(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (WhileStmt (BoolLit true)
                            (BlockStmt
                                (ExprStmt (CallExpr (Path work))))))))
        )");
    }

    SUBCASE("While without parentheses and without braces should fail") {
        auto node = fixture.parseProgram("func demo() { while true work(); }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
    }
}

TEST_CASE("For Statements with Optional Parentheses") {
    ParserTestFixture fixture;

    SUBCASE("Traditional for with parentheses and braces") {
        auto node = fixture.parseProgram("func demo() { for (const i: iterable) { process(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier i))
                            (Path iterable)
                            (BlockStmt
                                (ExprStmt (CallExpr (Path process) (Path i))))))))
        )");
    }

    SUBCASE("Traditional for with parentheses and single statement") {
        auto node = fixture.parseProgram("func demo() { for (var item: items) process(item); }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier item))
                            (Path items)
                            (ExprStmt (CallExpr (Path process) (Path item)))))))
        )");
    }

    SUBCASE("For without parentheses but with braces") {
        auto node = fixture.parseProgram("func demo() { for var i: iterable { process(i); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier i))
                            (Path iterable)
                            (BlockStmt
                                (ExprStmt (CallExpr (Path process) (Path i))))))))
        )");
    }

    SUBCASE("For with variable declaration without parentheses") {
        auto node = fixture.parseProgram("func demo() { for var item: items { process(item); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier item))
                            (Path items)
                            (BlockStmt
                                (ExprStmt (CallExpr (Path process) (Path item))))))))
        )");
    }

    SUBCASE("For with complex iteration without parentheses") {
        auto node = fixture.parseProgram("func demo() { for const i: 0..len { if (arr.[i] == target) { return i; } } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (ForStmt
                            (VarDecl (Identifier i))
                            (BinaryExpr .. (IntegerLit 0) (Path len))
                            (BlockStmt
                                (IfStmt
                                    (BinaryExpr ==
                                        (IndexExpr (Path arr) (Path i))
                                        (Path target))
                                    (BlockStmt
                                        (ReturnStmt (Path i)))))))))
        )");
    }

    SUBCASE("For without parentheses and without braces should fail") {
        auto node = fixture.parseProgram("func demo() { for var i: iterable process(i); }");
        CHECK(!fixture.getCapturedDiagnostics().empty());
    }
}

TEST_CASE("Switch Statements with Optional Parentheses") {
    ParserTestFixture fixture;

    SUBCASE("Traditional switch with parentheses") {
        auto node = fixture.parseProgram("func demo() { switch (value) { case 1 => {} default => } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (IntegerLit 1) (BlockStmt))
                            (CaseStmt)))))
        )");
    }

    SUBCASE("Switch without parentheses") {
        auto node = fixture.parseProgram("func demo() { switch value { case 1 => {} default => } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt (Path value)
                            (CaseStmt (IntegerLit 1) (BlockStmt))
                            (CaseStmt)))))
        )");
    }

    SUBCASE("Switch with function call expression without parentheses") {
        auto node = fixture.parseProgram("func demo() { switch getType(obj) { case TypeA => handleA() case TypeB => handleB() } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (SwitchStmt
                            (CallExpr (Path getType) (Path obj))
                            (CaseStmt (Path TypeA)
                                (ExprStmt (CallExpr (Path handleA))))
                            (CaseStmt (Path TypeB)
                                (ExprStmt (CallExpr (Path handleB))))))))
        )");
    }
}

TEST_CASE("Match Statements with Optional Parentheses") {
    ParserTestFixture fixture;

    SUBCASE("Traditional match with parentheses") {
        auto node = fixture.parseProgram("func demo() { match (value) { case i32 => return x case None => return 0 } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (MatchStmt (Path value)
                            (CaseStmt (PrimitiveType i32)
                                (ReturnStmt (Path x)))
                            (CaseStmt (Path None)
                                (ReturnStmt (IntegerLit 0)))))))
        )");
    }

    SUBCASE("Match without parentheses with complex expression") {
        auto node = fixture.parseProgram("func demo() { match parseResult() { case Ok => process(data) case Error => log(msg) } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (MatchStmt (CallExpr (Path parseResult))
                            (CaseStmt (Path Ok)
                                (ExprStmt (CallExpr (Path process) (Path data))))
                            (CaseStmt (Path Error)
                                (ExprStmt (CallExpr (Path log) (Path msg))))))))
        )");
    }

    SUBCASE("Match with method call without parentheses") {
        auto node = fixture.parseProgram("func demo() { match obj.getStatus() { case Active => activate() case Inactive => deactivate() } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (MatchStmt
                            (CallExpr (Path (PathElem obj) (PathElem getStatus)))
                            (CaseStmt (Path Active)
                                (ExprStmt (CallExpr (Path activate))))
                            (CaseStmt (Path Inactive)
                                (ExprStmt (CallExpr (Path deactivate))))))))
        )");
    }
}

TEST_CASE("Mixed Parentheses Usage") {
    ParserTestFixture fixture;

    SUBCASE("Nested statements with mixed parentheses usage") {
        auto node = fixture.parseProgram(
            "func demo() { "
            "if condition { "
            "  for (const i: iterable) { "
            "    while flag { "
            "      switch getValue() { "
            "        case 1 => continue "
            "        default => break "
            "      } "
            "    } "
            "  } "
            "} }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (Path condition)
                            (BlockStmt
                                (ForStmt
                                    (VarDecl (Identifier i))
                                    (Path iterable)
                                    (BlockStmt
                                        (WhileStmt (Path flag)
                                            (BlockStmt
                                                (SwitchStmt
                                                    (CallExpr (Path getValue))
                                                    (CaseStmt (IntegerLit 1) (ContinueStmt))
                                                    (CaseStmt (BreakStmt))))))))))))
        )");
    }

    SUBCASE("All statements without parentheses") {
        auto node = fixture.parseProgram(
            "func demo() { "
            "if condition { "
            "  for const i: iterable { "
            "    while flag { "
            "      switch getValue() { "
            "        case 1 => continue "
            "        default => break "
            "      } "
            "    } "
            "  } "
            "} }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (Path condition)
                            (BlockStmt
                                (ForStmt
                                    (VarDecl (Identifier i))
                                    (Path iterable)
                                    (BlockStmt
                                        (WhileStmt (Path flag)
                                            (BlockStmt
                                                (SwitchStmt
                                                    (CallExpr (Path getValue))
                                                    (CaseStmt (IntegerLit 1) (ContinueStmt))
                                                    (CaseStmt (BreakStmt))))))))))))
        )");
    }
}

TEST_CASE("Edge Cases and Error Conditions") {
    ParserTestFixture fixture;

    SUBCASE("Empty condition should still work") {
        auto node = fixture.parseProgram("func demo() { if true { } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt (BoolLit true) (BlockStmt)))))
        )");
    }

    SUBCASE("Complex nested expression without parentheses") {
        auto node = fixture.parseProgram("func demo() { if a.b.c > d.e.f && check(x, y, z) { action(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt
                            (BinaryExpr &&
                                (BinaryExpr >
                                    (Path (PathElem a) (PathElem b) (PathElem c))
                                    (Path (PathElem d) (PathElem e) (PathElem f)))
                                (CallExpr (Path check) (Path x) (Path y) (Path z)))
                            (BlockStmt
                                (ExprStmt (CallExpr (Path action))))))))
        )");
    }

    SUBCASE("Operator precedence should work correctly") {
        auto node = fixture.parseProgram("func demo() { if a + b * c == d { result(); } }");
        REQUIRE_AST_MATCHES_IGNORE_METADATA(node, R"(
            (Program
                (FuncDecl demo
                    (BlockStmt
                        (IfStmt
                            (BinaryExpr ==
                                (BinaryExpr + (Path a)
                                    (BinaryExpr * (Path b) (Path c)))
                                (Path d))
                            (BlockStmt
                                (ExprStmt (CallExpr (Path result))))))))
        )");
    }
}
