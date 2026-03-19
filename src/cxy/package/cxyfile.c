/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-15
 */

#include "package/cxyfile.h"
#include "package/validators.h"
#include "package/gitops.h"
#include "package/resolver.h"

#include "core/log.h"
#include "core/strpool.h"
#include "core/mempool.h"
#include "core/utils.h"

#include <yaml.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct YamlParser {
    yaml_parser_t parser;
    yaml_event_t event;
    StrPool *strings;
    Log *log;
    const char *currentFile;
} YamlParser;

typedef struct YamlEmitter {
    yaml_emitter_t emitter;
    yaml_event_t event;
    FILE *file;
    Log *log;
} YamlEmitter;

static bool initYamlParser(YamlParser *p, FILE *file, StrPool *strings, Log *log, const char *filename)
{
    memset(p, 0, sizeof(YamlParser));
    p->strings = strings;
    p->log = log;
    p->currentFile = filename;

    if (!yaml_parser_initialize(&p->parser)) {
        logError(log, NULL, "failed to initialize YAML parser", NULL);
        return false;
    }

    yaml_parser_set_input_file(&p->parser, file);
    return true;
}

static void cleanupYamlParser(YamlParser *p)
{
    yaml_event_delete(&p->event);
    yaml_parser_delete(&p->parser);
}

static bool parseYamlEvent(YamlParser *p)
{
    yaml_event_delete(&p->event);
    if (!yaml_parser_parse(&p->parser, &p->event)) {
        logError(p->log, NULL, "YAML parse error at line {u32}: {s}",
                (FormatArg[]){{.u32 = p->parser.problem_mark.line + 1},
                             {.s = p->parser.problem}});
        return false;
    }
    return true;
}

static cstring parseScalarValue(YamlParser *p)
{
    if (p->event.type != YAML_SCALAR_EVENT) {
        logError(p->log, NULL, "expected scalar value", NULL);
        return NULL;
    }

    const char *value = (const char *)p->event.data.scalar.value;
    return makeString(p->strings, value);
}

static bool expectScalar(YamlParser *p, cstring *out)
{
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_SCALAR_EVENT) {
        logError(p->log, NULL, "expected scalar value", NULL);
        return false;
    }

    *out = parseScalarValue(p);
    return true;
}

static bool skipValue(YamlParser *p);

static bool skipMapping(YamlParser *p)
{
    int depth = 1;
    while (depth > 0) {
        if (!parseYamlEvent(p))
            return false;

        switch (p->event.type) {
            case YAML_MAPPING_START_EVENT:
                depth++;
                break;
            case YAML_MAPPING_END_EVENT:
                depth--;
                break;
            case YAML_SEQUENCE_START_EVENT:
                if (!skipValue(p))
                    return false;
                break;
            default:
                break;
        }
    }
    return true;
}

static bool skipSequence(YamlParser *p)
{
    int depth = 1;
    while (depth > 0) {
        if (!parseYamlEvent(p))
            return false;

        switch (p->event.type) {
            case YAML_SEQUENCE_START_EVENT:
                depth++;
                break;
            case YAML_SEQUENCE_END_EVENT:
                depth--;
                break;
            case YAML_MAPPING_START_EVENT:
                if (!skipValue(p))
                    return false;
                break;
            default:
                break;
        }
    }
    return true;
}

static bool skipValue(YamlParser *p)
{
    if (p->event.type == YAML_MAPPING_START_EVENT) {
        return skipMapping(p);
    }
    else if (p->event.type == YAML_SEQUENCE_START_EVENT) {
        return skipSequence(p);
    }
    // Scalar values are already consumed
    return true;
}

static bool parseStringArray(YamlParser *p, DynArray *array)
{
    // Expect sequence start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_SEQUENCE_START_EVENT) {
        logError(p->log, NULL, "expected sequence", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_SEQUENCE_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected string in array", NULL);
            return false;
        }

        cstring value = parseScalarValue(p);
        pushOnDynArray(array, &value);
    }

    return true;
}

static bool parseDependency(YamlParser *p, PackageDependency *dep)
{
    memset(dep, 0, sizeof(PackageDependency));

    // Expect mapping start
    if (p->event.type != YAML_MAPPING_START_EVENT) {
        logError(p->log, NULL, "expected dependency mapping", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected dependency field name", NULL);
            return false;
        }

        const char *key = (const char *)p->event.data.scalar.value;

        if (strcmp(key, "name") == 0) {
            if (!expectScalar(p, &dep->name))
                return false;

            // Validate dependency package name
            cstring error = validatePackageName(dep->name);
            if (error) {
                logError(p->log, NULL, "invalid dependency name '{s}': {s}",
                        (FormatArg[]){{.s = dep->name}, {.s = error}});
                return false;
            }
        }
        else if (strcmp(key, "repository") == 0) {
            if (!expectScalar(p, &dep->repository))
                return false;

            // Validate dependency repository
            cstring error = validateGitRepository(dep->repository);
            if (error) {
                logError(p->log, NULL, "invalid dependency repository '{s}': {s}",
                        (FormatArg[]){{.s = dep->repository}, {.s = error}});
                return false;
            }
        }
        else if (strcmp(key, "version") == 0) {
            if (!expectScalar(p, &dep->version))
                return false;

            // Skip validation for wildcard "*"
            if (dep->version && strcmp(dep->version, "*") == 0) {
                // Valid wildcard, skip validation
            }
            else {
                // Validate dependency version constraint
                // Note: version constraints can have prefixes like ^, ~, >=, etc.
                // For now, we'll do basic validation - skip constraint prefix and validate base version
                cstring versionToValidate = dep->version;
                if (versionToValidate && (versionToValidate[0] == '^' || versionToValidate[0] == '~')) {
                    versionToValidate = versionToValidate + 1;
                }
                else if (versionToValidate && strlen(versionToValidate) >= 2) {
                    if ((versionToValidate[0] == '>' || versionToValidate[0] == '<') &&
                        versionToValidate[1] == '=') {
                        versionToValidate = versionToValidate + 2;
                    }
                    else if (versionToValidate[0] == '>' || versionToValidate[0] == '<') {
                        versionToValidate = versionToValidate + 1;
                    }
                }

                // Skip whitespace after constraint operator
                while (*versionToValidate == ' ') versionToValidate++;

                cstring error = validateSemanticVersion(versionToValidate);
                if (error) {
                    logError(p->log, NULL, "invalid dependency version '{s}': {s}",
                            (FormatArg[]){{.s = dep->version}, {.s = error}});
                    return false;
                }
            }
        }
        else if (strcmp(key, "tag") == 0) {
            if (!expectScalar(p, &dep->tag))
                return false;
        }
        else if (strcmp(key, "branch") == 0) {
            if (!expectScalar(p, &dep->branch))
                return false;
        }
        else if (strcmp(key, "path") == 0) {
            if (!expectScalar(p, &dep->path))
                return false;
        }
        else {
            // Unknown field, skip it
            if (!parseYamlEvent(p))
                return false;
            if (!skipValue(p))
                return false;
        }
    }

    return true;
}

static bool parseDependencies(YamlParser *p, DynArray *deps)
{
    // Expect sequence start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_SEQUENCE_START_EVENT) {
        logError(p->log, NULL, "expected dependencies sequence", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_SEQUENCE_END_EVENT)
            break;

        PackageDependency dep;
        if (!parseDependency(p, &dep))
            return false;

        pushOnDynArray(deps, &dep);
    }

    return true;
}

static bool parseTest(YamlParser *p, PackageTest *test)
{
    memset(test, 0, sizeof(PackageTest));
    test->args = newDynArray(sizeof(cstring));

    if (p->event.type == YAML_SCALAR_EVENT) {
        // Simple test file path
        test->file = parseScalarValue(p);
        test->isPattern = strstr(test->file, "*") != NULL;
        return true;
    }
    else if (p->event.type == YAML_MAPPING_START_EVENT) {
        // Test with configuration
        while (true) {
            if (!parseYamlEvent(p))
                return false;

            if (p->event.type == YAML_MAPPING_END_EVENT)
                break;

            if (p->event.type != YAML_SCALAR_EVENT) {
                logError(p->log, NULL, "expected test field name", NULL);
                return false;
            }

            const char *key = (const char *)p->event.data.scalar.value;

            if (strcmp(key, "file") == 0) {
                if (!expectScalar(p, &test->file))
                    return false;
                test->isPattern = strstr(test->file, "*") != NULL;
            }
            else if (strcmp(key, "args") == 0) {
                if (!parseStringArray(p, &test->args))
                    return false;
            }
            else {
                // Unknown field, skip it
                if (!parseYamlEvent(p))
                    return false;
                if (!skipValue(p))
                    return false;
            }
        }
        return true;
    }
    else {
        logError(p->log, NULL, "expected test string or mapping", NULL);
        return false;
    }
}

static bool parseTests(YamlParser *p, DynArray *tests)
{
    // Expect sequence start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_SEQUENCE_START_EVENT) {
        logError(p->log, NULL, "expected tests sequence", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_SEQUENCE_END_EVENT)
            break;

        PackageTest test;
        if (!parseTest(p, &test))
            return false;

        pushOnDynArray(tests, &test);
    }

    return true;
}

static bool parseScripts(YamlParser *p, DynArray *scripts, DynArray *scriptEnv)
{
    // Expect mapping start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_MAPPING_START_EVENT) {
        logError(p->log, NULL, "expected scripts mapping", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected script name", NULL);
            return false;
        }

        cstring scriptName = parseScalarValue(p);

        // Check if this is the special "env" section
        if (strcmp(scriptName, "env") == 0) {
            // Parse environment variables
            if (!parseYamlEvent(p))
                return false;

            if (p->event.type != YAML_MAPPING_START_EVENT) {
                logError(p->log, NULL, "expected env mapping", NULL);
                return false;
            }

            while (true) {
                if (!parseYamlEvent(p))
                    return false;

                if (p->event.type == YAML_MAPPING_END_EVENT)
                    break;

                if (p->event.type != YAML_SCALAR_EVENT) {
                    logError(p->log, NULL, "expected environment variable name", NULL);
                    return false;
                }

                EnvVar env;
                env.name = parseScalarValue(p);

                if (!expectScalar(p, &env.value))
                    return false;

                pushOnDynArray(scriptEnv, &env);
            }

            // Don't add env to scripts array, continue to next entry
            continue;
        }

        // Regular script entry
        PackageScript script;
        memset(&script, 0, sizeof(PackageScript));
        script.name = scriptName;
        script.dependencies = newDynArray(sizeof(cstring));
        script.inputs = newDynArray(sizeof(cstring));
        script.outputs = newDynArray(sizeof(cstring));

        // Parse script value (either string command or object with deps)
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_SCALAR_EVENT) {
            // Simple string command
            script.command = parseScalarValue(p);
        }
        else if (p->event.type == YAML_MAPPING_START_EVENT) {
            // Object with command and optional dependencies
            while (true) {
                if (!parseYamlEvent(p))
                    return false;

                if (p->event.type == YAML_MAPPING_END_EVENT)
                    break;

                if (p->event.type != YAML_SCALAR_EVENT) {
                    logError(p->log, NULL, "expected script field name", NULL);
                    return false;
                }

                const char *key = (const char *)p->event.data.scalar.value;

                if (strcmp(key, "command") == 0) {
                    if (!expectScalar(p, &script.command))
                        return false;
                }
                else if (strcmp(key, "depends") == 0 || strcmp(key, "dependencies") == 0) {
                    if (!parseStringArray(p, &script.dependencies))
                        return false;
                }
                else if (strcmp(key, "inputs") == 0) {
                    if (!parseStringArray(p, &script.inputs))
                        return false;
                }
                else if (strcmp(key, "outputs") == 0) {
                    if (!parseStringArray(p, &script.outputs))
                        return false;
                }
                else {
                    // Unknown field, skip it
                    if (!parseYamlEvent(p))
                        return false;
                    if (!skipValue(p))
                        return false;
                }
            }
        }
        else {
            logError(p->log, NULL, "expected script command string or object", NULL);
            return false;
        }

        pushOnDynArray(scripts, &script);
    }

    return true;
}

/**
 * Parse a single named build entry from builds: section
 */
static bool parseNamedBuild(YamlParser *p, PackageBuild *build, bool isTemplate)
{
    memset(build, 0, sizeof(PackageBuild));
    build->config.cLibs = newDynArray(sizeof(cstring));
    build->config.cLibDirs = newDynArray(sizeof(cstring));
    build->config.cHeaderDirs = newDynArray(sizeof(cstring));
    build->config.cDefines = newDynArray(sizeof(cstring));
    build->config.cFlags = newDynArray(sizeof(cstring));
    build->config.defines = newDynArray(sizeof(cstring));
    build->config.flags = newDynArray(sizeof(cstring));
    build->isDefault = false;

    // Expect mapping start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_MAPPING_START_EVENT) {
        logError(p->log, NULL, "expected build mapping", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected build field name", NULL);
            return false;
        }

        const char *key = (const char *)p->event.data.scalar.value;

        if (strcmp(key, "default") == 0) {
            cstring defaultValue = NULL;
            if (!expectScalar(p, &defaultValue))
                return false;
            build->isDefault = (defaultValue && strcmp(defaultValue, "true") == 0);
        }
        else if (strcmp(key, "entry") == 0) {
            if (isTemplate) {
                logError(p->log, NULL, "template builds (starting with '_') cannot have 'entry' field", NULL);
                return false;
            }
            if (!expectScalar(p, &build->config.entry))
                return false;
        }
        else if (strcmp(key, "output") == 0) {
            if (isTemplate) {
                logError(p->log, NULL, "template builds (starting with '_') cannot have 'output' field", NULL);
                return false;
            }
            if (!expectScalar(p, &build->config.output))
                return false;
        }
        else if (strcmp(key, "c-libs") == 0) {
            if (!parseStringArray(p, &build->config.cLibs))
                return false;
        }
        else if (strcmp(key, "c-lib-dirs") == 0) {
            if (!parseStringArray(p, &build->config.cLibDirs))
                return false;
        }
        else if (strcmp(key, "c-header-dirs") == 0) {
            if (!parseStringArray(p, &build->config.cHeaderDirs))
                return false;
        }
        else if (strcmp(key, "c-defines") == 0) {
            if (!parseStringArray(p, &build->config.cDefines))
                return false;
        }
        else if (strcmp(key, "c-flags") == 0) {
            if (!parseStringArray(p, &build->config.cFlags))
                return false;
        }
        else if (strcmp(key, "defines") == 0) {
            if (!parseStringArray(p, &build->config.defines))
                return false;
        }
        else if (strcmp(key, "flags") == 0) {
            if (!parseStringArray(p, &build->config.flags))
                return false;
        }
        else if (strcmp(key, "plugins-dir") == 0) {
            if (!expectScalar(p, &build->config.pluginsDir))
                return false;
        }
        else if (strcmp(key, "stdlib") == 0) {
            if (!expectScalar(p, &build->config.stdlib))
                return false;
        }
        else {
            // Unknown field, skip it
            if (!parseYamlEvent(p))
                return false;
            if (!skipValue(p))
                return false;
        }
    }

    return true;
}

/**
 * Parse builds: section (multiple named builds)
 */
static bool parseBuilds(YamlParser *p, DynArray *builds)
{
    // Expect mapping start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_MAPPING_START_EVENT) {
        logError(p->log, NULL, "expected builds mapping", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected build name", NULL);
            return false;
        }

        // Build name (e.g., "lib", "bin", "_common")
        const char *buildName = (const char *)p->event.data.scalar.value;

        // Store build name immediately (before parsing nested mapping)
        size_t nameLen = strlen(buildName);
        cstring name = makeStringSized(p->strings, buildName, nameLen);

        // Check if this is a template build (starts with '_')
        bool isTemplate = (buildName[0] == '_');

        PackageBuild build;
        if (!parseNamedBuild(p, &build, isTemplate))
            return false;

        // Assign the stored name
        build.name = name;

        // Skip template builds (those starting with '_')
        // These are only used for YAML anchors/merging, not real build targets
        if (isTemplate) {
            continue;
        }

        pushOnDynArray(builds, &build);
    }

    return true;
}

static bool parseBuildConfig(YamlParser *p, PackageBuildConfig *build)
{
    // Expect mapping start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_MAPPING_START_EVENT) {
        logError(p->log, NULL, "expected build mapping", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected build field name", NULL);
            return false;
        }

        const char *key = (const char *)p->event.data.scalar.value;

        if (strcmp(key, "entry") == 0) {
            if (!expectScalar(p, &build->entry))
                return false;
        }
        else if (strcmp(key, "output") == 0) {
            if (!expectScalar(p, &build->output))
                return false;
        }
        else if (strcmp(key, "c-libs") == 0) {
            if (!parseStringArray(p, &build->cLibs))
                return false;
        }
        else if (strcmp(key, "c-lib-dirs") == 0) {
            if (!parseStringArray(p, &build->cLibDirs))
                return false;
        }
        else if (strcmp(key, "c-header-dirs") == 0) {
            if (!parseStringArray(p, &build->cHeaderDirs))
                return false;
        }
        else if (strcmp(key, "c-defines") == 0) {
            if (!parseStringArray(p, &build->cDefines))
                return false;
        }
        else if (strcmp(key, "c-flags") == 0) {
            if (!parseStringArray(p, &build->cFlags))
                return false;
        }
        else if (strcmp(key, "defines") == 0) {
            if (!parseStringArray(p, &build->defines))
                return false;
        }
        else if (strcmp(key, "flags") == 0) {
            if (!parseStringArray(p, &build->flags))
                return false;
        }
        else if (strcmp(key, "plugins-dir") == 0) {
            if (!expectScalar(p, &build->pluginsDir))
                return false;
        }
        else if (strcmp(key, "stdlib") == 0) {
            if (!expectScalar(p, &build->stdlib))
                return false;
        }
        else {
            // Unknown field, skip it
            if (!parseYamlEvent(p))
                return false;
            if (!skipValue(p))
                return false;
        }
    }

    return true;
}

static bool parsePackageSection(YamlParser *p, PackageMetadata *meta)
{
    // Expect mapping start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_MAPPING_START_EVENT) {
        logError(p->log, NULL, "expected package mapping", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p->event.type != YAML_SCALAR_EVENT) {
            logError(p->log, NULL, "expected package field name", NULL);
            return false;
        }

        const char *key = (const char *)p->event.data.scalar.value;

        if (strcmp(key, "name") == 0) {
            if (!expectScalar(p, &meta->name))
                return false;

            // Validate package name immediately
            cstring error = validatePackageName(meta->name);
            if (error) {
                logError(p->log, NULL, "invalid package name '{s}': {s}",
                        (FormatArg[]){{.s = meta->name}, {.s = error}});
                return false;
            }
        }
        else if (strcmp(key, "version") == 0) {
            if (!expectScalar(p, &meta->version))
                return false;

            // Validate semantic version immediately
            cstring error = validateSemanticVersion(meta->version);
            if (error) {
                logError(p->log, NULL, "invalid package version '{s}': {s}",
                        (FormatArg[]){{.s = meta->version}, {.s = error}});
                return false;
            }
        }
        else if (strcmp(key, "description") == 0) {
            if (!expectScalar(p, &meta->description))
                return false;
        }
        else if (strcmp(key, "author") == 0) {
            if (!expectScalar(p, &meta->author))
                return false;
        }
        else if (strcmp(key, "license") == 0) {
            if (!expectScalar(p, &meta->license))
                return false;

            // Validate license identifier
            cstring error = validateLicenseIdentifier(meta->license);
            if (error) {
                logError(p->log, NULL, "invalid license identifier '{s}': {s}",
                        (FormatArg[]){{.s = meta->license}, {.s = error}});
                return false;
            }
        }
        else if (strcmp(key, "repository") == 0) {
            if (!expectScalar(p, &meta->repository))
                return false;

            // Validate repository identifier
            cstring error = validateGitRepository(meta->repository);
            if (error) {
                logError(p->log, NULL, "invalid repository '{s}': {s}",
                        (FormatArg[]){{.s = meta->repository}, {.s = error}});
                return false;
            }
        }
        else if (strcmp(key, "homepage") == 0) {
            if (!expectScalar(p, &meta->homepage))
                return false;
        }
        else {
            // Unknown field, skip it
            if (!parseYamlEvent(p))
                return false;
            if (!skipValue(p))
                return false;
        }
    }

    return true;
}

/**
 * Parse install or install-dev scripts section
 */
static bool parseInstallScripts(YamlParser *p, DynArray *installScripts)
{
    // Expect sequence start
    if (!parseYamlEvent(p))
        return false;

    if (p->event.type != YAML_SEQUENCE_START_EVENT) {
        logError(p->log, NULL, "expected install scripts sequence", NULL);
        return false;
    }

    while (true) {
        if (!parseYamlEvent(p))
            return false;

        if (p->event.type == YAML_SEQUENCE_END_EVENT)
            break;

        if (p->event.type != YAML_MAPPING_START_EVENT) {
            logError(p->log, NULL, "expected install script mapping", NULL);
            return false;
        }

        PackageInstallScript installScript;
        memset(&installScript, 0, sizeof(PackageInstallScript));
        installScript.required = false; // Default to optional

        while (true) {
            if (!parseYamlEvent(p))
                return false;

            if (p->event.type == YAML_MAPPING_END_EVENT)
                break;

            if (p->event.type != YAML_SCALAR_EVENT) {
                logError(p->log, NULL, "expected install script field name", NULL);
                return false;
            }

            const char *key = (const char *)p->event.data.scalar.value;

            if (strcmp(key, "name") == 0) {
                if (!expectScalar(p, &installScript.name))
                    return false;
            }
            else if (strcmp(key, "script") == 0) {
                if (!expectScalar(p, &installScript.script))
                    return false;
            }
            else if (strcmp(key, "required") == 0) {
                if (!parseYamlEvent(p))
                    return false;

                if (p->event.type != YAML_SCALAR_EVENT) {
                    logError(p->log, NULL, "expected boolean value for required", NULL);
                    return false;
                }

                const char *value = (const char *)p->event.data.scalar.value;
                installScript.required = (strcmp(value, "true") == 0 || strcmp(value, "yes") == 0);
            }
            else {
                // Unknown field, skip it
                if (!parseYamlEvent(p))
                    return false;
                if (!skipValue(p))
                    return false;
            }
        }

        // Validate required fields
        if (!installScript.name || installScript.name[0] == '\0') {
            logError(p->log, NULL, "install script missing 'name' field", NULL);
            return false;
        }
        if (!installScript.script || installScript.script[0] == '\0') {
            logError(p->log, NULL, "install script '{s}' missing 'script' field",
                    (FormatArg[]){{.s = installScript.name}});
            return false;
        }

        pushOnDynArray(installScripts, &installScript);
    }

    return true;
}

bool parseCxyfile(const char *path, PackageMetadata *meta, StrPool *strings, Log *log)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        logError(log, NULL, "failed to open '{s}': {s}",
                (FormatArg[]){{.s = path}, {.s = strerror(errno)}});
        return false;
    }

    YamlParser p;
    if (!initYamlParser(&p, file, strings, log, path)) {
        fclose(file);
        return false;
    }

    initPackageMetadata(meta, strings);

    bool success = true;

    // Expect stream start
    if (!parseYamlEvent(&p)) {
        success = false;
        goto cleanup;
    }

    if (p.event.type != YAML_STREAM_START_EVENT) {
        logError(log, NULL, "expected YAML stream start", NULL);
        success = false;
        goto cleanup;
    }

    // Expect document start
    if (!parseYamlEvent(&p)) {
        success = false;
        goto cleanup;
    }

    if (p.event.type != YAML_DOCUMENT_START_EVENT) {
        logError(log, NULL, "expected YAML document start", NULL);
        success = false;
        goto cleanup;
    }

    // Expect root mapping start
    if (!parseYamlEvent(&p)) {
        success = false;
        goto cleanup;
    }

    if (p.event.type != YAML_MAPPING_START_EVENT) {
        logError(log, NULL, "expected root mapping in Cxyfile.yaml", NULL);
        success = false;
        goto cleanup;
    }

    // Parse root-level keys
    while (true) {
        if (!parseYamlEvent(&p)) {
            success = false;
            goto cleanup;
        }

        if (p.event.type == YAML_MAPPING_END_EVENT)
            break;

        if (p.event.type != YAML_SCALAR_EVENT) {
            logError(log, NULL, "expected root field name", NULL);
            success = false;
            goto cleanup;
        }

        const char *key = (const char *)p.event.data.scalar.value;

        if (strcmp(key, "package") == 0) {
            if (!parsePackageSection(&p, meta)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "dependencies") == 0) {
            if (!parseDependencies(&p, &meta->dependencies)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "dev-dependencies") == 0) {
            if (!parseDependencies(&p, &meta->devDependencies)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "tests") == 0) {
            if (!parseTests(&p, &meta->tests)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "scripts") == 0) {
            if (!parseScripts(&p, &meta->scripts, &meta->scriptEnv)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "install") == 0) {
            if (!parseInstallScripts(&p, &meta->install)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "install-dev") == 0) {
            if (!parseInstallScripts(&p, &meta->installDev)) {
                success = false;
                goto cleanup;
            }
        }
        else if (strcmp(key, "build") == 0) {
            if (!parseBuildConfig(&p, &meta->build)) {
                success = false;
                goto cleanup;
            }
            meta->hasMultipleBuilds = false;
        }
        else if (strcmp(key, "builds") == 0) {
            if (!parseBuilds(&p, &meta->builds)) {
                success = false;
                goto cleanup;
            }
            meta->hasMultipleBuilds = true;
        }
        else {
            // Unknown root field, skip it
            if (!parseYamlEvent(&p)) {
                success = false;
                goto cleanup;
            }
            if (!skipValue(&p)) {
                success = false;
                goto cleanup;
            }
        }
    }

cleanup:
    cleanupYamlParser(&p);
    fclose(file);
    return success;
}

bool validatePackageMetadata(const PackageMetadata *meta, Log *log)
{
    bool valid = true;

    if (meta->name == NULL || meta->name[0] == '\0') {
        logError(log, NULL, "package name is required", NULL);
        valid = false;
    }
    else {
        // Validate package name format (already validated during parse, but double-check)
        cstring error = validatePackageName(meta->name);
        if (error) {
            logError(log, NULL, "invalid package name '{s}': {s}",
                    (FormatArg[]){{.s = meta->name}, {.s = error}});
            valid = false;
        }
    }

    if (meta->version == NULL || meta->version[0] == '\0') {
        logError(log, NULL, "package version is required", NULL);
        valid = false;
    }
    else {
        // Validate version format (already validated during parse, but double-check)
        cstring error = validateSemanticVersion(meta->version);
        if (error) {
            logError(log, NULL, "invalid package version '{s}': {s}",
                    (FormatArg[]){{.s = meta->version}, {.s = error}});
            valid = false;
        }
    }

    if (meta->author == NULL || meta->author[0] == '\0') {
        logError(log, NULL, "package author is required", NULL);
        valid = false;
    }

    // Validate dependencies (already validated during parse, but check for completeness)
    for (u32 i = 0; i < meta->dependencies.size; i++) {
        PackageDependency *dep = &dynArrayAt(PackageDependency*, &meta->dependencies, i);

        if (dep->name == NULL || dep->name[0] == '\0') {
            logError(log, NULL, "dependency name is required", NULL);
            valid = false;
        }

        // Must have either repository or path
        if ((dep->repository == NULL || dep->repository[0] == '\0') &&
            (dep->path == NULL || dep->path[0] == '\0')) {
            logError(log, NULL, "dependency '{s}' must specify repository or path",
                    (FormatArg[]){{.s = dep->name ?: "unknown"}});
            valid = false;
        }
    }

    // Validate license if present (optional field)
    if (meta->license != NULL && meta->license[0] != '\0') {
        cstring error = validateLicenseIdentifier(meta->license);
        if (error) {
            logError(log, NULL, "invalid license identifier '{s}': {s}",
                    (FormatArg[]){{.s = meta->license}, {.s = error}});
            valid = false;
        }
    }

    // Validate repository if present (optional field)
    if (meta->repository != NULL && meta->repository[0] != '\0') {
        cstring error = validateGitRepository(meta->repository);
        if (error) {
            logError(log, NULL, "invalid repository '{s}': {s}",
                    (FormatArg[]){{.s = meta->repository}, {.s = error}});
            valid = false;
        }
    }

    return valid;
}

bool findAndLoadCxyfile(const char *startDir, PackageMetadata *meta, StrPool *strings, Log *log, char **foundPath)
{
    char path[1024];
    char currentDir[1024];

    if (startDir) {
        strncpy(currentDir, startDir, sizeof(currentDir) - 1);
        currentDir[sizeof(currentDir) - 1] = '\0';
    }
    else {
        if (!getcwd(currentDir, sizeof(currentDir))) {
            logError(log, NULL, "failed to get current directory", NULL);
            return false;
        }
    }

    // Search up the directory tree
    while (true) {
        snprintf(path, sizeof(path), "%s/Cxyfile.yaml", currentDir);

        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
            // Found it!
            if (foundPath) {
                *foundPath = strdup(currentDir);
            }
            return parseCxyfile(path, meta, strings, log);
        }

        // Move up one directory
        char *lastSlash = strrchr(currentDir, '/');
        if (lastSlash == NULL || lastSlash == currentDir) {
            // Reached root
            break;
        }
        *lastSlash = '\0';
    }

    logError(log, NULL, "Cxyfile.yaml not found in current directory or any parent", NULL);
    return false;
}

/**
 * Helper function to determine if a string needs special YAML formatting
 */
static bool needsYamlEscaping(const char *str)
{
    if (!str || str[0] == '\0')
        return false;

    // Check for characters that need escaping or multiline formatting
    for (const char *p = str; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '"' || *p == '\'' ||
            *p == '\\' || *p == ':' || *p == '#' || *p == '[' ||
            *p == ']' || *p == '{' || *p == '}' || *p == '|' ||
            *p == '>' || *p == '&' || *p == '*' || *p == '!' ||
            (unsigned char)*p < 32) {
            return true;
        }
    }

    // Check if starts with special characters
    if (str[0] == '-' || str[0] == '?' || str[0] == ' ')
        return true;

    return false;
}

/**
 * Helper function to check if a string contains newlines
 */
static bool isMultiline(const char *str)
{
    if (!str)
        return false;
    return strchr(str, '\n') != NULL;
}

/**
 * Write a YAML string value with proper formatting
 * - Simple strings: write as-is
 * - Multiline strings: write with literal scalar (|)
 */
static void writeYamlStringWithIndent(FormatState *state, const char *str, int indent)
{
    if (!str || str[0] == '\0') {
        format(state, "\"\"", NULL);
        return;
    }

    // Check if multiline
    if (isMultiline(str)) {
        // Use literal block scalar (|) for multiline strings
        format(state, "|\n", NULL);

        // Write each line with proper indentation
        const char *line_start = str;
        const char *p = str;

        while (*p) {
            if (*p == '\n') {
                // Write line with indentation
                for (int i = 0; i < indent; i++) {
                    format(state, " ", NULL);
                }
                size_t len = p - line_start;
                if (len > 0) {
                    append(state, line_start, len);
                }
                format(state, "\n", NULL);
                line_start = p + 1;
            }
            p++;
        }

        // Write last line if any
        if (line_start < p) {
            for (int i = 0; i < indent; i++) {
                format(state, " ", NULL);
            }
            appendString(state, line_start);
        }
    }
    else if (needsYamlEscaping(str)) {
        // Use double-quoted string with escaping
        format(state, "\"", NULL);
        for (const char *p = str; *p; p++) {
            if (*p == '"' || *p == '\\') {
                appendString(state, "\\");
                append(state, p, 1);
            } else if (*p == '\n') {
                appendString(state, "\\n");
            } else if (*p == '\r') {
                appendString(state, "\\r");
            } else if (*p == '\t') {
                appendString(state, "\\t");
            } else {
                append(state, p, 1);
            }
        }
        format(state, "\"", NULL);
    }
    else {
        // Simple string, no escaping needed
        format(state, "{s}", (FormatArg[]){{.s = str}});
    }
}

static void writeYamlString(FormatState *state, const char *str)
{
    writeYamlStringWithIndent(state, str, 4);
}

bool writeCxyfile(const char *path, const PackageMetadata *meta, Log *log)
{
    FormatState state = newFormatState(NULL, true);

    // Package section
    format(&state, "package:\n", NULL);
    format(&state, "  name: {s}\n", (FormatArg[]){{.s = meta->name}});
    format(&state, "  version: {s}\n", (FormatArg[]){{.s = meta->version}});

    if (meta->author && meta->author[0] != '\0') {
        format(&state, "  author: {s}\n", (FormatArg[]){{.s = meta->author}});
    }
    if (meta->description && meta->description[0] != '\0') {
        format(&state, "  description: {s}\n", (FormatArg[]){{.s = meta->description}});
    }
    if (meta->license && meta->license[0] != '\0') {
        format(&state, "  license: {s}\n", (FormatArg[]){{.s = meta->license}});
    }
    if (meta->repository && meta->repository[0] != '\0') {
        format(&state, "  repository: {s}\n", (FormatArg[]){{.s = meta->repository}});
    }
    if (meta->homepage && meta->homepage[0] != '\0') {
        format(&state, "  homepage: {s}\n", (FormatArg[]){{.s = meta->homepage}});
    }
    format(&state, "\n", NULL);

    // Build or Builds section
    if (meta->hasMultipleBuilds && meta->builds.size > 0) {
        format(&state, "builds:\n", NULL);
        for (u32 i = 0; i < meta->builds.size; i++) {
            PackageBuild *build = &((PackageBuild *)meta->builds.elems)[i];
            PackageBuildConfig *config = &build->config;

            format(&state, "  {s}:\n", (FormatArg[]){{.s = build->name}});

            if (build->isDefault) {
                format(&state, "    default: true\n", NULL);
            }
            if (config->entry && config->entry[0] != '\0') {
                format(&state, "    entry: {s}\n", (FormatArg[]){{.s = config->entry}});
            }
            if (config->output && config->output[0] != '\0') {
                format(&state, "    output: {s}\n", (FormatArg[]){{.s = config->output}});
            }
            if (config->cLibs.size > 0) {
                format(&state, "    c-libs:\n", NULL);
                for (u32 j = 0; j < config->cLibs.size; j++) {
                    cstring lib = dynArrayAt(cstring *, &config->cLibs, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = lib}});
                }
            }
            if (config->cLibDirs.size > 0) {
                format(&state, "    c-lib-dirs:\n", NULL);
                for (u32 j = 0; j < config->cLibDirs.size; j++) {
                    cstring dir = dynArrayAt(cstring *, &config->cLibDirs, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = dir}});
                }
            }
            if (config->cHeaderDirs.size > 0) {
                format(&state, "    c-header-dirs:\n", NULL);
                for (u32 j = 0; j < config->cHeaderDirs.size; j++) {
                    cstring dir = dynArrayAt(cstring *, &config->cHeaderDirs, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = dir}});
                }
            }
            if (config->cDefines.size > 0) {
                format(&state, "    c-defines:\n", NULL);
                for (u32 j = 0; j < config->cDefines.size; j++) {
                    cstring define = dynArrayAt(cstring *, &config->cDefines, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = define}});
                }
            }
            if (config->cFlags.size > 0) {
                format(&state, "    c-flags:\n", NULL);
                for (u32 j = 0; j < config->cFlags.size; j++) {
                    cstring flag = dynArrayAt(cstring *, &config->cFlags, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = flag}});
                }
            }
            if (config->defines.size > 0) {
                format(&state, "    defines:\n", NULL);
                for (u32 j = 0; j < config->defines.size; j++) {
                    cstring define = dynArrayAt(cstring *, &config->defines, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = define}});
                }
            }
            if (config->flags.size > 0) {
                format(&state, "    flags:\n", NULL);
                for (u32 j = 0; j < config->flags.size; j++) {
                    cstring flag = dynArrayAt(cstring *, &config->flags, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = flag}});
                }
            }
            if (config->pluginsDir && config->pluginsDir[0] != '\0') {
                format(&state, "    plugins-dir: {s}\n", (FormatArg[]){{.s = config->pluginsDir}});
            }
            if (config->stdlib && config->stdlib[0] != '\0') {
                format(&state, "    stdlib: {s}\n", (FormatArg[]){{.s = config->stdlib}});
            }
        }
        format(&state, "\n", NULL);
    } else if (meta->build.entry && meta->build.entry[0] != '\0') {
        format(&state, "build:\n", NULL);
        format(&state, "  entry: {s}\n", (FormatArg[]){{.s = meta->build.entry}});

        if (meta->build.output && meta->build.output[0] != '\0') {
            format(&state, "  output: {s}\n", (FormatArg[]){{.s = meta->build.output}});
        }
        if (meta->build.cLibs.size > 0) {
            format(&state, "  c-libs:\n", NULL);
            for (u32 i = 0; i < meta->build.cLibs.size; i++) {
                cstring lib = dynArrayAt(cstring *, &meta->build.cLibs, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = lib}});
            }
        }
        if (meta->build.cLibDirs.size > 0) {
            format(&state, "  c-lib-dirs:\n", NULL);
            for (u32 i = 0; i < meta->build.cLibDirs.size; i++) {
                cstring dir = dynArrayAt(cstring *, &meta->build.cLibDirs, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = dir}});
            }
        }
        if (meta->build.cHeaderDirs.size > 0) {
            format(&state, "  c-header-dirs:\n", NULL);
            for (u32 i = 0; i < meta->build.cHeaderDirs.size; i++) {
                cstring dir = dynArrayAt(cstring *, &meta->build.cHeaderDirs, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = dir}});
            }
        }
        if (meta->build.cDefines.size > 0) {
            format(&state, "  c-defines:\n", NULL);
            for (u32 i = 0; i < meta->build.cDefines.size; i++) {
                cstring define = dynArrayAt(cstring *, &meta->build.cDefines, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = define}});
            }
        }
        if (meta->build.cFlags.size > 0) {
            format(&state, "  c-flags:\n", NULL);
            for (u32 i = 0; i < meta->build.cFlags.size; i++) {
                cstring flag = dynArrayAt(cstring *, &meta->build.cFlags, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = flag}});
            }
        }
        if (meta->build.defines.size > 0) {
            format(&state, "  defines:\n", NULL);
            for (u32 i = 0; i < meta->build.defines.size; i++) {
                cstring define = dynArrayAt(cstring *, &meta->build.defines, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = define}});
            }
        }
        if (meta->build.flags.size > 0) {
            format(&state, "  flags:\n", NULL);
            for (u32 i = 0; i < meta->build.flags.size; i++) {
                cstring flag = dynArrayAt(cstring *, &meta->build.flags, i);
                format(&state, "    - {s}\n", (FormatArg[]){{.s = flag}});
            }
        }
        if (meta->build.pluginsDir && meta->build.pluginsDir[0] != '\0') {
            format(&state, "  plugins-dir: {s}\n", (FormatArg[]){{.s = meta->build.pluginsDir}});
        }
        if (meta->build.stdlib && meta->build.stdlib[0] != '\0') {
            format(&state, "  stdlib: {s}\n", (FormatArg[]){{.s = meta->build.stdlib}});
        }
        format(&state, "\n", NULL);
    }

    // Dependencies section
    format(&state, "dependencies:", NULL);
    if (meta->dependencies.size == 0) {
        format(&state, " []\n\n", NULL);
    } else {
        format(&state, "\n", NULL);
        for (u32 i = 0; i < meta->dependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)meta->dependencies.elems)[i];
            format(&state, "  - name: {s}\n", (FormatArg[]){{.s = dep->name}});
            if (dep->repository && dep->repository[0] != '\0') {
                format(&state, "    repository: {s}\n", (FormatArg[]){{.s = dep->repository}});
            }
            if (dep->version && dep->version[0] != '\0') {
                format(&state, "    version: '{s}'\n", (FormatArg[]){{.s = dep->version}});
            }
            if (dep->tag && dep->tag[0] != '\0') {
                format(&state, "    tag: {s}\n", (FormatArg[]){{.s = dep->tag}});
            }
            if (dep->branch && dep->branch[0] != '\0') {
                format(&state, "    branch: {s}\n", (FormatArg[]){{.s = dep->branch}});
            }
            if (dep->path && dep->path[0] != '\0') {
                format(&state, "    path: {s}\n", (FormatArg[]){{.s = dep->path}});
            }
        }
        format(&state, "\n", NULL);
    }

    // Dev dependencies section
    if (meta->devDependencies.size > 0) {
        format(&state, "dev-dependencies:\n", NULL);
        for (u32 i = 0; i < meta->devDependencies.size; i++) {
            PackageDependency *dep = &((PackageDependency *)meta->devDependencies.elems)[i];
            format(&state, "  - name: {s}\n", (FormatArg[]){{.s = dep->name}});
            if (dep->repository && dep->repository[0] != '\0') {
                format(&state, "    repository: {s}\n", (FormatArg[]){{.s = dep->repository}});
            }
            if (dep->version && dep->version[0] != '\0') {
                format(&state, "    version: {s}\n", (FormatArg[]){{.s = dep->version}});
            }
            if (dep->tag && dep->tag[0] != '\0') {
                format(&state, "    tag: {s}\n", (FormatArg[]){{.s = dep->tag}});
            }
            if (dep->branch && dep->branch[0] != '\0') {
                format(&state, "    branch: {s}\n", (FormatArg[]){{.s = dep->branch}});
            }
            if (dep->path && dep->path[0] != '\0') {
                format(&state, "    path: {s}\n", (FormatArg[]){{.s = dep->path}});
            }
        }
        format(&state, "\n", NULL);
    }

    // Tests section
    if (meta->tests.size > 0) {
        format(&state, "tests:\n", NULL);
        for (u32 i = 0; i < meta->tests.size; i++) {
            PackageTest *test = &((PackageTest *)meta->tests.elems)[i];
            if (test->args.size == 0) {
                format(&state, "  - file: \"{s}\"\n", (FormatArg[]){{.s = test->file}});
            } else {
                format(&state, "  - file: \"{s}\"\n", (FormatArg[]){{.s = test->file}});
                format(&state, "    args:\n", NULL);
                for (u32 j = 0; j < test->args.size; j++) {
                    cstring arg = dynArrayAt(cstring *, &test->args, j);
                    format(&state, "      - {s}\n", (FormatArg[]){{.s = arg}});
                }
            }
        }
        format(&state, "\n", NULL);
    }

    // Scripts section
    if (meta->scriptEnv.size > 0 || meta->scripts.size > 0) {
        format(&state, "scripts:\n", NULL);

        // Write env section first if it exists
        if (meta->scriptEnv.size > 0) {
            format(&state, "  env:\n", NULL);
            for (u32 i = 0; i < meta->scriptEnv.size; i++) {
                EnvVar *env = &((EnvVar *)meta->scriptEnv.elems)[i];
                format(&state, "    {s}: ", (FormatArg[]){{.s = env->name}});
                writeYamlStringWithIndent(&state, env->value, 6);
                format(&state, "\n", NULL);
            }
        }

        // Then write regular scripts
        for (u32 i = 0; i < meta->scripts.size; i++) {
            PackageScript *script = &((PackageScript *)meta->scripts.elems)[i];

            // Check if script needs object notation (has dependencies, inputs, or outputs)
            bool needsObject = script->dependencies.size > 0 ||
                              script->inputs.size > 0 ||
                              script->outputs.size > 0;

            if (!needsObject) {
                // Simple scalar notation
                format(&state, "  {s}: ", (FormatArg[]){{.s = script->name}});
                writeYamlStringWithIndent(&state, script->command, 4);
                format(&state, "\n", NULL);
            } else {
                // Object notation with command and optional fields
                format(&state, "  {s}:\n", (FormatArg[]){{.s = script->name}});

                // Write depends if present
                if (script->dependencies.size > 0) {
                    format(&state, "    depends: [", NULL);
                    for (u32 j = 0; j < script->dependencies.size; j++) {
                        cstring dep = dynArrayAt(cstring *, &script->dependencies, j);
                        if (j > 0) {
                            format(&state, ", ", NULL);
                        }
                        else {
                            format(&state, " ", NULL);
                        }
                        format(&state, "{s}", (FormatArg[]){{.s = dep}});
                    }
                    format(&state, " ]\n", NULL);
                }

                // Write inputs if present
                if (script->inputs.size > 0) {
                    format(&state, "    inputs:\n", NULL);
                    for (u32 j = 0; j < script->inputs.size; j++) {
                        cstring input = dynArrayAt(cstring *, &script->inputs, j);
                        format(&state, "      - {s}\n", (FormatArg[]){{.s = input}});
                    }
                }

                // Write outputs if present
                if (script->outputs.size > 0) {
                    format(&state, "    outputs:\n", NULL);
                    for (u32 j = 0; j < script->outputs.size; j++) {
                        cstring output = dynArrayAt(cstring *, &script->outputs, j);
                        format(&state, "      - {s}\n", (FormatArg[]){{.s = output}});
                    }
                }

                // Write command
                format(&state, "    command: ", NULL);
                writeYamlStringWithIndent(&state, script->command, 6);
                format(&state, "\n", NULL);
            }
        }
    }

    // Write to file
    FILE *file = fopen(path, "w");
    if (!file) {
        logError(log, NULL, "failed to open '{s}' for writing: {s}",
                (FormatArg[]){{.s = path}, {.s = strerror(errno)}});
        freeFormatState(&state);
        return false;
    }

    writeFormatState(&state, file);
    fclose(file);
    freeFormatState(&state);

    return true;
}



bool installDependency(const PackageDependency *dep,
                       const char *packagesDir,
                       MemPool *pool,
                       Log *log,
                       bool noInstall,
                       bool verbose)
{
    if (!dep || !dep->name) {
        logError(log, NULL, "invalid dependency: name is required", NULL);
        return false;
    }

    // Build target directory path
    char targetPath[1024];
    snprintf(targetPath, sizeof(targetPath), "%s/%s", packagesDir, dep->name);

    // Check if already installed
    struct stat st;
    if (stat(targetPath, &st) == 0 && S_ISDIR(st.st_mode)) {
        // Already installed, check if it has Cxyfile.yaml
        char cxyfilePath[1024];
        snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", targetPath);

        if (stat(cxyfilePath, &st) == 0 && S_ISREG(st.st_mode)) {
            printStatusSticky(log, "Package '%s' already installed", dep->name);
            return true;
        } else {
            logWarning(log, NULL, "package directory exists but no Cxyfile.yaml found, reinstalling", NULL);
            // Remove and reinstall
            char rmCmd[1100];
            snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\"", targetPath);
            system(rmCmd);
        }
    }

    if (noInstall) {
        printStatusSticky(log, "Skipping installation (--no-install)", NULL);
        return true;
    }

    // Repository is required for installation
    if (!dep->repository || dep->repository[0] == '\0') {
        if (dep->path && dep->path[0] != '\0') {
            // Local path dependency - just verify it exists
            if (stat(dep->path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                logError(log, NULL, "local path '{s}' does not exist or is not a directory",
                        (FormatArg[]){{.s = dep->path}});
                return false;
            }

            char cxyfilePath[1024];
            snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", dep->path);
            if (stat(cxyfilePath, &st) != 0 || !S_ISREG(st.st_mode)) {
                logError(log, NULL, "local dependency at '{s}' does not contain Cxyfile.yaml",
                        (FormatArg[]){{.s = dep->path}});
                return false;
            }

            printStatusSticky(log, "Local dependency '%s' validated", dep->name);
            return true;
        }

        logError(log, NULL, "dependency '{s}' has no repository or path specified",
                (FormatArg[]){{.s = dep->name}});
        return false;
    }

    // Create packages directory if it doesn't exist
    if (!makeDirectory(packagesDir, true)) {
        logError(log, NULL, "failed to create packages directory '{s}'",
                (FormatArg[]){{.s = packagesDir}});
        return false;
    }

    printStatusSticky(log, " Installing package '%s'...", dep->name);

    // Determine what to clone
    bool success = false;
    if (dep->tag && dep->tag[0] != '\0') {
        // Clone specific tag
        printStatusSticky(log, " Cloning tag '%s' from %s", dep->tag, dep->repository);
        success = gitCloneTag(dep->repository, dep->tag, targetPath, log, verbose);
    } else if (dep->branch && dep->branch[0] != '\0') {
        // Clone specific branch
        printStatusSticky(log, " Cloning branch '%s' from %s", dep->branch, dep->repository);
        success = gitCloneBranch(dep->repository, dep->branch, targetPath, true, log, verbose);
    } else if (dep->version && dep->version[0] != '\0' && strcmp(dep->version, "*") != 0) {
        // Resolve version constraint using resolver
        printStatusSticky(log, " Resolving version constraint '%s'...", dep->version);

        // Parse the version constraint
        VersionConstraint constraint;
        if (!parseVersionConstraint(dep->version, &constraint, log)) {
            logError(log, NULL, "invalid version constraint '{s}'", (FormatArg[]){{.s = dep->version}});
            return false;
        }

        // Build constraints array
        DynArray constraints = newDynArray(sizeof(VersionConstraint));
        pushOnDynArray(&constraints, &constraint);

        // Find best matching version
        GitTag bestTag;
        if (!findBestMatchingVersion(dep->repository, &constraints, &bestTag, pool, log)) {
            logError(log, NULL, "no version satisfies constraint '{s}'", (FormatArg[]){{.s = dep->version}});
            freeDynArray(&constraints);
            return false;
        }

        printStatusSticky(log, " Using version %u.%u.%u (tag: %s)",
                   bestTag.version.major,
                   bestTag.version.minor,
                   bestTag.version.patch,
                   bestTag.name);

        success = gitCloneTag(dep->repository, bestTag.name, targetPath, log, verbose);
        freeDynArray(&constraints);
    } else {
        // Clone default branch
        printStatusSticky(log, " Cloning from %s", dep->repository);
        success = gitClone(dep->repository, targetPath, true, log, verbose);
    }

    if (!success) {
        logError(log, NULL, " failed to clone repository", NULL);
        return false;
    }

    // Verify Cxyfile.yaml exists
    char cxyfilePath[1024];
    snprintf(cxyfilePath, sizeof(cxyfilePath), "%s/Cxyfile.yaml", targetPath);

    if (stat(cxyfilePath, &st) != 0 || !S_ISREG(st.st_mode)) {
        logError(log, NULL, "cloned repository does not contain Cxyfile.yaml - not a valid Cxy package", NULL);

        // Clean up failed installation
        char rmCmd[1100];
        snprintf(rmCmd, sizeof(rmCmd), "rm -rf \"%s\"", targetPath);
        system(rmCmd);

        return false;
    }

    printStatusSticky(log, " Package '%s' installed successfully to %s", dep->name, targetPath);
    return true;
}
