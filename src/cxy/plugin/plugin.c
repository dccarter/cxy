//
// Created by Carter Mbotho on 2025-05-13.
//

#include "plugin.h"
#include "driver/plugin.h"

#include "core/alloc.h"

static Plugin *pluginOf(CxyPluginContext *ctx)
{
    return (Plugin *)((char *)ctx - offsetof(Plugin, ctx));
}

static AstNode *findEnvironmentVar(AstNode *env, const char *name)
{
    for (; env; env = env->next) {
        AstNode *node = resolveAstNode(env);
        if (nodeIs(node, ImportDecl)) {
            if (node->type->name == name)
                return node;
        }
        else if (getDeclarationName(node) == name)
            return node;
    }
    return NULL;
}

bool cxyPluginRegisterAction(CxyPluginContext *ctx,
                             const FileLoc *loc,
                             CxyPluginAction *actions,
                             size_t nActions)
{
    Plugin *plugin = pluginOf(ctx);
    csAssert0(plugin != NULL);
    for (int i = 0; i < nActions; ++i) {
        CxyPluginAction *action = &actions[i];
        action->name = makeString(ctx->strings, action->name);
        bool status = insertInHashTable(&plugin->actions,
                                        action,
                                        hashStr(hashInit(), action->name),
                                        sizeof(CxyPluginAction),
                                        compareCxyPluginActions);
        if (!status) {
            logError(ctx->L,
                     loc,
                     "action '{s}' already registered",
                     (FormatArg[]){{.s = action->name}});
            return false;
        }
    }
    return true;
}

TypeTable *cxyPluginGetTypeTable(CxyPluginContext *ctx)
{
    return ctx != NULL ? pluginOf(ctx)->types : NULL;
}

bool cxyPluginLoadEnvironment_(CxyPluginContext *ctx,
                               const FileLoc *loc,
                               AstNode *envArg,
                               CxyEnvironmentVar *env,
                               size_t nEntries)
{
    if (nEntries == 1) {
        AstNode *node = resolveAstNode(envArg);
        env->name = makeString(ctx->strings, env->name);
        if (getDeclarationName(node) != env->name) {
            logError(ctx->L,
                     loc,
                     "missing environment with name '{s}'",
                     (FormatArg[]){{.s = env->name}});
            return false;
        }
        env->value = node;
        return true;
    }

    if (!nodeIs(envArg, TupleExpr)) {
        logError(ctx->L, loc, "environment must be passed in a tuple", NULL);
        return NULL;
    }
    AstNode *elem = envArg->tupleExpr.elements;
    for (int i = 0; i < nEntries; ++i) {
        CxyEnvironmentVar *entry = &env[i];
        entry->name = makeString(ctx->strings, entry->name);
        entry->value = findEnvironmentVar(elem, entry->name);
        if (entry->value == NULL) {
            logError(ctx->L,
                     loc,
                     "missing environment with name '{s}'",
                     (FormatArg[]){{.s = entry->name}});
            return false;
        }
    }
    return true;
}
