//
// Created by Carter Mbotho on 2026-03-20.
//

#include "codegen.h"

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/frontend/typegraph.h"
#include "lang/frontend/ttable.h"

static void visitRecordExternalType(ConstAstVisitor *visitor, const AstNode *node)
{
    TypeGraph *g = getConstAstVisitorContext(visitor);
    const Type *type = stripAll(node->type);
    if (hasFlag(type, Extern)) {
        addTypeGraphNodeToModule(g, type);
    }
    astConstVisitFallbackVisitAll(visitor, node);
}

static void visitVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    TypeGraph *g = getConstAstVisitorContext(visitor);
    const Type *type = stripAll(node->type);
    if (!hasFlag(node, TopLevelDecl) && (isTupleType(type) || isUnionType(type) || isArrayType(type))) {
        addTypeGraphNodeToModule(g, type);
    }
    astConstVisitFallbackVisitAll(visitor, node);
}

static void visitFunctionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    TypeGraph *g = getConstAstVisitorContext(visitor);
    const Type *type = node->type;
    addTypeGraphNodeToModule(g, type->func.retType);
    for (int i = 0; i < type->func.paramsCount; i++) {
        addTypeGraphNodeToModule(g, type->func.params[i]);
    }
    astConstVisitFallbackVisitAll(visitor, node->funcDecl.body);
}

static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNode *decl = node->program.decls;
    for (; decl; decl = decl->next) {
        switch (decl->tag) {
        case astStructDecl:
        case astClassDecl:
            astConstVisitFallbackVisitAll(visitor, decl);
            break;
        case astFuncDecl:
            if (!hasFlag(decl->type, Extern)) {}
                visitFunctionDecl(visitor, decl);
        default:
            break;
        }
    }
}

void preCodeGen(TypeGraph *g, const AstNode *node)
{
    ConstAstVisitor visitor = makeConstAstVisitor(g, {
        [astProgram] = visitProgram,
        [astAsm] = astConstVisitSkip,
        [astBasicBlock] = astConstVisitSkip,
        [astBranch] = astConstVisitSkip,
        [astBackendCall] = astConstVisitSkip,
        [astIdentifier] = astConstVisitSkip,
        [astIntegerLit] = astConstVisitSkip,
        [astNullLit] = astConstVisitSkip,
        [astBoolLit] = astConstVisitSkip,
        [astCharLit] = astConstVisitSkip,
        [astFloatLit] = astConstVisitSkip,
        [astStringLit] = astConstVisitSkip,
        [astTupleExpr] = astConstVisitFallbackVisitAll,
        [astUnionValueExpr] = astConstVisitFallbackVisitAll,
        [astBlockStmt] = astConstVisitFallbackVisitAll,
        [astExprStmt] = astConstVisitFallbackVisitAll,
        [astReturnStmt] = astConstVisitFallbackVisitAll,
        [astIfStmt] = astConstVisitFallbackVisitAll,
        [astWhileStmt] = astConstVisitFallbackVisitAll,
        [astCaseStmt] = astConstVisitFallbackVisitAll,
        [astSwitchStmt] = astConstVisitFallbackVisitAll,
        [astMatchStmt] = astConstVisitFallbackVisitAll,
        [astContinueStmt] = astConstVisitSkip,
        [astBreakStmt] = astConstVisitSkip,
        [astVarDecl] = visitVariableDecl,
        [astFuncParamDecl] = astConstVisitFallbackVisitAll,
        [astExternDecl] = astConstVisitFallbackVisitAll,
        [astFuncDecl] = visitFunctionDecl,
        [astStructDecl] = astConstVisitFallbackVisitAll,
        [astClassDecl] = astConstVisitFallbackVisitAll,
        [astInterfaceDecl] = astConstVisitFallbackVisitAll,
        [astUnionDecl] = astConstVisitFallbackVisitAll,
        [astEnumDecl] = astConstVisitFallbackVisitAll,
        [astTypeRef] = astConstVisitFallbackVisitAll,
        [astTypeDecl] = astConstVisitFallbackVisitAll,
        [astGenericDecl] = astConstVisitSkip,
        [astImportDecl] = astConstVisitSkip,
        [astMacroDecl] = astConstVisitSkip,
        [astException] = astConstVisitFallbackVisitAll,
    }, .fallback = visitRecordExternalType);

    astConstVisit(&visitor, node);
}