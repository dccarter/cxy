//
// Created by Carter Mbotho on 2025-05-13.
//

#pragma once

#include "driver.h"
#include "plugin/plugin.h"

typedef struct Plugin {
    cstring name;
    cstring path;
    void *handle;
    HashTable actions;
    TypeTable *types;
    CxyPluginContext ctx;
} Plugin;

Plugin *loadPlugin(CompilerDriver *driver,
                   const FileLoc *loc,
                   cstring name,
                   const char *path);
void pluginInit(CompilerDriver *driver);
AstNode *invokeCxyPluginAction(Plugin *plugin,
                               cstring action,
                               const AstNode *node,
                               AstNode *args);
void pluginDeinit(CompilerDriver *driver);
bool compareCxyPluginActions(const void *a, const void *b);
bool compareAstNodes(const void *a, const void *b);
