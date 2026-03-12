# YAML Parser

The Cxy standard library provides a complete YAML 1.2 parser and serializer through `stdlib/yaml.cxy`. This implementation supports flexible parsing into `Value` objects, direct type casting, and YAML-specific features like anchors, aliases, and merge keys.

## Overview

The YAML parser provides:

- **Flexible parsing** - Parse into `Value` objects for dynamic data exploration
- **Type casting** - Cast parsed `Value` to custom types with `fromYaml[T]`
- **YAML 1.2 compliance** - Follows YAML 1.2 specification
- **Advanced features** - Anchors (`&`), aliases (`*`), and merge keys (`<<`)
- **Multi-document support** - Parse YAML files with multiple documents
- **Serialization** - Convert `Value` objects back to YAML format
- **Error reporting** - Clear error messages with line/column information

## Getting Started

Parse YAML into a `Value` for flexible data handling:

```cxy
import "stdlib/yaml.cxy" as yaml

func main(): i32 {
    var data = yaml.parse("""
name: Alice
age: 30
""")
    
    var name = data.["name"]
    if (name) {
        println("Name: ", name)
    }
    
    return 0
}
```

## Flexible Parsing with Value

Parse YAML into `Value` objects when structure is unknown or variable:

```cxy
var config = yaml.parse("""
database:
  host: localhost
  port: 5432
  credentials:
    user: admin
    password: secret
""")

// Navigate dynamic data safely
var dbConfig = config.["database"]
if (dbConfig&.isObject()) {
    var host = dbConfig&.get("host")
    var port = dbConfig&.get("port")
    
    if (host && port) {
        println("DB: ", host, ":", port)
    }
}

// Handle arrays
var users = config.["users"]
if (users&.isArray()) {
    for (i: 0..users&.size()) {
        var user = users&.get(i)
        if (user&.isObject()) {
            println("User: ", user)
        }
    }
}
```

## Type Casting with fromYaml

Parse YAML and cast to your types using the `Value` cast mechanism:

```cxy
struct User {
    name: String
    age: i64
    active: bool
}

struct Config {
    users: Vector[User]
    database: DatabaseConfig
}

// Parse and cast to Config type
var config = yaml.fromYaml[Config](yamlString)
println("Loaded ", config.users.size(), " users")
```

**Note**: YAML parsing uses the `Value` type system. Type casting relies on `Value.cast[T]()`, so types must be compatible with `Value` representation. No special `@yaml` attribute is needed - casting works through the existing `Value` type system.

## YAML-Specific Features

### Anchors and Aliases

Reuse YAML structures with anchors (`&`) and aliases (`*`):

```cxy
var config = yaml.parse("""
defaults: &default_settings
  timeout: 30
  retries: 3
  debug: false

production:
  <<: *default_settings
  debug: false

development:
  <<: *default_settings
  debug: true
  timeout: 60
""")

// Both production and development inherit from default_settings
var prod = config.["production"]
println("Prod timeout: ", prod&.get("timeout"))  // 30

var dev = config.["development"]
println("Dev timeout: ", dev&.get("timeout"))    // 60 (overridden)
println("Dev debug: ", dev&.get("debug"))       // true
```

**Anchor & Alias Semantics:**
- Anchors (`&name`) mark a value for reuse
- Aliases (`*name`) reference the anchored value
- Values are **copied** (not referenced) - modifications don't affect the original
- Anchors are scoped per document in multi-document YAML

### Merge Keys

Use `<<` to merge maps (objects) together:

```cxy
var yaml = yaml.parse("""
base: &base
  a: 1
  b: 2
  c: 3

child:
  <<: *base    # Merge base into child
  c: 300       # Override specific values
  d: 4         # Add new values
""")

var child = yaml.["child"]
// child contains: {a: 1, b: 2, c: 300, d: 4}
```

**Merge multiple maps:**
```cxy
credentials: &creds
  user: admin
  
settings: &settings
  timeout: 30

service:
  <<: [*creds, *settings]  # Merge both
  endpoint: /api
```

### Multi-Document YAML

Parse YAML files with multiple documents:

```cxy
var docs = yaml.parse("""
---
name: First Document
type: config
---
name: Second Document
type: data
---
name: Third Document
type: schema
""")

// Returns an array of Values
if (docs.isArray()) {
    println("Loaded ", docs.size(), " documents")
    
    for (i: 0..docs.size()) {
        var doc = docs.[i]
        var name = doc&.get("name")
        println("Document: ", name)
    }
}
```

**Note**: 
- Single document returns a `Value`
- Multiple documents (with `---` markers) returns a `Value.Array()` containing all documents
- Anchors are scoped per document - cannot reference anchors from previous documents

## Block and Flow Styles

YAML supports both block (indentation-based) and flow (JSON-like) styles:

### Block Style
```yaml
person:
  name: John
  age: 30
  address:
    street: 123 Main St
    city: Boston

items:
  - apple
  - banana
  - orange
```

### Flow Style
```yaml
person: {name: John, age: 30}
items: [apple, banana, orange]
mixed: {users: [alice, bob], count: 2}
```

Both styles parse identically into `Value` objects.

## Multiline Strings

YAML supports special syntax for multiline strings:

### Literal Style (`|`)
Preserves newlines exactly:
```cxy
var yaml = yaml.parse("""
text: |
  Line 1
  Line 2
  Line 3
""")

// text contains: "Line 1\nLine 2\nLine 3\n"
```

### Folded Style (`>`)
Folds newlines into spaces:
```cxy
var yaml = yaml.parse("""
description: >
  This is a long
  description that
  spans multiple lines.
""")

// description contains: "This is a long description that spans multiple lines.\n"
```

### Chomping Indicators
Control trailing newlines:
```yaml
# Strip trailing newlines
text1: |-
  content

# Clip to single newline (default)
text2: |
  content

# Keep all trailing newlines  
text3: |+
  content

```

## Serialization

Convert `Value` objects back to YAML:

```cxy
import "stdlib/yaml.cxy" as yaml

// Create a Value structure
var config = Value.Object()
config.["name"] = Value("MyApp")
config.["version"] = Value(1.0)

var settings = Value.Object()
settings.["debug"] = Value(true)
settings.["port"] = Value(8080)
config.["settings"] = &&settings

// Serialize to YAML string
var yamlStr = yaml.stringify(&config)
println(yamlStr)

// Output:
// name: MyApp
// version: 1
// settings:
//   debug: true
//   port: 8080
```

### Custom Indentation

```cxy
// Default indentation is 2 spaces
var yaml1 = yaml.stringify(&config)

// Use 4 spaces for indentation
var yaml2 = yaml.stringify(&config, 4)

// Using dump() alias
var yaml3 = yaml.dump(&config, 4)
```

### Serialization Behavior

**Scalar types:**
- `null` → `null`
- Booleans → `true` / `false`
- Numbers → numeric representation
- Strings → unquoted (if safe) or quoted

**Collections:**
- Empty objects → `{}`
- Empty arrays → `[]`
- Non-empty → block style with indentation

**String quoting:**
Strings are automatically quoted when they:
- Are empty
- Start with YAML indicators (`-`, `?`, `:`, `{`, `}`, `[`, `]`, `,`, `&`, `*`, `#`, etc.)
- Match reserved words (`true`, `false`, `null`, `~`)
- Contain special characters (newlines, tabs, quotes)
- Contain `: ` (colon-space sequence)

**Example:**
```cxy
var obj = Value.Object()
obj.["safe"] = Value("hello")           // safe: hello
obj.["quoted"] = Value("true")          // quoted: "true"
obj.["special"] = Value("-value")       // special: "-value"
obj.["newline"] = Value("line1\nline2") // newline: "line1\nline2"
```

## Error Handling

YAML parsing uses exceptions for error reporting:

```cxy
var config = yaml.parse(yamlData) catch (err: yaml.YamlError) {
    stderr << "YAML parse error: " << err << "\n"
    return getDefaultConfig()
}

// Convert to Optional for graceful handling
func tryLoadConfig(path: String): Optional[Value] {
    var content = readFile(path) catch return None[Value]()
    return yaml.parse(content) catch None[Value]()
}
```

Common errors:
- **Syntax errors**: Invalid YAML structure, unterminated strings
- **Indentation errors**: Inconsistent indentation levels
- **Undefined aliases**: Reference to non-existent anchor
- **Invalid escape sequences**: Malformed `\x` or `\u` sequences

## Type Support

### YAML to Value Type Mapping
- YAML `null` / `~` → `Value()` (null Value)
- YAML `true` / `false` → `Value(bool)`
- YAML numbers → `Value(i64)` or `Value(f64)`
- YAML strings → `Value(String)`
- YAML sequences → `Value.Array()` (Vector[Value])
- YAML mappings → `Value.Object()` (HashMap[String, Value])

### Supported Types for Casting

Since YAML uses the `Value` type system, any type that works with `Value.cast[T]()` is supported:

- **Primitives**: `bool`, `i32`, `i64`, `f32`, `f64`
- **Strings**: `String` class
- **Collections**: `Vector[T]` for any supported type T
- **Structs/Classes**: Must be compatible with Value representation

## YAML 1.2 Compliance

This implementation follows YAML 1.2 specification:

**Boolean values:**
- Only `true` and `false` are recognized as booleans
- YAML 1.1 values (`yes`, `no`, `on`, `off`, `Yes`, `No`) are treated as strings

**Null values:**
- `null` and `~` are recognized as null
- Empty values in some contexts may also be null

**Escape sequences:**
- Comprehensive escape support in double-quoted strings
- `\n`, `\t`, `\r`, `\\`, `\"`, `\'`
- `\x##` (hex), `\u####` (unicode 16-bit), `\U########` (unicode 32-bit)
- Control characters: `\0`, `\a`, `\b`, `\v`, `\f`, `\e`

## Performance Guidelines

**Use `Value` parsing when:**
- YAML structure is unknown or highly variable
- Working with configuration files
- Prototyping and development
- Need YAML-specific features (anchors, merge keys)

**Consider alternatives when:**
- Processing extremely large datasets (>100MB)
- Need streaming processing
- Performance is absolutely critical

The YAML parser is optimized for typical configuration files and data structures. For most use cases, performance is excellent.

## Examples

### Configuration File
```cxy
var config = yaml.parse("""
app:
  name: MyService
  version: 1.0.0
  
database: &db_defaults
  host: localhost
  port: 5432
  pool_size: 10

environments:
  development:
    database:
      <<: *db_defaults
      name: myapp_dev
  
  production:
    database:
      <<: *db_defaults
      host: db.production.com
      name: myapp_prod
      pool_size: 50
""")

var prodDb = config.["environments"].["production"].["database"]
println("Prod DB: ", prodDb&.get("host"))
```

### API Response
```cxy
struct User {
    id: i64
    name: String
    email: String
}

var response = yaml.parse("""
status: success
users:
  - id: 1
    name: Alice
    email: alice@example.com
  - id: 2
    name: Bob
    email: bob@example.com
""")

// Cast users array
var usersValue = response.["users"]
if (usersValue&.isArray()) {
    for (i: 0..usersValue&.size()) {
        var userVal = usersValue&.get(i)
        var user = userVal&.cast[User]()
        println("User: ", user.name, " <", user.email, ">")
    }
}
```

### Round-Trip (Parse and Serialize)
```cxy
// Parse YAML
var original = yaml.parse("""
name: Test
items: [a, b, c]
nested:
  key: value
""")

// Modify
original.["items"].push(Value("d"))
original.["nested"].["key"] = Value("updated")

// Serialize back
var updated = yaml.stringify(&original)
println(updated)
```

## API Reference

### `parse(str: __string): !Value`
Parse a YAML string into a `Value`.

**Returns:**
- Single document: `Value` (object, array, or scalar)
- Multiple documents: `Value.Array()` containing all documents

**Throws:** `YamlError` on parse errors

### `fromYaml[T](str: __string): !T`
Parse YAML and cast to type `T`.

**Returns:** Instance of type `T`

**Throws:** 
- `YamlError` on parse errors
- Cast errors if Value cannot be converted to type `T`

### `stringify(value: &const Value, indent: u32 = 2): String`
Serialize a `Value` to YAML string.

**Parameters:**
- `value`: The Value to serialize
- `indent`: Spaces per indentation level (default: 2)

**Returns:** YAML string representation

### `dump(value: &const Value, indent: u32 = 2): String`
Alias for `stringify()`. Matches common YAML library conventions (PyYAML, etc.).

## Limitations

**Not currently supported:**
- YAML tags (`!!str`, `!!int`, custom tags)
- Complex keys (non-string mapping keys)
- Explicit key indicator (`? key\n: value`)
- YAML 1.1 compatibility mode
- Streaming/incremental parsing

These features may be added in future versions if needed.