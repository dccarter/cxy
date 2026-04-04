/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/commands/commands.h"
#include "package/registry.h"
#include "core/log.h"
#include "core/strpool.h"
#include "package/types.h"
#include "package/validators.h"

#include <stdio.h>
#include <string.h>

bool packageYankCommand(const Options *options, StrPool *strings, Log *log)
{
    const char *name         = options->package.yankName;
    const char *version      = options->package.yankVersion;
    bool        undo         = options->package.yankUndo;
    const char *registryFile = options->package.yankRegistryFile;

    /* ------------------------------------------------------------------ */
    /* 1. Validate inputs                                                  */
    /* ------------------------------------------------------------------ */

    if (!name || name[0] == '\0') {
        logError(log, NULL, "package name is required", NULL);
        return false;
    }

    if (!version || version[0] == '\0') {
        logError(log, NULL, "version is required", NULL);
        return false;
    }

    cstring nameErr = validatePackageName(name);
    if (nameErr) {
        logError(log, NULL, "invalid package name '{s}': {s}",
                 (FormatArg[]){{.s = name}, {.s = nameErr}});
        return false;
    }

    cstring versionErr = validateSemanticVersion(version);
    if (versionErr) {
        logError(log, NULL, "invalid version '{s}': {s}",
                 (FormatArg[]){{.s = version}, {.s = versionErr}});
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 2. Initialise registry client                                       */
    /* ------------------------------------------------------------------ */

    RegistryClient *client = registryClientInit(strings, log, registryFile);
    if (!client) {
        logError(log, NULL,
                 "could not initialise registry client. "
                 "Check network or set CXY_REGISTRY_URL.",
                 NULL);
        return false;
    }

    if (!registryClientHasApiKey(client)) {
        logError(log, NULL,
                 "no API key found. Run 'cxy package login' first.",
                 NULL);
        registryClientFree(client);
        return false;
    }

    /* ------------------------------------------------------------------ */
    /* 3. Yank / un-yank                                                   */
    /* ------------------------------------------------------------------ */

    if (undo)
        printStatusSticky(log, "Un-yanking %s@%s...", name, version);
    else
        printStatusSticky(log, "Yanking %s@%s...", name, version);

    bool ok = registryYank(client, name, version, undo);
    registryClientFree(client);

    if (!ok)
        return false;

    if (undo)
        printStatusAlways(log,
                          cBGRN "✔" cDEF " Un-yanked " cBCYN "%s" cDEF "@" cBCYN "%s" cDEF
                          " — version is visible to the resolver again\n",
                          name, version);
    else
        printStatusAlways(log,
                          cBGRN "✔" cDEF " Yanked " cBCYN "%s" cDEF "@" cBCYN "%s" cDEF
                          " — version is hidden from fresh installs\n",
                          name, version);

    return true;
}