//
// Created by Carter Mbotho on 2025-05-10.
//

#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Log Log;
typedef struct MemPool MemPool;
typedef struct StrPool StrPool;
typedef struct FileLoc FileLoc;
typedef struct AstNode AstNode;
typedef struct TypeTable TypeTable;

typedef struct CxyPluginContext {
    Log *L;
    MemPool *pool;
    StrPool *strings;
} CxyPluginContext;

typedef struct CxyEnvironmentVar {
    const char *name;
    AstNode *value;
} CxyEnvironmentVar;

bool compareAstNodes(const void *a, const void *b);
bool compareCxyPluginActions(const void *a, const void *b);

typedef AstNode *(*CxyCxyPluginActionFn)(CxyPluginContext *ctx,
                                         const AstNode *node,
                                         AstNode *args);
typedef struct CxyPluginAction {
    const char *name;
    CxyCxyPluginActionFn fn;
} CxyPluginAction;

typedef bool (*CxyPluginInitFn)(CxyPluginContext *ctx, const FileLoc *loc);
typedef void (*CxyPluginDeInitFn)(CxyPluginContext *ctx);

bool cxyPluginRegisterAction(CxyPluginContext *ctx,
                             const FileLoc *loc,
                             CxyPluginAction *actions,
                             size_t nActions);

TypeTable *cxyPluginGetTypeTable(CxyPluginContext *ctx);

bool cxyPluginLoadEnvironment_(CxyPluginContext *ctx,
                               const FileLoc *loc,
                               AstNode *envArg,
                               CxyEnvironmentVar *env,
                               size_t nNames);

#define cxyPluginLoadEnvironment(ctx, loc, envArg, ...)                        \
    CxyEnvironmentVar env[] = {__VA_ARGS__};                                   \
    if (!cxyPluginLoadEnvironment_(                                            \
            ctx, loc, envArg, env, sizeof(env) / sizeof(CxyEnvironmentVar)))   \
    return NULL

AstNode *cxyPluginArgsPop(AstNode **args);
#define CXY_REQUIRED_ARG(L, name, args, loc)                                   \
    AstNode *name = cxyPluginArgsPop(&args);                                   \
    if (name == NULL) {                                                        \
        logError(L,                                                            \
                 loc,                                                          \
                 "{s}: argument '" #name "' is required",                      \
                 (FormatArg[]){{.s = __FUNCTION__}});                          \
        return NULL;                                                           \
    }

#define CXY_OPTIONAL_ARG(L, name, args) AstNode *name = pluginPopArgs(&args);

#endif // PLUGIN_H
