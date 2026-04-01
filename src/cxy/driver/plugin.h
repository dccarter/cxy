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
    bool initialized;
    CxyPluginInjectionPoint ip;
    CompilerDriver *driver;
    void *state;
    CxyPluginContext ctx;
} Plugin;

Plugin *loadPlugin(CompilerDriver *driver,
                   const FileLoc *loc,
                   cstring name,
                   const char *path);
Plugin *findPluginByPath(CompilerDriver *driver, const FileLoc *loc, cstring path);
void pluginInit(CompilerDriver *driver);
AstNode *invokeCxyPluginAction(Plugin *plugin,
                               cstring action,
                               const AstNode *node,
                               AstNode *args);
void pluginDeinit(CompilerDriver *driver);
void PluginInitialize(Plugin *plugin);
Plugin *findPluginByName(CompilerDriver *driver, cstring name);
bool compareCxyPluginActions(const void *a, const void *b);
bool compareAstNodes(const void *a, const void *b);
