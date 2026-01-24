# Cxy Compiler Architecture Summary

## Overview

Cxy is a transpiler for a high-level programming language that aims to simplify C development. The compiler follows a traditional multi-phase architecture, transforming source code through several intermediate representations before generating either C code or LLVM IR.

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Source    │    │   Lexical   │    │   Syntax    │    │     AST     │
│    Code     │───▶│   Analysis  │───▶│   Analysis  │───▶│  (Frontend) │
│   (.cxy)    │    │  (Lexer)    │    │  (Parser)   │    │             │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
                                                                 │
┌─────────────┐    ┌─────────────┐    ┌─────────────┐            │
│   Target    │    │   Backend   │    │   Middle    │            │
│ Code (C/IR) │◀───│   CodeGen   │◀───│    End      │◀───────────┘
│             │    │             │    │ Processing  │
└─────────────┘    └─────────────┘    └─────────────┘
```

## Project Structure

### Core Components
- **`src/cxy/core/`** - Core utilities (memory management, data structures, logging)
- **`src/cxy/driver/`** - Compiler driver and orchestration
- **`src/cxy/lang/`** - Language implementation (frontend, middle-end, backend)
- **`src/cxy/plugin/`** - Plugin system for extensibility
- **`src/cxy/runtime/`** - Runtime builtins and standard library bindings
- **`src/cxy/stdlib/`** - Standard library implementation

### Third-Party Dependencies
- **`src/3rdParty/`** - External libraries (msgpack, libyaml, cJSON, toml)
- **LLVM/Clang** - For optional LLVM backend and C import functionality

## Compilation Phases

The compiler implements the following stages as defined in `stages.h`:

### 1. Frontend Phases

#### Parse
- **Location**: `src/cxy/lang/frontend/`
- **Purpose**: Lexical analysis and syntax parsing
- **Key Files**:
  - `lexer.c` - Tokenization of source code
  - `parser.c` - Recursive descent parser
  - `ast.c` - Abstract Syntax Tree construction

```
Source Code (.cxy) ──┐
                     │
┌────────────────────▼────────────────────┐
│              Lexer                      │
│  ┌─────────────────────────────────┐    │
│  │ Keywords │ Operators │ Literals │    │
│  │ Strings  │ Numbers   │ Comments │    │
│  └─────────────────────────────────┘    │
└────────────────────┬────────────────────┘
                     │ Token Stream
┌────────────────────▼────────────────────┐
│              Parser                     │
│  ┌─────────────────────────────────┐    │
│  │ Recursive Descent               │    │
│  │ Precedence Climbing             │    │
│  │ Error Recovery                  │    │
│  └─────────────────────────────────┘    │
└────────────────────┬────────────────────┘
                     │ AST Nodes
                     ▼
```

#### Key AST Node Types:
- **Expressions**: `BinaryExpr`, `CallExpr`, `MemberExpr`, `IndexExpr`, etc.
- **Statements**: `BlockStmt`, `IfStmt`, `ForStmt`, `WhileStmt`, etc.
- **Declarations**: `FuncDecl`, `StructDecl`, `VarDecl`, `ModuleDecl`, etc.

### 2. Middle-End Phases

#### Preprocess
- **Location**: `src/cxy/lang/middle/preprocess/`
- **Purpose**: Macro expansion and conditional compilation
- **Key Operations**:
  - Macro definition resolution
  - Conditional compilation (`#if`, `#ifdef`)
  - Include file processing

#### Shake
- **Location**: `src/cxy/lang/middle/shake/`
- **Purpose**: Dead code elimination and tree shaking
- **Key Operations**:
  - Remove unused functions and variables
  - Closure capture optimization
  - Dependency analysis

```
     AST (Post-Parse)
           │
           ▼
    ┌─────────────┐
    │ Preprocess  │ ──── Macro Expansion
    └─────┬───────┘      Conditional Compilation
          │
          ▼
    ┌─────────────┐
    │    Shake    │ ──── Dead Code Elimination
    └─────┬───────┘      Dependency Analysis
          │
          ▼
```

#### Bind
- **Location**: `src/cxy/lang/middle/bind/`
- **Purpose**: Symbol resolution and scope analysis
- **Key Operations**:
  - Symbol table construction
  - Name resolution across scopes
  - Forward declaration handling
  - Module and import resolution

```
Symbol Resolution Flow:
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Discover  │───▶│   Resolve   │───▶│    Bind     │
│  (Phase 1)  │    │  (Phase 2)  │    │  (Phase 3)  │
└─────────────┘    └─────────────┘    └─────────────┘
      │                    │                    │
      ▼                    ▼                    ▼
Find all symbols    Link references      Final binding
Create scopes       to definitions       verification
```

#### ConstCheck & TypeCheck
- **Location**: `src/cxy/lang/middle/sema/`
- **Purpose**: Semantic analysis and type checking
- **Key Components**:
  - `check.c` - Main type checker
  - `function.c` - Function call analysis
  - `binary.c` - Binary operation type checking
  - `cast.c` - Type conversion validation
  - `generics.c` - Generic type instantiation

```
Type Checking Pipeline:
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Constant  │───▶│    Type     │───▶│  Generic    │
│ Evaluation  │    │  Checking   │    │Instantiation│
└─────────────┘    └─────────────┘    └─────────────┘
      │                    │                    │
      ▼                    ▼                    ▼
Compile-time        Validate types    Generate concrete
expression eval     & operations      types from templates
```

#### Simplify
- **Location**: `src/cxy/lang/middle/simplify/`
- **Purpose**: AST optimization and simplification
- **Key Operations**:
  - Constant folding
  - Expression simplification
  - Control flow optimization

#### MemoryMgmt
- **Location**: `src/cxy/lang/middle/mem/`
- **Purpose**: Memory management insertion
- **Key Files**:
  - `manage.c` - Reference counting insertion
  - `mem.c` - Memory allocation tracking
  - `finalize.c` - Cleanup code generation

#### Lower
- **Location**: `src/cxy/lang/middle/lower/`
- **Purpose**: Lower high-level constructs to simpler forms
- **Key Operations**:
  - Closure conversion
  - Exception handling lowering
  - Complex expression decomposition

### 3. Backend Phases

#### Codegen
The compiler supports two backend options:

##### C Backend
- **Location**: `src/cxy/lang/backend/c/`
- **Purpose**: Generate C source code
- **Output**: `.c` files that can be compiled with any C compiler

##### LLVM Backend (Optional)
- **Location**: `src/cxy/lang/backend/llvm/`
- **Purpose**: Generate LLVM IR
- **Output**: LLVM bitcode or object files
- **Files**:
  - `generate.cpp` - Main LLVM IR generation
  - `context.cpp` - LLVM context management
  - `debug.cpp` - Debug information generation

```
Backend Selection:
┌─────────────┐
│   Lowered   │
│     AST     │
└─────┬───────┘
      │
      ▼
┌─────────────┐
│   Backend   │
│  Selection  │
└─────┬───────┘
      │
   ┌──▼──┐
   │ C?  │──Yes──┐
   └─────┘       │
      │          ▼
     No    ┌─────────────┐
      │    │ C Backend   │
      ▼    │ generate.c  │
┌─────────────┐    │    └─────────────┘
│LLVM Backend│    │          │
│ generate.cpp│◀───┘          ▼
└─────────────┘         ┌─────────────┐
      │                 │   C Code    │
      ▼                 │   Output    │
┌─────────────┐         └─────────────┘
│  LLVM IR    │
│   Output    │
└─────────────┘
```

#### Collect & Compile
- **Purpose**: Final compilation steps
- **Operations**:
  - Link with runtime libraries
  - Generate executable/library
  - Handle native code integration

## Additional Features

### Plugin System
- **Location**: `src/cxy/plugin/`
- **Purpose**: Extensible compiler architecture
- **Features**:
  - Custom AST transformations
  - Additional backends
  - Language extensions

### C Import System
- **Location**: `src/cxy/driver/c-import/`
- **Purpose**: Import C headers and libraries
- **Features**:
  - Clang-based header parsing
  - Automatic binding generation
  - Type mapping between C and Cxy

### Standard Library
- **Location**: `src/cxy/stdlib/`
- **Components**:
  - Core data structures (vector, hash, list, trie)
  - I/O operations (file, network, HTTP)
  - System interfaces (OS, time, threads)
  - Utilities (JSON, base64, coroutines)

```
Standard Library Organization:
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Core Types    │    │   I/O & Network │    │   Utilities     │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ • vector.cxy    │    │ • http.cxy      │    │ • json.cxy      │
│ • hash.cxy      │    │ • tcp.cxy       │    │ • base64.cxy    │
│ • list.cxy      │    │ • ssl.cxy       │    │ • coro.cxy      │
│ • trie.cxy      │    │ • net.cxy       │    │ • jsonrpc.cxy   │
└─────────────────┘    └─────────────────┘    └─────────────────┘
```

## Build System

The project uses CMake with the following key targets:
- **`cxy`** - Main compiler executable
- **`cxy-plugin`** - Static library for plugin development
- **`amalgamate`** - Tool for combining multiple source files
- **Standard library modules** - Individual .cxy files
- **Plugin libraries** - Shared libraries for stdlib plugins

## Runtime Integration

The compiler integrates with a runtime system that provides:
- **Memory management** - Reference counting and GC support
- **Exception handling** - Error propagation mechanisms  
- **Standard library bindings** - Native function interfaces
- **Plugin loader** - Dynamic library loading for extensions

This architecture enables Cxy to provide high-level language features while maintaining compatibility with C ecosystems and offering multiple compilation targets.
