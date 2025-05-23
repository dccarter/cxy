//
// Created by Carter Mbotho on 2025-05-13.
//

#include "plugin.h"

#include <dlfcn.h>
#include <errno.h>

#include <limits.h>

static cstring CXY_pluginsDir = NULL;

static bool comparePlugins(const void *a, const void *b)
{
    return ((const Plugin *)a)->path == ((const Plugin *)b)->path;
}

static cstring getPluginPath(CompilerDriver *driver,
                             const FileLoc *loc,
                             cstring path)
{
    if (path[0] == '/') {
        logError(driver->L,
                 loc,
                 "absolute plugin paths are not supported, paths must be "
                 "relative plugin directories",
                 NULL);
        return NULL;
    }

    char stage[PATH_MAX];
    char fullPath[PATH_MAX];
    cstring pluginsDirs[] = {driver->options.pluginsDir, CXY_pluginsDir};
    FormatState buf = newFormatState("  ", false);
    for (size_t i = 0; i < sizeof(pluginsDirs) / sizeof(cstring); i++) {
        cstring dir = pluginsDirs[i];
        if (dir == NULL)
            continue;
        size_t len = strlen(dir);
        memcpy(stage, dir, len);
        if (stage[len - 1] != '/')
            stage[len++] = '/';
        size_t pathLen = strlen(path);
        memcpy(&stage[len], path, pathLen);
        stage[len + pathLen] = '\0';
        if (realpath(stage, fullPath) == NULL) {
            format(&buf,
                   "{s}{s}",
                   (FormatArg[]){{.s = i == 0 ? "" : ", "}, {.s = stage}});
            continue;
        }
        freeFormatState(&buf);
        return makeString(driver->strings, fullPath);
    }
    char *dirs = formatStateToString(&buf);
    logError(driver->L,
             loc,
             "plugin '{s}' not found at these lookup paths: ({s})",
             (FormatArg[]){{.s = path}, {.s = dirs}});
    free(dirs);
    freeFormatState(&buf);
    return NULL;
}

static cstring findCxyPluginDir(CompilerDriver *driver)
{
    char *cxyPluginDir = getenv("CXY_PLUGINS_DIR");
    if (cxyPluginDir != NULL)
        return makeString(driver->strings, cxyPluginDir);
    char *cxyRoot = getenv("CXY_ROOT");
    char tmp[PATH_MAX], tmp2[PATH_MAX];
    if (cxyRoot != NULL) {
        size_t len = strlen(cxyRoot);
        memcpy(tmp, cxyRoot, len);
        if (cxyRoot[len - 1] != '/')
            tmp[len++] = '/';
        memcpy(&tmp[len], "lib/cxy/plugins", 17);
        tmp[len + 17] = '\0';
        if (realpath(tmp, tmp2) == NULL) {
            logWarning(
                driver->L, NULL, "$CXY_ROOT/lib/cxy/plugins not found", NULL);
            return NULL;
        }
    }
    else {
        size_t len = strlen(driver->cxyBinaryPath);
        memcpy(tmp, driver->cxyBinaryPath, len);
        tmp[len] = '\0';
        char *binary = strrchr(tmp, '/');
        if (binary == NULL)
            return NULL;
        memcpy(binary, "/lib/cxy/plugins", 20);
        if (realpath(tmp, tmp2) == NULL) {
            logWarning(driver->L,
                       NULL,
                       "CXY plugins dir '{s}' not found",
                       (FormatArg[]){{.s = tmp}});
            return NULL;
        }
    }
    return makeString(driver->strings, tmp2);
}

Plugin *loadPlugin(CompilerDriver *driver,
                   const FileLoc *loc,
                   cstring name,
                   const char *path)
{
    printStatus(driver->L, "loading plugin '%s' as %s", path, name);
    path = getPluginPath(driver, loc, path);
    if (path == NULL)
        return NULL;

    Plugin *plugin = findInHashTable(&driver->plugins,
                                     &(Plugin){.path = path},
                                     hashStr(hashInit(), path),
                                     sizeof(Plugin),
                                     comparePlugins);
    if (plugin != NULL) {
        printStatus(driver->L, "plugin '%s' already loaded", path);
        return plugin;
    }

    void *handle = dlopen(path, RTLD_LAZY);
    if (handle == NULL) {
        logError(driver->L,
                 loc,
                 "loading plugin '{s}' failed: {s}",
                 (FormatArg[]){{.s = path}, {.s = dlerror()}});
        return NULL;
    }
    CxyPluginInitFn initFn = dlsym(handle, "pluginInit");
    if (initFn == NULL) {
        logError(driver->L,
                 loc,
                 "dlsym '{s}:pluginInit' failed: {s}",
                 (FormatArg[]){{.s = name}, {.s = dlerror()}});
        dlclose(handle);
        return NULL;
    }
    printStatus(driver->L, "initializing plugin '{s}'", name);

    plugin = allocFromMemPool(driver->pool, sizeof(Plugin));
    plugin->name = name;
    plugin->path = path;
    plugin->handle = handle;
    plugin->actions = newHashTable(sizeof(CxyPluginAction));
    plugin->types = driver->types;
    plugin->ctx.L = driver->L;
    plugin->ctx.pool = driver->pool;
    plugin->ctx.strings = driver->strings;

    insertInHashTable(&driver->plugins,
                      plugin,
                      hashStr(hashInit(), path),
                      sizeof(Plugin),
                      comparePlugins);

    if (!initFn(&plugin->ctx, loc)) {
        logError(driver->L,
                 loc,
                 "'{s}:pluginInit' failed",
                 (FormatArg[]){{.s = name}});
        // handle will be closed during driver deinit;
        return NULL;
    }
    return plugin;
}

AstNode *invokeCxyPluginAction(Plugin *plugin,
                               cstring action,
                               const AstNode *node,
                               AstNode *args)
{
    CxyPluginAction *actionNode =
        findInHashTable(&plugin->actions,
                        &(CxyPluginAction){.name = action},
                        hashStr(hashInit(), action),
                        sizeof(CxyPluginAction),
                        compareCxyPluginActions);
    if (actionNode == NULL) {
        logError(plugin->ctx.L,
                 &node->loc,
                 "plugin '{s}' does not have an action named '{s}'",
                 (FormatArg[]){{.s = plugin->name}, {.s = action}});
        return NULL;
    }

    AstNode *result = actionNode->fn(&plugin->ctx, node, args);
    return result;
}

void pluginInit(CompilerDriver *driver)
{
    CXY_pluginsDir = findCxyPluginDir(driver);
    driver->plugins = newHashTable(sizeof(Plugin));
}

void pluginDeinit(CompilerDriver *driver)
{
    HashtableIt it = newHashTableIt(&driver->plugins, sizeof(Plugin));
    while (hashTableItHasNext(&it)) {
        Plugin *plugin = hashTableItNext(&it);
        if (plugin->handle == NULL)
            dlclose(plugin->handle);
        freeHashTable(&plugin->actions);
    }
}
