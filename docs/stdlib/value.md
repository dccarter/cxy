# Universal Value Type

The Cxy standard library provides a universal value type through `stdlib/value.cxy`. This type allows you to work with dynamic data structures in a type-safe manner.

## Overview

The `Value` type is a tagged union for handling dynamic data:

- **Safe dynamic typing** - Runtime type checking with compile-time safety
- **Flexible access** - Optional returns prevent crashes on missing data
- **Format agnostic** - Same interface works with any parser (JSON, YAML, TOML)
- **Memory efficient** - Reference counting for shared data
- **Type conversion** - Cast to specific types using reflection

## Value Types

The `Value` type supports seven variants:

- **Null** - `Value()` for missing/empty values
- **Boolean** - `Value(true)` for true/false states  
- **Integer** - `Value(42)` for 64-bit signed integers
- **Float** - `Value(3.14)` for 64-bit floating point
- **String** - `Value("text")` for dynamic strings
- **Array** - `Value.Array()` for ordered collections
- **Object** - `Value.Object()` for key-value mappings

## Creating Values

```cxy
import { Value } from "stdlib/value.cxy"

// Basic types
var nullVal = Value()                    // Null
var boolVal = Value(true)                // Boolean
var intVal = Value(42)                   // Integer  
var floatVal = Value(3.14)               // Float
var strVal = Value("hello")              // String

// Collections
var arrVal = Value.Array()               // Empty array
var objVal = Value.Object()              // Empty object
```

## Type Safety

Three levels of type access for different safety needs:

### Checking Types
```cxy
if (value.isString()) { /* safe to use as string */ }
if (value.isObject()) { /* safe to access fields */ }
if (value.isArray()) { /* safe to iterate */ }
```

### Assertive Access  
```cxy
var text = value.asString()    // Throws TypeError if not string
var obj = value.asObject()     // Throws TypeError if not object
var arr = value.asArray()      // Throws TypeError if not array
```

### Optional Access
```cxy
var maybeText = value.getString()  // Returns Optional[&String]
if (maybeText) {
    println("Text: ", **maybeText)
}

var maybeInt = value.getInt()      // Returns Optional[i64]
if (maybeInt) {
    println("Number: ", *maybeInt)
}
```

## Working with Objects

```cxy
var config = Value.Object()
config.["host"] = Value("localhost")
config.["port"] = Value(8080)
config.["ssl"] = Value(true)

// Safe navigation with Optional returns
var host = config.["host"]
if (host && host&.isString()) {
    println("Host: ", host&.asString())
}

// Check for missing keys
var timeout = config.["timeout"]
if (!timeout) {
    println("No timeout configured")
}

// Iterate over all keys
if (config.isObject()) {
    var obj = config.asObject()
    for (key, value: obj) {
        println(key, " = ", value)
    }
}
```

## Working with Arrays

```cxy
var users = Value.Array()
users.push(Value("Alice"))
users.push(Value("Bob"))
users.push(Value("Charlie"))

// Safe iteration
for (i: 0..users.size()) {
    var user = users.[i]
    if (user && user&.isString()) {
        println("User ", i, ": ", user&.asString())
    }
}

// Array operations
var first = users.pop()  // Returns Optional[Value]
if (first) {
    println("Removed: ", *first)
}

println("Array size: ", users.size())
println("Is empty: ", users.empty())
```

## Nested Structures

```cxy
var data = Value.Object()
data.["users"] = Value.Array()
data.["config"] = Value.Object()

// Build nested structure
var user = Value.Object()
user.["name"] = Value("Alice")
user.["age"] = Value(30)
user.["active"] = Value(true)

data.["users"]&.asArray().push(user)
data.["config"]&.asObject().["debug"] = Value(false)

// Navigate nested data safely
var userName = data.["users"]
if (userName && userName&.isArray()) {
    var firstUser = userName&.[0]
    if (firstUser && firstUser&.isObject()) {
        var name = firstUser&.["name"]
        if (name && name&.isString()) {
            println("First user: ", name&.asString())
        }
    }
}
```

## Type Conversion and Casting

Cast `Value` objects to specific types using reflection:

```cxy
struct User {
    name: String
    age: i32
    active: bool
}

// Assume we have a Value containing user data
var userValue = getValue()  // Returns Value

// Cast to specific type
var user = userValue.cast[User]() catch {
    stderr << "Failed to convert to User\n"
    return User{name: "Unknown", age: 0, active: false}
}

println("User: ", user.name, ", Age: ", user.age)
```

## String Representation

Values can be converted to strings for debugging or display:

```cxy
var data = Value.Object()
data.["name"] = Value("Test")
data.["count"] = Value(42)
data.["items"] = Value.Array()

// Print the entire structure
println(data)  // {"name": "Test", "count": 42, "items": []}
```

## Memory Management

Values use automatic memory management:

- **Reference counting** - Shared data is automatically cleaned up
- **Copy semantics** - Values can be safely copied and passed around
- **Move semantics** - Use `&&value` for efficient transfers

```cxy
var original = Value.Object()
original.["data"] = Value("important")

var copy = original      // Safe copy, shared data
var moved = &&original  // Move, original becomes invalid

// Both copy and moved reference the same underlying data
```

## Error Handling

Type mismatches throw `TypeError` with descriptive messages:

```cxy
var value = Value("not a number")

var number = value.asInt() catch (err: TypeError) {
    stderr << "Type error: " << err << "\n"
    return 0
}
```

## Performance Guidelines

**Use `Value` when:**
- Working with dynamic/unknown data structures
- Building generic data processing tools  
- Prototyping and experimentation
- Handling variable content from external sources

**Avoid `Value` when:**
- Data structure is known and fixed
- Processing large amounts of data where performance is critical
- Memory usage must be minimized

## Integration with Parsers

The `Value` type integrates seamlessly with structured data parsers:

```cxy
// These examples assume appropriate parser imports
var jsonData = parseJSON(jsonString)     // Returns Value
var yamlData = parseYAML(yamlString)     // Returns Value  
var tomlData = parseTOML(tomlString)     // Returns Value

// All use the same Value interface
var name = jsonData.["name"]&.asString()
var port = yamlData.["server"].["port"]&.asInt()
var debug = tomlData.["debug"]&.asBool()
```
