//
// Created by Carter Mbotho on 2025-05-14.
//

#include <cxy/flag.h>

#include <cxy/ast.h>
#include <cxy/ttable.h>

#include <cxy/core/log.h>
#include <cxy/plugin.h>

AstNode *demoHello(CxyPluginContext *ctx, const AstNode *node, AstNode *args)
{
    return makeStringLiteral(ctx->pool, &node->loc, "Hello World!", NULL, NULL);
}

AstNode *demoAdd(CxyPluginContext *ctx, const AstNode *node, AstNode *args)
{
    if (countAstNodes(args) != 2) {
        logError(ctx->L, &node->loc, "Expected 2 arguments", NULL);
        return NULL;
    }
    return makeBinaryExpr(
        ctx->pool, &node->loc, flgNone, args, opAdd, args->next, NULL, NULL);
}

AstNode *demoCall(CxyPluginContext *ctx, const AstNode *node, AstNode *args)
{
    if (countAstNodes(args) != 3) {
        logError(ctx->L, &node->loc, "Expected 3 arguments", NULL);
        return NULL;
    }

    CXY_REQUIRED_ARG(ctx->L, add, args, &node->loc);
    CXY_REQUIRED_ARG(ctx->L, a, add, &node->loc);
    CXY_REQUIRED_ARG(ctx->L, b, add, &node->loc);
    AstNode *addFuncDecl = resolveAstNode(add);
    if (!nodeIs(addFuncDecl, FuncDecl)) {
        logError(ctx->L, &node->loc, "Could not resolve add function", NULL);
        return NULL;
    }

    return makeBinaryExpr(
        ctx->pool,
        &node->loc,
        flgNone,
        makeCallExpr(ctx->pool,
                     &node->loc,
                     makeResolvedIdentifier(ctx->pool,
                                            &node->loc,
                                            addFuncDecl->_name,
                                            0,
                                            addFuncDecl,
                                            NULL,
                                            NULL),
                     a,
                     flgNone,
                     NULL,
                     NULL),
        opAdd,
        b,
        NULL,
        NULL);
}

AstNode *demoAddMembers(CxyPluginContext *ctx,
                        const AstNode *node,
                        AstNode *args)
{
    AstNodeList members = {};
    AstNode *arg = args;
    for (; arg; arg = arg->next) {
        if (!nodeIs(arg, TupleExpr)) {
            logError(ctx->L, &arg->loc, "expecting member name and type", NULL);
            return NULL;
        }
        AstNode *name = arg->tupleExpr.elements, *type = name->next;
        AstNode *member = makeStructField(ctx->pool,
                                          &arg->loc,
                                          name->stringLiteral.value,
                                          flgNone,
                                          type,
                                          NULL,
                                          NULL);
        insertAstNode(&members, member);
    }
    return members.first;
}

bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    logNote(ctx->L, loc, "Hello from plugin!", NULL);
    cxyPluginRegisterAction(
        ctx,
        loc,
        (CxyPluginAction[]){{.name = "hello", .fn = demoHello},
                            {.name = "add", .fn = demoAdd},
                            {.name = "call", .fn = demoCall},
                            {.name = "addMembers", .fn = demoAddMembers}},
        4);
    return true;
}