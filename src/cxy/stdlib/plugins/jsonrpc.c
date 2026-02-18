//
// Created by Carter Mbotho on 2025-05-15.
//

#include <cxy/core/log.h>

#include <cxy/ast.h>
#include <cxy/flag.h>
#include <cxy/plugin.h>
#include <cxy/strings.h>
#include <cxy/ttable.h>
#include <cxy/types.h>

cstring jsonrpc_submit = NULL;
cstring jsonrpc_api = NULL;

static AstNode *makeReturnType(CxyPluginContext *ctx,
                               const FileLoc *loc,
                               const Type *retType,
                               AstNode *voidType,
                               AstNode *exceptionType)
{
    AstNode *resultType = isVoidType(retType)
                              ? makeResolvedPath(ctx->pool,
                                                 loc,
                                                 S_Void,
                                                 flgNone,
                                                 voidType,
                                                 NULL,
                                                 voidType->type)
                              : makeTypeReferenceNode(ctx->pool, retType, loc);
    resultType->next = makeResolvedPath(ctx->pool,
                                        loc,
                                        S_Exception,
                                        flgNone,
                                        exceptionType,
                                        NULL,
                                        exceptionType->type);
    return makeUnionDeclAst(ctx->pool, loc, flgNone, resultType, NULL, NULL);
}

static AstNode *createThisParameter(CxyPluginContext *ctx,
                                    const FileLoc *loc,
                                    AstNode *closure,
                                    AstNode *next)
{
    return makeFunctionParam(
        ctx->pool,
        loc,
        S_this,
        makeReferenceTypeAstNode(ctx->pool,
                                 loc,
                                 flgNone,
                                 makeResolvedPath(ctx->pool,
                                                  loc,
                                                  closure->structDecl.name,
                                                  flgNone,
                                                  closure,
                                                  NULL,
                                                  NULL),
                                 NULL,
                                 NULL),
        NULL,
        flgNone,
        next);
}

static AstNode *clientRpcMethodWrapper(CxyPluginContext *ctx,
                                       const FileLoc *loc,
                                       CxyEnvironmentVar *env,
                                       const AstNode *node)
{
    AstNodeList params = {};
    AstNodeList submitArgs = {};
    AstNode *submit = env[0].value, *exceptionType = env[1].value,
            *voidType = env[2].value;
    AstNode *closure = submit->parentScope;
    const Type *type = node->type;

    insertAstNode(&submitArgs,
                  makeStringLiteral(ctx->pool, loc, node->_name, NULL, NULL));

    // Clone function parameters, skipping over the `this` parameter
    AstNode *param = node->funcDecl.signature->params;
    if (param->_name == S_this)
        param = param->next;

    for (; param; param = param->next) {
        AstNode *symbol = deepCloneAstNode(ctx->pool, param);
        symbol->next = NULL;
        insertAstNode(&params, symbol);

        // each argument will be passed as a pair of parameter name and value.
        // i.e. ('a', a)
        insertAstNode(
            &submitArgs,
            makeTupleExpr(ctx->pool,
                          builtinLoc(),
                          flgNone,
                          makeStringLiteral(ctx->pool,
                                            builtinLoc(),
                                            param->_name,
                                            makeResolvedIdentifier(ctx->pool,
                                                                   builtinLoc(),
                                                                   param->_name,
                                                                   0,
                                                                   symbol,
                                                                   NULL,
                                                                   NULL),
                                            NULL),
                          NULL,
                          NULL));
    }

    // We need our function to throw so make the return type be !Ret
    AstNode *resultType =
        makeReturnType(ctx, loc, type->func.retType, voidType, exceptionType);
    AstNode *func = makeFunctionDecl(ctx->pool,
                                     builtinLoc(),
                                     node->_name,
                                     params.first,
                                     resultType,
                                     NULL,
                                     flgGenerated,
                                     NULL,
                                     NULL);
    func->funcDecl.this_ = createThisParameter(ctx, loc, closure, params.first);

    // Create submit[RetType]("name", ('a0', a0), ("a1", a1), ...)
    AstNode *expr = makeCallExpr(
        ctx->pool,
        builtinLoc(),
        makeResolvedPathWithArgs(
            ctx->pool,
            builtinLoc(),
            jsonrpc_submit,
            flgMember,
            submit,
            makeTypeReferenceNode(ctx->pool, type->func.retType, loc),
            NULL),
        submitArgs.first,
        flgNone,
        NULL,
        NULL);

    AstNode *stmt = func->funcDecl.body =
        makeBlockStmt(ctx->pool, builtinLoc(), NULL, NULL, NULL);
    if (isVoidType(type->func.retType)) {
        stmt->blockStmt.stmts =
            makeExprStmt(ctx->pool, builtinLoc(), flgNone, expr, NULL, NULL);
    }
    else {
        stmt->blockStmt.stmts = makeReturnAstNode(
            ctx->pool, builtinLoc(), flgNone, expr, NULL, NULL);
        stmt->blockStmt.stmts->returnStmt.func = func;
    }

    return func;
}

static AstNode *addClientMethods(CxyPluginContext *ctx,
                                 const AstNode *node,
                                 AstNode *args)
{
    CXY_REQUIRED_ARG(ctx->L, envArg, args, &node->loc);
    cxyPluginLoadEnvironment(
        ctx, &node->loc, envArg, {"submit"}, {"Exception"}, {"Void"});
    CXY_REQUIRED_ARG(ctx->L, base, args, &node->loc);
    const Type *type = resolveAstNode(base)->type;
    if (type == NULL)
        return NULL;
    if (!typeIs(type, Class)) {
        logError(ctx->L,
                 &base->loc,
                 "argument must be a class, got '{t}'",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    AstNodeList methods = {};
    for (int i = 0; i < type->tClass.members->count; i++) {
        const NamedTypeMember *member = &type->tClass.members->members[i];
        if (!nodeIs(member->decl, FuncDecl) || hasFlag(member->decl, Static))
            continue;
        const AstNode *api = findAttribute(member->decl, jsonrpc_api);
        if (api == NULL)
            continue;
        insertAstNode(
            &methods,
            clientRpcMethodWrapper(ctx, &node->loc, env, member->decl));
    }
    return methods.first
               ?: makeAstNode(
                      ctx->pool, &node->loc, &(AstNode){.tag = astNoop});
}

static AstNode *serverRpcMethodWrapper(CxyPluginContext *ctx,
                                       const FileLoc *loc,
                                       CxyEnvironmentVar *env,
                                       const AstNode *node)
{
    AstNode *_Value = env[0].value, *api = env[1].value,
            *paramFunc = env[2].value, *exceptionType = env[3].value,
            *voidType = env[4].value;
    AstNode *closure = paramFunc->parentScope;
    const Type *type = node->type;
    AstNode *params = makeFunctionParam(
        ctx->pool,
        loc,
        makeString(ctx->strings, "params"),
        makeReferenceTypeAstNode(
            ctx->pool,
            loc,
            flgConst,
            makeResolvedPath(
                ctx->pool,
                loc,
                makeString(ctx->strings, "Value"),
                flgNone,
                _Value,
                NULL,
                _Value->type),
            NULL,
            NULL),
        NULL,
        flgNone,
        NULL);
    AstNode *resultType =
        makeReturnType(ctx, loc, type->func.retType, voidType, exceptionType);
    AstNode *func = makeFunctionDecl(ctx->pool,
                                     builtinLoc(),
                                     node->_name,
                                     params,
                                     resultType,
                                     NULL,
                                     flgGenerated,
                                     NULL,
                                     NULL);
    func->funcDecl.this_ = createThisParameter(ctx, loc, closure, params);

    AstNodeList stmts = {}, args = {};
    AstNode *param = node->funcDecl.signature->params;
    if (param && param->_name == S_this)
        param = param->next;
    for (u64 i = 0; param; param = param->next, i++) {
        // var <param.name> = paramFunc(params, "param.name", i)
        AstNode *var = makeVarDecl(
            ctx->pool,
            loc,
            flgNone,
            param->_name,
            NULL,
            makeCallExpr(
                ctx->pool,
                loc,
                makeResolvedPathWithArgs(
                    ctx->pool,
                    loc,
                    getDeclarationName(paramFunc),
                    flgNone,
                    paramFunc,
                    makeTypeReferenceNode(ctx->pool, param->type, loc),
                    NULL),
                makeResolvedPath(
                    ctx->pool,
                    loc,
                    params->_name,
                    flgNone,
                    params,
                    makeStringLiteral(ctx->pool, loc, param->_name,
                        makeIntegerLiteral(
                            ctx->pool, loc, i, NULL, NULL), NULL),
                    NULL),
                flgNone,
                NULL,
                NULL),
            NULL,
            NULL);
        insertAstNode(&stmts, var);
        insertAstNode(
            &args,
            makeResolvedPath(
                ctx->pool, loc, param->_name, flgNone, var, NULL, NULL));
    }
    // handler.func(<args>)
    AstNode *expr = makeCallExpr(
        ctx->pool,
        loc,
        makePathWithElements(
            ctx->pool,
            loc,
            flgNone,
            makeResolvedPathElement(
                ctx->pool,
                loc,
                api->_name,
                flgMember,
                api,
                makePathElement(
                    ctx->pool, loc, node->_name, flgNone, NULL, NULL),
                api->type),
            NULL),
        args.first,
        flgNone,
        NULL,
        NULL);

    AstNode *stmt = NULL;
    if (isVoidType(type->func.retType)) {
        stmt = makeExprStmt(ctx->pool, loc, flgNone, expr, NULL, NULL);
    }
    else {
        stmt = makeReturnAstNode(ctx->pool, loc, flgNone, expr, NULL, NULL);
        stmt->returnStmt.func = func;
    }
    insertAstNode(&stmts, stmt);
    func->funcDecl.body =
        makeBlockStmt(ctx->pool, loc, stmts.first, NULL, NULL);
    return func;
}

static AstNode *addServerMethods(CxyPluginContext *ctx,
                                 const AstNode *node,
                                 AstNode *args)
{
    CXY_REQUIRED_ARG(ctx->L, envArg, args, &node->loc);
    cxyPluginLoadEnvironment(ctx,
                             &node->loc,
                             envArg,
                             {"Value"},
                             {"api"},
                             {"param"},
                             {"Exception"},
                             {"Void"});
    CXY_REQUIRED_ARG(ctx->L, base, args, &node->loc);
    const Type *type = resolveAstNode(base)->type;
    if (type == NULL)
        return NULL;
    if (!typeIs(type, Class)) {
        logError(ctx->L,
                 &base->loc,
                 "argument must be a class, got '{t}'",
                 (FormatArg[]){{.t = type}});
        return NULL;
    }

    AstNodeList methods = {};
    for (int i = 0; i < type->tClass.members->count; i++) {
        const NamedTypeMember *member = &type->tClass.members->members[i];
        if (!nodeIs(member->decl, FuncDecl) || hasFlag(member->decl, Static))
            continue;
        const AstNode *api = findAttribute(member->decl, jsonrpc_api);
        if (api == NULL)
            continue;
        insertAstNode(
            &methods,
            serverRpcMethodWrapper(ctx, &node->loc, env, member->decl));
    }
    return methods.first
               ?: makeAstNode(
                      ctx->pool, &node->loc, &(AstNode){.tag = astNoop});
}

bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    internCommonStrings(ctx->strings);
    printStatus(ctx->L, "Initializing jsonrpc plugin", NULL);
    jsonrpc_submit = makeString(ctx->strings, "submit");
    jsonrpc_api = makeString(ctx->strings, "api");
    cxyPluginRegisterAction(
        ctx,
        loc,
        (CxyPluginAction[]){
            {.name = "addClientMethods", .fn = addClientMethods},
            {.name = "addServerMethods", .fn = addServerMethods},
        },
        2);
    return true;
}
