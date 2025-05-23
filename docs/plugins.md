# Plugins

> This is a complex part of cxy, plugin authors must understand the internal workings of the compiler.
> :construction: This is still a work in progress, things might change.

`cxy` compiler can be extended through plugins, which are libraries written in `c` (or `c++`) and loaded by
`cxy` at compile time. The idea behind plugins is to generate code that would otherwise be impossible a hard to do in
`cxy`. This is done through actions, which are named functions provided by the plugin cxy can invoke at compile time.

```c
// Include cxy headers needed to build the plugin
#include <cxy/core/log.h>
#include <cxy/plugin.h>

// Each plugin must export this function. This will be invoked when the plugin is loaded
bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    logNote(ctx->L, loc, "Hello from plugin!", NULL);
    return true;
}

// Each plugin must export this function. This will be invoked when the plugin is deinitialized
void pluginDeinit(CxyPluginContext *ctx)
{
}
```

## Plugin Action

A plugin action is named `c` function that can be invoked in `cxy` as a macro at compile time. The `c` function needs to
be registered as an action to be available in `cxy`.

```c
// An action is a function that takes a plugin context, a `node`, which is the invocation AST node and
// `args` which is a list of arguments passed when the action was invoked.
//
// Actions must return an `AstNode` which will replace the invocation AST node. If NULL is returned, the compiler
// will spit an error.
//
// `astNoop` can be returned if the action wishes to return nothing
AstNode *demoHello(CxyPluginContext *ctx, const AstNode *node, AstNode *args)
{
    return makeStringLiteral(ctx->pool, &node->loc, "Hello World!", NULL, NULL);
}

bool pluginInit(CxyPluginContext *ctx, const FileLoc *loc)
{
    ...
    // Register the plugin('s) action using `cxyPluginRegisterAction` macro. Currently here is no limit on the
    // number of actions that can be registered
    cxyPluginRegisterAction(
        ctx,
        loc,
        (CxyPluginAction[]){
            {.name = "hello", .fn = demoHello}
        },
        1);
    ...
}
```

## Building The Plugin

Plugin source code will need to be transformed into a shared library that can be loaded by `cxy`. Simply invoking
`cxy build --plugin <source.c>` should do the job.
:note: Plugins must be built before building the code that uses the plugin, `cxy` will fail if it can't find the plugin.

```makefile
#app/Makefile

build-plugin: plugins/example.c
    cxy build --plugin $^

# Ensure that the plugin is built before building the app
app: build-plugin
    cxy build app.cxy
```

## Loading a Plugin

A plugin can be loaded as an import in `cxy` code. Once loaded, the plugin will be cached such that future imports don't
have to reload the plugin.

```c
// 'plugin' keyword after plugin is required, so is the alias
import plugin "example" as example

func main() {
    // Invoke an action (see example above).
    // This will print "Hello World"
    println(example.hello!)
}
```

**See** [stdlib/plugins/jsonrpc.c](../src/cxy/stdlib/plugins/jsonrpc.c)
and [stdlib/jsonrpc.cxy](../src/cxy/stdlib/jsonrpc.cxy) for more examples

## Future

- What if we want to load a module using plugins, maybe add support for import time invocation where the action will
  return a module declaration. Assume we want to compile a protobuf file into a module, we do sometime like

    ```c
    import plugin "protobuf" (
        compile!("message.proto") as messages,
        compile!("service.proto") as service
    )
    ```

  This syntax would allow us to load the imaginary protobuf plugin and immediately invoke some actions from the plugin
  that return module AST nodes.
- Currently, there is nothing preventing us from invoking actions anywhere. This could lead to compiler crashes if an
  action is invoked in the wrong place. We need a mechanism to restrict action invocation to certain contexts. e.g. if
  an action generations function declarations, it should only be invocable at module or class or struct level scope.

    ```c
    #define PLUGIN_SCOPES(f)    \
        f(Import)               \
        f(Module)               \
        f(Class)                \
        f(Struct)               \
        f(Function)             \
        f(Expression)           \
    ```
  Having such scopes would also allow the compiler to validate the AST nodes returned from the module