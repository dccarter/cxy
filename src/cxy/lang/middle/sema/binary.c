//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/builtins.h"

typedef enum {
    optInvalid = -1,
    optNumeric,
    optInteger,
    optLogical,
    optComparison,
    optEquality,
    optTypeEquality,
    optRange,
    optCatch,
} BinaryOperatorKind;

static BinaryOperatorKind getBinaryOperatorKind(Operator op)
{
    switch (op) {
        // Numeric arithmetic
#define f(O, ...) case op##O:
        AST_ARITH_EXPR_LIST(f)
        return optNumeric;

        AST_BIT_EXPR_LIST(f)
        AST_SHIFT_EXPR_LIST(f)
        return optInteger;

        AST_LOGIC_EXPR_LIST(f)
        return optLogical;

        AST_CMP_EXPR_LIST(f)
        if (op == opEq || op == opNe)
            return optEquality;
        return (op == opIs) ? optTypeEquality : optComparison;
#undef f
    case opRange:
        return optRange;
    case opCatch:
        return optCatch;
    default:
        unreachable("");
    }
}

static bool checkClassBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    cstring name = getOpOverloadName(node->binaryExpr.op);
    const Type *target = stripPointerOrReference(node->binaryExpr.lhs->type);
    const NamedTypeMember *overload =
        findOverloadMemberUpInheritanceChain(target, name);

    const Type *rhs = checkType(visitor, node->binaryExpr.rhs);
    if (typeIs(rhs, Error)) {
        node->type = ERROR_TYPE(ctx);
        return false;
    }

    if (overload == NULL)
        return false;

    const AstNode *decl = nodeIs(overload->decl, GenericDecl)
                              ? overload->decl->genericDecl.decl
                              : overload->decl;
    const AstNode *param = decl->funcDecl.signature->params;
    if (nodeIsThisParam(param))
        param = param->next;

    if (nodeIs(param->funcParam.type, ReferenceType) && !isReferenceType(rhs) &&
        isReferable(rhs)) {
        node->binaryExpr.rhs = makeReferenceOfExpr(ctx->pool,
                                                   &node->binaryExpr.rhs->loc,
                                                   flgNone,
                                                   node->binaryExpr.rhs,
                                                   NULL,
                                                   NULL);
    }

    transformToMemberCallExpr(
        visitor, node, node->binaryExpr.lhs, name, node->binaryExpr.rhs);

    checkType(visitor, node);
    return true;
}

static void checkBinaryOperatorOverload(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    const Type *left = node->binaryExpr.lhs->type;
    cstring name = getOpOverloadName(node->binaryExpr.op);
    const Type *target = stripPointer(left);
    const NamedTypeMember *overload =
        findOverloadMemberUpInheritanceChain(stripAll(target), name);

    if (overload == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "struct '{t}' does not not overload '{s}' binary operator",
                 (FormatArg[]){{.t = target},
                               {.s = getBinaryOpString(node->binaryExpr.op)}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    const Type *right = checkType(visitor, node->binaryExpr.rhs);
    if (typeIs(right, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    //
    //    if (isPointerType(left) && !isPointerType(right)) {
    //        node->binaryExpr.rhs = makePointerOfExpr(
    //            ctx->pool,
    //            &node->binaryExpr.rhs->loc,
    //            node->binaryExpr.rhs->flags,
    //            node->binaryExpr.rhs,
    //            NULL,
    //            makePointerType(ctx->types, right, right->flags & flgConst));
    //    }
    //    else if (isReferenceType(left) && !isReferenceType(right)) {
    //        node->binaryExpr.rhs = makeReferenceOfExpr(
    //            ctx->pool,
    //            &node->binaryExpr.rhs->loc,
    //            node->binaryExpr.rhs->flags,
    //            node->binaryExpr.rhs,
    //            NULL,
    //            makeReferenceType(ctx->types, right, right->flags &
    //            flgConst));
    //    }

    const AstNode *decl = nodeIs(overload->decl, GenericDecl)
                              ? overload->decl->genericDecl.decl
                              : overload->decl;
    const AstNode *param = decl->funcDecl.signature->params;
    if (nodeIsThisParam(param))
        param = param->next;

    if (nodeIs(param->funcParam.type, ReferenceType) &&
        !isReferenceType(right) && isReferable(right)) {
        node->binaryExpr.rhs = makeReferenceOfExpr(ctx->pool,
                                                   &node->binaryExpr.rhs->loc,
                                                   flgNone,
                                                   node->binaryExpr.rhs,
                                                   NULL,
                                                   NULL);
    }

    transformToMemberCallExpr(
        visitor, node, node->binaryExpr.lhs, name, node->binaryExpr.rhs);

    checkType(visitor, node);
}

void checkBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    ctx->explicitCatch = node->binaryExpr.op == opCatch;
    bool currentReturnState = ctx->returnState;
    const Type *left = checkType(visitor, lhs), *left_ = stripAll(left);

    Operator op = node->binaryExpr.op;
    BinaryOperatorKind opKind = getBinaryOperatorKind(op);
    bool isNullEquality = opKind == optEquality && nodeIs(rhs, NullLit);

    if ((opKind == optComparison || opKind == optEquality) &&
        typeIs(unwrapType(left, NULL), String) && !isNullEquality) {
        lhs = node->binaryExpr.lhs;
        AstNode *cStringDecl = findBuiltinDecl(S___string);
        node->binaryExpr.lhs = makeCallExpr(ctx->pool,
                                            &lhs->loc,
                                            makeResolvedPath(ctx->pool,
                                                             &lhs->loc,
                                                             S___string,
                                                             flgNone,
                                                             cStringDecl,
                                                             NULL,
                                                             cStringDecl->type),
                                            lhs,
                                            flgNone,
                                            NULL,
                                            NULL);
        left_ = stripAll(checkType(visitor, node->binaryExpr.lhs));
    }

    if (typeIs(left_, Struct) && !hasFlag(left_->tStruct.decl, Extern)) {
        if (!isNullEquality || !isPointerType(left)) {
            checkBinaryOperatorOverload(visitor, node);
            return;
        }
    }

    if (typeIs(left_, Class) && !isNullEquality) {
        if (checkClassBinaryOperatorOverload(visitor, node))
            return;
        if (typeIs(node->type, Error))
            return;
    }

    node->binaryExpr.rhs->parentScope = node;

    __typeof(ctx->catcher) catcher = ctx->catcher, newCatcher = {};
    ctx->catcher.expr = lhs;
    ctx->catcher.block = rhs;
    ctx->catcher.variable = NULL;
    ctx->catcher.ex = NULL;
    const Type *right = checkType(visitor, node->binaryExpr.rhs);
    newCatcher = ctx->catcher;
    ctx->catcher = catcher;

    if (typeIs(right, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    if (isNullEquality) {
        node->type = getPrimitiveType(ctx->types, prtBool);
        return;
    }

    if (opKind == optTypeEquality) {
        if (!nodeIs(rhs, TypeRef) && !hasFlag(rhs, Typeinfo)) {
            logError(
                ctx->L,
                &node->loc,
                "right hand side of `{$}is{$}` operator must be a type",
                (FormatArg[]){{.style = keywordStyle}, {.style = resetStyle}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        node->type = getPrimitiveType(ctx->types, prtBool);
        if (typeIsBaseOf(left_, right)) {
            // transform to lhs.v_table.__tid == tidof!(right)
            AstNode *target = makeMemberExpr(
                ctx->pool,
                &lhs->loc,
                flgNone,
                duplicateAstNode(ctx->pool, lhs),
                makeIdentifier(ctx->pool, &lhs->loc, S_vtable, 0, NULL, NULL),
                NULL,
                NULL);
            lhs->type = NULL;
            clearAstBody(lhs);
            lhs->tag = astMemberExpr;
            lhs->memberExpr.target = target;
            lhs->memberExpr.member =
                makeIdentifier(ctx->pool, &lhs->loc, S___tid, 0, NULL, NULL);

            clearAstBody(rhs);
            rhs->tag = astIntegerLit;
            rhs->intLiteral.uValue = resolveAndUnThisType(right)->index;
            rhs->type = getPrimitiveType(ctx->types, prtU64);

            node->type = NULL;
            node->binaryExpr.op = opEq;
            astVisit(visitor, node);
            return;
        }

        if (!isUnionType(left_)) {
            node->tag = astBoolLit;
            node->boolLiteral.value = compareTypes(left, right);
        }
        return;
    }

    if (opKind == optCatch) {
        checkCatchBinaryOperator(visitor, node, &newCatcher);
        ctx->returnState = currentReturnState;
        return;
    }

    const Type *type = unwrapType(promoteType(ctx->types, left, right), NULL);

    if (type == NULL) {
        logError(ctx->L,
                 &node->loc,
                 "binary operation '{s}' between type '{t}' and '{t}' is not "
                 "supported",
                 (FormatArg[]){
                     {.s = getBinaryOpString(op)},
                     {.t = left},
                     {.t = right ?: makeErrorType(ctx->types)},
                 });
        node->type = ERROR_TYPE(ctx);
        return;
    }
    node->type = type;

    switch (opKind) {
    case optNumeric:

        if (isNumericType(type) ||
            (isPointerType(type) && (op == opAdd || op == opSub)))
            break;

        logError(ctx->L,
                 &node->loc,
                 "cannot perform binary operation '{s}' on non-numeric "
                 "type '{t}'",
                 (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
        node->type = ERROR_TYPE(ctx);
        return;

    case optInteger:
        if (!isIntegralType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform binary operation '{s}' on non-integeral "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
        }
        break;

    case optLogical:
        if (!isBooleanType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform logical binary operation '{s}' on "
                     "non-boolean "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
        }
        break;

    case optComparison:
        if (!isNumericType(type)) {
            logError(ctx->L,
                     &node->loc,
                     "cannot perform comparison binary operation '{s}' on "
                     "non-numeric "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;

    case optEquality:
        switch (type->tag) {
        case typPrimitive:
        case typPointer:
        case typString:
        case typOpaque:
        case typEnum:
        case typFunc:
        case typClass:
            break;
        default:
            logError(ctx->L,
                     &node->loc,
                     "cannot perform equality binary operation '{s}' on "
                     "type '{t}'",
                     (FormatArg[]){{.s = getBinaryOpString(op)}, {.t = type}});
            node->type = ERROR_TYPE(ctx);
            return;
        }
        node->type = getPrimitiveType(ctx->types, prtBool);
        break;
    case optRange: {
        if (!isIntegralType(left)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression "
                     "start, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        if (!isIntegralType(right)) {
            logError(ctx->L,
                     &node->loc,
                     "expecting an integral type for range expression end, got "
                     "type '{t}'",
                     (FormatArg[]){{.t = left}});
            node->type = ERROR_TYPE(ctx);
            return;
        }

        AstNode binary = *node;
        clearAstBody(node);
        node->tag = astRangeExpr;
        node->rangeExpr.start = binary.binaryExpr.lhs;
        node->rangeExpr.end = binary.binaryExpr.rhs;
        node->rangeExpr.step = NULL;
        node->type = type;
        break;
    }
    default:
        unreachable("");
    }
}
