# JSON Parser

The Cxy standard library provides a modern JSON parser through `stdlib/json.cxy`. This parser supports both flexible parsing into `Value` objects and high-performance direct parsing into your custom types.

## Overview

The JSON parser provides dual parsing approaches:

- **Flexible parsing** - Parse into `Value` objects for dynamic data exploration
- **Direct parsing** - Parse directly into structs/classes for maximum performance
- **Type-safe serialization** - Convert any type back to JSON with reflection
- **Custom parsing** - Implement custom `fromJSON`/`toJSON` methods for special cases
- **Attribute support** - Use `@["json"]` attributes to customize behavior
- **Error reporting** - Clear error messages with line/column information

## Getting Started

Parse JSON into a `Value` for flexible data handling:

```cxy
import "stdlib/json.cxy" as json

func main(): i32 {
    var data = json.parse[Value]("""{"name": "Alice", "age": 30}""".s)
    
    var name = data.["name"]
    if (name && name&.isString()) {
        println("Name: ", name&.asString())
    }
    
    return 0
}
```

## Flexible Parsing with Value

Parse JSON into `Value` objects when structure is unknown or variable:

```cxy
var config = json.parse[Value](jsonString)

// Navigate dynamic data safely
var dbConfig = config.["database"]
if (dbConfig && dbConfig&.isObject()) {
    var host = dbConfig&.["host"]
    var port = dbConfig&.["port"]
    
    if (host && host&.isString() && port && port&.isInt()) {
        println("DB: ", host&.asString(), ":", port&.asInt())
    }
}

// Handle arrays
var users = config.["users"]
if (users && users&.isArray()) {
    for (i: 0..users&.size()) {
        var user = users&.[i]
        if (user && user&.isObject()) {
            println("User: ", user)
        }
    }
}
```

## Direct Type Parsing

Parse directly into your types for better performance:

```cxy
@json
struct User {
    name: String
    age: i32
    active: bool = true  // Default values supported
}

@json
struct Config {
    users: Vector[User]
    database: DatabaseConfig
    timeout: Optional[i32]  // Optional fields
}

var config = json.parse[Config](jsonString)
println("Loaded ", config.users.size(), " users")
```

## Parsing Attributes

Customize parsing behavior with attributes:

### Basic JSON Support
```cxy
@json
struct ServerConfig {
    host: String
    port: i32
}
```

### Partial Parsing
```cxy
@json(partial: true)
struct ApiResponse {
    success: bool
    data: String
    // Unknown fields in JSON will be ignored
}
```

### Base64 Decoding
```cxy
@json
struct SecureConfig {
    username: String
    @json(b64: true)
    password: String  // Automatically base64 decoded
}
```

## JSON Attributes Reference

The JSON parser supports several attributes to customize parsing and serialization behavior:

### `@json` - Enable JSON Support
Marks a struct or class for JSON parsing. Required for all types that should be parseable from JSON.

```cxy
@json
struct User {
    name: String
    age: i32
}

// Without @json, this would cause a compile error:
// error: type 'User' cannot be implicitly parsed from json
```

### `@json(partial: true)` - Allow Unknown Fields
Enables partial parsing where unknown JSON fields are ignored instead of causing errors.

```cxy
@json(partial: true)
struct Config {
    host: String
    port: i32
    // JSON with additional fields like "timeout", "ssl" will be accepted
}

// Parses successfully: {"host": "localhost", "port": 8080, "unknown_field": "ignored"}
```

### `@json(b64: true)` - Base64 String Decoding
Automatically decodes base64-encoded strings during parsing and encodes them during serialization.

```cxy
@json
struct SecureData {
    username: String
    @json(b64: true)
    password: String      // Stored as decoded bytes, transmitted as base64
    @json(b64: true)
    apiKey: String
}

// JSON: {"username": "alice", "password": "cGFzc3dvcmQ=", "apiKey": "YWJjZGVmZ2g="}
// password field contains decoded "password", apiKey contains decoded "abcdefgh"
```

### Combining Attributes
Multiple attributes can be combined:

```cxy
@[json, other_attr]  // Multiple attributes using array syntax

@json(partial: true)
struct FlexibleConfig {
    @json(b64: true)
    secret: String           // Field-specific b64 decoding
    host: String
    // Unknown fields ignored due to "partial: true"
}
```

### Attribute Inheritance
Classes inherit JSON parsing capabilities but can override behavior:

```cxy
@json
class BaseEntity {
    id: String
    createdAt: String
}

@json(partial: true)
class User : BaseEntity {
    name: String
    email: String
    // Inherits id, createdAt from BaseEntity
    // But uses partial parsing for unknown fields
}
```

### Field-Level Attributes
Individual fields can have their own JSON-specific attributes:

```cxy
@json
struct Document {
    title: String
    @json(b64: true)
    content: String          // Base64 encoded content
    metadata: String         // Regular string field
}
```

### Error Behavior
Different attribute combinations produce different error behaviors:

```cxy
// Strict parsing - fails on unknown fields
@json
struct StrictConfig {
    host: String
    port: i32
}

// Flexible parsing - ignores unknown fields  
@json(partial: true)
struct FlexibleConfig {
    host: String
    port: i32
}

// Custom parsing - complete control
struct CustomConfig {
    // No @json attribute - must implement fromJSON/toJSON
    func fromJSON(parser: &json.JsonParser): !void { ... }
    func toJSON(os: &OutputStream): void { ... }
}
```

## Custom Parsing Methods

Implement custom parsing for types that need special handling:

```cxy
struct Timestamp {
    epochSeconds: i64
    
    func `init`() {
        epochSeconds = 0
    }
    
    func fromJSON(parser: &json.JsonParser): !void {
        var timeStr = parser.expectString()
        epochSeconds = parseTimestamp(timeStr)
    }
    
    const func toJSON(os: &OutputStream): void {
        os << "\"" << formatTimestamp(epochSeconds) << "\""
    }
}

@json
struct Event {
    name: String
    timestamp: Timestamp  // Uses custom parsing automatically
}
```

## Vector and Array Support

Vectors are automatically handled:

```cxy
@json
struct Playlist {
    name: String
    songs: Vector[String]
    ratings: Vector[f64]
}

// Parses: {"name": "My Mix", "songs": ["Song 1", "Song 2"], "ratings": [4.5, 3.8]}
```

## Optional Type Support

Optional fields handle missing JSON data gracefully:

```cxy
@json
struct UserProfile {
    name: String
    email: Optional[String]     // Can be null or missing
    age: Optional[i32]
    avatar: Optional[String]
}

// Handles: {"name": "Alice"} or {"name": "Bob", "email": "bob@example.com", "age": null}
```

## Serialization

Convert any type back to JSON:

```cxy
struct Product {
    name: String
    price: f64
    inStock: bool
    @json(b64: true)
    description: String
}

var product = Product{
    name: "Widget",
    price: 19.99,
    inStock: true,
    description: "A useful widget"
}

// Stream to output
json.toJSON(&cout, &product)

// For string output, write to string stream or use Value
var output = StringStream()
json.toJSON(&output, &product)
var jsonStr = output.toString()
```

## Error Handling

JSON parsing uses exceptions for clear error reporting:

```cxy
var config = json.parse[Config](data) catch (err: json.JsonError) {
    stderr << "JSON parse error: " << err << "\n"
    return getDefaultConfig()
}

// Convert to Optional for graceful handling
func tryLoadConfig(path: String): Optional[Config] {
    var content = readFile(path) catch return None[Config]()
    return json.parse[Config](content) catch None[Config]()
}
```

## Performance Guidelines

**Use `Value` parsing when:**
- JSON structure is unknown or highly variable
- Building generic JSON processing tools
- Prototyping and development
- Processing configuration with optional sections

**Use direct parsing when:**
- JSON structure is known and stable
- Processing large amounts of data
- Performance is critical
- Building production APIs

Direct parsing can be 3-5x faster for large datasets by eliminating intermediate allocations.

## Type Support

### Supported Types for Direct Parsing
- **Primitives**: `bool`, `i32`, `i64`, `f32`, `f64`
- **Strings**: `String` class (not primitive `string`)
- **Collections**: `Vector[T]` for any supported type T
- **Optionals**: `Optional[T]` for nullable fields
- **Structs/Classes**: With `@["json"]` attribute or custom methods

### JSON to Cxy Type Mapping
- JSON `null` → `None` for Optional types
- JSON `true`/`false` → `bool`
- JSON numbers → `i64` or `f64` based on target type
- JSON strings → `String`
- JSON arrays → `Vector[T]`
- JSON objects → Struct/Class or `HashMap[String, Value]`

## Advanced Features

### Nested Structures
```cxy
@json
struct Address {
    street: String
    city: String
    zipCode: String
}

@json
struct Company {
    name: String
    address: Address           // Nested struct
    employees: Vector[User]    // Array of structs
}
```

### Inheritance Support
```cxy
@json
class Vehicle {
    make: String
    model: String
    
    func `init`() {}
    
    // Base class can have custom parsing
    func fromJSON(parser: &json.JsonParser): !void {
        // Custom parsing logic
    }
}

@json
class Car : Vehicle {
    doors: i32
    // Inherits JSON parsing from Vehicle
}
```

### Polymorphic Parsing
```cxy
@json
struct ApiResponse {
    success: bool
    message: String
    data: Value  // Can hold any JSON structure
}

var response = json.parse[ApiResponse](jsonString)
if (response.data.isArray()) {
    // Handle array data
} else if (response.data.isObject()) {
    // Handle object data  
}
```
