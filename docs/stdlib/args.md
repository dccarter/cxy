# CLI Arguments

The Cxy standard library provides a powerful and modern command-line argument parser through `stdlib/args.cxy`. This library is designed to make building CLI applications as easy and intuitive as possible, following patterns established by popular tools like Docker, Git, and kubectl.

## Overview

The args library takes a hierarchical approach to command structure, where your application can have multiple subcommands, each with their own flags and further subcommands. This creates a tree-like structure that's both powerful for complex applications and simple for basic use cases.

When you create a CLI application with this library, you get automatic help generation, type-safe flag parsing, and a clean separation between argument parsing and command execution. The library handles all the tedious work of parsing different flag formats, validating inputs, and generating helpful error messages.

## Getting Started

The simplest way to create a CLI application is to use the `RootCommand` factory function, which automatically sets up help flags and a help command for you:

```cxy
import "stdlib/args.cxy" as args

func main(argv: [string]): i32 {
    var cli = args.RootCommand("myapp", "My awesome application")
    
    var (finalCmd, shouldExecute) = cli.parse(argv) catch (null, false)
    if shouldExecute && finalCmd {
        return finalCmd.execute()
    }
    return 0
}
```

This creates a basic CLI that responds to `myapp --help` and `myapp help` commands automatically. The `parse` method returns two values: the final command that should be executed, and a boolean indicating whether execution should proceed (it returns false when help is requested).

## Understanding Commands

Commands are the building blocks of your CLI application. Every CLI starts with a root command, which can contain subcommands, and those subcommands can contain their own subcommands. This creates a hierarchy like `myapp server start` or `git branch create feature-xyz`.

Each command can have its own flags (options that modify behavior), its own action (code that runs when the command is invoked), and its own set of valid arguments. Commands also inherit flags from their parent commands, making it easy to have global options that apply everywhere.

Creating subcommands is straightforward:

```cxy
var serverCmd = args.Command("server", "Manage the application server")
serverCmd.setAction((cmd: &args.Command) => {
    println("Server management selected")
    return 0
})
cli.addCommand(serverCmd)
```

The action is a lambda function that receives the command object and returns an exit code. Inside this function, you can access all the parsed flags and arguments to implement your command's behavior.

## Working with Flags

Flags (also called options) are how users customize the behavior of your commands. The args library supports three main types of flags: boolean flags for on/off options, string flags for text input, and integer flags for numeric input.

Boolean flags are the simplest and can be specified in multiple ways. When you create a flag like `args.Flag("verbose", 'v', "Enable verbose output", false)`, users can activate it with `--verbose`, `-v`, `--verbose=true`, or even combine it with other boolean flags like `-vd` for both verbose and debug.

String and integer flags require values and support the standard Unix conventions. Users can specify them as `--config=file.json`, `--config file.json`, or `-c file.json`. The library automatically parses and validates the input based on the flag type you declare.

One powerful feature is environment variable binding. When you create a flag with an environment variable name, the library will automatically use that environment variable as the default value if the user doesn't specify the flag:

```cxy
var tokenFlag = args.Flag("token", 't', "API token", "".s, "API_TOKEN")
cmd.addFlag(tokenFlag)
```

This allows users to set `export API_TOKEN=xyz` in their shell and your application will pick it up automatically.

## Flag Inheritance and Scope

The library distinguishes between local flags (available only to a specific command) and persistent flags (inherited by all subcommands). This distinction is crucial for building intuitive CLIs.

Local flags are added with `addFlag` and only apply to that specific command. Persistent flags are added with `addPersistentFlag` and are available to the command and all its children. This is perfect for global options like `--debug` or `--config` that should be available everywhere.

When you use the `RootCommand` factory, it automatically adds persistent `--help` and `-h` flags, which is why every command in your application automatically supports help without any extra work.

## Flag Groups and Validation

For more complex scenarios, the library supports flag groups that enforce relationships between flags. You might have mutually exclusive flags where only one can be specified, or flags that must be provided together.

Mutually exclusive groups are useful for output formatting options where you want either JSON or YAML, but not both:

```cxy
var formatGroup = args.FlagGroup("format", args.FlagGroupType.MutuallyExclusive)
formatGroup.flags.push("json")
formatGroup.flags.push("yaml")
cmd.addFlagGroup(formatGroup)
```

Required together groups ensure that related flags are provided as a set, like username and password for authentication.

## Help System

One of the library's strongest features is its automatic help generation. Every command gets properly formatted help output that includes usage information, available subcommands, and all flags with their descriptions. The help system uses dynamic padding to ensure everything aligns nicely regardless of command or flag name length.

The help system works in multiple ways. Users can use `--help` or `-h` on any command, or they can use the dedicated help command with specific targets like `myapp help server start`. This flexibility matches what users expect from modern CLI tools.

When a command has no action defined, executing it automatically shows help instead of failing. This makes exploration intuitive - users can type partial commands to see what's available.

## Practical Example

Here's a more complete example showing how these concepts work together:

```cxy
func main(argv: [string]): i32 {
    var docker = args.RootCommand("docker", "Container management platform")
    
    // Global configuration that all commands inherit
    docker.addPersistentFlag(args.Flag("config", 'c', "Configuration directory", "~/.docker".s))
    docker.addPersistentFlag(args.Flag("debug", 'd', "Enable debug output", false))
    
    // Container management subcommand
    var container = args.Command("container", "Manage containers")
    
    // Run command with specific flags
    var run = args.Command("run", "Create and run a new container")
    run.addFlag(args.Flag("detach", 'd', "Run in background", false))
    run.addFlag(args.Flag("name", 'n', "Container name", "".s))
    run.addFlag(args.Flag("port", 'p', "Port mapping", "".s))
    
    run.setAction((cmd: &args.Command) => {
        // Access the image name from positional arguments
        if cmd.args.empty() {
            stderr << "Error: image name required\n"
            return 1
        }
        
        var image = cmd.args.[0]
        var detached = cmd.flagValues.getBool("detach")
        var name = cmd.flagValues.getString("name")
        
        println("Running container from image: ", image)
        if detached {
            println("Running in background mode")
        }
        if !name.empty() {
            println("Container name: ", name)
        }
        
        return 0
    })
    
    container.addCommand(run)
    docker.addCommand(container)
    
    // Parse and execute
    var (finalCmd, shouldExecute) = docker.parse(argv) catch (null, false)
    
    if !finalCmd {
        stderr << "Failed to parse arguments\n"
        return 1
    }
    
    if !shouldExecute {
        // Help was shown, exit successfully
        return 0
    }
    
    return finalCmd.execute()
}
```

This creates a CLI that supports commands like `docker container run --detach --name web nginx`, with automatic help available at every level.

## Accessing Parsed Values

Once parsing is complete, you access flag values through the command's `flagValues` object using type-specific methods. The `getBool`, `getString`, and `getInt` methods return the parsed values with proper type conversion and default handling.

Positional arguments (non-flag arguments) are available through the command's `args` vector. These are typically used for required parameters like file names, URLs, or other inputs that don't need flag names.

The library ensures type safety throughout this process. If a user provides an invalid integer for an integer flag, parsing fails with a clear error message rather than silently using a default value.

## Future Enhancements

The following features are planned but not yet available:

### Flag Types
- [ ] **FloatFlag**: `--ratio 1.5` support
- [ ] **StringSliceFlag**: Repeated flags like `--env KEY=value --env KEY2=value`

### Command Features  
- [ ] **Command aliases**: `ls`, `list`, `ps` for same command
- [ ] **Hidden commands**: Commands not shown in help
- [ ] **Command suggestions**: "Did you mean 'start'?" for typos

### Validation
- [ ] **Argument validation**: `exactly(2)`, `atLeast(1)`, `between(1,3)` 
- [ ] **Custom validators**: User-defined validation functions
- [ ] **Flag validation**: Port ranges, file existence, etc.

### Configuration
- [ ] **Config file integration**: JSON/YAML config file support
- [ ] **Precedence**: CLI > ENV > config file > defaults

### Advanced Help
- [ ] **Flag grouping in help**: Separate local/global/persistent flags
- [ ] **Examples section**: Show usage examples in help
- [ ] **Custom help templates**: User-defined help formatting

### Shell Integration
- [ ] **Shell completion**: Bash/zsh completion generation
- [ ] **Colored output**: Terminal color support

Despite these future plans, the current library provides everything needed to build sophisticated command-line applications with clean, maintainable code.