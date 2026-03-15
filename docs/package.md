# Package Management

The Cxy package manager provides a complete solution for managing dependencies, building projects, running tests, and executing development scripts. It handles dependency resolution, version locking, and reproducible builds through declarative configuration.

## Overview

The package manager provides comprehensive project lifecycle management:

- **Project scaffolding** - Create new library or binary packages with standard layouts
- **Dependency management** - Add, remove, and install dependencies from git repositories
- **Version resolution** - Automatic resolution of semantic version constraints
- **Reproducible builds** - Lock files ensure consistent dependency versions across environments
- **Build configuration** - Declarative build settings with multiple build targets
- **Test execution** - Discover and run tests with parallel execution support
- **Script runner** - Define and execute custom development workflows with dependency resolution
- **Script caching** - Incremental builds based on input/output file timestamps
- **Package inspection** - View information about installed and remote packages

## Getting Started

Create a new library package and add dependencies:

```bash
# Create a new library package
cxy package create --name my-library

# Add a dependency
cxy package add https://github.com/cxy-lang/json.git

# Install all dependencies
cxy package install

# Run tests
cxy package test
```

This creates a `Cxyfile.yaml` that defines your package, installs dependencies to `.cxy/packages/`, and generates a `Cxyfile.lock` for reproducible builds.

## The Cxyfile

The `Cxyfile.yaml` is the central manifest for your package. It defines package metadata, dependencies, build configuration, tests, and development scripts.

### Minimal Example

A minimal library package requires only basic metadata:

```yaml
package:
  name: my-library
  version: 0.1.0
  license: MIT

dependencies: []

tests:
  - file: "src/**/*.cxy"
```

### Complete Example

A full-featured package with dependencies, multiple builds, and scripts:

```yaml
package:
  name: json-parser
  version: 2.1.0
  description: A fast JSON parser for Cxy
  author: John Doe <john@example.com>
  license: MIT
  repository: https://github.com/username/json-parser
  homepage: https://json-parser.example.com

dependencies:
  - name: string-utils
    repository: https://github.com/cxy-lang/string-utils.git
    version: "^1.2.0"
  - name: memory-pool
    repository: https://github.com/cxy-lang/memory.git
    version: "~2.0.5"
    tag: v2.0.5

dev-dependencies:
  - name: test-framework
    repository: https://github.com/cxy-lang/test.git
    version: "^3.0.0"

builds:
  lib:
    default: true
    entry: src/lib.cxy
    output: build/json.so
    c-libs:
      - pthread
      - dl
  
  cli:
    entry: src/cli.cxy
    output: build/json-cli

tests:
  - file: "tests/**/*.cxy"
  - file: "tests/benchmarks.cxy"
    args: ["--iterations", "1000"]

scripts:
  build: cxy build src/lib.cxy -o build/json.so
  test:
    depends: [build]
    command: cxy package test
  bench:
    depends: [build]
    command: ./build/benchmarks
  clean: rm -rf build/
```

### Package Metadata

The `package` section defines basic information about your package:

- `name` - Package identifier used in imports and dependency resolution (required)
- `version` - Semantic version number (required)
- `description` - Brief description of the package's purpose
- `author` - Package maintainer with optional email
- `license` - Software license (MIT, Apache-2.0, GPL-3.0, etc.)
- `repository` - Git repository URL for source code
- `homepage` - Documentation or project website URL

### Dependencies and Dev Dependencies

Dependencies are external packages your code imports and uses. Dependencies are split into two categories:

- `dependencies` - Required packages for production use
- `dev-dependencies` - Packages only needed during development (tests, benchmarks, tooling)

Each dependency specifies:

- `name` - Package name used in import statements (required)
- `repository` - Git repository URL (required for remote packages)
- `version` - Semantic version constraint (default: `*` for any version)
- `tag` - Specific git tag to use (overrides version constraint)
- `branch` - Specific git branch to use (for development)
- `path` - Local filesystem path (for local development)

### Version Constraints

Version constraints follow semantic versioning:

- `*` - Any version
- `1.2.3` - Exact version match
- `^1.2.3` - Compatible versions (1.2.3 to <2.0.0)
- `~1.2.3` - Patch versions (1.2.3 to <1.3.0)
- `>=1.2.3` - Minimum version
- `>1.2.3 <2.0.0` - Range (not yet implemented)

The caret constraint `^1.2.3` allows updates that do not change the leftmost non-zero digit. The tilde constraint `~1.2.3` allows patch-level updates only.

### Build Configuration

The `build` section (single build) or `builds` section (multiple builds) defines how to compile your package:

**Single Build:**
```yaml
build:
  entry: src/main.cxy
  output: app
```

**Multiple Builds:**
```yaml
builds:
  lib:
    default: true
    entry: src/lib.cxy
    output: build/library.so
  
  app:
    entry: src/main.cxy
    output: build/app
```

Build configuration fields:

- `entry` - Entry point source file (required)
- `output` - Output binary path
- `default` - Mark as default build target (for multiple builds)
- `c-libs` - C libraries to link against
- `c-lib-dirs` - Additional library search paths
- `c-header-dirs` - Additional header search paths
- `c-defines` - C preprocessor definitions
- `c-flags` - Additional C compiler flags
- `defines` - Cxy compiler definitions
- `flags` - Additional Cxy compiler flags
- `plugins-dir` - Directory containing compiler plugins
- `stdlib` - Custom standard library path

### Tests

The `tests` section defines test file patterns and execution arguments:

```yaml
tests:
  - file: "src/**/*.cxy"
  - file: "tests/integration.cxy"
    args: ["--verbose"]
```

Test entries support glob patterns (`**` for recursive matching). Each test can specify command-line arguments passed during execution.

### Scripts

Scripts define reusable development commands with dependency ordering, caching, and environment variables:

**Simple Scripts:**
```yaml
scripts:
  build: cxy build src/lib.cxy -o build/lib.so
  clean: rm -rf build/
```

**Scripts with Dependencies:**
```yaml
scripts:
  install: cxy package install
  build:
    depends: [install]
    command: cxy build src/main.cxy -o build/app
  test:
    depends: [build]
    command: cxy package test
  deploy:
    depends: [test]
    command: ./deploy.sh
```

The `depends` field ensures scripts run in the correct order. When you run `cxy package run deploy`, it executes `install`, then `build`, then `test`, then `deploy` in sequence.

**Scripts with Caching:**

Scripts can specify `inputs` and `outputs` to enable incremental builds. A script is skipped if all output files exist and are newer than all input files:

```yaml
scripts:
  compile:
    inputs:
      - "src/**/*.cxy"
      - "include/**/*.h"
    outputs:
      - "build/app"
    command: cxy build src/main.cxy -o build/app
  
  bundle:
    depends: [compile]
    inputs:
      - "build/app"
      - "assets/**/*"
    outputs:
      - "dist/bundle.tar.gz"
    command: tar -czf dist/bundle.tar.gz build/app assets/
```

**Caching Behavior:**
- Scripts without `inputs`/`outputs` always run
- Scripts with only `inputs` (no `outputs`) always run
- Scripts with `outputs` but no `inputs` always run (with a warning)
- Scripts with both `inputs` and `outputs` are cached based on modification times
- Input patterns support glob expansion including `**` for recursive matching
- Use `--no-cache` flag to force re-execution: `cxy package run build --no-cache`

**Multiline Scripts:**
```yaml
scripts:
  setup: >
    echo "Setting up environment..."
    mkdir -p build
    mkdir -p logs
    echo "Ready!"
```

Multiline scripts use YAML's folded scalar syntax (`>`). Each line executes as a shell command.

### Environment Variables for Scripts

The `env:` section within `scripts:` defines environment variables shared by all scripts. This provides a centralized place to configure paths, flags, and other values used across multiple scripts.

**Basic Environment Variables:**
```yaml
scripts:
  env:
    BUILD_DIR: build
    APP_NAME: my-app
    C_FLAGS: "-O2 -Wall"
  
  build: cxy build src/main.cxy -o {{BUILD_DIR}}/{{APP_NAME}} {{C_FLAGS}}
  clean: rm -rf {{BUILD_DIR}}
```

**Variable Substitution:**

Variables are substituted using `{{VAR_NAME}}` syntax:
- `{{VAR_NAME}}` in script commands is replaced by cxy before execution
- Variables must be defined in the `env:` section or be built-in
- Undefined variables generate warnings and are left as-is

**Variable References:**

Variables can reference other variables defined before them:
```yaml
scripts:
  env:
    BUILD_DIR: "{{SOURCE_DIR}}/build"
    OUTPUT_DIR: "{{BUILD_DIR}}/output"
    APP_NAME: my-app
    FULL_PATH: "{{OUTPUT_DIR}}/{{APP_NAME}}"
  
  build: cxy build src/main.cxy -o {{FULL_PATH}}
```

Variables are resolved in order, so forward references are not allowed:
```yaml
# This will NOT work - BAD cannot reference GOOD defined later
env:
  BAD: "{{GOOD}}/path"  # Error: GOOD not yet defined
  GOOD: "/usr/local"
```

**Built-in Variables:**

These variables are automatically available without definition:
- `SOURCE_DIR` - Absolute path to directory containing Cxyfile.yaml
- `PACKAGE_NAME` - Package name from package metadata
- `PACKAGE_VERSION` - Package version from package metadata
- `CXY_PACKAGES_DIR` - Path to `.cxy/packages` directory

Use built-in variables in your env definitions:
```yaml
scripts:
  env:
    BUILD_DIR: "{{SOURCE_DIR}}/build"
    DIST_NAME: "{{PACKAGE_NAME}}-{{PACKAGE_VERSION}}"
    PACKAGES: "{{CXY_PACKAGES_DIR}}"
  
  package: tar -czf dist/{{DIST_NAME}}.tar.gz build/
```

**Shell Variable Access:**

Environment variables are also set as shell environment variables, accessible using `$VAR`:
```yaml
scripts:
  env:
    BUILD_DIR: build
    LOG_FILE: build.log
  
  build: >
    mkdir -p $BUILD_DIR
    cxy build src/main.cxy -o {{BUILD_DIR}}/app 2>&1 | tee $LOG_FILE
    echo "Build completed in $BUILD_DIR"
```

Use `{{VAR}}` for paths in command strings and `$VAR` for shell logic.

**Complete Example:**
```yaml
scripts:
  env:
    # Base directories
    BUILD_DIR: "{{SOURCE_DIR}}/build"
    DIST_DIR: "{{SOURCE_DIR}}/dist"
    
    # Build configuration
    APP_NAME: "{{PACKAGE_NAME}}"
    VERSION: "{{PACKAGE_VERSION}}"
    
    # Compiler flags
    C_FLAGS: "-O2 -Wall -Werror"
    CXY_FLAGS: "--release"
    
    # Output paths
    OUTPUT_BIN: "{{BUILD_DIR}}/{{APP_NAME}}"
    ARCHIVE_NAME: "{{APP_NAME}}-{{VERSION}}.tar.gz"
  
  prepare: mkdir -p {{BUILD_DIR}} {{DIST_DIR}}
  
  build:
    depends: [prepare]
    command: cxy build {{CXY_FLAGS}} src/main.cxy -o {{OUTPUT_BIN}}
  
  test:
    depends: [build]
    command: >
      export TEST_BIN={{OUTPUT_BIN}}
      cxy package test
  
  package:
    depends: [test]
    command: tar -czf {{DIST_DIR}}/{{ARCHIVE_NAME}} -C {{BUILD_DIR}} .
  
  clean: rm -rf {{BUILD_DIR}} {{DIST_DIR}}
```

**Error Detection:**

The package manager detects common environment variable errors:
- **Circular references**: `A: "{{B}}"` and `B: "{{A}}"` generates an error
- **Undefined variables**: Using `{{UNDEFINED}}` generates a warning
- **Malformed syntax**: Missing closing `}}` treats the text as literal

## The Lock File

The `Cxyfile.lock` ensures reproducible builds by recording exact dependency versions and commit hashes. It is automatically generated and should be committed to version control.

### Lock File Format

```yaml
dependencies:
  - name: json-parser
    repository: https://github.com/cxy-lang/json
    version: 2.1.4
    tag: v2.1.4
    commit: abc123def456789012345678901234567890abcd
    checksum: sha256:a1b2c3d4e5f6...
  
  - name: crypto
    repository: https://github.com/cxy-lang/crypto
    version: 1.4.3
    tag: v1.4.3
    commit: def456abc123456789012345678901234567890ab
    checksum: sha256:f6e5d4c3b2a1...
    dependencies:
      - json-parser

dev-dependencies:
  - name: test-framework
    repository: https://github.com/cxy-lang/test
    version: 3.0.1
    tag: v3.0.1
    commit: 1234567890abcdef1234567890abcdef12345678
    checksum: sha256:1234567890...
```

### Lock File Behavior

The lock file ensures consistency:

- **First install** - Generates lock file with resolved versions
- **Subsequent installs** - Uses exact versions from lock file
- **Dependency changes** - Automatically detects Cxyfile changes and re-resolves
- **Clean installs** - Force fresh resolution with `--clean` flag
- **Frozen lockfile** - Fail if lock is missing or invalid with `--frozen-lockfile`

Always commit `Cxyfile.lock` to your repository so all developers and CI systems use identical dependency versions.

## Command Reference

### cxy package create

Create a new package with standard project structure.

**Basic Usage:**
```bash
cxy package create --name my-package
```

**Options:**
- `--name <name>` - Package name (required)
- `--version <version>` - Initial version (default: 0.1.0)
- `--author <author>` - Package author
- `--license <license>` - Software license (default: MIT)
- `--directory <dir>` - Target directory (default: current directory)
- `--bin` - Create binary package instead of library

**Library Package Structure:**

Creates a library package with:
- `Cxyfile.yaml` - Package manifest
- `index.cxy` - Library entry point
- `src/hello.cxy` - Example source file
- `src/` - Source directory for implementation

The generated package exports symbols through `index.cxy` for import by other packages.

**Binary Package Structure:**

With `--bin` flag, creates:
- `Cxyfile.yaml` - Package manifest with build configuration
- `main.cxy` - Application entry point with main function
- `src/hello.cxy` - Example module
- `src/` - Source directory for modules

Binary packages include a `build` entry in Cxyfile that defines the executable output.

### cxy package add

Add a dependency to the current package.

**Basic Usage:**
```bash
cxy package add https://github.com/cxy-lang/json.git
```

**With Options:**
```bash
cxy package add https://github.com/cxy-lang/json.git \
  --name json \
  --constraint "^2.0.0" \
  --tag v2.1.0 \
  --dev
```

**Options:**
- `--name <name>` - Custom package name (default: derived from repository)
- `--constraint <version>` - Version constraint (default: *)
- `--tag <tag>` - Specific git tag to use
- `--branch <branch>` - Specific git branch for development
- `--path <path>` - Local filesystem path for development
- `--dev` - Add as development dependency
- `--no-install` - Only update Cxyfile.yaml without installing

**Development Dependencies:**

Use `--dev` for dependencies only needed during development:
```bash
cxy package add https://github.com/cxy-lang/test.git --dev
```

Development dependencies are excluded from production builds and only installed with `cxy package install --dev`.

**Local Development:**

Use `--path` for dependencies under active development:
```bash
cxy package add --name my-lib --path ../my-library
```

Local path dependencies always use the current state of the directory without version constraints.

### cxy package remove

Remove dependencies from the current package.

**Basic Usage:**
```bash
cxy package remove json-parser
```

**Remove Multiple:**
```bash
cxy package remove json-parser crypto string-utils
```

**Behavior:**

The remove command:
- Removes entries from `Cxyfile.yaml` (both dependencies and dev-dependencies)
- Regenerates `Cxyfile.lock` with updated dependency graph
- Prompts to optionally remove package directories from `.cxy/packages/`
- Preserves exact versions of remaining dependencies in lock file

Regenerating the lock file ensures the next `install` doesn't reinstall removed packages while maintaining reproducibility for unchanged dependencies.

### cxy package install

Install all dependencies defined in Cxyfile.yaml.

**Basic Usage:**
```bash
cxy package install
```

**Common Options:**
```bash
# Include development dependencies
cxy package install --dev

# Force fresh resolution ignoring lock file
cxy package install --clean

# Use only cached packages (offline mode)
cxy package install --offline

# Fail if lock file is missing (CI mode)
cxy package install --frozen-lockfile
```

**Options:**
- `--dev` - Include development dependencies
- `--clean` - Ignore lock file and perform fresh resolution
- `--packages-dir <dir>` - Custom packages directory (default: .cxy/packages)
- `--verify` - Verify integrity of installed packages
- `--offline` - Use only cached packages without network access
- `--frozen-lockfile` - Fail if lock file is missing or outdated

**Installation Process:**

Without `--clean`:
1. Load `Cxyfile.yaml` to get dependency declarations
2. Load `Cxyfile.lock` if it exists
3. Validate that lock file matches Cxyfile dependencies
4. If lock is out of sync, perform fresh resolution
5. Install packages to `.cxy/packages/`
6. Generate new lock file if resolution was performed

With `--clean`:
1. Load `Cxyfile.yaml` only
2. Ignore any existing lock file
3. Perform fresh dependency resolution
4. Install all packages
5. Generate new lock file

**Frozen Lockfile Mode:**

Use `--frozen-lockfile` in CI/production to ensure reproducible builds:
```bash
cxy package install --frozen-lockfile
```

This fails if the lock file is missing or doesn't match Cxyfile, preventing unexpected dependency resolution during deployment.

### cxy package update

Update dependencies to newer versions within constraints (not yet implemented).

**Planned Usage:**
```bash
# Update all dependencies
cxy package update

# Update specific packages
cxy package update json-parser crypto

# Update to latest ignoring constraints
cxy package update --latest
```

This command will update dependencies to the latest versions that satisfy the version constraints in Cxyfile.yaml.

### cxy package build

Build the package using configuration from Cxyfile.yaml.

**Basic Usage:**
```bash
# Build default target
cxy package build

# Build specific target
cxy package build lib

# Build all targets
cxy package build --all
```

**Options:**
- `--release` - Build with optimizations enabled
- `--debug` - Build with debug symbols
- `--build-dir <dir>` - Output directory (default: .cxy/build)
- `--clean` - Clean build artifacts before building
- `--all` - Build all defined targets
- `--list` - List available build targets

**Multiple Build Targets:**

For packages with multiple builds defined in Cxyfile.yaml:
```bash
# Build specific target
cxy package build cli

# List all targets
cxy package build --list

# Build everything
cxy package build --all
```

The default build target (marked with `default: true` in Cxyfile) is built when no target is specified.

### cxy package test

Run package tests defined in Cxyfile.yaml.

**Basic Usage:**
```bash
cxy package test
```

**With Options:**
```bash
# Run specific test files
cxy package test tests/integration.cxy tests/performance.cxy

# Filter tests by pattern
cxy package test --filter "json_*"

# Run tests in parallel
cxy package test --parallel 4
```

**Options:**
- `--build-dir <dir>` - Build directory for test binaries (default: .cxy/build)
- `--filter <pattern>` - Run only tests matching pattern
- `--parallel <n>` - Run tests in parallel (default: 1)

**Test Discovery:**

Tests are discovered using the patterns in the `tests` section of Cxyfile.yaml. The test runner:
- Compiles each test file
- Executes the test binary
- Reports success/failure with timing information
- Provides a summary of results

Tests can receive command-line arguments specified in the Cxyfile.yaml test configuration.

### cxy package run

Execute scripts defined in Cxyfile.yaml with automatic dependency resolution.

**Basic Usage:**
```bash
# List available scripts
cxy package run --list

# Run a script
cxy package run build

# Pass arguments to script
cxy package run test -- --verbose --filter "integration_*"
```

**Options:**
- `--list` - List all available scripts
- `--no-cache` - Disable script caching and force re-execution
- `-- <args>` - Pass arguments to the script (after the `--` separator)

**Script Execution:**

The run command:
- Resolves script dependencies from the `depends` field
- Detects circular dependencies and reports errors
- Executes scripts in correct dependency order
- Passes arguments only to the final script in the chain

**Example with Environment Variables and Caching:**
```yaml
scripts:
  env:
    BUILD_DIR: "{{SOURCE_DIR}}/build"
    TEST_ARGS: "--verbose --parallel 4"
  
  install: cxy package install
  
  build:
    depends: [install]
    inputs:
      - "src/**/*.cxy"
      - "Cxyfile.yaml"
    outputs:
      - "{{BUILD_DIR}}/app"
    command: cxy build src/main.cxy -o {{BUILD_DIR}}/app
  
  test:
    depends: [build]
    command: cxy package test {{TEST_ARGS}}
```

Running `cxy package run test` executes `install`, then `build` (skipped if cached), then `test` in sequence. Environment variables are substituted in commands and available as shell variables.

**Caching Behavior:**

When `inputs` and `outputs` are specified, the script is automatically cached:
- On first run, the script executes normally
- On subsequent runs, if all output files exist and are newer than all input files, the script is skipped
- Use `--no-cache` to force execution regardless of cache status:
  ```bash
  cxy package run build --no-cache
  ```
- Cache status is checked before each script execution
- Glob patterns in `inputs` are expanded to actual file paths
- Missing input or output files invalidate the cache

**Passing Arguments:**

Arguments after `--` are passed only to the final script:
```bash
cxy package run test -- --verbose --filter "unit_*"
```

This passes `--verbose --filter unit_*` to the test script but not to install or build.

**Built-in Environment Variables:**

These variables are automatically available in all scripts:
- `SOURCE_DIR` - Absolute path to directory containing Cxyfile.yaml
- `PACKAGE_NAME` - Package name from Cxyfile
- `PACKAGE_VERSION` - Package version from Cxyfile
- `CXY_PACKAGES_DIR` - Path to `.cxy/packages` directory

Use them in the `env:` section or directly in commands:
```yaml
scripts:
  env:
    OUTPUT: "{{SOURCE_DIR}}/dist"
  
  package: tar -czf {{OUTPUT}}/{{PACKAGE_NAME}}-{{PACKAGE_VERSION}}.tar.gz src/
```

### cxy package info

Display information about installed or remote packages.

**Basic Usage:**
```bash
# Show info for installed package
cxy package info json-parser

# Output as JSON
cxy package info json-parser --json
```

**Options:**
- `--json` - Output in JSON format for machine parsing

**Information Displayed:**

Human-readable format shows:
- Package name and version
- Description and metadata (author, license, repository, homepage)
- Dependencies with version constraints
- Development dependencies
- Test patterns
- Available scripts
- Build configuration
- Installation status and location

JSON format includes all fields in machine-readable structure for tooling integration.

**Remote Package Info:**

Support for viewing remote package information (not yet implemented):
```bash
cxy package info https://github.com/cxy-lang/json.git
```

This will fetch and display package metadata without installing the package.

### cxy package clean

Clean build artifacts and package cache.

**Basic Usage:**
```bash
# Clean build directory
cxy package clean

# Clean package cache
cxy package clean --cache

# Clean everything
cxy package clean --all

# Skip confirmation prompts
cxy package clean --all --force
```

**Options:**
- `--cache` - Clean package cache (.cxy/packages)
- `--build` - Clean build directory (default if no flags)
- `--all` - Clean both cache and build
- `--force`, `-f` - Skip confirmation prompts

**Behavior:**

The clean command:
- Shows directory sizes before removal
- Prompts for confirmation unless `--force` is used
- Reports successful removal with confirmation
- Handles non-existent directories gracefully

Build directory location is determined from the `build.output` field in Cxyfile.yaml or defaults to `.cxy/build`.

### cxy package publish

Publish package by creating a git tag (not yet implemented).

**Planned Usage:**
```bash
# Publish with version bump
cxy package publish --bump patch

# Publish with custom tag
cxy package publish --tag v1.0.0

# Dry run to preview
cxy package publish --bump minor --dry-run
```

This will create annotated git tags for version releases.

### cxy package list

List all installed dependencies (not yet implemented).

**Planned Usage:**
```bash
# List all installed packages
cxy package list

# Show dependency tree
cxy package list --tree
```

This will display all packages in `.cxy/packages/` with version information.

## Import Resolution

The package manager resolves imports prefixed with `@` to installed packages in `.cxy/packages/`.

### Basic Package Import

Import a package's default entry point:
```cxy
import "@json-parser"
```

This resolves to `.cxy/packages/json-parser/index.cxy`.

### Specific File Import

Import a specific file from a package:
```cxy
import "@json-parser/parser.cxy"
import "@string-utils/format.cxy"
```

This allows direct access to package internals when the package exports specific modules.

### Import Alias

Use aliases to avoid naming conflicts:
```cxy
import "@json-parser" as json
import "@json-parser/parser.cxy" as JsonParser
```

The imported symbols are then accessed through the alias name.

### Resolution Order

The compiler resolves imports in this order:
1. Standard library (stdlib/)
2. Package imports (@package-name)
3. Relative imports (./file.cxy, ../dir/file.cxy)
4. Absolute imports from import search paths

## File System Layout

Package manager uses a standard directory structure:

```
project-root/
├── Cxyfile.yaml          # Package manifest
├── Cxyfile.lock          # Lock file (generated)
├── index.cxy             # Library entry point
├── main.cxy              # Binary entry point
├── src/                  # Source files
│   ├── module1.cxy
│   └── module2.cxy
├── tests/                # Test files
│   ├── unit_tests.cxy
│   └── integration.cxy
├── .cxy/                 # Package manager data (git-ignored)
│   ├── packages/         # Installed dependencies
│   │   ├── json-parser/
│   │   └── string-utils/
│   └── build/            # Build artifacts
│       ├── app
│       └── lib.so
└── build/                # Alternative build output
```

### Generated Directories

The `.cxy/` directory is automatically created and should be added to `.gitignore`:
- `.cxy/packages/` - Contains all installed dependencies
- `.cxy/build/` - Default location for build artifacts

Each dependency in `.cxy/packages/<name>/` is a complete package with its own Cxyfile.yaml.

## Development Workflows

### Starting a New Project

Create and set up a new library:
```bash
mkdir my-library
cd my-library
cxy package create --name my-library --author "Your Name"
cxy package add https://github.com/cxy-lang/test.git --dev
```

Create and set up a new application:
```bash
cxy package create --name my-app --bin
cxy package add https://github.com/cxy-lang/json.git
cxy package install
cxy package build
```

### Adding Dependencies

Add production dependencies:
```bash
cxy package add https://github.com/cxy-lang/json.git
cxy package add https://github.com/cxy-lang/http-client.git --constraint "^2.0.0"
```

Add development dependencies:
```bash
cxy package add https://github.com/cxy-lang/test.git --dev
cxy package add https://github.com/cxy-lang/benchmarks.git --dev
```

### Building and Testing

Standard development cycle:
```bash
# Install dependencies
cxy package install --dev

# Build the project
cxy package build

# Run tests
cxy package test

# Clean and rebuild
cxy package clean
cxy package build
```

Using scripts for automation:
```bash
# Define scripts in Cxyfile.yaml
scripts:
  dev:
    depends: [install, build]
    command: cxy package test
  
# Run the development workflow
cxy package run dev
```

### Continuous Integration

CI configuration for reproducible builds:
```bash
# In CI environment
cxy package install --frozen-lockfile --verify
cxy package build --release
cxy package test --parallel 4
```

The `--frozen-lockfile` flag ensures the build fails if dependencies change unexpectedly, and `--verify` checks package integrity.

### Local Development

Working with local dependencies during development:
```bash
# Add local dependency
cxy package add --name shared-lib --path ../shared-library

# Make changes to shared-library, they're immediately available

# Before committing, switch to git dependency
cxy package remove shared-lib
cxy package add https://github.com/username/shared-library.git
```

Local dependencies always use the current state without version locking.

## Best Practices

### Version Control

Commit these files:
- `Cxyfile.yaml` - Package manifest
- `Cxyfile.lock` - Ensures reproducible builds
- Source code and tests

Add to `.gitignore`:
- `.cxy/` - Package manager working directory
- `build/` - Build artifacts

### Version Constraints

Use appropriate version constraints:
- `^1.2.3` for stable APIs (allows minor and patch updates)
- `~1.2.3` for stricter control (allows only patch updates)
- Exact versions `1.2.3` for critical dependencies
- `*` only for development or prototyping

### Dependency Management

Keep dependencies minimal:
- Only add dependencies you actually use
- Prefer standard library when possible
- Regularly update to patch security issues
- Review dependency count and size

### Scripts Organization

Define scripts for common tasks:
```yaml
scripts:
  install: cxy package install --dev
  build: cxy build src/main.cxy -o build/app
  test:
    depends: [build]
    command: cxy package test
  bench:
    depends: [build]
    command: ./build/benchmarks
  format: cxy fmt src/**/*.cxy
  lint: cxy lint src/**/*.cxy
  check:
    depends: [format, lint, test]
    command: echo "All checks passed"
```

This provides a consistent interface for development tasks.

### Lock File Maintenance

Keep lock file up to date:
- Commit lock file changes with dependency updates
- Run `cxy package install --clean` periodically to update dependencies
- Use `--frozen-lockfile` in CI to catch unintended changes

### Build Configuration

Separate build targets for different outputs:
```yaml
builds:
  lib:
    default: true
    entry: src/lib.cxy
    output: build/library.so
  
  tests:
    entry: tests/runner.cxy
    output: build/test-runner
  
  examples:
    entry: examples/demo.cxy
    output: build/demo
```

This allows building different artifacts without conflicting configurations.

## Troubleshooting

### Common Issues

**Lock file out of sync:**
```bash
# Force fresh resolution
cxy package install --clean
```

**Dependency conflicts:**
```bash
# Check dependency tree
cxy package list --tree  # (not yet implemented)

# Update specific package
cxy package update problematic-package  # (not yet implemented)
```

**Build failures:**
```bash
# Clean build artifacts
cxy package clean

# Rebuild with verbose output
cxy package build --verbose
```

**Missing dependencies:**
```bash
# Verify all dependencies are installed
cxy package install --verify

# Check package info
cxy package info package-name
```

### Error Messages

**"no Cxyfile.yaml found"**
- Run command from package root directory
- Create package with `cxy package create`

**"frozen lockfile enabled but lock file not found"**
- Commit Cxyfile.lock to version control
- Or remove `--frozen-lockfile` flag

**"package 'xyz' not found in dependencies"**
- Check spelling of package name
- Run `cxy package info xyz` to verify installation

**"dependency resolution failed"**
- Check version constraints in Cxyfile.yaml
- Try `--clean` to force fresh resolution
- Verify git repository URLs are accessible

## Future Enhancements

Planned features for future releases:

- **Package registry** - Central package repository for discovery and distribution
- **Binary cache** - Pre-built binaries to speed up installation
- **Workspace support** - Multi-package monorepo management
- **Package publishing** - Automated release workflow with git tags
- **Dependency auditing** - Security vulnerability scanning
- **Private dependencies** - Support for private git repositories with authentication
- **Mirror support** - Alternative package sources for air-gapped environments
- **Plugin system** - Extensible package manager with custom commands

## See Also

- [Cxy Language Guide](../README.md) - Language syntax and features
- [Standard Library](stdlib/) - Built-in modules and utilities
- [Build System](build.md) - Compiler and build configuration
- [Testing Guide](testing.md) - Writing and running tests