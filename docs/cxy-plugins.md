# Cxy Plugin System

## Overview

The Cxy plugin system allows you to extend the compiler with custom compile-time code generation and transformations. Plugins are dynamic libraries that register "actions" — functions that can be invoked from Cxy source code to generate or transform AST nodes during compilation.

Plugins have access to the compiler's internal APIs including:
- Memory allocation (MemPool, StrPool)
- Type system (TypeTable)
- AST manipulation
- Logging and diagnostics

## Using Plugins in Cxy Code

Plugins are imported using `import plugin` syntax and their actions are invoked at **compile-time** using the `!` operator:

```cxy
// Import a plugin
import plugin 'jsonrpc' as rpc

// Invoke plugin action at compile-time (note the !)
class MyClient[T] {
    rpc.addClientMethods!((submit, Exception, Void), #T)
    // Plugin injects methods here during compilation
}
```

Plugin actions run during compilation and typically generate or inject AST nodes into your program. They are **not** runtime functions.

## Plugin Structure

A plugin is a shared library (`.so`, `.dylib`, `.dll`) that exports two key functions:

```c
bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc);
void pluginDeInit(CxyPluginContext *ctx);  // optional
```

### Plugin Context

The `CxyPluginContext` provides access to compiler infrastructure:

```c
typedef struct CxyPluginContext {
    Log *L;              // Logging and diagnostics
    MemPool *pool;       // Memory pool for AST nodes
    StrPool *strings;    // String interning pool
} CxyPluginContext;
```

## Creating a Plugin

### 1. Implement `pluginInit`

This function is called when the plugin is loaded. It should:
1. Register actions (compile-time functions)
2. Initialize any plugin-specific state
3. Return `true` on success, `false` on error

**Example:**

```c
#include <cxy/plugin.h>
#include <cxy/strings.h>

bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    // Intern common strings if needed
    internCommonStrings(ctx->strings);
    
    // Register actions
    cxyPluginRegisterAction(
        ctx,
        loc,
        (CxyPluginAction[]){
            {.name = "myAction", .fn = myActionImpl},
            {.name = "anotherAction", .fn = anotherActionImpl},
        },
        2  // number of actions
    );
    
    return true;
}
```

### 2. Register Actions

Actions are compile-time functions that your Cxy code can invoke. They receive:
- The invocation node (contains location info)
- Arguments passed from source code
- Return AST nodes to inject into the program

**Action Signature:**

```c
typedef AstNode *(*CxyCxyPluginActionFn)(
    CxyPluginContext *ctx,
    const AstNode *node,
    AstNode *args
);
```

### 3. Implement Action Functions

Actions are where your plugin does its work. They typically:
1. Parse arguments from the argument list
2. Load required environment variables
3. Generate/transform AST nodes
4. Return the generated nodes

**Example Action:**

```c
static AstNode *myAction(CxyPluginContext *ctx,
                         const AstNode *node,
                         AstNode *args)
{
    // 1. Extract required arguments
    CXY_REQUIRED_ARG(ctx->L, envArg, args, &node->loc);
    CXY_REQUIRED_ARG(ctx->L, targetClass, args, &node->loc);
    
    // 2. Load environment (context passed from caller)
    cxyPluginLoadEnvironment(
        ctx, &node->loc, envArg,
        {"SomeType"},      // variable names to load
        {"someFunction"}
    );
    
    // 3. Access loaded environment variables
    AstNode *someType = env[0].value;
    AstNode *someFunc = env[1].value;
    
    // 4. Generate AST nodes
    AstNode *result = makeAstNode(...);
    
    return result;
}
```

## Argument Handling

### Required Arguments

Use `CXY_REQUIRED_ARG` to extract required arguments. If missing, an error is logged and `NULL` is returned:

```c
CXY_REQUIRED_ARG(ctx->L, argName, args, &node->loc);
// 'argName' is now available as AstNode*
```

### Optional Arguments

Use `CXY_OPTIONAL_ARG` for optional parameters:

```c
CXY_OPTIONAL_ARG(ctx->L, optionalArg, args);
// 'optionalArg' may be NULL
```

### Manual Argument Popping

You can manually pop arguments from the list:

```c
AstNode *arg = cxyPluginArgsPop(&args);
if (arg == NULL) {
    // handle missing argument
}
```

## Environment Loading

The environment mechanism allows your plugin actions to receive type/function references from the calling code. This enables type-safe code generation.

### Single Environment Variable

```c
cxyPluginLoadEnvironment(
    ctx, &node->loc, envArg,
    {"VariableName"}
);
// Access via: env[0].value
```

### Multiple Environment Variables

Pass a tuple from the caller:

```c
cxyPluginLoadEnvironment(
    ctx, &node->loc, envArg,
    {"Type1"},
    {"Type2"},
    {"Function1"}
);
// Access via: env[0].value, env[1].value, env[2].value
```

### How It Works

1. Caller passes types/functions as arguments to the plugin action
2. Plugin requests named variables via `cxyPluginLoadEnvironment`
3. Macro expands to create `env[]` array
4. Each entry contains `.name` and `.value` (the AST node)

**Cxy Source Example:**

```cxy
import plugin 'myplugin' as mp

class Generated {
    // Compile-time invocation with ! operator
    mp.myAction!((MyType, someFunction), targetClass)
    // Generated code is injected here
}
```

The plugin receives `MyType` and `someFunction` in the environment and injects generated methods/fields into the class.
</text>

<old_text line=343>
### Usage in Cxy Code

```cxy
@addClientMethods((submit, Exception, Void), ApiInterface)
class ApiClient {
    // Generated methods will be inserted here
}
```

## AST Generation

Plugins generate code by creating AST nodes using the memory pool.

### Common AST Construction Functions

```c
// Function declaration
AstNode *makeFunctionDecl(MemPool *pool, const FileLoc *loc,
                         cstring name, AstNode *params,
                         AstNode *retType, AstNode *body,
                         u64 flags, ...);

// Variable declaration
AstNode *makeVarDecl(MemPool *pool, const FileLoc *loc,
                    u64 flags, cstring name, AstNode *type,
                    AstNode *init, ...);

// Function call
AstNode *makeCallExpr(MemPool *pool, const FileLoc *loc,
                     AstNode *callee, AstNode *args,
                     u64 flags, ...);

// Return statement
AstNode *makeReturnAstNode(MemPool *pool, const FileLoc *loc,
                          u64 flags, AstNode *expr, ...);

// Block statement
AstNode *makeBlockStmt(MemPool *pool, const FileLoc *loc,
                      AstNode *stmts, ...);
```

### Linking Nodes

AST nodes are linked via the `next` pointer for sibling nodes:

```c
AstNode *param1 = makeFunctionParam(...);
AstNode *param2 = makeFunctionParam(...);
param1->next = param2;  // Link parameters
```

Or use `AstNodeList` for building lists:

```c
AstNodeList params = {};
insertAstNode(&params, param1);
insertAstNode(&params, param2);
// Use params.first to get the head
```

## Type System Access

Access the compiler's type table:

```c
TypeTable *types = cxyPluginGetTypeTable(ctx);
```

### Working with Types

```c
const Type *type = resolveAstNode(node)->type;

// Type checking
if (typeIs(type, Class)) { /* ... */ }
if (isVoidType(type)) { /* ... */ }

// Access class members
for (int i = 0; i < type->tClass.members->count; i++) {
    const NamedTypeMember *member = &type->tClass.members->members[i];
    // Process member
}

// Function type
const Type *retType = type->func.retType;
const Type *param0Type = type->func.params[0];
```

## Example: JSON-RPC Plugin

The `jsonrpc` plugin demonstrates a real-world use case: generating client and server RPC wrappers.

### Plugin Initialization

```c
bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    internCommonStrings(ctx->strings);
    
    jsonrpc_submit = makeString(ctx->strings, "submit");
    jsonrpc_api = makeString(ctx->strings, "api");
    
    cxyPluginRegisterAction(
        ctx, loc,
        (CxyPluginAction[]){
            {.name = "addClientMethods", .fn = addClientMethods},
            {.name = "addServerMethods", .fn = addServerMethods},
        },
        2
    );
    return true;
}
```

### Client Method Generation

The `addClientMethods` action:
1. Takes a base class with `@api` annotated methods
2. Generates wrapper methods that serialize calls
3. Returns generated methods as AST nodes

```c
static AstNode *addClientMethods(CxyPluginContext *ctx,
                                 const AstNode *node,
                                 AstNode *args)
{
    CXY_REQUIRED_ARG(ctx->L, envArg, args, &node->loc);
    cxyPluginLoadEnvironment(
        ctx, &node->loc, envArg,
        {"submit"},      // RPC submit function
        {"Exception"},   // Exception type
        {"Void"}         // Void type
    );
    CXY_REQUIRED_ARG(ctx->L, base, args, &node->loc);
    
    // Iterate over class methods
    const Type *type = resolveAstNode(base)->type;
    AstNodeList methods = {};
    
    for (int i = 0; i < type->tClass.members->count; i++) {
        const NamedTypeMember *member = &type->tClass.members->members[i];
        if (!nodeIs(member->decl, FuncDecl))
            continue;
            
        // Check for @api attribute
        const AstNode *api = findAttribute(member->decl, jsonrpc_api);
        if (api == NULL)
            continue;
            
        // Generate wrapper method
        insertAstNode(&methods,
            clientRpcMethodWrapper(ctx, &node->loc, env, member->decl));
    }
    
    return methods.first;
}
```

### Usage in Cxy Code

The JSON-RPC plugin injects RPC methods into generic client and server classes:

```cxy
// From stdlib/jsonrpc.cxy
import plugin "jsonrpc" as jsonrpc

pub class JSONRPCClient[T] {
    // ... client implementation ...
    
    func submit[T](method: string, ...args: auto): !T {
        // Submit implementation
    }
    
    // Plugin action injects methods at compile-time
    jsonrpc.addClientMethods!((submit, Exception, Void), #T)
    // For Calculator class, this generates: add(a: u32, b: u32)
}

pub class JSONRPCServer[T] {
    // ... server implementation ...
    
    func param[T](params: &const Value, name: String, idx: i32): !T {
        // Parameter extraction
    }
    
    // Plugin action injects handler methods at compile-time
    jsonrpc.addServerMethods!((Value, api, param, Exception, Void), #T)
    // For Calculator class, this generates: add(params: &const Value)
}
```

**How it works:**

1. User defines an API interface with `@api` annotations:
   ```cxy
   pub class Calculator {
       @api
       func add(a: u32, b: u32) => a + b
   }
   ```

2. `JSONRPCClient[Calculator]`:
   - Plugin scans `Calculator` for `@api` methods
   - Generates client-side stub methods that serialize calls via `submit`
   - Example: `func add(a: u32, b: u32) => submit("add", ("a", a), ("b", b))`

3. `JSONRPCServer[Calculator]`:
   - Plugin scans `Calculator` for `@api` methods
   - Generates server-side handler methods that deserialize params and dispatch
   - Example: `func add(params: &const Value) { return api.add(param(params, "a", 0), param(params, "b", 1)) }`

The `!` operator executes the plugin action at **compile-time**, injecting the generated methods into the class definition.

## Loading Plugins

Plugins are loaded **on-demand** when imported in your Cxy source code. There is no need to preload them.

### Building a Plugin

Build a plugin from C source:

```bash
cxy build --plugin plugin.c -o myplugin.so
```

This creates a shared library that can be imported in Cxy code.

### Using a Plugin

Import plugins using the `import plugin` syntax and invoke actions with `!`:

```cxy
import plugin 'myplugin' as mp

class MyClass {
    // Compile-time invocation with ! operator
    mp.someAction!(args)
    // Generated code appears here
}
```

The plugin is automatically loaded when the import is encountered, and actions execute during compilation to generate/inject code.

## Best Practices

1. **Memory Management**: Always allocate AST nodes from `ctx->pool`. Never use `malloc`.

2. **String Interning**: Use `makeString(ctx->strings, str)` to intern strings. This ensures string identity comparison works.

3. **Error Reporting**: Use `logError(ctx->L, loc, msg, args)` for errors. Return `NULL` on failure.

4. **Location Info**: Preserve source location information by passing `loc` to AST construction functions. Use `builtinLoc()` for generated code.

5. **Type Safety**: Validate argument types before use:
   ```c
   const Type *type = resolveAstNode(arg)->type;
   if (!typeIs(type, Class)) {
       logError(ctx->L, &arg->loc, "expected class type", NULL);
       return NULL;
   }
   ```

6. **Flags**: Use appropriate flags (`flgGenerated`, `flgNone`, etc.) when creating nodes.

7. **Return Values**: 
   - Return `NULL` on error
   - Return a single node or linked list of nodes on success
   - Use `astNoop` if no code should be generated

## API Reference

### Core Functions

| Function | Description |
|----------|-------------|
| `cxyPluginRegisterAction` | Register one or more actions |
| `cxyPluginGetTypeTable` | Access the compiler's type table |
| `cxyPluginLoadEnvironment_` | Load environment variables (use macro) |
| `cxyPluginArgsPop` | Pop an argument from the list |

### Macros

| Macro | Description |
|-------|-------------|
| `CXY_REQUIRED_ARG` | Extract required argument, error if missing |
| `CXY_OPTIONAL_ARG` | Extract optional argument |
| `cxyPluginLoadEnvironment` | Load environment and create `env[]` array |

### AST Utilities

Include `<cxy/ast.h>` for AST construction functions:
- `makeAstNode`, `makeVarDecl`, `makeFunctionDecl`
- `makeCallExpr`, `makeReturnAstNode`, `makeBlockStmt`
- `makeTypeReferenceNode`, `makeResolvedPath`
- And many more...

### Type System

Include `<cxy/types.h>` and `<cxy/ttable.h>`:
- `typeIs(type, tag)` - Check type kind
- `resolveAstNode(node)` - Resolve to concrete node
- `isVoidType(type)` - Check for void type

## Debugging

Enable plugin debug output:

```bash
cxy --plugin-debug source.cxy
```

This shows:
- Plugin loading/initialization
- Action registration
- Action invocations
- Generated AST (if using `--dump-ast`)

## Limitations

- Plugins run during semantic analysis, after parsing but before code generation
- Cannot modify existing AST nodes (only generate new ones)
- No access to runtime values (compile-time only)
- Platform-specific binary compatibility required

## Further Reading

- See `src/cxy/stdlib/plugins/` for more plugin examples
- Refer to `src/cxy/plugin/plugin.h` for complete API
- Check `src/cxy/ast.h` for all AST construction functions
- Review `src/cxy/types.h` for type system details