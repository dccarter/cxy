//
// Created by Carter on 2023-04-26.
//

#include "eval.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"

static bool comptimeCompareManyTypes(const AstNode *lhs, const AstNode *rhs)
{
    while (lhs && rhs) {
        if (!comptimeCompareTypes(lhs, rhs))
            return false;
        lhs = lhs->next;
        rhs = rhs->next;
    }

    return lhs == NULL && rhs == NULL;
}

static void evalGroupExpr(AstVisitor *visitor, AstNode *node)
{
    if (!evaluate(visitor, node->groupExpr.expr)) {
        node->tag = astError;
        return;
    }

    replaceAstNodeWith(node, node->groupExpr.expr);
    node->flags &= ~flgComptime;
}

static void evalCastExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    if (!evaluate(visitor, node->castExpr.expr)) {
        node->tag = astError;
        return;
    }
    const Type *type = evalType(ctx, node);
    if (typeIs(type, Error)) {
        node->tag = astError;
    }
}

static void evalFallback(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    logError(ctx->L, &node->loc, "unsupported compile time expression", NULL);
    node->tag = astError;
}

bool isNoopNodeAfterEval(const AstNode *node)
{
    return nodeIs(node, Noop) ||
           (nodeIs(node, VarDecl) && hasFlag(node, Comptime));
}

const Type *evalType(EvalContext *ctx, AstNode *node)
{
    astVisit(ctx->typer, node);
    return node->type;
}

bool comptimeCompareTypes(const AstNode *lhs, const AstNode *rhs)
{
    if (lhs->tag != rhs->tag) {
        if (nodeIs(lhs, TypeRef) || nodeIs(rhs, TypeRef))
            return lhs->type == rhs->type;
        return lhs->tag == astAutoType;
    }

    switch (lhs->tag) {
    case astStringType:
    case astVoidType:
        return true;
    case astPrimitiveType:
        return lhs->primitiveType.id == rhs->primitiveType.id;
    case astArrayType:
        return lhs->arrayType.dim == rhs->arrayType.dim &&
               comptimeCompareTypes(lhs->arrayType.elementType,
                                    rhs->arrayType.elementType);
    case astTupleType:
        return comptimeCompareManyTypes(lhs->tupleType.elements,
                                        rhs->tupleType.elements);
    case astPointerType:
        return comptimeCompareTypes(lhs->pointerType.pointed,
                                    rhs->pointerType.pointed);
    case astReferenceType:
        return comptimeCompareTypes(lhs->referenceType.referred,
                                    rhs->referenceType.referred);
    case astOptionalType:
        return comptimeCompareTypes(lhs->optionalType.type,
                                    rhs->optionalType.type);
    case astFuncType:
        return comptimeCompareTypes(lhs->funcType.ret, rhs->funcType.params);
    case astEnumDecl:
        return lhs->enumDecl.name == rhs->enumDecl.name;
    case astStructDecl:
        return lhs->structDecl.name == rhs->structDecl.name;
    case astFuncDecl:
        return lhs->funcDecl.name == rhs->funcDecl.name;
    case astTypeRef:
        return lhs->type == rhs->type;
    case astRef:
        lhs = lhs->reference.target;
        return nodeIs(rhs, Ref)
                   ? comptimeCompareTypes(lhs, rhs->reference.target)
                   : comptimeCompareTypes(lhs, rhs);
    default:
        return lhs == rhs;
    }
}

bool evaluate(AstVisitor *visitor, AstNode *node)
{
    if (node->tag != astUnaryExpr &&
        (isLiteralExpr(node) || isBuiltinTypeExpr(node)))
        return true;

    astVisit(visitor, node);
    return !nodeIs(node, Error);
}

void evalIfStmt(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ifStmt.cond;
    if (!evaluate(visitor, cond) || !evalBooleanCast(ctx, cond)) {
        node->tag = astError;
        return;
    }

    AstNode *next = node->next;
    u64 visited = node->flags & flgVisited;

    if (cond->boolLiteral.value) {
        // select then branch & reclaim else branch if any
        AstNode *replacement = node->ifStmt.body;
        if (nodeIs(replacement, BlockStmt))
            replacement = replacement->blockStmt.stmts
                              ?: makeAstNop(ctx->pool, &replacement->loc);
        replacement->parentScope = node->parentScope;
        clearAstBody(node);
        node->tag = astNoop;
        node->flags &= ~flgComptime;
        node->next = replacement;
        getLastAstNode(replacement)->next = next;
    }
    else if (node->ifStmt.otherwise) {
        // select otherwise, reclaim if branch
        AstNode *replacement = node->ifStmt.otherwise;
        if (nodeIs(replacement, BlockStmt))
            replacement = replacement->blockStmt.stmts
                              ?: makeAstNop(ctx->pool, &replacement->loc);

        // select next statement, reclaim if branch
        replacement->parentScope = node->parentScope;
        clearAstBody(node);
        node->tag = astNoop;
        node->flags &= ~flgComptime;
        node->next = replacement;
        getLastAstNode(replacement)->next = next;
    }
    else {
        // select next statement, reclaim if branch
        clearAstBody(node);
        node->tag = astNoop;
        node->flags &= ~flgComptime;
    }

    while (nodeIs(node, IfStmt) && hasFlag(node, Comptime)) {
        node->flags &= ~flgComptime;
        if (!evaluate(visitor, node)) {
            node->tag = astError;
            return;
        }
    }

    node->flags |= visited;
}

void evalTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);

    AstNode *cond = node->ternaryExpr.cond;
    if (!evaluate(visitor, cond) || !evalBooleanCast(ctx, cond)) {
        node->tag = astError;
        return;
    }

    AstNode *next = node->next;
    u64 visited = node->flags & flgVisited;

    if (cond->boolLiteral.value) {
        // select then branch & reclaim else branch if any x
        AstNode *replacement = node->ternaryExpr.body;
        replacement->parentScope = node->parentScope;
        *node = *replacement;
        node->next = next;
    }
    else {
        // select otherwise, reclaim if branch
        AstNode *replacement = node->ternaryExpr.otherwise;
        // select next statement, reclaim if branch
        replacement->parentScope = node->parentScope;
        *node = *replacement;
        node->next = next;
    }

    while (nodeIs(node, TernaryExpr) || hasFlag(node, Comptime)) {
        node->flags &= ~flgComptime;
        if (!evaluate(visitor, node)) {
            node->tag = astError;
            return;
        }
    }

    node->flags |= visited;
}

void evalTupleExpr(AstVisitor *visitor, AstNode *node)
{
    u64 i = 0;
    AstNode *elem = node->tupleExpr.elements;
    for (; elem; elem = elem->next, i++) {
        if (!evaluate(visitor, elem)) {
            node->tag = astError;
            return;
        }
    }
    node->tupleExpr.len = i;
}

void evalAssignExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *left = node->assignExpr.lhs, *right = node->assignExpr.rhs;
    AstNode *resolved = getResolvedPath(left);

    if (!nodeIs(resolved, VarDecl)) {
        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, right)) {
        node->tag = astError;
        return;
    }

    node->tag = astNoop;
    if (node->assignExpr.op == opAssign) {
        resolved->varDecl.init = right;
        return;
    }

    AstNode lhs = *resolved->varDecl.init;
    AstNode binary = (AstNode){
        .tag = astBinaryExpr,
        .loc = node->loc,
        .binaryExpr = {.op = node->assignExpr.op, .lhs = &lhs, .rhs = right}};

    evalBinaryExpr(visitor, &binary);

    *resolved->varDecl.init = binary;
}

void evalRangeExpr(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *start = node->rangeExpr.start, *end = node->rangeExpr.end,
            *step = node->rangeExpr.step;
    AstNode *next = node->next, *parentScope = node->parentScope;
    if (!evaluate(visitor, start)) {
        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, end)) {
        node->tag = astError;
        return;
    }

    if (step && !evaluate(visitor, step)) {
        node->tag = astError;
        return;
    }

    if (!nodeIs(start, IntegerLit) || !nodeIs(end, IntegerLit) ||
        (step && nodeIs(step, IntegerLit))) {
        logError(
            ctx->L,
            &node->loc,
            "unsupported comp-time operation '..', range start and stop must "
            "be compile time expressions",
            NULL);
    }

    node->next = next;
    node->parentScope = parentScope;
}

void evalArrayExpr(AstVisitor *visitor, AstNode *node)
{
    AstNode *arg = node->arrayExpr.elements;
    for (; arg; arg = arg->next) {
        if (!evaluate(visitor, arg)) {
            node->tag = astError;
            return;
        }
    }
}

void evalVarDecl(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    AstNode *names = node->varDecl.names, *init = node->varDecl.init;
    if (names->next) {
        logError(ctx->L,
                 &node->loc,
                 "unsupported: compile time multi-variable declaration not "
                 "supported",
                 NULL);

        node->tag = astError;
        return;
    }

    if (!evaluate(visitor, init)) {
        node->tag = astError;
        return;
    }

    // retain comptime
    node->type = node->varDecl.init->type;
    node->flags = flgVisited | flgComptime;
}

static void evalTypeDecl(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    const Type *type = evalType(ctx, node);
    node->tag = astTypeRef;
    node->type = type;
    clearAstBody(node);
}

static void evalRef(AstVisitor *visitor, AstNode *node)
{
    EvalContext *ctx = getAstVisitorContext(visitor);
    const Type *type = evalType(ctx, node->reference.target);
    node->tag = astTypeRef;
    node->type = type;
    clearAstBody(node);
}

void initEvalVisitor(AstVisitor *visitor, EvalContext *ctx)
{
    // clang-format off
    *visitor = makeAstVisitor(ctx, {
        [astPath] = evalPath,
        [astIfStmt] = evalIfStmt,
        [astForStmt] = evalForStmt,
        [astTupleExpr] = evalTupleExpr,
        [astBinaryExpr] = evalBinaryExpr,
        [astAssignExpr] = evalAssignExpr,
        [astRangeExpr] = evalRangeExpr,
        [astUnaryExpr] = evalUnaryExpr,
        [astIndexExpr] = evalIndexExpr,
        [astMemberExpr] = evalMemberExpr,
        [astArrayExpr]= evalArrayExpr,
        [astGroupExpr] = evalGroupExpr,
        [astTernaryExpr] = evalTernaryExpr,
        [astCastExpr] = evalCastExpr,
        [astTypedExpr] = evalCastExpr,
        [astEnumDecl] = evalEnumDecl,
        [astMacroCallExpr] = evalMacroCall,
        [astVarDecl] = evalVarDecl,
        [astFuncType] = evalTypeDecl,
        [astUnionDecl] = evalTypeDecl,
        [astTupleType] = evalTypeDecl,
        [astArrayType] = evalTypeDecl,
        [astRef] = evalRef,
        [astTupleXform] = evalXformType,
        [astIdentifier] = astVisitSkip,
        [astTypeRef] = astVisitSkip,
        [astList] = astVisitSkip,
        [astComptimeOnly] = astVisitSkip,
        [astNoop] = astVisitSkip,
    }, .fallback = evalFallback);

    initComptime(ctx);
}
