//
// Created by Carter Mbotho on 2025-12-03.
//

#include "bind.h"

#include "lang/middle/builtins.h"
#include "lang/middle/macro.h"
#include "lang/middle/scope.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"

static void macroBindPath(AstVisitor *visitor, AstNode *node)
{
    BindContext *ctx = getAstVisitorContext(visitor);
    AstNode *base = node->path.elements;
    if (hasFlag(base, Module)) {
        base->pathElement.resolvesTo = findSymbolInRoot(ctx->env, ctx->L, base->_name, &base->loc);
    }
}

static void macroBindMacroCallExpr(AstVisitor *visitor, AstNode *node)
{
    astVisitManyNodes(visitor, node->macroCallExpr.args);
}

void bindAstMacroDecl(BindContext *ctx , AstNode *node)
{
    // clang-format off
    AstVisitor visitor = makeAstVisitor(ctx, {
        [astPath] = macroBindPath,
        [astMacroCallExpr] = macroBindMacroCallExpr,
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);
}
