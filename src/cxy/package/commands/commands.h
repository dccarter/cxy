/**
 * commands.h
 *
 * Declarations for package subcommand handlers and dispatcher.
 *
 * This header is used by package command implementations and the CLI
 * dispatcher to invoke package subcommands (create, add, install, ...).
 *
 * The implementations are intentionally minimal for Phase 2 (basic commands).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/options.h" /* Options definition (contains package subcommand data) */
#include <stdbool.h>

/* Forward declarations to avoid including heavy headers here. */
typedef struct Log Log;
struct StrPool;

/**
 * Command handler signature for package commands.
 *
 * @param options Parsed command-line options (contains subcommand payload).
 * @param strings  String pool for allocating temporary/permanent strings.
 * @param log      Logger for reporting info/errors.
 * @return true on success, false on error.
 */
typedef bool (*PackageCommandHandler)(const Options *options, struct StrPool *strings, Log *log);

/* Individual package command handlers (Phase 2: implement `create`, stubs for others) */

/* Create a new package with interactive scaffolding or provided flags. */
bool packageCreateCommand(const Options *options, struct StrPool *strings, Log *log);

/* Add a dependency to the current package (updates Cxyfile.yaml). */
bool packageAddCommand(const Options *options, struct StrPool *strings, Log *log);

/* Remove one or more dependencies from the current package. */
bool packageRemoveCommand(const Options *options, struct StrPool *strings, Log *log);

/* Install dependencies defined in Cxyfile.yaml into the packages directory. */
bool packageInstallCommand(const Options *options, struct StrPool *strings, Log *log);

/* Update specified packages to newer versions. */
bool packageUpdateCommand(const Options *options, struct StrPool *strings, Log *log);

/* Build the package using configuration from Cxyfile.yaml. */
bool packageBuildCommand(const Options *options, struct StrPool *strings, Log *log);

/* Run package tests (discover tests from Cxyfile or inline tests). */
bool packageTestCommand(const Options *options, struct StrPool *strings, Log *log);

/* Publish package (MVP may only create a git tag; registry publishing is future work). */
bool packagePublishCommand(const Options *options, struct StrPool *strings, Log *log);

/* List installed dependencies in the local package cache. */
bool packageListCommand(const Options *options, struct StrPool *strings, Log *log);

/* Show information about a package (local or remote). */
bool packageInfoCommand(const Options *options, struct StrPool *strings, Log *log);

/* Clean package artifacts and cached packages. */
bool packageCleanCommand(const Options *options, struct StrPool *strings, Log *log);

/* Run a script defined in Cxyfile.yaml with dependency resolution. */
bool packageRunCommand(const Options *options, struct StrPool *strings, Log *log);

/* Find system packages and output build configuration flags. */
bool packageFindSystemCommand(const Options *options, struct StrPool *strings, Log *log);



/**
 * Dispatch the parsed package command to the corresponding handler.
 *
 * This examines `options->package.subcmd` and calls the appropriate
 * function above. Returns the handler's boolean result.
 */
bool dispatchPackageCommand(const Options *options, struct StrPool *strings, Log *log);

#ifdef __cplusplus
}
#endif