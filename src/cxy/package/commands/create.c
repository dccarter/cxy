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
#include "package/gitops.h"
#include "core/log.h"
#include "core/format.h"
#include "core/utils.h"
#include "core/alloc.h"
#include "core/strpool.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>

static const char *mitLicenseText =
    "MIT License\n"
    "\n"
    "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
    "of this software and associated documentation files (the \"Software\"), to deal\n"
    "in the Software without restriction, including without limitation the rights\n"
    "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
    "copies of the Software, and to permit persons to whom the Software is\n"
    "furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in all\n"
    "copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
    "SOFTWARE.\n";

static const char *gitIgnoreText =
    "# Generated C files\n"
    "app.c\n"
    "\n"
    "# Build artifacts\n"
    "/build/\n"
    "*.o\n"
    "*.so\n"
    "*.dylib\n"
    "*.dll\n"
    "*.a\n"
    "\n"
    "# Editor files\n"
    ".vscode/\n"
    ".idea/\n"
    ".cache/\n"
    "*.swp\n"
    "*.swo\n"
    "*~\n"
    "\n"
    "# OS files\n"
    ".DS_Store\n"
    "Thumbs.db\n"
    "\n"
    "# Package manager\n"
    "Cxyfile.lock\n"
    ".cxy/\n"
    ".build/\n";

/**
 * Sanitize package name for use as a module name
 * Converts hyphens to underscores (snake_case)
 */
static cstring sanitizeModuleName(const char *packageName, StrPool *strings)
{
    size_t len = strlen(packageName);
    char *sanitized = mallocOrDie(len + 1);

    for (size_t i = 0; i < len; i++) {
        if (packageName[i] == '-') {
            sanitized[i] = '_';
        } else {
            sanitized[i] = packageName[i];
        }
    }
    sanitized[len] = '\0';

    cstring tmp = makeString(strings, sanitized);
    free(sanitized);
    return tmp;
}

bool packageCreateCommand(const Options *options, struct StrPool *strings, Log *log)
{
    const char *packageName = options->package.name;
    const char *author = options->package.author;
    const char *description = options->package.description;
    const char *license = options->package.license;
    const char *version = options->package.version;
    const char *directory = options->package.directory;
    bool isBinary = options->package.bin;

    printStatus(log, "Creating %s package '%s'...",
                isBinary ? "binary" : "library", packageName);

    // Create and change to target directory
    if (strcmp(directory, ".") != 0) {
        if (!makeDirectory(directory, true)) {
            logError(log, NULL, "failed to create directory '{s}': {s}",
                     (FormatArg[]){{.s = directory}, {.s = strerror(errno)}});
            return false;
        }
        if (chdir(directory) != 0) {
            logError(log, NULL, "failed to change to directory '{s}': {s}",
                     (FormatArg[]){{.s = directory}, {.s = strerror(errno)}});
            return false;
        }
    }

    // Check if directory is empty
    if (!isDirectoryEmpty(".")) {
        logError(log, NULL, "directory is not empty, cannot create package here", NULL);
        return false;
    }

    // Create src directory (needed for both library and binary packages)
    if (!makeDirectory("src", false)) {
        logError(log, NULL, "failed to create directory 'src': {s}",
                 (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    // Build and write Cxyfile.yaml
    FILE *cxyfile = fopen("Cxyfile.yaml", "w");
    if (!cxyfile) {
        logError(log, NULL, "failed to create Cxyfile.yaml: {s}",
                 (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    FormatState cxyfileState = newFormatState(NULL, true);
    format(&cxyfileState, "package:\n  name: {s}\n  version: {s}\n",
           (FormatArg[]){{.s = packageName}, {.s = version}});

    if (author && author[0] != '\0') {
        format(&cxyfileState, "  author: {s}\n", (FormatArg[]){{.s = author}});
    }
    if (description && description[0] != '\0') {
        format(&cxyfileState, "  description: {s}\n", (FormatArg[]){{.s = description}});
    }

    format(&cxyfileState, "  license: {s}\n\n", (FormatArg[]){{.s = license}});

    if (isBinary) {
        format(&cxyfileState,
            "builds:\n"
            "  app:\n"
            "    entry: main.cxy\n"
            "    output: app\n"
            "\n", NULL);
    }

    format(&cxyfileState,
        "dependencies: []\n"
        "\n"
        "tests:\n"
        "  - file: \"src/**/*.cxy\"\n",
        NULL);

    writeFormatState(&cxyfileState, cxyfile);
    fclose(cxyfile);
    freeFormatState(&cxyfileState);

    // Sanitize package name for module (replace hyphens with underscores)
    cstring moduleName = sanitizeModuleName(packageName, strings);

    // Build and write src/hello.cxy
    FILE *helloFile = fopen("src/hello.cxy", "w");
    if (!helloFile) {
        logError(log, NULL, "failed to create src/hello.cxy: {s}",
                 (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    FormatState helloState = newFormatState(NULL, true);
    format(&helloState,
           "module hello\n"
           "\n"
           "pub func hello(name: string) {{\n"
           "  return f\"Hello, {{name}!\"\n"
           "}\n"
           "\n"
           "test \"hello test\" {{\n"
           "  ok!(hello(\"World\") == \"Hello, World!\")\n"
           "}\n",
           NULL);

    writeFormatState(&helloState, helloFile);
    fclose(helloFile);
    freeFormatState(&helloState);

    if (isBinary) {
        // Build and write main.cxy for binary package
        FILE *mainFile = fopen("main.cxy", "w");
        if (!mainFile) {
            logError(log, NULL, "failed to create main.cxy: {s}",
                     (FormatArg[]){{.s = strerror(errno)}});
            return false;
        }

        FormatState mainState = newFormatState(NULL, true);
        format(&mainState,
               "import {{ hello } from \"./src/hello.cxy\"\n"
               "\n"
               "func main() {{\n"
               "  println(hello(\"World\"))\n"
               "}\n",
               NULL);

        writeFormatState(&mainState, mainFile);
        fclose(mainFile);
        freeFormatState(&mainState);
    } else {
        // Build and write index.cxy for library package
        FILE *indexFile = fopen("index.cxy", "w");
        if (!indexFile) {
            logError(log, NULL, "failed to create index.cxy: {s}",
                     (FormatArg[]){{.s = strerror(errno)}});
            return false;
        }

        FormatState indexState = newFormatState(NULL, true);
        format(&indexState,
               "package {s}\n"
               "\n"
               "export {{ hello } from \"./src/hello.cxy\"\n",
               (FormatArg[]){{.s = moduleName}});

        writeFormatState(&indexState, indexFile);
        fclose(indexFile);
        freeFormatState(&indexState);
    }

    // Build and write README.md
    FILE *readmeFile = fopen("README.md", "w");
    if (!readmeFile) {
        logError(log, NULL, "failed to create README.md: {s}",
                 (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    FormatState readmeState = newFormatState(NULL, true);
    format(&readmeState, "# {s}\n"
           "\n"
           "{s}\n",
           (FormatArg[]){{.s = packageName},
                         {.s = (description && description[0]) ? description : (isBinary ? "A Cxy binary" : "A Cxy library")}});

    if (isBinary) {
        format(&readmeState,
               "\n## Building\n"
               "\n"
               "```bash\n"
               "cxy package build\n"
               "```\n"
               "\n"
               "## Running\n"
               "\n"
               "```bash\n"
               "./app\n"
               "```\n",
               NULL);
    } else {
        format(&readmeState,
               "\n## Usage\n"
               "\n"
               "Import this library in your Cxy project:\n"
               "\n"
               "```cxy\n"
               "import {{ hello } from \"@{s}\"\n"
               "\n"
               "func main() {{\n"
               "  println(hello(\"World\"))\n"
               "}\n"
               "```\n",
               (FormatArg[]){{.s = packageName}});
    }

    format(&readmeState,
           "\n## Testing\n"
           "\n"
           "```bash\n"
           "cxy package test\n"
           "```\n",
           NULL);

    writeFormatState(&readmeState, readmeFile);
    fclose(readmeFile);
    freeFormatState(&readmeState);

    // Write LICENSE if MIT
    if (strcmp(license, "MIT") == 0) {
        if (!writeToFile("LICENSE", mitLicenseText, strlen(mitLicenseText))) {
            logError(log, NULL, "failed to create LICENSE: {s}",
                     (FormatArg[]){{.s = strerror(errno)}});
            return false;
        }
    }

    // Create .gitignore
    if (!writeToFile(".gitignore", gitIgnoreText, strlen(gitIgnoreText))) {
        logError(log, NULL, "failed to create .gitignore: {s}",
                 (FormatArg[]){{.s = strerror(errno)}});
        return false;
    }

    // Initialize git repository if not already in one
    if (!gitIsRepository(".")) {
        printStatus(log, "Initializing git repository...");

        // Initialize git repository
        if (system("git init -q") != 0) {
            logWarning(log, NULL, "failed to initialize git repository", NULL);
        } else {
            // Stage all files
            if (system("git add .") != 0) {
                logWarning(log, NULL, "failed to stage files", NULL);
            } else {
                // Create initial commit
                if (system("git commit -q -m \"Initial commit\"") != 0) {
                    logWarning(log, NULL, "failed to create initial commit", NULL);
                }
            }
        }
    }

    printStatusAlways(log, cBGRN "✔" cDEF " Package '%s' created successfully\n", packageName);

    return true;
}
