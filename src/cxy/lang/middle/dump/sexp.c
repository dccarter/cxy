/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "core/hash.h"
#include "lang/frontend/operator.h"

#include "sexp.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/types.h"
#include "lang/frontend/visitor.h"
#include "lang/operations.h"

#include "lang/frontend/ast.h"

#include <inttypes.h>

typedef struct {
    FormatState *state;
    Log *L;
    int depth;
    bool needsSpace;
    bool needsNewline;

    struct {
        bool withLocation;
        bool withoutAttrs;
        bool withNamedEnums;
    } config;
} SexpDumpContext;

static void emitSpace(SexpDumpContext *ctx)
{
    if (ctx->needsSpace) {
        format(ctx->state, " ", NULL);
        ctx->needsSpace = false;
    }
}

static void emitNewline(SexpDumpContext *ctx)
{
    format(ctx->state, "\n", NULL);
    for (int i = 0; i < ctx->depth; i++) {
        format(ctx->state, "  ", NULL);
    }
    ctx->needsSpace = false;
}

static void emitOpenParen(SexpDumpContext *ctx)
{
    emitSpace(ctx);
    format(ctx->state, "(", NULL);
    ctx->depth++;
    ctx->needsSpace = false;
}

static void emitCloseParen(SexpDumpContext *ctx)
{
    ctx->depth--;
    format(ctx->state, ")", NULL);
    ctx->needsSpace = true;
}

static void emitSymbol(SexpDumpContext *ctx, cstring symbol)
{
    emitSpace(ctx);
    format(ctx->state, "{s}", (FormatArg[]){{.s = symbol}});
    ctx->needsSpace = true;
}

static void emitType(SexpDumpContext *ctx, const Type *type)
{
    if (type != NULL) {
        emitSpace(ctx);
        format(ctx->state, "`{t}`", (FormatArg[]){{.t = type}});
        ctx->needsSpace = true;
    }
}

static void emitString(SexpDumpContext *ctx, cstring str)
{
    emitSpace(ctx);
    format(ctx->state, "\"", NULL);
    for (const char *c = str; *c; c++) {
        switch (*c) {
        case '"':
            format(ctx->state, "\\\"", NULL);
            break;
        case '\\':
            format(ctx->state, "\\\\", NULL);
            break;
        case '\n':
            format(ctx->state, "\\n", NULL);
            break;
        case '\t':
            format(ctx->state, "\\t", NULL);
            break;
        case '\r':
            format(ctx->state, "\\r", NULL);
            break;
        default:
            format(ctx->state, "{c}", (FormatArg[]){{.c = *c}});
            break;
        }
    }
    format(ctx->state, "\"", NULL);
    ctx->needsSpace = true;
}

static void emitInteger(SexpDumpContext *ctx, i128 value)
{
    emitSpace(ctx);
    format(ctx->state, "{i128}", (FormatArg[]){{.i128 = value}});
    ctx->needsSpace = true;
}

static void emitUInteger(SexpDumpContext *ctx, u128 value)
{
    emitSpace(ctx);
    format(ctx->state, "{u128}", (FormatArg[]){{.u128 = value}});
    ctx->needsSpace = true;
}

static void emitFloat(SexpDumpContext *ctx, f64 value)
{
    emitSpace(ctx);
    format(ctx->state, "{f64}", (FormatArg[]){{.f64 = value}});
    ctx->needsSpace = true;
}

static void emitChar(SexpDumpContext *ctx, u32 value)
{
    emitSpace(ctx);
    format(ctx->state, "'{cE}'", (FormatArg[]){{.c = (char)value}});
    ctx->needsSpace = true;
}

static void emitBool(SexpDumpContext *ctx, bool value)
{
    emitSymbol(ctx, value ? "true" : "false");
}

static void emitNil(SexpDumpContext *ctx)
{
    emitSymbol(ctx, "nil");
}

// Forward declarations
static void manyNodesToSexp(ConstAstVisitor *visitor, const AstNode *nodes);

static void emitMetadata(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    bool hasMetadata = false;

    // Check if we have any metadata to emit
    if (!ctx->config.withoutAttrs && node->attrs) hasMetadata = true;
    if (ctx->config.withLocation) hasMetadata = true;
    if (ctx->config.withNamedEnums && node->flags) hasMetadata = true;

    if (!hasMetadata) return;

    emitNewline(ctx);
    emitOpenParen(ctx);
    emitSymbol(ctx, "Metadata");

    // Emit attributes
    if (!ctx->config.withoutAttrs && node->attrs) {
        emitNewline(ctx);
        emitOpenParen(ctx);
        emitSymbol(ctx, "Attributes");
        manyNodesToSexp(visitor, node->attrs);
        emitCloseParen(ctx);
    }

    // Emit location
    if (ctx->config.withLocation) {
        const FileLoc *loc = &node->loc;
        emitNewline(ctx);
        emitOpenParen(ctx);
        emitSymbol(ctx, "Location");
        emitString(ctx, loc->fileName);
        emitInteger(ctx, loc->begin.row);
        emitInteger(ctx, loc->begin.col);
        emitInteger(ctx, loc->end.row);
        emitInteger(ctx, loc->end.col);
        emitCloseParen(ctx);
    }

    // Emit flags
    if (node->flags && ctx->config.withNamedEnums) {
        emitNewline(ctx);
        emitOpenParen(ctx);
        emitSymbol(ctx, "Flags");
        char *flagStr = flagsToString(node->flags);
        emitString(ctx, flagStr);
        free(flagStr);
        emitCloseParen(ctx);
    } else if (node->flags) {
        emitNewline(ctx);
        emitOpenParen(ctx);
        emitSymbol(ctx, "Flags");
        emitUInteger(ctx, node->flags);
        emitCloseParen(ctx);
    }

    emitCloseParen(ctx);
}

static void dispatch(ConstVisitor func, ConstAstVisitor *visitor, const AstNode *node)
{
    if (node == NULL) {
        SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
        emitNil(ctx);
        return;
    }

    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitOpenParen(ctx);
    emitSymbol(ctx, getAstNodeName(node));
    func(visitor, node);
    emitCloseParen(ctx);
    if (node->next != NULL)
        emitNewline(ctx);
}

static void manyNodesToSexp(ConstAstVisitor *visitor, const AstNode *nodes)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    for (const AstNode *node = nodes; node; node = node->next) {
        emitNewline(ctx);
        astConstVisit(visitor, node);
    }
}

static void visitFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    // For any nodes not explicitly handled, just emit metadata
    emitMetadata(visitor, node);
    astConstVisitFallbackVisitAll(visitor, node);
}

// Visitor functions
static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->program.module) {
        emitNewline(ctx);
        astConstVisit(visitor, node->program.module);
    }

    manyNodesToSexp(visitor, node->program.top);
    manyNodesToSexp(visitor, node->program.decls);
}

static void visitError(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);
    emitString(ctx, node->error.message);
}

static void visitNoop(ConstAstVisitor *visitor, const AstNode *node)
{
    emitMetadata(visitor, node);
}

static void visitLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    switch (node->tag) {
    case astNullLit:
        emitNil(ctx);
        break;
    case astBoolLit:
        emitBool(ctx, node->boolLiteral.value);
        break;
    case astCharLit:
        emitChar(ctx, node->charLiteral.value);
        break;
    case astIntegerLit:
        emitInteger(ctx, integerLiteralValue(node));
        break;
    case astFloatLit:
        emitFloat(ctx, node->floatLiteral.value);
        break;
    case astStringLit:
        emitString(ctx, node->stringLiteral.value);
        break;
    default:
        emitSymbol(ctx, "unknown-literal");
    }
}

static void visitAttr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->attr.name);
    manyNodesToSexp(visitor, node->attr.args);
}

static void visitPath(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *elements = node->path.elements;
    if (elements->next == NULL) {
        emitSymbol(ctx, elements->_name);
        return;
    }
    manyNodesToSexp(visitor, elements);
}

static void visitPathElem(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->_name);
}

static void visitStringExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    emitMetadata(visitor, node);
    manyNodesToSexp(visitor, node->stringExpr.parts);
}

static void visitCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);
    astConstVisit(visitor, node->castExpr.expr);
    astConstVisit(visitor, node->castExpr.to);
}

static void visitDefine(ConstAstVisitor *visitor, const AstNode *node)
{
    emitMetadata(visitor, node);
    manyNodesToSexp(visitor, node->define.names);

    if (node->define.type) {
        SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
        emitNewline(ctx);
        astConstVisit(visitor, node->define.type);
    }

    if (node->define.container) {
        SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
        emitNewline(ctx);
        astConstVisit(visitor, node->define.container);
    }
}

static void visitBackendCall(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);
    emitUInteger(ctx, node->backendCallExpr.func);
    manyNodesToSexp(visitor, node->backendCallExpr.args);
}

static void visitImport(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->import.module) {
        emitNewline(ctx);
        astConstVisit(visitor, node->import.module);
    }

    manyNodesToSexp(visitor, node->import.entities);
}

static void visitImportEntity(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);
    emitString(ctx, node->importEntity.name);

    if (node->importEntity.alias) {
        emitString(ctx, node->importEntity.alias);
    }
}

static void visitModuleDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->moduleDecl.name) {
        emitString(ctx, node->moduleDecl.name);
    }
}

static void visitIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);
    emitSymbol(ctx, node->ident.value);
}

static void visitTuple(ConstAstVisitor *visitor, const AstNode *node)
{
    emitMetadata(visitor, node);
    manyNodesToSexp(visitor, node->tupleType.elements);
}

static void visitArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->arrayType.elementType) {
        emitNewline(ctx);
        astConstVisit(visitor, node->arrayType.elementType);
    }

    if (node->arrayType.dim) {
        emitNewline(ctx);
        astConstVisit(visitor, node->arrayType.dim);
    }
}

static void visitFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    manyNodesToSexp(visitor, node->funcType.params);

    if (node->funcType.ret) {
        emitNewline(ctx);
        astConstVisit(visitor, node->funcType.ret);
    }
}

static void visitOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->optionalType.type) {
        emitNewline(ctx);
        astConstVisit(visitor, node->optionalType.type);
    }
}

static void visitPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);
    emitSymbol(ctx, getPrimitiveTypeName(node->primitiveType.id));
}

static void visitPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->pointerType.pointed) {
        emitNewline(ctx);
        astConstVisit(visitor, node->pointerType.pointed);
    }
}

static void visitReferenceType(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->referenceType.referred) {
        emitNewline(ctx);
        astConstVisit(visitor, node->referenceType.referred);
    }
}

static void visitArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    emitMetadata(visitor, node);
    manyNodesToSexp(visitor, node->arrayExpr.elements);
}

static void visitMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->memberExpr.target) {
        emitNewline(ctx);
        astConstVisit(visitor, node->memberExpr.target);
    }

    if (node->memberExpr.member) {
        emitNewline(ctx);
        astConstVisit(visitor, node->memberExpr.member);
    }
}

static void visitIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->indexExpr.target) {
        emitNewline(ctx);
        astConstVisit(visitor, node->indexExpr.target);
    }

    if (node->indexExpr.index) {
        emitNewline(ctx);
        astConstVisit(visitor, node->indexExpr.index);
    }
}

static void visitCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->callExpr.callee) {
        emitNewline(ctx);
        astConstVisit(visitor, node->callExpr.callee);
    }

    manyNodesToSexp(visitor, node->callExpr.args);
}

static void visitBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, getBinaryOpString(node->binaryExpr.op));
    emitMetadata(visitor, node);

    if (node->binaryExpr.lhs) {
        emitNewline(ctx);
        astConstVisit(visitor, node->binaryExpr.lhs);
    }

    if (node->binaryExpr.rhs) {
        emitNewline(ctx);
        astConstVisit(visitor, node->binaryExpr.rhs);
    }
}

static void visitUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, getUnaryOpString(node->unaryExpr.op));
    emitMetadata(visitor, node);

    if (node->unaryExpr.operand) {
        emitNewline(ctx);
        astConstVisit(visitor, node->unaryExpr.operand);
    }
}

static void visitFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->funcDecl.name);
    emitMetadata(visitor, node);

    // Parameters
    if (node->funcDecl.signature && node->funcDecl.signature->params) {
        manyNodesToSexp(visitor, node->funcDecl.signature->params);
    }

    // Return type
    if (node->funcDecl.signature && node->funcDecl.signature->ret) {
        emitNewline(ctx);
        astConstVisit(visitor, node->funcDecl.signature->ret);
    }

    // Body
    if (node->funcDecl.body) {
        emitNewline(ctx);
        astConstVisit(visitor, node->funcDecl.body);
    }
}

static void visitFuncParamDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->funcParam.name);
    emitMetadata(visitor, node);

    if (node->funcParam.type) {
        emitNewline(ctx);
        astConstVisit(visitor, node->funcParam.type);
    }

    if (node->funcParam.def) {
        emitNewline(ctx);
        astConstVisit(visitor, node->funcParam.def);
    }
}

static void visitVarDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    manyNodesToSexp(visitor, node->varDecl.names);

    if (node->varDecl.type) {
        emitNewline(ctx);
        astConstVisit(visitor, node->varDecl.type);
    }

    if (node->varDecl.init) {
        emitNewline(ctx);
        astConstVisit(visitor, node->varDecl.init);
    }
}

static void visitBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    emitMetadata(visitor, node);
    manyNodesToSexp(visitor, node->blockStmt.stmts);
}

static void visitReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->returnStmt.expr) {
        emitNewline(ctx);
        astConstVisit(visitor, node->returnStmt.expr);
    }
}

static void visitExpressionStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    if (node->exprStmt.expr) {
        emitNewline(ctx);
        astConstVisit(visitor, node->exprStmt.expr);
    }
}

// static void visitCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
// {
//     SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
//     if (node->caseStmt.variable) {
//         emitNewline(ctx);
//         astConstVisit(visitor, node->caseStmt.variable);
//     }
//     if (node->caseStmt.match) {
//         emitNewline(ctx);
//         astConstVisit(visitor, node->caseStmt.match);
//     }
//     if (node->caseStmt.body) {
//         emitNewline(ctx);
//         astConstVisit(visitor, node->caseStmt.body);
//     }
// }

// static void visitSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
// {
//     SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
//     emitMetadata(visitor, node);

//     if (node->exprStmt.expr) {
//         emitNewline(ctx);
//         astConstVisit(visitor, node->exprStmt.expr);
//     }
// }

static void visitIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitMetadata(visitor, node);

    // Condition
    if (node->ifStmt.cond) {
        emitNewline(ctx);
        astConstVisit(visitor, node->ifStmt.cond);
    }

    // Then branch
    if (node->ifStmt.body) {
        emitNewline(ctx);
        astConstVisit(visitor, node->ifStmt.body);
    }

    // Else branch
    if (node->ifStmt.otherwise) {
        emitNewline(ctx);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
}

static void visitStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->structDecl.name);
    emitMetadata(visitor, node);

    manyNodesToSexp(visitor, node->structDecl.members);
}

static void visitFieldDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->structField.name);
    emitMetadata(visitor, node);

    if (node->structField.type) {
        emitNewline(ctx);
        astConstVisit(visitor, node->structField.type);
    }

    if (node->structField.value) {
        emitNewline(ctx);
        astConstVisit(visitor, node->structField.value);
    }
}

static void visitExternDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitSymbol(ctx, node->externDecl.func->_name);
}

static void visitTypeRef(ConstAstVisitor *visitor, const AstNode *node)
{
    SexpDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitType(ctx, node->type);
}

AstNode *dumpAstToSexpState(CompilerDriver *driver, AstNode *node, FormatState *state)
{
    SexpDumpContext ctx = {
        .state = state,
        .L = driver->L,
        .depth = 0,
        .needsSpace = false,
        .config = {
            .withLocation = driver->options.dev.withLocation,
            .withoutAttrs = driver->options.dev.withoutAttrs,
            .withNamedEnums = driver->options.dev.withNamedEnums
        }
    };

    ConstAstVisitor visitor = makeConstAstVisitor(&ctx, {
        [astProgram] = visitProgram,
        [astError] = visitError,
        [astNoop] = visitNoop,
        [astAttr] = visitAttr,
        [astPath] = visitPath,
        [astPathElem] = visitPathElem,
        [astTypeRef] = visitTypeRef,
        [astNullLit] = visitLiteral,
        [astBoolLit] = visitLiteral,
        [astCharLit] = visitLiteral,
        [astIntegerLit] = visitLiteral,
        [astFloatLit] = visitLiteral,
        [astStringLit] = visitLiteral,
        [astStringExpr] = visitStringExpr,
        [astCastExpr] = visitCastExpr,
        [astDefine] = visitDefine,
        [astBackendCall] = visitBackendCall,
        [astImportDecl] = visitImport,
        [astImportEntity] = visitImportEntity,
        [astModuleDecl] = visitModuleDecl,
        [astIdentifier] = visitIdentifier,
        [astTupleType] = visitTuple,
        [astTupleExpr] = visitTuple,
        [astArrayType] = visitArrayType,
        [astFuncType] = visitFuncType,
        [astOptionalType] = visitOptionalType,
        [astPrimitiveType] = visitPrimitiveType,
        [astPointerType] = visitPointerType,
        [astReferenceType] = visitReferenceType,
        [astArrayExpr] = visitArrayExpr,
        [astMemberExpr] = visitMemberExpr,
        [astIndexExpr] = visitIndexExpr,
        [astCallExpr] = visitCallExpr,
        [astBinaryExpr] = visitBinaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astFuncDecl] = visitFuncDecl,
        [astFuncParamDecl] = visitFuncParamDecl,
        [astVarDecl] = visitVarDecl,
        [astStructDecl] = visitStructDecl,
        [astFieldDecl] = visitFieldDecl,
        [astExternDecl] = visitExternDecl,
        [astBlockStmt] = visitBlockStmt,
        [astReturnStmt] = visitReturnStmt,
        [astExprStmt] = visitExpressionStmt,
        [astIfStmt] = visitIfStmt,
    }, .fallback = visitFallback, .dispatch = dispatch);

    // Handle metadata node like YAML dumper does
    if (nodeIs(node, Metadata)) {
        astConstVisit(&visitor, node->metadata.node);
    } else {
        astConstVisit(&visitor, node);
    }

    return node;
}

AstNode *dumpAstToSexp(CompilerDriver *driver, AstNode *node, FILE *file)
{
    FormatState state = newFormatState("  ", true);

    AstNode *result = dumpAstToSexpState(driver, node, &state);

    // Write to file
    char *output = formatStateToString(&state);
    if (output) {
        fprintf(file, "%s\n", output);
        free(output);
    }

    freeFormatState(&state);
    return result;
}
