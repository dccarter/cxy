# TOML Parser Design for Cxy

This document outlines the design for a TOML (Tom's Obvious, Minimal Language) parser that leverages the universal `Value` type and follows the architectural patterns established in `json.cxy`.

## Overview

TOML is a configuration file format that's easy to read due to obvious semantics. Our parser will convert TOML documents into a structured `Value` representation that can contain nested objects, arrays, and primitive values, following the same patterns as the JSON parser implementation.

## TOML Language Support

### Supported TOML Features

#### Basic Types
- **Strings**: `key = "value"`, `key = 'literal'`, `key = """multiline"""`
- **Integers**: `key = 42`, `key = +17`, `key = -5`, `key = 0x1A2B`, `key = 0o755`, `key = 0b11010110`
- **Floats**: `key = 3.14`, `key = 5e+22`, `key = 6.626e-34`, `key = inf`, `key = nan`
- **Booleans**: `key = true`, `key = false`
- **Dates**: `key = 1979-05-27T07:32:00Z`, `key = 1979-05-27`, `key = 07:32:00`

#### Collections
- **Arrays**: `key = [1, 2, 3]`, `mixed = [1, "two", 3.0]`
- **Tables**: `[section]` and `[[array-of-tables]]`
- **Inline Tables**: `key = { x = 1, y = 2 }`

#### Advanced Features
- **Nested Tables**: `[database.server]`
- **Array of Tables**: `[[products]]`
- **Dotted Keys**: `physical.color = "orange"`
- **Comments**: `# This is a comment`

## Architecture

### Core Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   TomlLexer     │───▶│   TomlParser    │───▶│     Value       │
│                 │    │                 │    │                 │
│ - nextToken()   │    │ - parse()       │    │ - HashMap       │
│ - peek()        │    │ - parseTable()  │    │ - Vector        │
│ - advance()     │    │ - parseValue()  │    │ - primitives    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

### Value Type Integration

The parser utilizes the existing `Value` struct that supports recursive `HashMap[String, Value]` usage:

```cxy
pub struct Value {
    _value: Null | bool | i64 | f64 | String | Vector[Value] | HashMap[String, Value] = Null{}
}
```

This enables TOML structures like:
```toml
[database]
server = "192.168.1.1"
ports = [ 8001, 8001, 8002 ]

[database.connection_max]
enabled = true
max = 5000

[[products]]
name = "Hammer"
sku = 738594937

[[products]]
name = "Nail"
sku = 284758393
```

## Implementation Design

### File Structure

```
src/cxy/stdlib/toml.cxy
├── TomlToken (enum)
├── TomlLexer (class)
├── TomlParser (class)
├── TomlError (exception)
└── Public API functions
```

### Token Types

```cxy
enum TomlToken {
    // Literals
    String,
    Integer, 
    Float,
    Boolean,
    DateTime,
    
    // Structural
    LeftBracket,    // [
    RightBracket,   // ]
    DoubleBracket,  // [[
    LeftBrace,      // {
    RightBrace,     // }
    Equals,         // =
    Comma,          // ,
    Dot,            // .
    
    // Special
    Identifier,
    Comment,
    Newline,
    EOF,
    Invalid
}
```

### Error Handling

Following the JSON parser pattern:

```cxy
exception TomlError(msg: String) => msg != null ? msg.str() : "TOML error"
```

### Lexer Design

```cxy
class TomlLexer {
    - _input: __string
    - _position = 0 as u64
    - _line = 1 as u64
    - _column = 1 as u64
    - _tokenValue = String()
    
    func `init`(input: __string) {
        _input = input
    }
    
    func nextToken(): TomlToken
    
    @inline
    func getTokenValue() => &&_tokenValue
    
    @inline
    func getCurrentPosition() => (_line, _column)
    
    // Internal methods
    - func peek(): char
    - func advance(): char
    - func skipWhitespace()
    - func skipComment()
    - func scanString(): TomlToken
    - func scanNumber(): TomlToken
    - func scanIdentifier(): TomlToken
    - func scanDateTime(): TomlToken
    - func isAtEnd(): bool
    - func isDigit(c: char): bool
    - func isAlpha(c: char): bool
}
```

### Parser Design

Following the JSON parser architecture:

```cxy
class TomlParser {
    - _lexer: TomlLexer
    - _currentToken: TomlToken = .Invalid
    - _tokenValue: String = null
    - _depth = 0 as u32
    - _maxDepth = 1000 as u32
    - _root: Value
    - _currentTable: &Value
    
    func `init`(input: __string) {
        _lexer = TomlLexer(input)
        _root = Value.Object()
        _currentTable = &&_root
        this.advance()
    }
    
    func parse(): !Value
    
    // Accessor methods
    @inline
    func currentToken() => _currentToken
    
    @inline
    func tokenValue() => &&_tokenValue
    
    @inline
    func tag() => _lexer.getCurrentPosition()
    
    // Core parsing methods
    - func parseDocument(): !Value
    - func parseStatement(): !void
    - func parseKeyValue(): !void
    - func parseTable(): !void
    - func parseArrayOfTables(): !void
    - func parseValue(): !Value
    - func parseArray(): !Value
    - func parseInlineTable(): !Value
    
    // Token management
    - func advance(): void
    - func expectToken(expected: TomlToken): !void
    - func consumeToken(expected: TomlToken): !void
    - func expectString(): !String
    
    // Value creation helpers
    - func createString(): !Value
    - func createNumber(): !Value
    - func createBoolean(): !Value
    - func createDateTime(): !Value
    
    // Navigation helpers  
    - func navigateToTable(path: Vector[String]): !&Value
    - func ensureTableExists(path: Vector[String]): !&Value
    - func parseDottedKey(): !Vector[String]
    - func checkDepth(): !void
}
```

## Parsing Strategy

### Document Structure Parsing

1. **Root Level**: Start with empty `Value.Object()`
2. **Key-Value Pairs**: Parse into current table context
3. **Tables**: Navigate to nested structure in root object
4. **Array of Tables**: Create/append to array of objects

### Table Context Management

The parser maintains a current table context to handle nested structures:

```cxy
// For TOML:
// [database]
// host = "localhost"
// [database.credentials] 
// user = "admin"

// Results in Value structure:
// {
//   "database": {
//     "host": "localhost",
//     "credentials": {
//       "user": "admin"
//     }
//   }
// }
```

### Dotted Key Handling

Dotted keys create nested structures automatically:

```cxy
// TOML: physical.color = "orange"
// Creates: { "physical": { "color": "orange" } }
```

## Public API

Following the JSON parser API patterns:

### Core Functions

```cxy
// Generic type-specific parsing
pub func fromTOML[T](parser: &TomlParser): !T

// Convenience function that creates parser from string
pub func parse[T](str: __string): !T

// Parse TOML file 
pub func parseTomlFile[T](filepath: String): !T

// Generic serialization to OutputStream
pub func toTOML[T](os: &OutputStream, it: &const T): void

// Convenience serialization to String
pub func toTOML[T](it: &const T): String
```

### Type-Specific Parsing with Reflection

Following the JSON implementation pattern:

```cxy
pub func fromTOML[T](parser: &TomlParser): !T {
    #if (#T == #Value) {
        return parser.parse()
    } else {
        var result: T
        var value = parser.parse()
        return value.cast[T]()
    }
}
```

### Attribute Support

Support TOML-specific attributes following the established pattern:

- `@toml` — mark type as TOML-parseable
- `@toml(partial: true)` — allow ignoring unknown fields
- `@toml(key: "custom_name")` — use custom key name for field
- `@toml(inline: true)` — serialize struct as inline table

### Usage Examples

```cxy
import { parse, parseTomlFile, toTOML } from "stdlib/toml.cxy"

// Parse from string to Value
var config = parse[Value]("""
[database]
host = "localhost"
port = 5432
enabled = true

[[servers]]
name = "alpha"
ip = "10.0.0.1"

[[servers]]  
name = "beta"
ip = "10.0.0.2"
""")

// Access values
var dbHost = config["database"]["host"].asString()
var dbPort = config["database"]["port"].asInt()
var servers = config["servers"].asArray()

// Parse from file to specific type
@toml
struct Config {
    database: DatabaseConfig
    servers: Vector[ServerConfig]
}

@toml
struct DatabaseConfig {
    host: String
    port: i64
    enabled: bool
}

@toml
struct ServerConfig {
    name: String
    ip: String
}

var appConfig = parseTomlFile[Config]("config.toml")

// Serialize back to TOML
var tomlString = toTOML(appConfig)
```

## Advanced Features

### Custom Parsing/Serialization

Following the JSON pattern, types can define custom methods:

```cxy
@toml
struct CustomType {
    // Custom parsing
    static func fromTOML(parser: &TomlParser): !This {
        // Custom implementation
    }
    
    // Custom serialization  
    func toTOML(os: &OutputStream): void {
        // Custom implementation
    }
}
```

### Partial Parsing

Support for ignoring unknown fields:

```cxy
@toml(partial: true)
struct PartialConfig {
    knownField: String
    // Unknown fields in TOML will be ignored
}
```

## Performance Considerations

### Memory Efficiency

Following the JSON implementation patterns:

- Use `__string` input for zero-copy parsing
- Single-pass parsing with minimal lookahead
- Arena allocation for temporary parsing structures
- Reuse Value objects where possible

### Parsing Speed

- Direct streaming to OutputStream for serialization
- Fast table lookup using HashMap
- Efficient string handling for large documents

## Testing Strategy

### Test Categories

1. **Unit Tests**: Individual components (lexer, parser methods)
2. **Integration Tests**: Full document parsing  
3. **Compliance Tests**: Official TOML test suite compatibility
4. **Performance Tests**: Large document parsing benchmarks
5. **Error Tests**: Invalid TOML handling
6. **Round-trip Tests**: Parse → serialize → parse

### Test Cases

```cxy
test "Basic key-value parsing" {
    var result = parse[Value]('key = "value"')
    ok!(result["key"].asString() == "value")
}

test "Nested table parsing" {
    var result = parse[Value]("""
    [section.subsection]
    key = 42
    """)
    ok!(result["section"]["subsection"]["key"].asInt() == 42)
}

test "Array of tables" {
    var result = parse[Value]("""
    [[products]]
    name = "Hammer"
    
    [[products]]
    name = "Nail"
    """)
    var products = result["products"].asArray()
    ok!(products.size() == 2)
    ok!(products[0]["name"].asString() == "Hammer")
}

test "Type-specific parsing with attributes" {
    @toml
    struct Product {
        name: String
        sku: i64
    }
    
    var products = parse[Vector[Product]]("""
    [[products]]
    name = "Hammer"
    sku = 738594937
    
    [[products]]
    name = "Nail"
    sku = 284758393
    """)
    
    ok!(products.size() == 2)
    ok!(products[0].name == "Hammer")
    ok!(products[0].sku == 738594937)
}
```

## Implementation Phases

### Phase 1: Core Implementation
- Basic TOML parsing (strings, numbers, booleans) to Value
- Simple tables and key-value pairs
- Array support
- Follow json.cxy lexer/parser patterns

### Phase 2: Advanced Features  
- Inline tables
- Array of tables
- Dotted keys
- DateTime support
- Type-specific parsing with reflection

### Phase 3: Ecosystem Integration
- Attribute system (@toml, @toml(partial: true), etc.)
- Custom parsing/serialization methods
- File I/O utilities
- Error reporting improvements

### Phase 4: Optimization
- Streaming parser for large files
- Memory pool optimization
- Performance benchmarking
- Advanced numeric formats (hex, octal, binary)

## Conclusion

This TOML parser design follows the proven architectural patterns from `json.cxy`, leveraging the recursive HashMap implementation and universal `Value` type. The design provides:

- **Consistency**: Same API patterns as JSON parser (`fromTOML[T]`, `parse[T]`, `toTOML[T]`)
- **Performance**: Zero-copy parsing with `__string` input and single-pass architecture
- **Flexibility**: Support for both `Value`-based and type-specific parsing via reflection
- **Extensibility**: Attribute system for customization and partial parsing
- **Robustness**: Exception-based error handling with detailed diagnostics

The modular design allows for incremental implementation and testing, while the comprehensive error handling ensures good developer experience when working with malformed TOML files. The integration with the existing `Value` type creates a unified interface for working with structured configuration data across JSON, TOML, and future format parsers.