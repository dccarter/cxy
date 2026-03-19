# Cxy Utils

Script utility commands for use within `cxy package run` scripts and the terminal.

`cxy utils` provides a set of tools for managing background processes, waiting for
services, coordinating concurrent scripts, and validating environments.

## Overview

```bash
cxy utils <command> [options]
```

All commands are available both from the terminal and from within scripts executed
by `cxy package run`. Commands that manage background processes (`async-cmd-*`)
**must** be called from within a script executed by `cxy package run`, as they
require the async tracker to be initialized.

---

## Commands

### async-cmd-start

Start a command in the background. The process is tracked and automatically killed
when the parent script exits, even if the script crashes or is interrupted.

```bash
cxy utils async-cmd-start "<cmd>" [--capture]
```

**Arguments:**
- `<cmd>` - Command string to run in the background (passed to `sh -c`)

**Options:**
- `--capture` - Redirect process stdout/stderr to a log file in the build directory
  instead of discarding it. Log file path: `BUILD_DIR/.async-cmd-<pid>.log`

**Output:**

The spawned process PID is written to `BUILD_DIR/.async-last-pid` so scripts can
read it even when stdout is captured by the progress runner:

```bash
cxy utils async-cmd-start "python3 -m http.server 8080"
SERVER_PID=$(cat "$CXY_BUILD_DIR/.async-last-pid")
echo "Server running as PID $SERVER_PID"
```

**With output capture:**
```bash
cxy utils async-cmd-start "./server --port 8080" --capture
SERVER_PID=$(cat "$CXY_BUILD_DIR/.async-last-pid")

# View server output later
cat "$CXY_BUILD_DIR/.async-cmd-$SERVER_PID.log"
```

**Automatic Cleanup:**

All processes started with `async-cmd-start` are automatically killed (SIGTERM,
then SIGKILL after 2 seconds) when the parent `cxy package run` script exits,
regardless of whether it succeeded, failed, or was interrupted (SIGINT/SIGTERM).

**Limits:**
- Maximum 64 tracked processes per `cxy package run` session

**Notes:**
- The command is run via `sh -c`, so shell features (pipes, redirects, etc.) work:
  ```bash
  cxy utils async-cmd-start "tail -f /var/log/app.log | grep ERROR"
  ```
- Processes are isolated in their own process group (`setpgid`), so killing them
  also kills any children they spawned.
- This command requires `CXY_ASYNC_TRACKER` to be set, which `cxy package run`
  exports automatically. It is not usable outside of a script context.

---

### async-cmd-stop

Stop a tracked background process by PID.

```bash
cxy utils async-cmd-stop <pid>
```

**Arguments:**
- `<pid>` - Process ID to stop (as returned in `.async-last-pid`)

**Behavior:**
1. Sends SIGTERM to the process group
2. Waits up to 2 seconds for graceful shutdown
3. Sends SIGKILL if process has not exited
4. Removes the PID from the tracker

If the process is already dead, the command succeeds silently.

**Example:**
```bash
cxy utils async-cmd-start "python3 -m http.server 8080"
SERVER_PID=$(cat "$CXY_BUILD_DIR/.async-last-pid")

# ... run tests ...

# Stop manually (optional - auto-cleanup happens anyway)
cxy utils async-cmd-stop "$SERVER_PID"
```

---

### wait-for

Poll a command repeatedly until it exits with code 0 or the timeout is reached.

```bash
cxy utils wait-for "<cmd>" [--timeout <ms>] [--period <ms>]
```

**Arguments:**
- `<cmd>` - Command to poll (passed to `sh -c`)

**Options:**
- `--timeout <ms>` - Total time to wait in milliseconds (default: `30000`)
- `--period <ms>` - How often to retry in milliseconds (default: `500`)

**Exit Codes:**
- `0` - Command succeeded within the timeout
- `1` - Timed out before command succeeded

**Examples:**
```bash
# Wait for an HTTP server to respond
cxy utils wait-for "curl -s http://localhost:8080/health" --timeout 15000

# Wait for a database to accept connections
cxy utils wait-for "pg_isready -h localhost -p 5432" --timeout 30000 --period 1000

# Wait for a file to appear
cxy utils wait-for "test -f /tmp/ready.flag" --timeout 5000 --period 200
```

**Typical Pattern with async-cmd-start:**
```bash
# Start server in background
cxy utils async-cmd-start "./build/server --port 8080"

# Wait for it to be ready before running tests
cxy utils wait-for "curl -sf http://localhost:8080/health" --timeout 15000 --period 500

# Now run tests
./build/run-tests
```

**Notes:**
- The polled command's output is suppressed (redirected to `/dev/null`)
- A spinner with elapsed/remaining time is shown while waiting
- Can be used outside of `cxy package run` (does not require the async tracker)

---

### wait-for-port

Wait until a TCP port on localhost is open and accepting connections.

```bash
cxy utils wait-for-port <port> [--timeout <ms>] [--period <ms>]
```

**Arguments:**
- `<port>` - Port number to wait for (1-65535)

**Options:**
- `--timeout <ms>` - Total time to wait in milliseconds (default: `30000`)
- `--period <ms>` - How often to retry in milliseconds (default: `500`)

**Exit Codes:**
- `0` - Port is open and accepting connections
- `1` - Timed out before port became available

**Examples:**
```bash
# Wait for a web server
cxy utils wait-for-port 8080 --timeout 10000

# Wait for PostgreSQL
cxy utils wait-for-port 5432 --timeout 30000 --period 1000

# Wait for Redis
cxy utils wait-for-port 6379
```

**Compared to wait-for:**

`wait-for-port` uses a direct TCP `connect()` rather than running a command,
making it lighter and more reliable for simple port checks:

```bash
# These are equivalent, but wait-for-port is simpler
cxy utils wait-for-port 8080 --timeout 10000
cxy utils wait-for "curl -s http://localhost:8080" --timeout 10000
```

Use `wait-for` when you need to check HTTP status codes or run a more specific
readiness check. Use `wait-for-port` when you just need to know the port is open.

---

### find-free-port

Find and print an available TCP port in a given range.

```bash
cxy utils find-free-port [--range-start <port>] [--range-end <port>]
```

**Options:**
- `--range-start <port>` - Start of port range to search (default: `8000`)
- `--range-end <port>` - End of port range to search (default: `9000`)

**Output:**

Prints the first available port number to stdout.

**Examples:**
```bash
# Find any free port in the default range (8000-9000)
PORT=$(cxy utils find-free-port)

# Find a free port in a specific range
PORT=$(cxy utils find-free-port --range-start 3000 --range-end 4000)

# Use it to start a server on a dynamic port
PORT=$(cxy utils find-free-port)
cxy utils async-cmd-start "./server --port $PORT"
cxy utils wait-for-port $PORT --timeout 10000
```

**Notes:**
- Uses `bind()` to check port availability, which is more reliable than `connect()`
- Returns the first free port found (scans sequentially from `range-start`)
- Write the port to `.async-last-port` if you need it across subshells (the
  command only writes to stdout)

---

### env-check

Assert that one or more environment variables are set and non-empty. Fails with a
clear error message listing every missing variable.

```bash
cxy utils env-check VAR1 VAR2 ...
```

**Arguments:**
- `VAR1 VAR2 ...` - One or more environment variable names to check

**Exit Codes:**
- `0` - All variables are set and non-empty
- `1` - One or more variables are missing or empty

**Examples:**
```bash
# Check a single variable
cxy utils env-check DATABASE_URL

# Check multiple variables at once
cxy utils env-check DATABASE_URL API_KEY S3_BUCKET

# Use at the top of a script to fail fast with clear errors
cxy utils env-check CI_TOKEN DEPLOY_HOST DEPLOY_USER
```

**Output:**
```
 ✔ DATABASE_URL=postgres://localhost/mydb
 ✔ API_KEY=sk-...
error: required environment variable 'S3_BUCKET' is not set
```

**Best Practice:**

Place `env-check` at the top of scripts that require specific environment
variables so failures are reported immediately with clear messages rather than
failing cryptically deep in the script:

```yaml
scripts:
  deploy:
    command: |
      #!/bin/bash
      set -e

      # Fail fast if required vars are missing
      cxy utils env-check DEPLOY_HOST DEPLOY_USER SSH_KEY_PATH

      # ... rest of deploy script
      rsync -avz build/ $DEPLOY_USER@$DEPLOY_HOST:/opt/app/
```

---

### lock

Run a command while holding a named lock file, preventing concurrent execution of
the same operation across multiple processes.

```bash
cxy utils lock <name> "<cmd>" [--timeout <ms>]
```

**Arguments:**
- `<name>` - Lock name. Used as the lock file name: `$TMPDIR/.cxy-lock-<name>.lock`
- `<cmd>` - Command to run while holding the lock (passed to `sh -c`)

**Options:**
- `--timeout <ms>` - How long to wait to acquire the lock in milliseconds
  (default: `30000`). Use `0` to fail immediately if the lock is held.

**Exit Codes:**
- `0` - Lock acquired and command ran successfully
- `1` - Lock acquisition timed out, or command exited with non-zero code

**Examples:**
```bash
# Prevent concurrent database migrations
cxy utils lock db-migrate "./scripts/migrate.sh"

# Prevent concurrent deploys, fail immediately if one is in progress
cxy utils lock deploy "./deploy.sh" --timeout 0

# Serialize access to a shared resource with a long timeout
cxy utils lock shared-cache "python3 update-cache.py" --timeout 60000
```

**How It Works:**

`lock` uses `flock(2)` for locking, which has important properties:

- **Kernel-enforced** - the OS manages the lock, not a userspace daemon
- **Crash-safe** - if the lock holder crashes, the OS releases the lock
  automatically when the file descriptor is closed (process exit)
- **Cross-process** - any process on the same machine can compete for the same
  named lock
- **No cleanup needed** - lock files in `$TMPDIR` are reused across invocations

**Lock File Location:**

Lock files are stored in `$TMPDIR` (or `/tmp` if `$TMPDIR` is unset):
```
/tmp/.cxy-lock-<name>.lock
```

**Spinner:**

While waiting for a lock, a spinner with remaining time is shown:
```
⠙ Waiting for lock... 4500ms remaining
```

When the lock is acquired, the spinner is cleared and the command runs.

**Use in Parallel Test Suites:**

When running tests in parallel (e.g. with `-j` in a Makefile or CI matrix),
`lock` prevents race conditions on shared resources:

```yaml
scripts:
  test-with-db:
    command: |
      #!/bin/bash
      set -e

      # Only one test suite migrates the DB at a time
      cxy utils lock db-setup "./scripts/reset-db.sh"

      # Tests run in parallel after setup
      ./build/run-tests
```

---

## Using with cxy package run

All `async-cmd-*` commands require `CXY_ASYNC_TRACKER` to be set, which
`cxy package run` exports automatically before executing scripts.

The tracker file is created at `BUILD_DIR/.async-cmds.<cxy-pid>` and tracks all
background processes started during the script. When the script finishes (or is
interrupted), all tracked processes are killed automatically.

**Full integration example:**

```yaml
scripts:
  env:
    CXY_BUILD_DIR: "{{SOURCE_DIR}}/.cxy/build"

  integration-test:
    command: |
      #!/bin/bash
      set -e

      # Fail fast if required env vars are missing
      cxy utils env-check DATABASE_URL

      # Find a free port so tests don't conflict with each other
      PORT=$(cxy utils find-free-port --range-start 8000 --range-end 9000)
      echo "Using port $PORT"

      # Start the server in the background with output capture
      cxy utils async-cmd-start "./build/server --port $PORT" --capture
      SERVER_PID=$(cat "$CXY_BUILD_DIR/.async-last-pid")

      # Wait for server to be ready
      cxy utils wait-for-port $PORT --timeout 15000

      # Prevent concurrent migrations
      cxy utils lock db-migrate "./scripts/migrate.sh"

      # Run the tests
      ./build/integration-tests --port $PORT

      # Server is automatically killed when script exits
```

Run with:
```bash
cxy package run integration-test
```

---

## Stale Process Cleanup

When `cxy package run` starts, it scans the build directory for tracker files from
previous sessions whose parent `cxy` process no longer exists (e.g. after a crash).
Any processes still alive from those sessions are killed and the stale tracker files
are removed.

This means orphaned background processes from crashed scripts are cleaned up
automatically on the next run.

---

## See Also

- [Package Management](package.md) - Dependency management and build system
- [Cxy Language Guide](../README.md) - Language syntax and features