//

#include "driver/options.h"
#include "driver/stages.h"
#include "options.h"
#include "package/commands/commands.h"
#include "package/validators.h"

#include "core/args.h"
#include "core/log.h"
#include "core/strpool.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    cstring name;
    u64 value;
} EnumOptionMap;

typedef struct {
    Options *options;
    StrPool *strings;
    Log *log;
} ParseContext;

static bool cmdParseDumpAstModes(CmdParser *P,
                                 CmdFlagValue *dst,
                                 const char *str,
                                 const char *name)
{
#define DUMP_OPTIONS_COUNT (sizeof(dumpModes) / sizeof(EnumOptionMap))
    static CmdEnumValueDesc dumpModes[] = {
#define ff(NN) {#NN, dmp##NN},
        DUMP_OPTIONS(ff)
#undef ff
    };
    return cmdParseEnumValue(P, dst, str, name, dumpModes, DUMP_OPTIONS_COUNT);
#undef DUMP_OPTIONS_COUNT
}

static bool cmdParseDumpStatsModes(CmdParser *P,
                                   CmdFlagValue *dst,
                                   const char *str,
                                   const char *name)
{
#define DUMP_STATS_MODES_COUNT (sizeof(dumpModes) / sizeof(EnumOptionMap))
    static CmdEnumValueDesc dumpModes[] = {
#define ff(NN) {#NN, dsm##NN},
        DRIVER_STATS_MODE(ff)
#undef ff
    };
    return cmdParseEnumValue(
        P, dst, str, name, dumpModes, DUMP_STATS_MODES_COUNT);
#undef DUMP_STATS_MODES_COUNT
}

static bool cmdParseLastStage(CmdParser *P,
                              CmdFlagValue *dst,
                              const char *str,
                              const char *name)
{
    static CmdEnumValueDesc stagesDesc[] = {
#define ff(NN, _) {#NN, ccs##NN},
        CXY_PUBLIC_COMPILER_STAGES(ff)
#undef ff
    };
    return cmdParseEnumValue(
        P, dst, str, name, stagesDesc, sizeof__(stagesDesc));
}

static bool cmdOptimizationLevel(CmdParser *P,
                                 CmdFlagValue *dst,
                                 const char *str,
                                 const char *name)
{
    if (str && str[0] != '\0' && str[1] == '\0') {
        dst->state = cmdNumber;
        switch (str[0]) {
        case '0':
        case 'd':
            dst->num = O0;
            return true;
        case '1':
            dst->num = O1;
            return true;
        case '2':
            dst->num = O2;
            return true;
        case '3':
            dst->num = O3;
            return true;
        case 's':
            dst->num = O3;
            return true;
        default:
            dst->state = cmdNoValue;
            break;
        }
    }
    sprintf(P->error,
            "error: value '%s' passed to flag '%s' cannot be parsed as an "
            "optimization flag, supported values -O1, -O2, -O3 -Os -Od\n",
            str ?: "null",
            name);
    return false;
}

static bool cmdCompilerDefine(CmdParser *P,
                              CmdFlagValue *dst,
                              const char *str,
                              const char *name)
{
    if (str == NULL || str[0] == '\0') {
        sprintf(P->error,
                "error: command line argument '%s' is missing a value",
                name);
        return false;
    }
    if (dst->array.elems == NULL)
        dst->array = newDynArray(sizeof(CompilerDefine));

    const char *it = str;
    while (isspace(it[0]))
        it++;
    cstring variable = it, value = NULL;
    if (!isalpha(it[0]) && it[0] != '_') {
        sprintf(P->error,
                "error: define variable '%s' does not conform to cxy variable "
                "specification",
                name);
        return false;
    }

    while (isalnum(it[0]) || it[0] == '_')
        it++;
    if (it[0] == '=') {
        value = it + 1;
        ((char *)it)[0] = '\0';
    }
    else if (it[0] != '\0') {
        sprintf(P->error,
                "error: define variable '%s' does not conform to cxy variable "
                "specification",
                name);
        return false;
    }

    pushOnDynArray(&dst->array,
                   &(CompilerDefine){.name = variable, .value = value});
    return true;
}

static bool cmdArrayArgument(CmdParser *P,
                             CmdFlagValue *dst,
                             const char *str,
                             const char *name)
{
    if (str == NULL || str[0] == '\0') {
        sprintf(P->error,
                "error: command line argument '%s' is missing a value",
                name);
        return false;
    }
    if (str[0] == '[' && str[strlen(str) - 1] == ']') {
        // Skip default empty array marker
        return true;
    }

    if (dst->array.elems == NULL)
        dst->array = newDynArray(sizeof(cstring));

    pushStringOnDynArray(&dst->array, str);
    dst->state = cmdArray;
    return true;
}

// Package validation wrappers for command-line parser
static bool cmdValidatePackageName(CmdParser *P,
                                   CmdFlagValue *dst,
                                   const char *str,
                                   const char *name)
{
    // First parse as string
    if (!cmdParseString(P, dst, str, name)) {
        return false;
    }

    // Then validate
    cstring error = validatePackageName(str);
    if (error) {
        snprintf(P->error, sizeof(P->error),
                "error: invalid package name '%s' for flag '%s': %s\n",
                str, name, error);
        return false;
    }

    return true;
}

static bool cmdValidateSemanticVersion(CmdParser *P,
                                       CmdFlagValue *dst,
                                       const char *str,
                                       const char *name)
{
    // First parse as string
    if (!cmdParseString(P, dst, str, name)) {
        return false;
    }

    // Empty or "*" is valid for version constraints
    if (str[0] == '\0' || strcmp(str, "*") == 0) {
        return true;
    }

    // Skip constraint prefix for validation (^, ~, >=, >, <=, <)
    const char *versionToValidate = str;
    if (str[0] == '^' || str[0] == '~') {
        versionToValidate = str + 1;
    }
    else if (strlen(str) >= 2) {
        if ((str[0] == '>' || str[0] == '<') && str[1] == '=') {
            versionToValidate = str + 2;
        }
        else if (str[0] == '>' || str[0] == '<') {
            versionToValidate = str + 1;
        }
    }

    // Skip whitespace after constraint operator
    while (*versionToValidate == ' ') versionToValidate++;

    // Validate the base version
    cstring error = validateSemanticVersion(versionToValidate);
    if (error) {
        snprintf(P->error, sizeof(P->error),
                "error: invalid version '%s' for flag '%s': %s\n",
                str, name, error);
        return false;
    }

    return true;
}

static bool cmdValidateGitRepository(CmdParser *P,
                                     CmdFlagValue *dst,
                                     const char *str,
                                     const char *name)
{
    // First parse as string
    if (!cmdParseString(P, dst, str, name)) {
        return false;
    }

    // Then validate
    cstring error = validateGitRepository(str);
    if (error) {
        snprintf(P->error, sizeof(P->error),
                "error: invalid repository '%s' for flag '%s': %s\n",
                str, name, error);
        return false;
    }

    return true;
}

static bool cmdValidateLicenseIdentifier(CmdParser *P,
                                         CmdFlagValue *dst,
                                         const char *str,
                                         const char *name)
{
    // First parse as string
    if (!cmdParseString(P, dst, str, name)) {
        return false;
    }

    // Then validate
    cstring error = validateLicenseIdentifier(str);
    if (error) {
        snprintf(P->error, sizeof(P->error),
                "error: invalid license '%s' for flag '%s': %s\n",
                str, name, error);
        return false;
    }

    return true;
}

Command(
    dev,
    "development mode build, useful when debugging issues",
    Positionals(Use(cmdArrayArgument,
                    Name("sources"),
                    Help("One or more source files to compile"),
                    Many())),
    Use(cmdParseLastStage,
        Name("last-stage"),
        Help("the last compiler stage to execute, e.g "
             "'Codegen'"),
        Def("Compile")),
    Use(cmdParseDumpAstModes,
        Name("dump-ast"),
        Help("Dumps the the AST as either JSON, YAML or CXY. Supported values: "
             "JSON|YAML|CXY"),
        Def("NONE")),
    Opt(Name("clean-ast"),
        Help("Prints the AST exactly as generated without any comments"),
        Def("false")),
    Opt(Name("with-location"),
        Help("Include the node location on the dumped AST"),
        Def("false")),
    Opt(Name("without-attrs"),
        Help("Exclude node attributes when dumping AST"),
        Def("false")),
    Opt(Name("with-named-enums"),
        Help("Use named enums on the when dumping AST"),
        Def("true")),
    Str(Name("output"),
        Sf('o'),
        Help("path to file to generate code or print AST to (default is "
             "stdout)"),
        Def("")),
    Opt(Name("print-ir"),
        Help("prints the generate IR (on supported backends, e.g LLVM)"),
        Def("false")),
    Opt(Name("emit-assembly"),
        Help("emits the generated assembly code to given filename on supported "
             "backends (e.g LLVM)"),
        Def("false")),
    Opt(Name("emit-bitcode"),
        Help("emits the generated bitcode to given filename on supported "
             "platforms (e.g LLVM)"),
        Def("false")));

Command(build,
        "transpiles the given cxy what file and builds it using gcc",
        Positionals(Use(cmdArrayArgument,
                        Name("sources"),
                        Help("One or more source files to compile"),
                        Many())),
        Str(Name("output"),
            Sf('o'),
            Help("output file for the compiled binary (default: app)"),
            Def("app")),
        Str(Name("build-dir"),
            Help("the build directory, used as the working directory for the "
                 "compiler"),
            Def("")),
        Opt(Name("plugin"),
            Sf('p'),
            Help("Build a plugin for the given file"),
            Def("false")));

Command(test,
        "Runs unit tests declared on the given source files",
        Positionals(Use(cmdArrayArgument,
                        Name("sources"),
                        Help("One or more source files to test"),
                        Many())),
        Str(Name("build-dir"),
            Help("the build directory, used as the working directory for the "
                 "compiler"),
            Def("")),
            Str(Name("output"),
                Sf('o'),
                Help("output file for the compiled binary (default: app)"),
                Def("app")));

Command(package,
        "Package management commands (create, add, install, etc.)",
        Positionals());

Command(utils,
        "Utility commands for use within scripts (async processes, port management, etc.)",
        Positionals());

// clang-format off
#define BUILD_COMMANDS(f)                                                      \
    PARSER_BUILTIN_COMMANDS(f)                                                 \
    f(dev)                                                                     \
    f(build)                                                                   \
    f(test)                                                                    \
    f(package)                                                                 \
    f(utils)

#define DEV_CMD_LAYOUT(f, ...)                                                 \
    f(dev.lastStage, Local, Int, 0, ## __VA_ARGS__)                             \
    f(dev.dumpMode, Local, Int, 1, ## __VA_ARGS__)                              \
    f(dev.cleanAst, Local, Option, 2, ## __VA_ARGS__)                           \
    f(dev.withLocation, Local, Option, 3, ## __VA_ARGS__)                       \
    f(dev.withoutAttrs, Local, Option, 4, ## __VA_ARGS__)                       \
    f(dev.withNamedEnums, Local, Option, 5, ## __VA_ARGS__)                     \
    f(output, Local, String, 6, ## __VA_ARGS__)                                 \
    f(dev.printIR, Local, Option, 7, ## __VA_ARGS__)                            \
    f(dev.emitAssembly, Local, Option, 8, ## __VA_ARGS__)                       \
    f(dev.emitBitCode, Local, Option, 9, ## __VA_ARGS__)                        \

#define BUILD_CMD_LAYOUT(f, ...)                                                \
    f(output, Local, String, 0, ## __VA_ARGS__)                                 \
    f(buildDir, Local, String, 1, ## __VA_ARGS__)                               \
    f(build.plugin, Local, Option, 2, ## __VA_ARGS__)                           \

#define TEST_CMD_LAYOUT(f, ...)                                                \
    f(buildDir, Local, String, 0, ## __VA_ARGS__)                              \
    f(output, Local, String, 1, ## __VA_ARGS__)

#define PACKAGE_CMD_LAYOUT(f, ...)                                             \
    f(package.cxyfile, Global, String, 0, ## __VA_ARGS__)                       \
    f(package.packagesDir, Global, String, 1, ## __VA_ARGS__)                   \
    f(package.quiet, Global, Option, 2, ## __VA_ARGS__)                         \
    f(package.verbose, Global, Option, 3, ## __VA_ARGS__)

// Package subcommand definitions
#define PACKAGE_SUBCOMMANDS(f)                                              \
    PARSER_BUILTIN_COMMANDS(f)                                              \
    f(create)                                                               \
    f(add)                                                                  \
    f(install)                                                              \
    f(remove)                                                               \
    f(update)                                                               \
    f(test)                                                                 \
    f(build)                                                                \
    f(publish)                                                              \
    f(list)                                                                 \
    f(info)                                                                 \
    f(clean)                                                                \
    f(run)                                                                  \
    f(find_system)

#define UTILS_SUBCOMMANDS(f)                                                \
    PARSER_BUILTIN_COMMANDS(f)                                              \
    f(async_cmd_start)                                                      \
    f(async_cmd_stop)                                                       \
    f(async_cmd_logs)                                                       \
    f(async_cmd_status)                                                     \
    f(wait_for)                                                             \
    f(wait_for_port)                                                        \
    f(find_free_port)                                                       \
    f(env_check)                                                            \
    f(lock)

// Package subcommand option layouts
#define PKG_CREATE_CMD_LAYOUT(f, ...)                                          \
    f(package.name, Local, String, 0, ## __VA_ARGS__)                          \
    f(package.author, Local, String, 1, ## __VA_ARGS__)                        \
    f(package.description, Local, String, 2, ## __VA_ARGS__)                   \
    f(package.license, Local, String, 3, ## __VA_ARGS__)                       \
    f(package.version, Local, String, 4, ## __VA_ARGS__)                       \
    f(package.directory, Local, String, 5, ## __VA_ARGS__)                     \
    f(package.bin, Local, Option, 6, ## __VA_ARGS__)

#define PKG_ADD_CMD_LAYOUT(f, ...)                                             \
    f(package.packageName, Local, String, 0, ## __VA_ARGS__)                   \
    f(package.constraint, Local, String, 1, ## __VA_ARGS__)                    \
    f(package.tag, Local, String, 2, ## __VA_ARGS__)                           \
    f(package.branch, Local, String, 3, ## __VA_ARGS__)                        \
    f(package.path, Local, String, 4, ## __VA_ARGS__)                          \
    f(package.dev, Local, Option, 5, ## __VA_ARGS__)                           \
    f(package.noInstall, Local, Option, 6, ## __VA_ARGS__)

#define PKG_REMOVE_CMD_LAYOUT(f, ...)                                          \
    /* No specific options for remove */

#define PKG_INSTALL_CMD_LAYOUT(f, ...)                                         \
    f(package.includeDev, Local, Option, 0, ## __VA_ARGS__)                    \
    f(package.clean, Local, Option, 1, ## __VA_ARGS__)                         \
    f(package.verify, Local, Option, 2, ## __VA_ARGS__)                        \
    f(package.offline, Local, Option, 3, ## __VA_ARGS__)                       \
    f(package.frozenLockfile, Local, Option, 4, ## __VA_ARGS__)

#define PKG_UPDATE_CMD_LAYOUT(f, ...)                                          \
    f(package.latest, Local, Option, 0, ## __VA_ARGS__)                        \
    f(package.dryRun, Local, Option, 1, ## __VA_ARGS__)                        \
    f(package.includeDev, Local, Option, 2, ## __VA_ARGS__)

#define PKG_TEST_CMD_LAYOUT(f, ...)                                            \
    f(package.buildDir, Local, String, 0, ## __VA_ARGS__)                      \
    f(package.filter, Local, String, 1, ## __VA_ARGS__)                        \
    f(package.parallel, Local, Int, 2, ## __VA_ARGS__)

#define PKG_PUBLISH_CMD_LAYOUT(f, ...)                                         \
    f(package.bump, Local, String, 0, ## __VA_ARGS__)                          \
    f(package.tagName, Local, String, 1, ## __VA_ARGS__)                       \
    f(package.message, Local, String, 2, ## __VA_ARGS__)                       \
    f(package.dryRun, Local, Option, 3, ## __VA_ARGS__)

#define PKG_LIST_CMD_LAYOUT(f, ...)                                            \
    /* No specific options beyond global package options */

#define PKG_INFO_CMD_LAYOUT(f, ...)                                            \
    f(package.json, Local, Option, 0, ## __VA_ARGS__)

#define PKG_CLEAN_CMD_LAYOUT(f, ...)                                           \
    f(package.cleanCache, Local, Option, 0, ## __VA_ARGS__)                    \
    f(package.cleanBuild, Local, Option, 1, ## __VA_ARGS__)                    \
    f(package.cleanAll, Local, Option, 2, ## __VA_ARGS__)                      \
    f(package.force, Local, Option, 3, ## __VA_ARGS__)

#define PKG_BUILD_CMD_LAYOUT(f, ...)                                           \
    f(package.release, Local, Option, 0, ## __VA_ARGS__)                       \
    f(package.debug, Local, Option, 1, ## __VA_ARGS__)                         \
    f(package.buildDir, Local, String, 2, ## __VA_ARGS__)                      \
    f(package.clean, Local, Option, 3, ## __VA_ARGS__)                         \
    f(package.buildAll, Local, Option, 4, ## __VA_ARGS__)                      \
    f(package.listBuilds, Local, Option, 5, ## __VA_ARGS__)

#define PKG_RUN_CMD_LAYOUT(f, ...)                                             \
    f(package.listScripts, Local, Option, 0, ## __VA_ARGS__)                  \
    f(package.noCache, Local, Option, 1, ## __VA_ARGS__)

#define PKG_FIND_SYSTEM_CMD_LAYOUT(f, ...)                                     \
    f(package.findSystemFormat, Local, String, 0, ## __VA_ARGS__)             \
    f(package.findSystemSearchRoots, Local, Array, 1, ## __VA_ARGS__)         \
    f(package.findSystemIncludeDir, Local, Option, 2, ## __VA_ARGS__)         \
    f(package.findSystemLibDir, Local, Option, 3, ## __VA_ARGS__)             \
    f(package.findSystemLib, Local, Option, 4, ## __VA_ARGS__)                \
    f(package.findSystemVersion, Local, Option, 5, ## __VA_ARGS__)            \
    f(package.findSystemCFlags, Local, Option, 6, ## __VA_ARGS__)             \
    f(package.findSystemLdFlags, Local, Option, 7, ## __VA_ARGS__)

#define UTL_ASYNC_CMD_START_CMD_LAYOUT(f, ...)                                 \
    f(utils.captureOutput, Local, Option, 0, ## __VA_ARGS__)

#define UTL_ASYNC_CMD_STOP_CMD_LAYOUT(f, ...)                                  \
    /* No specific options for async-cmd-stop */

#define UTL_ASYNC_CMD_LOGS_CMD_LAYOUT(f, ...)                                  \
    f(utils.logsFollow, Local, Option, 0, ## __VA_ARGS__)

#define UTL_ASYNC_CMD_STATUS_CMD_LAYOUT(f, ...)                                \
    /* No specific options for async-cmd-status */

#define UTL_WAIT_FOR_CMD_LAYOUT(f, ...)                                        \
    f(utils.waitForTimeout, Local, Int, 0, ## __VA_ARGS__)                     \
    f(utils.waitForPeriod, Local, Int, 1, ## __VA_ARGS__)

#define UTL_WAIT_FOR_PORT_CMD_LAYOUT(f, ...)                                   \
    f(utils.waitForTimeout, Local, Int, 0, ## __VA_ARGS__)                     \
    f(utils.waitForPeriod, Local, Int, 1, ## __VA_ARGS__)

#define UTL_FIND_FREE_PORT_CMD_LAYOUT(f, ...)                                  \
    f(utils.portRangeStart, Local, Int, 0, ## __VA_ARGS__)                     \
    f(utils.portRangeEnd, Local, Int, 1, ## __VA_ARGS__)

#define UTL_ENV_CHECK_CMD_LAYOUT(f, ...)                                       \
    /* No specific options - vars are positional, set via getPositionalArray */

#define UTL_LOCK_CMD_LAYOUT(f, ...)                                            \
    f(utils.lockTimeout, Local, Int, 0, ## __VA_ARGS__)
// clang-format on

static void parseSpaceSeperatedList(DynArray *into,
                                    StrPool *strings,
                                    cstring str)
{
    if (str == NULL || str[0] == '\0')
        return;
    char *copy = (char *)makeString(strings, str);
    char *it = copy;
    do {
        while (*it && isspace(*it))
            it++;
        if (*it == '\0')
            break;
        pushStringOnDynArray(into, it);
        while (!isspace(*it) && *it)
            it++;
        if (*it == '\0')
            break;
        *it = '\0';
        it++;
    } while (true);
}

static void initializeOptions(StrPool *strings, Options *options)
{
    options->cflags = newDynArray(sizeof(char *));
    options->cDefines = newDynArray(sizeof(char *));
    options->librarySearchPaths = newDynArray(sizeof(char *));
    options->importSearchPaths = newDynArray(sizeof(char *));
    options->frameworkSearchPaths = newDynArray(sizeof(char *));
    options->libraries = newDynArray(sizeof(char *));
    options->defines = newDynArray(sizeof(CompilerDefine));
    options->operatingSystem = getenv("CXY_OS");
    bool isAlpine = options->operatingSystem &&
                    strcmp(options->operatingSystem, "__ALPINE__") == 0;
#if 0 // ifdef __APPLE__
    pushOnDynArray(&options->defines, &(CompilerDefine){"MACOS", "1"});
    FormatState state = newFormatState(NULL, true);
    exec("xcrun --show-sdk-path", &state);
    char *sdkPath = formatStateToString(&state);
    freeFormatState(&state);
    if (sdkPath && sdkPath[0] != '\0') {
        cstring trimmedSdkPath = makeTrimmedString(strings, sdkPath);
        pushStringOnDynArray(&options->cflags, "-isysroot");
        pushStringOnDynArray(&options->cflags, trimmedSdkPath);
        free(sdkPath);

        state = newFormatState(NULL, true);
        appendString(&state, trimmedSdkPath);
        appendString(&state, "/usr/include");
        char *sdkIncludeDir = formatStateToString(&state);
        freeFormatState(&state);
        pushStringOnDynArray(&options->importSearchPaths,
                             makeString(strings, sdkIncludeDir));
        free(sdkIncludeDir);
    }
#else
    FormatState state = newFormatState(NULL, true);
    exec("clang -E -v -x c /dev/null 2>&1 | grep -e \"^ /\" | cut -d \" \" "
         "-f 2 | xargs realpath",
         &state);
    char *includeDirs = formatStateToString(&state);
    const char *p = includeDirs;
    while (p && p[0] != '\0') {
        const char *end = strchr(p, '\n');
        if (isAlpine && strstr(p, "fortify") != NULL) {
            p = end ? end + 1 : NULL;
            continue;
        }

        if (end) {
            pushStringOnDynArray(&options->importSearchPaths,
                                 makeStringSized(strings, p, end - p));
            p = end + 1;
        }
        else {
            pushStringOnDynArray(&options->importSearchPaths,
                                 makeString(strings, p));
            p = NULL;
        }
    }
    free(includeDirs);
    freeFormatState(&state);
#ifdef __APPLE__
    pushOnDynArray(&options->defines, &(CompilerDefine){"MACOS", "1"});
    pushOnDynArray(&options->defines, &(CompilerDefine){"UNIX", "1"});
    pushStringOnDynArray(&options->cDefines, "-D_DARWIN_C_SOURCE");
#else
    pushStringOnDynArray(&options->cDefines, "-D_XOPEN_SOURCE=1");
    pushStringOnDynArray(&options->cDefines, "-D_DEFAULT_SOURCE");
    pushOnDynArray(&options->defines, &(CompilerDefine){"UNIX", "1"});
    if (options->operatingSystem != NULL) {
        pushOnDynArray(&options->defines,
                       &(CompilerDefine){options->operatingSystem, "1"});
        pushStringOnDynArray(
            &options->cDefines,
            makeStringConcat(strings, "-D", options->operatingSystem));
    }
#endif

    // Architecture detection
#if defined(__x86_64__) || defined(_M_X64)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_X86_64", "1"});
#elif defined(__i386__) || defined(_M_IX86)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_X86", "1"});
#elif defined(__aarch64__) || defined(_M_ARM64)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_ARM64", "1"});
#elif defined(__arm__) || defined(_M_ARM)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_ARM", "1"});
#elif defined(__riscv)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_RISCV", "1"});
#elif defined(__powerpc64__)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_PPC64", "1"});
#elif defined(__powerpc__)
    pushOnDynArray(&options->defines, &(CompilerDefine){"__ARCH_PPC", "1"});
#endif

#endif
}

static void fixCmdDevOptions(Options *options)
{
    if (options->dev.emitBitCode) {
        options->dev.emitAssembly = false;
        options->dev.printIR = false;
        options->dev.dumpMode = dmpNONE;
        options->dev.lastStage = ccsCompile;
    }
    else if (options->dev.emitAssembly) {
        options->dev.printIR = false;
        options->dev.dumpMode = dmpNONE;
        options->dev.lastStage = ccsCompile;
    }
    else if (options->dev.printIR) {
        options->dev.dumpMode = dmpNONE;
        options->dev.lastStage = ccsCodegen;
    }
    else if (options->dev.dumpMode != dmpNONE) {
        options->dev.lastStage = MIN(options->dev.lastStage, ccsLower);
    }
}

static void moveListOptions(DynArray *dst, DynArray *src)
{
    copyDynArray(dst, src);
    freeDynArray(src);
}

static int parsePackageCommand(
    int *argc, char **argv, StrPool *strings, Options *options, Log *log)
{
    // Define package subcommands
    InteractiveCommand(create,
            "Create a new Cxy package with scaffolding",
            Positionals(),
            Use(cmdValidatePackageName,
                Name("name"),
                Help("Package name (Must satisfy [_a-zA-Z][_\\-a-zA-Z0-9]*)"),
                Def("")),
            Str(Name("author"),
                Help("Author name and email"),
                Def("")),
            Str(Name("description"),
                Help("Short package description"),
                Def("")),
            Use(cmdValidateLicenseIdentifier,
                Name("license"),
                Help("License identifier (MIT, Apache-2.0, GPL-3.0, etc.)"),
                Def("MIT")),
            Use(cmdValidateSemanticVersion,
                Name("version"),
                Help("Initial version"),
                Def("0.1.0")),
            Str(Name("directory"),
                Sf('d'),
                Help("Target directory"),
                Def(".")),
            Opt(Name("bin"),
                Help("Create binary package (main.cxy instead of library)")));

    Command(add,
            "Add a dependency to the package",
            Positionals(Use(cmdValidateGitRepository,
                           Name("repository"),
                           Help("Git repository URL or package identifier"),
                           Def(""))),
            Use(cmdValidatePackageName,
                Name("name"),
                Help("Custom package name"),
                Def("")),
            Use(cmdValidateSemanticVersion,
                Name("constraint"),
                Help("Version constraint (default: *)"),
                Def("*")),
            Str(Name("tag"),
                Help("Specific Git tag"),
                Def("")),
            Str(Name("branch"),
                Help("Specific Git branch"),
                Def("")),
            Str(Name("path"),
                Help("Local filesystem path"),
                Def("")),
            Opt(Name("dev"),
                Help("Add as development dependency")),
            Opt(Name("no-install"),
                Help("Skip installation (validation only)")),
    );

    Command(remove,
            "Remove a dependency from the package",
            Positionals(Use(cmdArrayArgument,
                           Name("packages"),
                           Help("One or more package names to remove"),
                           Many())));

    Command(install,
            "Install all dependencies from Cxyfile.yaml",
            Positionals(),
            Opt(Name("dev"),
                Help("Include development dependencies")),
            Opt(Name("clean"),
                Help("Ignore lock file and perform clean resolution")),
            Opt(Name("verify"),
                Help("Verify integrity of installed packages")),
            Opt(Name("offline"),
                Help("Use only cached packages, no network access")),
            Opt(Name("frozen-lockfile"),
                Help("Fail if lock file is missing or outdated (CI mode)")),
            );

    Command(update,
            "Update dependencies to latest compatible versions",
            Positionals(Use(cmdArrayArgument,
                           Name("packages"),
                           Help("Optional: specific packages to update"),
                           Many())),
            Opt(Name("latest"),
                Help("Update to latest version, ignoring constraints")),
            Opt(Name("dry-run"),
                Help("Show what would be updated without changing files")),
            Opt(Name("dev"),
                Help("Include dev dependencies")));

    Command(build,
            "Build the package",
            Positionals(Str(Name("target"),
                           Help("Build target name (e.g., 'lib', 'bin')"),
                           Def("")),
                       PositionalRest()),
            Opt(Name("release"),
                Help("Build for release")),
            Opt(Name("debug"),
                Help("Build for debug")),
            Str(Name("build-dir"),
                Help("Output directory for build artifacts"),
                Def(".cxy/build")),
            Opt(Name("clean"),
                Help("Clean build artifacts before building")),
            Opt(Name("all"),
                Help("Build all targets")),
            Opt(Name("list"),
                Help("List available build targets")));

    Command(test,
            "Run package tests",
            Positionals(Use(cmdArrayArgument,
                           Name("test-files"),
                           Help("Optional: specific test files to run"),
                           Def("[]"),
                           Many()),
                        PositionalRest()),
            Str(Name("build-dir"),
                Help("Build directory for test binaries"),
                Def(".cxy/build")),
            Str(Name("filter"),
                Help("Run only tests matching pattern"),
                Def("")),
            Int(Name("parallel"),
                Sf('j'),
                Help("Run tests in parallel"),
                Def("1")));

    Command(publish,
            "Publish package by creating a Git tag",
            Positionals(),
            Str(Name("bump"),
                Help("Bump version before publishing (major|minor|patch)"),
                Def("")),
            Str(Name("tag"),
                Help("Custom tag name"),
                Def("")),
            Str(Name("message"),
                Sf('m'),
                Help("Tag annotation message"),
                Def("")),
            Opt(Name("dry-run"),
                Help("Show what would be published")));

    Command(list,
            "List all installed dependencies",
            Positionals());

    Command(info,
            "Show information about a package",
            Positionals(Str(Name("package"),
                           Help("Package name to show info for"),
                           Def(""))),
            Opt(Name("json"),
                Help("Output in JSON format")));

    Command(clean,
            "Clean package cache and build artifacts",
            Positionals(),
            Opt(Name("cache"),
                Help("Clean package cache (.cxy/packages)")),
            Opt(Name("build"),
                Help("Clean build directory")),
            Opt(Name("all"),
                Help("Clean everything (cache + build)")),
            Opt(Name("force"),
                Sf('f'),
                Help("Skip confirmation prompts")));

    Command(run,
            "Run a script defined in Cxyfile.yaml",
            Positionals(Str(Name("script"),
                           Help("Script name to execute"),
                           Def("")),
                       PositionalRest()),
            Opt(Name("list"),
                Help("List all available scripts")),
            Opt(Name("no-cache"),
                Help("Disable script caching (force re-run)")));

    Command(find_system,
            "Find system packages and output build configuration",
            Positionals(Str(Name("package"),
                           Help("Package name to find (e.g., openssl, postgresql)"),
                           Def(""))),
            Str(Name("format"),
                Help("Output format: flags (default), json, yaml"),
                Def("flags")),
            Use(cmdArrayArgument,
                Name("search-root"),
                Help("Additional search root directory (repeatable)"),
                Def("[]")),
            Opt(Name("include-dir"),
                Help("Output include/header directories")),
            Opt(Name("lib-dir"),
                Help("Output library directories")),
            Opt(Name("lib"),
                Help("Output library names to link")),
            Opt(Name("version"),
                Help("Output package version")),
            Opt(Name("cflags"),
                Help("Output compiler flags")),
            Opt(Name("ldflags"),
                Help("Output linker flags")));
    
    // Override the command name to use hyphen instead of underscore
    find_system.meta.name = "find-system";

    Parser("cxy package",
           CXY_VERSION,
           PACKAGE_SUBCOMMANDS,
           DefaultCmd(help),
           DisableVersionOpt(),
           Str(Name("cxyfile"),
               Help("Path to Cxyfile.yaml (default: searches current and parent directories)"),
               Def("")),
           Str(Name("packages-dir"),
               Help("Directory for installed packages"),
               Def(".cxy/packages")),
           Opt(Name("quiet"),
               Sf('q'),
               Help("Suppress non-error output")),
           Opt(Name("verbose"),
               Help("Enable verbose output")));
    SubParser(P);

    int selected = argparse(argc, &argv, parser);
    CmdCommand *cmd = parser.cmds[selected];

    // Load global package options
    UnloadCmd(cmd, options, PACKAGE_CMD_LAYOUT);

    // Dispatch to subcommand
    if (cmd->id == CMD_create) {
        options->package.subcmd = pkgSubCreate;
        UnloadCmd(cmd, options, PKG_CREATE_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_add) {
        options->package.subcmd = pkgSubAdd;
        if (hasPositional(cmd, 0)) {
            options->package.repository = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, PKG_ADD_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_remove) {
        options->package.subcmd = pkgSubRemove;
        if (hasPositional(cmd, 0)) {
            options->package.packages = getPositionalArray(cmd, 0);
        }
        UnloadCmd(cmd, options, PKG_REMOVE_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_install) {
        options->package.subcmd = pkgSubInstall;
        UnloadCmd(cmd, options, PKG_INSTALL_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_update) {
        options->package.subcmd = pkgSubUpdate;
        options->package.packages = getPositionalArray(cmd, 0);
        UnloadCmd(cmd, options, PKG_UPDATE_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_test) {
        options->package.subcmd = pkgSubTest;
        if (hasPositional(cmd, 0)) {
            options->package.testFiles = getPositionalArray(cmd, 0);
        }
        if (hasPositional(cmd, 1)) {
            options->package.rest = getPositionalArray(cmd, 1);
        }
        UnloadCmd(cmd, options, PKG_TEST_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_build) {
        options->package.subcmd = pkgSubBuild;
        if (hasPositional(cmd, 0)) {
            options->package.buildTarget = getPositionalString(cmd, 0);
        }
        if (hasPositional(cmd, 1)) {
            options->package.rest = getPositionalArray(cmd, 1);
        }
        UnloadCmd(cmd, options, PKG_BUILD_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_publish) {
        options->package.subcmd = pkgSubPublish;
        UnloadCmd(cmd, options, PKG_PUBLISH_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_list) {
        options->package.subcmd = pkgSubList;
        UnloadCmd(cmd, options, PKG_LIST_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_info) {
        options->package.subcmd = pkgSubInfo;
        if (hasPositional(cmd, 0)) {
            options->package.package = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, PKG_INFO_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_clean) {
        options->package.subcmd = pkgSubClean;
        UnloadCmd(cmd, options, PKG_CLEAN_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_run) {
        options->package.subcmd = pkgSubRun;
        if (hasPositional(cmd, 0)) {
            options->package.scriptName = getPositionalString(cmd, 0);
        }
        if (hasPositional(cmd, 1)) {
            options->package.rest = getPositionalArray(cmd, 1);
        }
        UnloadCmd(cmd, options, PKG_RUN_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_find_system) {
        options->package.subcmd = pkgSubFindSystem;
        if (hasPositional(cmd, 0)) {
            options->package.findSystemPackage = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, PKG_FIND_SYSTEM_CMD_LAYOUT);
    }
    return cmdPackage;
    CMD_parse_error:
    return -1;
}

static int parsePackageCommandFwd(void *ctx, int argc, char **argv)
{
    ParseContext *pctx = (ParseContext *)ctx;
    return parsePackageCommand(&argc, argv, pctx->strings, pctx->options, pctx->log);
}

static int parseUtilsCommand(
    int *argc, char **argv, StrPool *strings, Options *options, Log *log)
{
    Command(async_cmd_status,
            "Check whether a background command is still running",
            Positionals(Str(Name("pid"),
                           Help("Process ID to check"),
                           Def(""))));

    async_cmd_status.meta.name = "async-cmd-status";

    Command(async_cmd_logs,
            "Print (or follow) the captured log output of a background command",
            Positionals(Str(Name("pid"),
                           Help("Process ID whose log to read"),
                           Def(""))),
            Opt(Name("follow"),
                Sf('f'),
                Help("Follow log output as the process writes it (like tail -f)")));

    async_cmd_logs.meta.name = "async-cmd-logs";

    Command(async_cmd_start,
            "Start a background command (for use within scripts)",
            Positionals(Str(Name("cmd"),
                           Help("Command to run in background (e.g. \"./server --port 8080\")"),
                           Def(""))),
            Opt(Name("capture"),
                Help("Capture output to log file instead of terminal")));

    async_cmd_start.meta.name = "async-cmd-start";

    Command(async_cmd_stop,
            "Stop a background command by PID",
            Positionals(Str(Name("pid"),
                           Help("Process ID to stop"),
                           Def(""))));

    async_cmd_stop.meta.name = "async-cmd-stop";

    Command(wait_for,
            "Poll a command until it exits 0 or timeout is reached",
            Positionals(Str(Name("cmd"),
                           Help("Command to poll (e.g. \"curl 0.0.0.0:80\")"),
                           Def(""))),
            Int(Name("timeout"),
                Help("Total timeout in milliseconds"),
                Def("30000")),
            Int(Name("period"),
                Help("Poll interval in milliseconds"),
                Def("500")));

    wait_for.meta.name = "wait-for";

    Command(wait_for_port,
            "Wait until a TCP port is open and accepting connections",
            Positionals(Int(Name("port"),
                           Help("Port number to wait for"),
                           Def("0"))),
            Int(Name("timeout"),
                Help("Total timeout in milliseconds"),
                Def("30000")),
            Int(Name("period"),
                Help("Poll interval in milliseconds"),
                Def("500")));

    wait_for_port.meta.name = "wait-for-port";

    Command(find_free_port,
            "Find and print an available TCP port",
            Positionals(),
            Int(Name("range-start"),
                Help("Start of port range to search"),
                Def("8000")),
            Int(Name("range-end"),
                Help("End of port range to search"),
                Def("9000")));

    find_free_port.meta.name = "find-free-port";

    Command(env_check,
            "Assert that required environment variables are set",
            Positionals(Use(cmdArrayArgument,
                           Name("vars"),
                           Help("Environment variable names to check"),
                           Many())));

    env_check.meta.name = "env-check";

    Command(lock,
            "Run a command while holding a named lock file, preventing concurrent execution",
            Positionals(Str(Name("name"),
                           Help("Lock name (e.g. \"db-migrate\")"),
                           Def("")),
                        Str(Name("cmd"),
                           Help("Command to run while holding the lock"),
                           Def(""))),
            Int(Name("timeout"),
                Help("Timeout waiting for lock in milliseconds (0 = fail immediately)"),
                Def("30000")));

    lock.meta.name = "lock";

    Parser("cxy utils",
           CXY_VERSION,
           UTILS_SUBCOMMANDS,
           DefaultCmd(help),
           DisableVersionOpt(),
           Opt(Name("quiet"),
               Sf('q'),
               Help("Suppress non-error output")),
           Opt(Name("verbose"),
               Help("Enable verbose output")));
    SubParser(P);

    int selected = argparse(argc, &argv, parser);
    CmdCommand *cmd = parser.cmds[selected];

    if (cmd->id == CMD_async_cmd_start) {
        options->utils.subcmd = utlSubAsyncCmdStart;
        if (hasPositional(cmd, 0)) {
            options->utils.cmd = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_ASYNC_CMD_START_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_async_cmd_stop) {
        options->utils.subcmd = utlSubAsyncCmdStop;
        if (hasPositional(cmd, 0)) {
            options->utils.cmd = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_ASYNC_CMD_STOP_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_async_cmd_logs) {
        options->utils.subcmd = utlSubAsyncCmdLogs;
        if (hasPositional(cmd, 0)) {
            options->utils.cmd = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_ASYNC_CMD_LOGS_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_async_cmd_status) {
        options->utils.subcmd = utlSubAsyncCmdStatus;
        if (hasPositional(cmd, 0)) {
            options->utils.cmd = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_ASYNC_CMD_STATUS_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_wait_for) {
        options->utils.subcmd = utlSubWaitFor;
        if (hasPositional(cmd, 0)) {
            options->utils.cmd = getPositionalString(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_WAIT_FOR_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_wait_for_port) {
        options->utils.subcmd = utlSubWaitForPort;
        if (hasPositional(cmd, 0)) {
            options->utils.port = getPositionalInt(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_WAIT_FOR_PORT_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_find_free_port) {
        options->utils.subcmd = utlSubFindFreePort;
        UnloadCmd(cmd, options, UTL_FIND_FREE_PORT_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_env_check) {
        options->utils.subcmd = utlSubEnvCheck;
        if (hasPositional(cmd, 0)) {
            options->utils.envVars = getPositionalArray(cmd, 0);
        }
        UnloadCmd(cmd, options, UTL_ENV_CHECK_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_lock) {
        options->utils.subcmd = utlSubLock;
        if (hasPositional(cmd, 0)) {
            options->utils.lockName = getPositionalString(cmd, 0);
        }
        if (hasPositional(cmd, 1)) {
            options->utils.cmd = getPositionalString(cmd, 1);
        }
        UnloadCmd(cmd, options, UTL_LOCK_CMD_LAYOUT);
    }

    return cmdUtils;
    CMD_parse_error:
    return -1;
}

static int parseUtilsCommandFwd(void *ctx, int argc, char **argv)
{
    ParseContext *pctx = (ParseContext *)ctx;
    return parseUtilsCommand(&argc, argv, pctx->strings, pctx->options, pctx->log);
}

bool parseCommandLineOptions(
    int *argc, char **argv, StrPool *strings, Options *options, Log *log)
{
    bool status = true;
    int file_count = 0;
    initializeOptions(strings, options);
    ParseContext ctx = { options, strings, log };

    Parser(
        "cxy",
        CXY_VERSION " (build: " CXY_BUILD_ID ", " __DATE__ ", " __TIME__ ")",
        BUILD_COMMANDS,
        DefaultCmd(dev),
        VersionOpt(),
        Int(Name("max-errors"),
            Help(
                "Set the maximum number of errors incurred before the compiler "
                "aborts"),
            Def("10")),
        Str(Name("warnings"),
            Help("Sets a list of enabled/disabled compiler warning (eg "
                 "'~Warning' disables a flag, 'Warn1|~Warn2' combines flag "
                 "configurations))"),
            Def("")),
        Opt(Name("warnings-all"), Help("enables all compiler warnings")),
        Opt(Name("no-color"),
            Help("disable colored output when formatting outputs")),
        Opt(Name("without-builtins"),
            Help("Disable building builtins (does not for build command)")),
        Use(cmdArrayArgument,
            Name("cflags"),
            Help("C compiler flags to add to the c compiler when importing C "
                 "files"),
            Def("[]")),
        Opt(Name("no-pie"), Help("disable position independent code")),
        Use(cmdOptimizationLevel,
            Name("optimization"),
            Sf('O'),
            Help("Code optimization level, valid values '0', 'd', '1', '2', "
                 "'3', 's'"),
            Def("d")),
        Use(cmdCompilerDefine,
            Name("define"),
            Sf('D'),
            Help("Adds a compiler definition, e.g -DDISABLE_ASSERT, "
                 "-DAPP_VERSION=\\\"0.0.1\\\""),
            Def("[]")),
        Opt(Name("without-mm"),
            Help("Compile program without builtin (RC) memory manager")),
        Use(cmdArrayArgument,
            Name("c-define"),
            Help("Adds a compiler definition that will be parsed to the C "
                 "importer"),
            Def("[]")),
        Use(cmdArrayArgument,
            Name("c-header-dir"),
            Help("Adds a directory to search for C header files"),
            Def("[]")),
        Use(cmdArrayArgument,
            Name("c-lib-dir"),
            Help("Adds a directory to search for C libraries"),
            Def("[]")),
        Use(cmdArrayArgument,
            Name("c-lib"),
            Help("Adds library to link against"),
            Def("[]")),
        Opt(Sf('g'),
            Name("debug"),
            Help("Produce debug information for the program")),
        Opt(Name("debug-pass-manager"),
            Help("Print out information about LLVM executed passes"),
            Def("false")),
        Str(Name("passes"),
            Help("Describes a list of LLVM passes making up the pipeline"),
            Def("")),
        Use(cmdArrayArgument,
            Name("load-pass-plugin"),
            Help("Loads passes from the plugin library"),
            Def("[]")),
        Use(cmdParseDumpStatsModes,
            Name("dump-stats"),
            Help("Dump compilation statistics to console after compilation "
                 "values: NONE|SUMMARY|FULL"),
            Def("SUMMARY")),
        Opt(Name("no-progress"),
            Help("Do not print progress messages during compilation")),
        Str(Name("stdlib"),
            Help("root directory where cxy standard library is installed"),
            Def("")),
        Str(Name("plugins-dir"),
            Help("The directory where plugins are installed or where to "
                 "install them"),
            Def("./plugins")),
        Str(Name("deps-dir"),
            Help("Directory containing installed package dependencies"),
            Def(".cxy/packages")));

    CustomParser(package, parsePackageCommandFwd, &ctx);
    CustomParser(utils, parseUtilsCommandFwd, &ctx);

    int selected = argparse(argc, &argv, parser);
    if (selected == CMD_package) {
        options->cmd = cmdPackage;
        return true;
    }
    if (selected == CMD_utils) {
        options->cmd = cmdUtils;
        return true;
    }

    CmdCommand *cmd = parser.cmds[selected];
    log->maxErrors = getGlobalInt(cmd, 0);
    log->ignoreStyles = getGlobalOption(cmd, 3);

    if (getGlobalOption(cmd, 2))
        log->enabledWarnings.num = wrnAll;
    log->enabledWarnings.str = getGlobalString(cmd, 1);
    log->enabledWarnings.num =
        parseWarningLevels(log, log->enabledWarnings.str);
    if (log->enabledWarnings.num & wrn_Error)
        return false;

    options->withoutBuiltins = getGlobalOption(cmd, 4);
    if (cmd->id == CMD_dev) {
        options->cmd = cmdDev;
        options->sources = getPositionalArray(cmd, 0);
        UnloadCmd(cmd, options, DEV_CMD_LAYOUT);
        fixCmdDevOptions(options);
    }
    else if (cmd->id == CMD_build) {
        options->cmd = cmdBuild;
        options->sources = getPositionalArray(cmd, 0);
        UnloadCmd(cmd, options, BUILD_CMD_LAYOUT);
        if (options->build.plugin) {
            options->buildPlugin = true;
            if (options->output != NULL && options->output[0] == '/') {
                logError(
                    log,
                    NULL,
                    "Plugin output '{s}' must not be an absolute path, please"
                    " use path relative to plugins directory.",
                    (FormatArg[]){{.s = options->output}});
                return false;
            }
        }
    }
    else if (cmd->id == CMD_test) {
        options->cmd = cmdTest;
        options->sources = getPositionalArray(cmd, 0);
        UnloadCmd(cmd, options, TEST_CMD_LAYOUT);
    }
    else if (cmd->id == CMD_package) {
        // This should not be reached due to early interception
        logError(log, NULL, "package command should be intercepted earlier", NULL);
        return false;
    }

    moveListOptions(&options->cflags, &getGlobalArray(cmd, 5));
    options->noPIE = getGlobalOption(cmd, 6);
    options->optimizationLevel = getGlobalInt(cmd, 7);
    moveListOptions(&options->defines, &getGlobalArray(cmd, 8));
    options->withMemoryManager = !getGlobalBool(cmd, 9);
    moveListOptions(&options->cDefines, &getGlobalArray(cmd, 10));
    moveListOptions(&options->importSearchPaths, &getGlobalArray(cmd, 11));
    moveListOptions(&options->librarySearchPaths, &getGlobalArray(cmd, 12));
    moveListOptions(&options->libraries, &getGlobalArray(cmd, 13));
    options->debug = getGlobalBool(cmd, 14);
    options->debugPassManager = getGlobalBool(cmd, 15);
    options->passes = getGlobalString(cmd, 16);
    moveListOptions(&options->loadPassPlugins, &getGlobalArray(cmd, 17));
    options->dsmMode = getGlobalInt(cmd, 18);
    log->progress = !getGlobalOption(cmd, 19);
    options->libDir = getGlobalString(cmd, 20);
    options->pluginsDir = getGlobalString(cmd, 21);
    options->depsDir = getGlobalString(cmd, 22);

    if (options->libDir == NULL) {
        options->libDir = makeString(strings, getenv("CXY_STDLIB_DIR"));
    }
    cstring cxyRoot = getenv("CXY_ROOT");
    if (options->libDir == NULL && cxyRoot != NULL) {
        options->libDir = makeStringConcat(strings, cxyRoot, "/lib/cxy/std");
    }

    if (options->pluginsDir == NULL) {
        if (options->buildDir) {
            options->pluginsDir =
                makeStringConcat(strings, options->buildDir, "/plugins");
        }
        else {
            options->pluginsDir = makeString(strings, "./plugins");
        }
    }

    file_count = *argc - 1;

    if (dynArrayEmpty(&options->sources)) {
        logError(log,
                 NULL,
                 "no input file, run with '--help' to display usage",
                 NULL);
        status = false;
    }
    goto exit;

CMD_parse_error:
    logError(log, NULL, P->error, NULL);
    status = false;
exit:
    *argc = file_count + 1;
    return status;
}

void deinitCommandLineOptions(Options *options)
{
    freeDynArray(&options->cflags);
    freeDynArray(&options->cDefines);
    freeDynArray(&options->frameworkSearchPaths);
    freeDynArray(&options->importSearchPaths);
    freeDynArray(&options->librarySearchPaths);
    freeDynArray(&options->libraries);
    freeDynArray(&options->defines);
    freeDynArray(&options->package.packages);
    freeDynArray(&options->package.testFiles);
    freeDynArray(&options->package.rest);
    freeDynArray(&options->package.findSystemSearchRoots);
}
