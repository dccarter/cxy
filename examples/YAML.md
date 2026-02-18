# YAML Parser Design for Cxy

This document outlines the design for a YAML (YAML Ain't Markup Language) parser that leverages the universal `Value` type and follows the architectural patterns established in `json.cxy`.

## Overview

YAML is a human-readable data serialization standard commonly used for configuration files, data exchange, and document markup. Our parser will convert YAML documents into a structured `Value` representation that can handle YAML's complex nested structures, references, and multi-document format, following the same patterns as the JSON parser implementation.

## YAML Language Support

### Supported YAML Features

#### Scalars
- **Strings**: `key: value`, `key: "quoted"`, `key: 'single'`, `key: |`, `key: >`
- **Numbers**: `key: 42`, `key: 3.14`, `key: 1.2e+3`, `key: 0xFF`, `key: 0o755`, `key: 0b1010`
- **Booleans**: `key: true`, `key: false`, `key: yes`, `key: no`, `key: on`, `key: off`
- **Null**: `key: null`, `key: ~`, `key:`, `key: Null`, `key: NULL`

#### Collections
- **Sequences**: `- item1`, `- item2`, `[item1, item2]`
- **Mappings**: `key: value`, `{key1: value1, key2: value2}`
- **Mixed Collections**: Nested sequences and mappings

#### Advanced Features
- **Multi-line Strings**: Literal (`|`) and folded (`>`) styles with strip/keep/clip indicators
- **Anchors and Aliases**: `&anchor` and `*reference`
- **Merge Keys**: `<<:` for mapping inheritance
- **Tags**: `!!str`, `!!int`, `!!float`, `!!bool`, `!!null`
- **Multi-document**: `---` document separator, `...` document end
- **Comments**: `# This is a comment`

#### YAML 1.2 Compatibility
- JSON compatibility mode
- Core schema types
- Proper Unicode support
- Flow and block styles

## Architecture

### Core Components

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   YamlLexer     │───▶│   YamlParser    │───▶│     Value       │
│                 │    │                 │    │                 │
│ - nextToken()   │    │ - parse()       │    │ - HashMap       │
│ - scanIndent()  │    │ - parseSeq()    │    │ - Vector        │
│ - scanString()  │    │ - parseMap()    │    │ - primitives    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                │
                       ┌─────────────────┐
                       │ AnchorRegistry  │
                       │                 │
                       │ - register()    │
                       │ - resolve()     │
                       └─────────────────┘
```

### Value Type Integration

The parser utilizes the existing `Value` struct that supports recursive `HashMap[String, Value]` usage:

```cxy
pub struct Value {
    _value: Null | bool | i64 | f64 | String | Vector[Value] | HashMap[String, Value] = Null{}
}
```

This enables complex YAML structures like:
```yaml
database:
  host: localhost
  credentials:
    username: admin
    password: secret
  replicas:
    - host: db1.example.com
      port: 5432
    - host: db2.example.com
      port: 5432

services: &services
  - web
  - api
  - worker

environments:
  production:
    services: *services
    replicas: 3
  staging:
    services: *services
    replicas: 1
```

## Implementation Design

### File Structure

```
src/cxy/stdlib/yaml.cxy
├── YamlToken (enum)
├── YamlLexer (class)
├── YamlParser (class)
├── AnchorRegistry (class)
├── IndentStack (class)
├── YamlError (exception)
└── Public API functions
```

### Token Types

```cxy
enum YamlToken {
    // Scalars
    String,
    Integer,
    Float,
    Boolean,
    Null,
    
    // Structural
    SequenceStart,      // -
    MappingKey,         // key:
    MappingValue,       // :
    FlowSeqStart,       // [
    FlowSeqEnd,         // ]
    FlowMapStart,       // {
    FlowMapEnd,         // }
    FlowSeparator,      // ,
    
    // Special
    Anchor,             // &anchor
    Alias,              // *alias
    Tag,                // !!type
    MergeKey,           // <<
    DocumentStart,      // ---
    DocumentEnd,        // ...
    BlockLiteral,       // |
    BlockFolded,        // >
    
    // Formatting
    Indent,
    Dedent,
    Newline,
    Comment,
    EOF,
    Invalid
}
```

### Error Handling

Following the JSON parser pattern:

```cxy
exception YamlError(msg: String) => msg != null ? msg.str() : "YAML error"
```

### Indent Management

```cxy
class IndentStack {
    - _levels: Vector[u32]
    
    func `init`() {
        _levels = Vector[u32]()
        _levels.push(0)  // Always start with level 0
    }
    
    func push(level: u32): void
    func pop(): u32?
    func current(): u32
    func checkLevel(level: u32): (bool, u32)  // (isValid, dedentCount)
    func clear(): void
    func size(): u64
}
```

### Anchor Registry

```cxy
class AnchorRegistry {
    - _anchors: HashMap[String, Value]
    
    func `init`() {
        _anchors = HashMap[String, Value]()
    }
    
    func registerAnchor(name: String, value: Value): void
    func resolveAlias(name: String): !Value
    func hasAnchor(name: String): bool
    func clear(): void
}
```

### Lexer Design

Following the JSON lexer architecture:

```cxy
class YamlLexer {
    - _input: __string
    - _position = 0 as u64
    - _line = 1 as u64
    - _column = 1 as u64
    - _tokenValue = String()
    - _indentStack: IndentStack
    - _blockScalarMode = false as bool
    - _flowLevel = 0 as u32
    
    func `init`(input: __string) {
        _input = input
        _indentStack = IndentStack()
    }
    
    func nextToken(): YamlToken
    
    @inline
    func getTokenValue() => &&_tokenValue
    
    @inline
    func getCurrentPosition() => (_line, _column)
    
    // Internal methods
    - func peek(offset: u32 = 0): char
    - func advance(): char
    - func scanIndentation(): u32
    - func scanPlainString(): YamlToken
    - func scanQuotedString(quote: char): YamlToken
    - func scanBlockScalar(style: char): YamlToken
    - func scanNumber(): YamlToken
    - func scanKeyword(): YamlToken
    - func skipWhitespace(): void
    - func skipComment(): void
    - func skipToNextLine(): void
    - func isAtEnd(): bool
    - func isFlowContext(): bool
}
```

### Parser Design

Following the JSON parser architecture:

```cxy
class YamlParser {
    - _lexer: YamlLexer
    - _currentToken: YamlToken = .Invalid
    - _tokenValue: String = null
    - _depth = 0 as u32
    - _maxDepth = 1000 as u32
    - _anchors: AnchorRegistry
    - _documents: Vector[Value]
    - _currentDocument: Value
    
    func `init`(input: __string) {
        _lexer = YamlLexer(input)
        _anchors = AnchorRegistry()
        _documents = Vector[Value]()
        this.advance()
    }
    
    func parse(): !Vector[Value]
    func parseSingleDocument(): !Value
    
    // Accessor methods
    @inline
    func currentToken() => _currentToken
    
    @inline
    func tokenValue() => &&_tokenValue
    
    @inline
    func tag() => _lexer.getCurrentPosition()
    
    // Document-level parsing
    - func parseDocument(): !Value
    - func parseValue(): !Value
    - func parseSequence(): !Value
    - func parseMapping(): !Value
    - func parseFlowSequence(): !Value
    - func parseFlowMapping(): !Value
    
    // Scalar parsing
    - func parseScalar(): !Value
    - func parseBlockScalar(style: char): !Value
    - func createScalarValue(): !Value
    
    // Special features
    - func handleAnchor(anchorName: String, value: Value): !Value
    - func resolveAlias(aliasName: String): !Value
    - func processTag(tag: String, value: Value): !Value
    - func processMergeKey(base: &Value, merge: Value): !void
    
    // Token management
    - func advance(): void
    - func expectToken(expected: YamlToken): !void
    - func consumeToken(expected: YamlToken): !void
    - func expectString(): !String
    - func checkDepth(): !void
    
    // Utility methods
    - func isScalar(): bool
    - func isCollectionStart(): bool
    - func parseDocumentSeparator(): !void
}
```

## Parsing Strategy

### Indentation-Based Structure

YAML uses indentation to define structure. The parser maintains an indent stack:

```yaml
# Level 0
root:
  # Level 2  
  child1: value1
  child2:
    # Level 4
    grandchild: value2
  # Back to level 2
  child3: value3
```

### Flow vs Block Collections

#### Block Style (indentation-based)
```yaml
sequence:
  - item1
  - item2
  - item3

mapping:
  key1: value1
  key2: value2
```

#### Flow Style (bracketed)
```yaml
sequence: [item1, item2, item3]
mapping: {key1: value1, key2: value2}
```

### Multi-line String Handling

#### Literal Style (`|`)
```yaml
literal: |
  These lines
  will be preserved
  exactly as written
```

#### Folded Style (`>`)
```yaml
folded: >
  These lines
  will be folded
  into a single line
```

### Anchor and Alias Resolution

Two-phase processing:
1. **Registration Phase**: Store anchors during parsing
2. **Resolution Phase**: Resolve aliases after document is complete

```yaml
# Phase 1: Store anchors
defaults: &defaults
  timeout: 30
  retries: 3

# Phase 2: Resolve aliases  
service1:
  <<: *defaults
  port: 8080

service2:
  <<: *defaults
  port: 8081
```

## Public API

Following the JSON parser API patterns:

### Core Functions

```cxy
// Generic type-specific parsing
pub func fromYAML[T](parser: &YamlParser): !T

// Convenience function that creates parser from string
pub func parse[T](str: __string): !T

// Parse single document (first document if multi-document)
pub func parseSingleDocument[T](str: __string): !T

// Parse YAML file
pub func parseYamlFile[T](filepath: String): !T

// Multi-document parsing
pub func parseDocuments(str: __string): !Vector[Value]

// Generic serialization to OutputStream
pub func toYAML[T](os: &OutputStream, it: &const T): void

// Convenience serialization to String
pub func toYAML[T](it: &const T): String

// Multi-document serialization
pub func documentsToYAML(documents: Vector[Value]): String
```

### Type-Specific Parsing with Reflection

Following the JSON implementation pattern:

```cxy
pub func fromYAML[T](parser: &YamlParser): !T {
    #if (#T == #Value) {
        return parser.parseSingleDocument()
    } else #if (#T == #Vector[Value]) {
        return parser.parse()
    } else {
        var result: T
        var value = parser.parseSingleDocument()
        return value.cast[T]()
    }
}
```

### Attribute Support

Support YAML-specific attributes following the established pattern:

- `@yaml` — mark type as YAML-parseable
- `@yaml(partial: true)` — allow ignoring unknown fields
- `@yaml(key: "custom_name")` — use custom key name for field
- `@yaml(flow: true)` — prefer flow style in serialization
- `@yaml(literal: true)` — use literal style for multi-line strings

### Usage Examples

```cxy
import { parse, parseDocuments, parseSingleDocument, toYAML } from "stdlib/yaml.cxy"

// Parse single document to Value
var config = parse[Value]("""
database:
  host: localhost
  port: 5432
  credentials:
    username: admin
    password: secret

services:
  - web
  - api
  - worker
""")

// Access nested values
var dbHost = config["database"]["host"].asString()
var services = config["services"].asArray()

// Parse to specific type
@yaml
struct Config {
    database: DatabaseConfig
    services: Vector[String]
}

@yaml
struct DatabaseConfig {
    host: String
    port: i64
    credentials: Credentials
}

@yaml
struct Credentials {
    username: String
    password: String
}

var typedConfig = parse[Config](yamlContent)

// Multi-document YAML
var docs = parseDocuments("""
---
kind: Service
metadata:
  name: web-service
spec:
  port: 80
---
kind: Deployment  
metadata:
  name: web-deployment
spec:
  replicas: 3
""")

// Anchors and aliases
var result = parse[Value]("""
defaults: &defaults
  timeout: 30
  retries: 3

services:
  web:
    <<: *defaults
    port: 8080
  api:
    <<: *defaults
    port: 8081
""")

// Serialize back to YAML
var yamlString = toYAML(typedConfig)
```

## Advanced Features

### Custom Parsing/Serialization

Following the JSON pattern, types can define custom methods:

```cxy
@yaml
struct CustomType {
    // Custom parsing
    static func fromYAML(parser: &YamlParser): !This {
        // Custom implementation
    }
    
    // Custom serialization  
    func toYAML(os: &OutputStream): void {
        // Custom implementation
    }
}
```

### Partial Parsing

Support for ignoring unknown fields:

```cxy
@yaml(partial: true)
struct PartialConfig {
    knownField: String
    // Unknown fields in YAML will be ignored
}
```

### Multi-Document Support

```cxy
// Parse all documents
var allDocs = parseDocuments(multiDocYaml)

// Parse only first document
var firstDoc = parseSingleDocument[Config](multiDocYaml)

// Process each document
for (var doc in allDocs) {
    processDocument(doc)
}
```

## Performance Considerations

### Memory Efficiency

Following the JSON implementation patterns:

- Use `__string` input for zero-copy parsing
- Single-pass parsing with minimal lookahead
- Arena allocation for temporary parsing structures
- Efficient anchor registry with HashMap
- Reuse Value objects where possible

### Parsing Speed

- Direct streaming to OutputStream for serialization
- Fast table lookup using HashMap
- Efficient indentation tracking with IndentStack
- Lazy alias resolution when possible
- Optimized string scanning for different styles

### YAML-Specific Optimizations

- Indentation caching to avoid repeated calculations
- Block scalar streaming for large text blocks
- Anchor deduplication to save memory
- Flow context detection for faster parsing

## Testing Strategy

### Test Categories

1. **Unit Tests**: Individual components (lexer, parser methods, indent stack)
2. **Integration Tests**: Full document parsing with Value
3. **Feature Tests**: Anchors, aliases, multi-line strings, flow styles
4. **Compliance Tests**: YAML 1.2 specification compliance
5. **Error Tests**: Invalid YAML handling and recovery
6. **Performance Tests**: Large document parsing benchmarks
7. **Round-trip Tests**: Parse → serialize → parse

### Test Cases

```cxy
test "Basic mapping parsing" {
    var result = parse[Value]("key: value")
    ok!(result["key"].asString() == "value")
}

test "Nested structure parsing" {
    var result = parse[Value]("""
    parent:
      child:
        grandchild: value
    """)
    ok!(result["parent"]["child"]["grandchild"].asString() == "value")
}

test "Sequence parsing" {
    var result = parse[Value]("""
    items:
      - first
      - second
      - third
    """)
    var items = result["items"].asArray()
    ok!(items.size() == 3)
    ok!(items[0].asString() == "first")
}

test "Anchor and alias" {
    var result = parse[Value]("""
    defaults: &defaults
      timeout: 30
    service:
      <<: *defaults
      port: 8080
    """)
    ok!(result["service"]["timeout"].asInt() == 30)
    ok!(result["service"]["port"].asInt() == 8080)
}

test "Multi-document parsing" {
    var docs = parseDocuments("""
    ---
    doc1: value1
    ---
    doc2: value2
    """)
    ok!(docs.size() == 2)
    ok!(docs[0]["doc1"].asString() == "value1")
    ok!(docs[1]["doc2"].asString() == "value2")
}

test "Type-specific parsing with attributes" {
    @yaml
    struct Service {
        name: String
        port: i64
        @yaml(key: "host_name")
        hostname: String
    }
    
    var service = parse[Service]("""
    name: web
    port: 8080
    host_name: example.com
    """)
    
    ok!(service.name == "web")
    ok!(service.port == 8080)
    ok!(service.hostname == "example.com")
}

test "Block scalar parsing" {
    var result = parse[Value]("""
    literal: |
      Line 1
      Line 2
      Line 3
    folded: >
      This is a long
      folded line that
      should be joined
    """)
    
    var literal = result["literal"].asString()
    ok!(literal.contains("Line 1\nLine 2\nLine 3"))
    
    var folded = result["folded"].asString()
    ok!(folded == "This is a long folded line that should be joined")
}
```

## Implementation Phases

### Phase 1: Core Foundation
- Basic lexer with indentation handling following `JsonLexer` patterns
- Simple mappings and sequences to Value
- Scalar value parsing (strings, numbers, booleans)
- Exception-based error handling
- Basic tests for core functionality

### Phase 2: YAML Features
- Multi-line strings (literal and folded styles)
- Flow style collections ([...], {...})
- Comments and document separators
- Anchor registration and alias resolution
- Indent stack management

### Phase 3: Advanced Features
- Multi-document support
- Merge key processing (<<:)
- Tag processing (!!type)
- Type-specific parsing with reflection
- Attribute system implementation

### Phase 4: Polish and Optimization
- Custom parsing/serialization methods
- Performance optimization
- YAML 1.2 specification compliance
- Comprehensive test suite
- Error recovery and diagnostics

## Security Considerations

### Input Validation
- Maximum document size limits
- Recursion depth limits (`_maxDepth = 1000`)
- Anchor reference limits to prevent cycles
- Memory consumption monitoring

### Safe Parsing
- Validate anchor names and prevent overwrites
- Prevent billion laughs attacks via anchor explosion
- Input sanitization for special characters
- Bounded parsing for untrusted input

## Conclusion

This YAML parser design follows the proven architectural patterns from `json.cxy`, while handling YAML's unique complexities like indentation-based structure, anchors/aliases, and multi-document format. The design provides:

- **Consistency**: Same API patterns as JSON parser (`fromYAML[T]`, `parse[T]`, `toYAML[T]`)
- **Performance**: Zero-copy parsing with `__string` input and single-pass architecture
- **Flexibility**: Support for both `Value`-based and type-specific parsing via reflection
- **YAML Features**: Full support for anchors, aliases, multi-line strings, and flow styles
- **Robustness**: Exception-based error handling with detailed diagnostics
- **Security**: Built-in protections against common YAML vulnerabilities

The modular architecture allows for incremental development, while comprehensive error handling ensures good developer experience. The integration with the existing `Value` type creates a unified interface for working with structured configuration data across JSON, TOML, YAML, and future format parsers.

By leveraging the recursive HashMap implementation and following established stdlib patterns, this design provides efficient and safe YAML processing capabilities for the Cxy language ecosystem.