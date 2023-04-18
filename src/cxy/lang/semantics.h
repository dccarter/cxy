/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#pragma once

#include "core/strpool.h"
#include "lang/scope.h"

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strPool;
    TypeTable *typeTable;
    Env env;
    Scope *closure;
    AstNode *previousTopLevelDecl;
    AstNode *currentTopLevelDecl;
    AstNode *program;
    const AstNode *lastReturn;
    u64 anonymousDeclsIndex;
    bool mainOptimized : 1;
    bool deferFuncBodyCheck : 1;
    bool skipFuncDefineSymbol : 1;
} SemanticsContext;

#define ERROR_TYPE(CTX) makeErrorType((CTX)->typeTable)

const Type *evalType(AstVisitor *visitor, AstNode *node);
u64 checkMany(AstVisitor *visitor, AstNode *node);
void addTopLevelDecl(SemanticsContext *ctx, cstring name, AstNode *node);
AstNode *findSymbolByPath(SemanticsContext *ctx, const Env *env, AstNode *node);
AstNode *findSymbolByNode(SemanticsContext *ctx, const Env *env, AstNode *node);

void checkLiterals(AstVisitor *visitor, AstNode *node);
void checkStringExpr(AstVisitor *visitor, AstNode *node);
void checkForStmt(AstVisitor *visitor, AstNode *node);
void checkMember(AstVisitor *visitor, AstNode *node);
void checkClosure(AstVisitor *visitor, AstNode *node);
void checkMethodDeclBody(AstVisitor *visitor, AstNode *node);
const Type *checkMethodDeclSignature(AstVisitor *visitor, AstNode *node);
void checkFunctionDecl(AstVisitor *visitor, AstNode *node);