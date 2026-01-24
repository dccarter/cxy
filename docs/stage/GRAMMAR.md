# CXY Language Grammar Specification

## Overview

This document defines the grammar for the CXY programming language. The grammar is specified in Extended Backus-Naur Form (EBNF) and is designed for an LL(3) recursive descent parser.

This grammar will evolve as we build the parser in phases.

**Grammar Notation:**

- `::=` - Production rule
- `|` - Alternative (OR)
- `?` - Optional (0 or 1 occurrence)
- `*` - Zero or more occurrences
- `+` - One or more occurrences
- `()` - Grouping
- `[]` - Optional grouping
- `{}` - Zero or more repetitions
- `UPPERCASE` - Terminal tokens (from lexer)
- `camelCase` - Non-terminal productions

## Lexical Tokens

### Keywords

```
virtual auto true false null if else match for in is while break return yield continue
func var const type native extern exception struct enum pub opaque catch raise
async launch ptrof await delete discard switch case default defer macro void
string range module import include cSources as asm from unsafe interface this
This super class defined test plugin __cc
```

### Primitive Types

```
i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 f32 f64 bool char
```

### Symbols

```
( ) [ ] { } @ # ! ~ . .. ... ? , : ; = == != => -> < <= << <<= > >= >> >>=
+ - * / % & ^ | && || ++ -- += -= *= /= %= &= &. ^= |= ` () [] []= #. ## !:
```

### Literals & Special Tokens

```
Ident          - identifier
IntLiteral     - integer literal
FloatLiteral   - floating-point literal
CharLiteral    - character literal
StringLiteral  - string literal
LString        - `(
RString        - )`
EoF            - end of file
Error          - invalid token
```

## Grammar Productions

### Phase 1: Literals and Identifiers (Completed)

```ebnf
primaryExpression ::=
    | literalExpression
    | identifierExpression
    | '(' expression ')'

literalExpression ::=
    | integerLiteral
    | floatLiteral
    | characterLiteral
    | stringLiteral
    | booleanLiteral
    | nullLiteral

integerLiteral ::= IntLiteral

floatLiteral ::= FloatLiteral

characterLiteral ::= CharLiteral

stringLiteral ::= StringLiteral | interpolatedString

interpolatedString ::= LString expression* RString

booleanLiteral ::= 'true' | 'false'

nullLiteral ::= 'null'

identifierExpression ::= Ident
```

### Phase 2: Basic Expressions (Current Implementation Target)

```ebnf
expression ::= assignmentExpression

assignmentExpression ::=
    | conditionalExpression
    | conditionalExpression '=' assignmentExpression
    | conditionalExpression '+=' assignmentExpression
    | conditionalExpression '-=' assignmentExpression
    | conditionalExpression '*=' assignmentExpression
    | conditionalExpression '/=' assignmentExpression
    | conditionalExpression '%=' assignmentExpression
    | conditionalExpression '&=' assignmentExpression
    | conditionalExpression '^=' assignmentExpression
    | conditionalExpression '|=' assignmentExpression
    | conditionalExpression '<<=' assignmentExpression
    | conditionalExpression '>>=' assignmentExpression

conditionalExpression ::= logicalOrExpression

logicalOrExpression ::=
    | logicalAndExpression
    | logicalOrExpression '||' logicalAndExpression

logicalAndExpression ::=
    | bitwiseOrExpression
    | logicalAndExpression '&&' bitwiseOrExpression

bitwiseOrExpression ::=
    | bitwiseXorExpression
    | bitwiseOrExpression '|' bitwiseXorExpression

bitwiseXorExpression ::=
    | bitwiseAndExpression
    | bitwiseXorExpression '^' bitwiseAndExpression

bitwiseAndExpression ::=
    | equalityExpression
    | bitwiseAndExpression '&' equalityExpression

equalityExpression ::=
    | relationalExpression
    | equalityExpression '==' relationalExpression
    | equalityExpression '!=' relationalExpression

relationalExpression ::=
    | shiftExpression
    | relationalExpression '<' shiftExpression
    | relationalExpression '<=' shiftExpression
    | relationalExpression '>' shiftExpression
    | relationalExpression '>=' shiftExpression

shiftExpression ::=
    | additiveExpression
    | shiftExpression '<<' additiveExpression
    | shiftExpression '>>' additiveExpression

additiveExpression ::=
    | multiplicativeExpression
    | additiveExpression '+' multiplicativeExpression
    | additiveExpression '-' multiplicativeExpression

multiplicativeExpression ::=
    | unaryExpression
    | multiplicativeExpression '*' unaryExpression
    | multiplicativeExpression '/' unaryExpression
    | multiplicativeExpression '%' unaryExpression

unaryExpression ::=
    | castExpression
    | '++' unaryExpression
    | '--' unaryExpression
    | '+' unaryExpression
    | '-' unaryExpression
    | '!' unaryExpression
    | '~' unaryExpression
    | '&' unaryExpression
    | '&&' unaryExpression
    | '^' unaryExpression

postfixExpression ::=
    | primaryExpression
    | postfixExpression '++'
    | postfixExpression '--'
    | postfixExpression '(' argumentList? ')'
    | postfixExpression '[' expression ']'
    | postfixExpression '.' expression
    | postfixExpression '&.' Ident
```

### Phase 3: Complex Expressions (Completed)

```ebnf
# Update primaryExpression to include collection literals
primaryExpression ::=
    | literalExpression
    | identifierExpression
    | '(' expression ')'
    | arrayLiteral
    | tupleLiteral

# Function call argument lists
argumentList ::=
    | expression
    | argumentList ',' expression

# Array literals
arrayLiteral ::= '[' arrayElementList? ']'

arrayElementList ::=
    | expression
    | arrayElementList ',' expression

# Tuple literals (minimum 2 elements to distinguish from grouped expressions)
tupleLiteral ::= '(' tupleElementList ')'

tupleElementList ::=
    | expression ',' expression
    | tupleElementList ',' expression

# Member access uses expressions (supports identifiers, literals, and compile-time expressions)
# Examples: obj.field, obj.0, obj.#{compile_time_expr}
```

### Phase 3.1: Cast Expressions

```ebnf
# Update expression precedence to include cast expressions
# Cast expressions have precedence between unary and postfix

castExpression ::=
    | postfixExpression
    | castExpression 'as' typeExpression
    | castExpression '!:' typeExpression

# Type expressions for cast targets (simplified to primitives only)
typeExpression ::= primitiveType

primitiveType ::= 'i8' | 'i16' | 'i32' | 'i64' | 'i128'
                | 'u8' | 'u16' | 'u32' | 'u64' | 'u128'
                | 'f32' | 'f64' | 'bool' | 'char' | 'void'
                | 'auto' | 'string'
```

### Phase 3.2: Struct Literals

```ebnf
# Update primaryExpression to include struct literals
primaryExpression ::=
    | literalExpression
    | identifierExpression
    | '(' expression ')'
    | arrayLiteral
    | tupleLiteral
    | structLiteral

# Struct literals - both typed and anonymous
structLiteral ::=
    | typeExpression '{' structFieldList? '}'  # Typed struct literal (can be empty)
    | '{' structFieldList '}'                  # Anonymous struct literal (requires at least one field)

structFieldList ::=
    | structField
    | structFieldList ',' structField

structField ::=
    | Ident ':' expression     # Named field with explicit value
    | Ident                    # Shorthand field (field name = variable name)

# For now, type expressions are limited to simple identifiers
# Later phases will expand this to support complex types
typeExpression ::= Ident
```

### Phase 3.3: Range Expressions

```ebnf
# Update primaryExpression to include range expressions
primaryExpression ::=
    | literalExpression
    | identifierExpression
    | '(' expression ')'
    | arrayLiteral
    | tupleLiteral
    | structLiteral
    | rangeExpression

# Range expressions - various forms
rangeExpression ::=
    | expression '..' expression           # Inclusive range: 1..10
    | expression '..<' expression          # Exclusive range: 1..<10
    | '..' expression                      # Open start range: ..10
    | expression '..'                      # Open end range: 5..
    | '..'                                 # Full open range: ..
    | 'range' '(' rangeArgumentList ')'    # Function-style range

rangeArgumentList ::=
    | expression                           # range(max)
    | expression ',' expression            # range(min, max)
    | expression ',' expression ',' expression  # range(min, max, step)
```

**Key Grammar Features for Range Expressions:**

1. **Inclusive Ranges**: `1..10` - includes both start and end values
2. **Exclusive Ranges**: `1..<10` - includes start, excludes end
3. **Open Start Ranges**: `..10` - from beginning up to value
4. **Open End Ranges**: `5..` - from value to end
5. **Full Open Ranges**: `..` - entire range/slice
6. **Function-Style Ranges**: `range(min, max)`, `range(min, max, step)`

**Examples Supported by This Grammar:**

```
1..10                               // Inclusive range (1 to 10)
1..<10                              // Exclusive range (1 to 9)
..5                                 // Open start (up to 5)
5..                                 // Open end (from 5)
..                                  // Full open range
range(10)                           // Function-style: 0 to 9
range(1, 10)                        // Function-style: 1 to 9
range(0, 100, 2)                    // Function-style: 0 to 98, step 2
array[1..5]                         // Range as array slice
for i in 0..10 { }                  // Range in iteration
```

**Precedence Integration:**

Range expressions have precedence between relational and additive operators, allowing:

- `x + 1..10` parses as `x + (1..10)`
- `1..x + 5` parses as `1..(x + 5)`
- `arr[0..len-1]` parses as `arr[(0..(len-1))]`

**Key Grammar Features:**

1. **Function Calls**: `function()`, `method(arg1, arg2)`
2. **Array Indexing**: `array[index]`, `matrix[i][j]`
3. **Member Access**: `object.property`, `object.0`, `object.#{expr}`
4. **Overloaded Member Access**: `object&.property` (user-defined operator)
5. **Array Literals**: `[1, 2, 3]`, `[expr1, expr2]`
6. **Tuple Literals**: `(value1, value2)`, `(x, y, z)`
7. **Cast Expressions**: `value as Type`, `ptr !: ^Type`
8. **Struct Literals**: `Point { x: 1, y: 2 }`, `{ field: value }`
9. **Range Expressions**: `1..10`, `start..<end`, `range(min, max, step)`

**Examples Supported by This Grammar:**

```
myFunc(42, "hello")                 // Function call with arguments
array[index]                        // Array indexing
matrix[i][j]                        // Chained indexing
obj.field                           // Member access
obj.0                               // Numeric field access (tuple indexing)
obj.#{Size - 1}                     // Compile-time expression field access
obj&.field                          // Overloaded member access operator
obj.method()                        // Method call
obj.method(arg)                     // Method call with arguments
[1, 2, 3, 4]                       // Array literal
(x, y)                              // Tuple literal
myFunc()[0]                         // Function result indexing
obj.array[index].field              // Chained access
arr[i]&.property                    // Mixed indexing and overloaded access
x as i32                            // Normal cast
ptr !: u64                          // Unsafe retype cast
value.field as bool                 // Cast on member access result
Point { x: 1, y: 2 }                // Typed struct literal
{ name: "test", count: 42 }         // Anonymous struct literal
Point { x, y }                      // Shorthand struct literal
1..10                               // Inclusive range
5..<20                              // Exclusive range
..100                               // Open start range
array[0..5]                         // Range as slice index
range(0, 100, 2)                    // Function-style range with step
```

**Precedence Integration:**

- **Function calls, indexing, member access**: Highest precedence (Level 16)
- **Collection literals**: Primary expression level (Level 15)

**Parsing Considerations:**

1. **Chaining Support**: All postfix operations can be chained (e.g., `myFunc().field[0]`)
2. **Argument Disambiguation**: Empty `()` vs non-empty argument lists
3. **Tuple vs Parentheses**: `(expr)` is parenthesized expression, `(expr, expr)` is tuple
4. **Expression Member Access**: `obj.field`, `obj.0`, `obj.#{expr}` - supports identifiers, literals, and compile-time expressions
5. **Overloaded Access**: `&.` is a user-defined operator, not built-in pointer access

### Phase 3.5: Advanced Expression Features

```ebnf
# Update primaryExpression to include advanced expression features
primaryExpression ::=
    | literalExpression
    | identifierExpression
    | '(' expression ')'
    | arrayLiteral
    | tupleLiteral
    | structLiteral
    | rangeExpression
    | spreadExpression
    | interpolatedString
    | macroCall

# Spread expressions for unpacking collections
spreadExpression ::=
    | '...' expression                 # Spread operator: ...array, ...tuple

# Enhanced string interpolation expressions
interpolatedString ::=
    | '"' interpolationContent* '"'    # "Hello {name}!"

interpolationContent ::=
    | stringChar*                      # Regular string content
    | '{' expression '}'               # Interpolated expression: {value}

# Macro call expressions
macroCall ::=
    | identifier '!'                            # Bare macro call: println!
    | identifier '!' '(' argumentList? ')'      # Function-like macro: println!()
```

**Key Grammar Features for Advanced Expressions:**

1. **Spread Expressions**: `...array`, `...tuple` - unpacks collections into argument lists or other collections
2. **String Interpolation**: `"Hello {name}!"` - embedded expressions in string literals
3. **Macro Calls**: `println!`, `println!(expr)` - compile-time macro invocation

**Examples Supported by This Grammar:**

```
...array                            // Spread array elements
...getTuple()                       // Spread function result
"Hello {name}!"                     // Basic string interpolation
"Value: {x}"                        // Expression interpolation
println!                            // Bare macro call
println!("Debug: {}", value)        // Function-like macro call
format!("{} + {} = {}", a, b, a+b)  // Macro with multiple arguments
assert!()                           // Macro call with empty arguments
```

**Advanced Expression Usage:**

Spread expressions and interpolated strings can be used in expression contexts:

- `func(a, ...args, b)` - spread in function arguments
- `[1, 2, ...middle, 5]` - spread in array literals
- `let msg = "User {user.name} has {user.score} points"` - interpolated string assignment
- `println!("Processing {count} items...")` - interpolated string in macro

**Macro Call Precedence:**

Macro calls are primary expressions and have the same precedence as function calls:

- `macro!().field` - member access on macro result
- `macro!()[index]` - indexing macro result
- `macro!` - bare macro call (equivalent to `macro!()`)

**String Interpolation Features:**

1. **Expression Embedding**: Any valid expression can be embedded in `{}`
2. **Nested Interpolation**: Interpolated strings can contain complex expressions

### Phase 4.1: Expression Statements

```ebnf
# Expression statements - expressions that are evaluated for their side effects
expressionStatement ::=
    | expression ';'?                  # Simple expression statement (semicolon optional)

# Examples of expression statements:
# foo()                              // Function call statement (no semicolon)
# foo();                             // Function call statement (with semicolon)
# x = 42                             // Assignment statement  
# x += 5;                            // Compound assignment statement
# ++counter                          // Increment statement
# println!("Hello")                  // Macro call statement
# obj.method().chain();              // Method chain statement
```

**Key Grammar Features for Expression Statements:**

1. **Optional Semicolon**: Expression statements can optionally end with `;` for clarity or style
2. **Side Effect Focus**: While expressions return values, expression statements are evaluated for side effects
3. **Universal Expression Support**: Any valid expression can be used as a statement

**Examples Supported by This Grammar:**

```
// Function calls for side effects (semicolons optional)
println!("Debug output")
processData(input);
obj.mutate()

// Assignment operations
x = getValue()
arr[i] = newValue;
obj.field = computed()

// Compound assignments
counter += 1
total *= factor;
buffer &= mask

// Increment/decrement operations
++index
value--;

// Complex expression chains
getData()
    .transform(mapper)
    .filter(predicate)
    .save()
```

**Expression Statement Design Principles:**

1. **Consistency**: Any expression that can appear in expression context can be a statement
2. **Flexible Termination**: Semicolons are optional, allowing for cleaner syntax while preserving clarity when needed
3. **Side Effect Clarity**: Distinguishes between expressions (return values) and statements (side effects)
4. **Parser Considerations**: Statement boundaries determined by syntactic context when semicolons are omitted

### Phase 4.2: Break and Continue Statements

```ebnf
# Break and continue statements - control flow within loops and match expressions
breakStatement ::=
    | 'break' ';'?                     # Break statement (no labels)
    
continueStatement ::=
    | 'continue' ';'?                  # Continue statement (no labels)

# Examples of break/continue statements:
# break                              // Break from innermost loop
# break;                             // Break with explicit semicolon  
# continue                           // Continue to next iteration
# continue;                          // Continue with explicit semicolon
```

**Key Grammar Features for Break/Continue Statements:**

1. **Simple Control Flow**: Basic break/continue statements without label support
2. **Optional Semicolon**: Following Phase 4.1 pattern, semicolons are optional for consistency
3. **Context Sensitivity**: Valid only within loop constructs and switch/match statements
4. **Immediate Effect**: Transfer control immediately without evaluating subsequent statements

**Examples Supported by This Grammar:**

```
// Simple loop control
while condition {
    if should_skip {
        continue        // Skip to next iteration of current loop
    }
    if should_exit {
        break          // Exit current loop entirely
    }
    process_item()
}

// Nested loop control (targets innermost loop only)
while outer_condition {
    for item in collection {
        if item.invalid {
            continue       // Continue inner loop only
        }
        if item.critical_error {
            break         // Exit inner loop only (returns to outer loop)
        }
        process(item)
    }
    // Control returns here after break from inner loop
}

// Switch statement control
switch value {
    case Pattern1: {
        if special_case {
            break          // Exit switch statement
        }
        handle_pattern1()
    }
    case Pattern2: {
        // break implicit at end of case
    }
}

// For loop control  
for i in 0..10 {
    if i % 2 == 0 {
        continue          // Skip to next iteration (i++)
    }
    if i > 7 {
        break            // Exit loop entirely
    }
    process(i)
}
```

**Break/Continue Statement Design Principles:**

1. **Clear Intent**: Explicit keywords (`break`/`continue`) make control flow obvious
2. **Simple Semantics**: No labels means unambiguous targeting of innermost construct
3. **Consistent Syntax**: Follows established optional semicolon pattern from Phase 4.1
4. **Safety First**: Parser validates that statements appear only in valid contexts
5. **Predictable Behavior**: Always targets innermost enclosing breakable/continuable construct

**Context Validation Rules:**

1. **Break Statement Contexts**:
   - Loop constructs (`while`, `for`)
   - Switch/match statements (exits switch, not containing loop)
   - Invalid outside of breakable constructs

2. **Continue Statement Contexts**:
   - Loop constructs only (`while`, `for`) 
   - Invalid in switch/match statements
   - Invalid outside of continuable constructs

3. **Target Resolution**:
   - Break/continue always target innermost valid construct
   - Break in switch exits the switch (not containing loop)
   - Continue in nested loops targets innermost loop only
   - Compile-time error if no valid target exists

**Parser Implementation Considerations:**

1. **Context Tracking**: Parser maintains stack of breakable/continuable contexts
2. **Simple Parsing**: No label parsing required - just keyword and optional semicolon
3. **Statement Boundary Detection**: Same ASI-style rules as expression statements
4. **Error Recovery**: Clear diagnostics for context violations and missing constructs

### Phase 4.3: Block Statements

```ebnf
# Block statements - groups of statements enclosed in braces
blockStatement ::=
    | '{' statement* '}'                 # Block with zero or more statements

# Examples of block statements:
# {}                                   // Empty block
# { foo(); }                          // Single statement block
# { x = 42; y = 24; }                 // Multiple statement block
# { break; continue; x += 1; }        // Mixed statement types
```

**Key Grammar Features for Block Statements:**

1. **Explicit Scope**: Braces `{}` create new lexical scope for variables and control flow
2. **Statement Sequence**: Zero or more statements can appear within the block
3. **Flexible Content**: Any valid statement type can appear inside blocks
4. **Nested Blocks**: Blocks can contain other blocks for hierarchical scoping

**Examples Supported by This Grammar:**

```
// Empty block
{}

// Single statement block
{
    println!("Hello");
}

// Multiple statements
{
    let x = getValue();
    let y = process(x);
    return y * 2;
}

// Nested blocks with scoping
{
    let outer = 10;
    {
        let inner = 20;
        println!("inner: {}, outer: {}", inner, outer);
    }
    // 'inner' is no longer accessible here
    println!("outer: {}", outer);
}

// Control flow within blocks
{
    for i in 0..10 {
        if i % 2 == 0 {
            continue;
        }
        if i > 7 {
            break;
        }
        process(i);
    }
}

// Mixed statement types
{
    // Expression statements
    foo();
    obj.method();
    
    // Control flow
    if condition {
        break;
    }
    
    // Assignments
    result = calculate();
    counter += 1;
}

// Function body blocks
func example() {
    initialize();
    let value = compute();
    finalize(value);
}

// Loop body blocks
while condition {
    processItem();
    updateState();
}

// Conditional blocks
if test {
    handleTrue();
} else {
    handleFalse();
}
```

**Block Statement Design Principles:**

1. **Clear Scope Boundaries**: Braces explicitly define variable and lifetime scopes
2. **Statement Composition**: Any statement can appear within blocks for flexible program structure
3. **Nested Scoping**: Supports hierarchical scope nesting for complex control flow
4. **Consistent Syntax**: Same block syntax used for functions, loops, conditionals, and standalone blocks
5. **No Semicolon Required**: Blocks themselves don't need trailing semicolons

**Scoping and Semantic Rules:**

1. **Variable Scope**:
   - Variables declared within a block are only accessible within that block
   - Variables from outer scopes remain accessible unless shadowed
   - Block exit destroys all variables declared within the block

2. **Control Flow Scope**:
   - `break`/`continue` statements target constructs in enclosing scopes
   - Block boundaries don't affect `break`/`continue` targeting
   - Labeled blocks (future feature) could be break targets

3. **Statement Sequencing**:
   - Statements execute in order from top to bottom
   - Early exits (`break`, `continue`, `return`) skip remaining statements
   - Block ends when all statements complete or early exit occurs

**Parser Implementation Considerations:**

1. **Brace Matching**: Parser must track opening/closing brace balance
2. **Statement Parsing**: Use existing `parseStatement()` dispatch for block contents
3. **Scope Management**: Semantic analyzer handles variable scope tracking
4. **Error Recovery**: Synchronize on closing brace for error recovery
5. **Empty Blocks**: Handle `{}` as valid empty statement sequence

### Phase 4.4: Defer, Return, and Yield Statements

```ebnf
# Defer, return, and yield statements - function control flow and resource management
deferStatement ::=
    | 'defer' statement                  # Defer execution of statement until scope exit

returnStatement ::=
    | 'return' expression? ';'?          # Return with optional value and semicolon

yieldStatement ::=
    | 'yield' expression? ';'?           # Yield with optional value and semicolon

# Examples of defer/return/yield statements:
# defer cleanup()                      // Defer function call
# defer { resource.close(); }          // Defer block statement
# return                               // Return without value
# return 42;                           // Return with value
# yield                                // Yield without value  
# yield getValue();                    // Yield with value
```

**Key Grammar Features for Defer/Return/Yield Statements:**

1. **Defer Statement**: Executes the deferred statement when current scope exits (LIFO order)
2. **Optional Values**: Return and yield statements can optionally include expression values
3. **Optional Semicolon**: Following established pattern, semicolons are optional for return/yield
4. **Statement Flexibility**: Defer can take any statement, including blocks and expressions
5. **Function Control**: Return/yield provide function exit and suspension points

**Examples Supported by This Grammar:**

```
// Defer statements for resource management
func processFile(filename: String) {
    let file = openFile(filename);
    defer file.close();                  // Always close file on function exit
    
    defer {
        println!("Cleanup completed");   // Defer block statement
        logActivity("file_processed");
    }
    
    processData(file.read());
    // Deferred statements execute here in LIFO order:
    // 1. The defer block (println + logActivity)
    // 2. file.close()
}

// Return statements with and without values
func getValue(): i32 {
    if condition {
        return 42;                       // Return with value
    }
    return                               // Return without value (implicit default)
}

func earlyExit() {
    if shouldExit {
        cleanup();
        return;                          // Early return, no value needed
    }
    normalProcessing()
}

// Yield statements for generators/coroutines
func* fibonacci(): i32 {
    let a = 0;
    let b = 1;
    
    yield a                              // Yield first value (no semicolon)
    yield b;                             // Yield second value (with semicolon)
    
    while true {
        let next = a + b;
        a = b;
        b = next;
        yield next                       // Yield computed value
    }
}

func* simpleGenerator() {
    yield                                // Yield without value (signal)
    yield getValue()                     // Yield expression result
    yield {
        let computed = expensiveOperation();
        computed * 2
    }
}

// Mixed usage in functions
func complexFunction(): Result {
    defer logExit("complexFunction");
    
    let resource = acquireResource();
    defer resource.release();
    
    if !resource.isValid() {
        return Error("Invalid resource");
    }
    
    let result = processWithResource(resource);
    
    if result.needsSaving() {
        defer result.save();
        return Success(result);
    }
    
    return Success(result)
}
```

**Defer/Return/Yield Statement Design Principles:**

1. **Resource Safety**: Defer ensures cleanup code runs regardless of exit path
2. **LIFO Execution**: Deferred statements execute in reverse order of declaration
3. **Flexible Values**: Return/yield support any expression or no expression
4. **Consistent Syntax**: Optional semicolons maintain consistency with other statements
5. **Function Semantics**: Clear control flow for function exit and suspension points

**Semantic and Execution Rules:**

1. **Defer Statement Semantics**:
   - Deferred statements execute when containing scope exits (return, exception, block end)
   - Multiple defer statements execute in LIFO (Last-In-First-Out) order
   - Deferred statement can be any valid statement (expression, block, other statements)
   - Defer does not evaluate the statement immediately - only schedules for later execution

2. **Return Statement Semantics**:
   - Immediately exits current function with optional return value
   - Missing expression in valued function context may require explicit default
   - Triggers execution of all pending defer statements before actual return
   - Return type must match function signature or be void

3. **Yield Statement Semantics**:
   - Suspends function execution and produces value to caller (generators/coroutines)
   - Function can resume from yield point when called again
   - Missing expression yields void/unit value to caller
   - Triggers execution of defer statements only on final function exit, not yield

**Parser Implementation Considerations:**

1. **Statement Recognition**: Add `defer`, `return`, `yield` keywords to statement dispatch
2. **Expression Parsing**: Parse optional expression for return/yield using existing infrastructure
3. **Statement Parsing**: Parse any statement for defer body using existing `parseStatement()`
4. **Semicolon Handling**: Apply same optional semicolon rules as other statements
5. **Error Recovery**: Handle missing expressions, invalid defer statements, malformed syntax

### Phase 4.5: If Statements

If statements provide conditional execution with support for expression or variable declaration conditions. Parentheses around conditions are optional but affect body syntax requirements.

**EBNF Productions:**

```ebnf
ifStatement ::= 'if' condition ifBody elseClause?

condition ::= '(' conditionExpr ')'     # Parenthesized condition
           | conditionExpr              # Bare condition

conditionExpr ::= expression
               | singleVariableDeclaration

singleVariableDeclaration ::= ('var'|'const'|'auto') identifier (':' type)? '=' expression

ifBody ::= statement                    # Single statement (requires parentheses)
        | blockStatement               # Block statement (always allowed)

elseClause ::= 'else' ifStatement      # Chained else-if
            | 'else' blockStatement    # Final else block
```

**Examples:**

```cxy
// Expression conditions
if true { println("always") }
if (x > 10) println("greater than 10")
if flag && ready { doWork() }

// Variable declaration conditions
if const x = fetch() { println(x) }
if var result = tryOperation() {
    handleSuccess(result)
}

// Chained if-else
if const user = getUser() {
    processUser(user)
}
else if var backup = getBackup() {
    useBackup(backup)
}
else {
    handleError()
}

// Parentheses allow single statements
if (ready) start()
if (const data = load()) process(data)
```

**Syntax Rules:**

1. **Condition Types**:
   - **Expression**: Any boolean expression (`x > 0`, `flag && ready`)
   - **Variable Declaration**: Single variable only (`const x = expr`, `var y = func()`)
   - **No Multiple Variables**: `var a, b = tuple()` not allowed in conditions

2. **Parentheses Rules**:
   - **Optional**: Both `if expr { }` and `if (expr) { }` are valid
   - **With Parentheses**: Single statement bodies allowed (`if (cond) stmt`)
   - **Without Parentheses**: Block statement required (`if cond { stmts }`)

3. **Body Requirements**:
   - **Bare condition**: Must use block statement `if cond { ... }`
   - **Parenthesized condition**: Can use single statement `if (cond) stmt` or block
   - **Else clauses**: Follow same rules as main if body

4. **Variable Scope**: Variables declared in condition are available in if body

**Semantic Constraints:**

1. **Single Variable Only**: Declaration conditions cannot declare multiple variables
   - Valid: `if const x = getValue() { ... }`
   - Invalid: `if var a, b = getPair() { ... }`

2. **Initialization Required**: Variable declarations in conditions must have initializer
   - Valid: `if const x = compute() { ... }`
   - Invalid: `if var x: i32 { ... }`

3. **Boolean Context**: Expressions used as conditions must be boolean-compatible
   - Variable declarations evaluate based on success/failure of initialization

4. **Variable Lifetime**: Variables declared in conditions are scoped to the if statement body

**AST Mapping:**

- Maps to `IfStatementNode` with:
  - `condition`: Expression or VariableDeclaration node
  - `thenBody`: Statement or BlockStatement node  
  - `elseBody`: Optional IfStatement or BlockStatement node
  - `hasParentheses`: Flag indicating if condition was parenthesized

**Parser Implementation Considerations:**

1. **Condition Parsing**: Detect parentheses, then parse expression or variable declaration
2. **Body Parsing**: Enforce block requirement based on parentheses presence
3. **Declaration Validation**: Ensure single variable and required initializer
4. **Else Chaining**: Parse recursive else-if chains correctly
5. **Scope Management**: Handle variable scope for condition declarations
6. **Error Recovery**: Handle missing conditions, malformed bodies, incomplete chains

### Phase 4.6: While Statements

While statements provide iterative execution with support for expression or variable declaration conditions, plus infinite loops. Parentheses around conditions are optional but affect body syntax requirements.

**EBNF Productions:**

```ebnf
whileStatement ::= 'while' condition? whileBody

condition ::= '(' conditionExpr ')'     # Parenthesized condition
           | conditionExpr              # Bare condition

conditionExpr ::= expression
               | singleVariableDeclaration

singleVariableDeclaration ::= ('var'|'const'|'auto') identifier (':' type)? '=' expression

whileBody ::= statement                 # Single statement (requires parentheses)
           | blockStatement            # Block statement (always allowed)
```

**Examples:**

```cxy
// Infinite loops
while { println("forever") }

// Expression conditions
while flag { doWork() }
while (x > 0) x = x - 1
while ready && !done { process() }

// Variable declaration conditions
while const item = getNext() { process(item) }
while var data = readData() {
    handleData(data)
}

// Parenthesized conditions with single statements
while (const line = readLine()) println(line)
while (hasMore()) process()
```

**Syntax Rules:**

1. **Condition Types**:
   - **No condition**: Infinite loop (`while { ... }`)
   - **Expression**: Any boolean expression (`x > 0`, `flag && ready`)
   - **Variable Declaration**: Single variable only (`const x = expr`, `var y = func()`)
   - **No Multiple Variables**: `var a, b = tuple()` not allowed in conditions

2. **Parentheses Rules**:
   - **Optional**: Both `while expr { }` and `while (expr) { }` are valid
   - **With Parentheses**: Single statement bodies allowed (`while (cond) stmt`)
   - **Without Parentheses**: Block statement required (`while cond { stmts }`)

3. **Body Requirements**:
   - **Bare condition**: Must use block statement `while cond { ... }`
   - **Parenthesized condition**: Can use single statement `while (cond) stmt` or block
   - **Infinite loop**: Must use block statement `while { ... }`

4. **Variable Scope**: Variables declared in condition are available in while body and re-evaluated each iteration

**Semantic Constraints:**

1. **Single Variable Only**: Declaration conditions cannot declare multiple variables
   - Valid: `while const x = getValue() { ... }`
   - Invalid: `while var a, b = getPair() { ... }`

2. **Initialization Required**: Variable declarations in conditions must have initializer
   - Valid: `while const x = compute() { ... }`
   - Invalid: `while var x: i32 { ... }`

3. **Boolean Context**: Expressions used as conditions must be boolean-compatible
   - Variable declarations evaluate based on success/failure of initialization

4. **Variable Lifetime**: Variables declared in conditions are scoped to the while loop body and re-evaluated each iteration

5. **Infinite Loop Syntax**: Empty condition requires block statement for clarity

**AST Mapping:**

- Maps to `WhileStatementNode` with:
  - `condition`: Optional Expression or VariableDeclaration node (null for infinite loops)
  - `body`: Statement or BlockStatement node  
  - `hasParentheses`: Flag indicating if condition was parenthesized

**Parser Implementation Considerations:**

1. **Condition Parsing**: Handle optional condition, detect parentheses, parse expression or variable declaration
2. **Body Parsing**: Enforce block requirement based on parentheses presence and infinite loop case
3. **Declaration Validation**: Ensure single variable and required initializer (reuse if statement logic)
4. **Infinite Loop Detection**: Handle missing condition gracefully
5. **Scope Management**: Handle variable scope for condition declarations with iteration semantics
6. **Error Recovery**: Handle missing conditions, malformed bodies, incomplete syntax

### Phase 4.7: For Statements

For statements provide iteration over ranges, collections, or sequences with support for iterator variables, optional conditions, and both parenthesized and bare syntax forms.

**EBNF Productions:**

```ebnf
forStatement ::= 'for' forClause forBody

forClause ::= '(' forClauseCore ')'      # Parenthesized clause
           | forClauseCore              # Bare clause

forClauseCore ::= iteratorVariableList 'in' rangeExpression (',' conditionExpression)?

forBody ::= statement                   # Single statement (requires parentheses)
         | blockStatement               # Block statement (always allowed)

iteratorVariableList ::= iteratorName (',' iteratorName)* ','?

iteratorName ::= identifier | '_'

rangeExpression ::= expression

conditionExpression ::= expression
```

**Grammar Examples:**

```cxy
// Basic iteration over range (braces required)
for a in 0..10 { println(a) }

// With wildcard (braces required)
for _ in items { processItem() }

// Multiple variables with destructuring (braces required)
for a, b in pairs { println(a, b) }

// Multiple variables with partial wildcards (braces required)
for value, _ in arr { process(value) }

// Multiple variables with trailing comma (braces required)
for value, idx, in arr { println(value, idx) }

// With condition filter (braces required)
for item in collection, item.isValid { use(item) }

// Parenthesized form with single statement
for (a in 0..10) println(a)

// Parenthesized form with multiple variables
for (a, b in pairs) print(a, b)

// Parenthesized form with tuple literal
for (a, in (0, 1, 2)) print(a)

// Parenthesized form with wildcard
for (_, a in (0, 2, 1)) { print(a) }

// Parenthesized form can also use block
for (i in 0..5) { println(i) }
```

**AST Node Mapping:**

- **ForStatementNode**: Contains iterator variable, range expression, optional condition, and body
- **variable**: Variable declaration node (auto-inferred type, created from identifier list)
- **range**: Expression node for the iterable (range, identifier, call, etc.)
- **condition**: Optional expression node for filtering
- **body**: Statement node (block or single statement based on parentheses)

**Note**: The parser creates Identifier nodes for variables corresponding to each variable.

**Parser Implementation Notes:**

1. **Iterator Variables**: Parse identifier list (supporting multiple variables and wildcards), create VariableDeclaration nodes with inferred types
2. **Range Expression**: Parse full expression that produces an iterable value
3. **Condition Parsing**: Handle optional comma-separated condition expression
4. **Body Requirements**: Block statement `{ }` required for bare form, single statement or block allowed for parenthesized form
5. **Scope Management**: Iterator variable scoped to loop body and condition
6. **Error Recovery**: Handle missing colons, malformed ranges, incomplete variable declarations

**Semantic Constraints:**

1. Iterator variable types are automatically inferred from the range expression
2. Range expression must be iterable (ranges, arrays, tuples, collections)
3. Condition expression must evaluate to boolean
4. Iterator variables are immutable within loop body (const by default)
5. Wildcard '_' names cannot be referenced in body or condition
6. Number of iterator variables must match the destructuring pattern of the range expression

### Phase 4.8: Switch Statements

Switch statements provide value-based branching with pattern matching capabilities, supporting multiple values per case, range expressions, and a default fallback case.

**EBNF Productions:**

```ebnf
switchStatement ::= 'switch' switchClause switchBody

switchClause ::= '(' switchClauseCore ')'      # Parenthesized clause (optional)
              | switchClauseCore              # Bare clause

switchClauseCore ::= (declarationKeyword identifier '=')? expression

declarationKeyword ::= 'var' | 'const' | 'auto'

switchBody ::= '{' caseList '}'

caseList ::= caseStatement*

caseStatement ::= casePattern '=>' caseBody
               | defaultCase '=>' caseBody

casePattern ::= expressionList

expressionList ::= expression (',' expression)* ','?

defaultCase ::= '...'

caseBody ::= statement                        # Single statement
          | blockStatement                   # Block statement
```

**Grammar Examples:**

```cxy
// Basic switch with single values
switch (value) {
  0 => println("Zero")
  1 => println("One")
  2 => println("Two")
}

// Switch with multiple values per case
switch (status) {
  0, 1, 2 => println("Success codes")
  404, 500 => println("Error codes")
  ... => println("Unknown status")
}

// Switch with range expressions
switch (score) {
  0..59 => println("F")
  60..69 => println("D")
  70..79 => println("C")
  80..89 => println("B")
  90..100 => println("A")
  ... => println("Invalid score")
}

// Switch with variable declaration
switch (var result = computeValue()) {
  0 => useResult(result)
  1..10 => processRange(result)
  ... => handleError(result)
}

// Switch with const variable
switch (const status = getStatus()) {
  "ok", "success" => handleSuccess(status)
  "error", "fail" => handleError(status)
  ... => handleUnknown(status)
}

// Switch with block statements
switch (operation) {
  "add" => {
    result = a + b
    println("Addition: {}", result)
  }
  "subtract", "sub" => {
    result = a - b
    println("Subtraction: {}", result)
  }
  ... => {
    println("Unknown operation")
    return null
  }
}

// Bare switch (no parentheses) - braces always required
switch value {
  true => println("True case")
  false => println("False case")
}

// Parenthesized switch - braces still required
switch (value) {
  true => println("True case")
  false => println("False case")
}
```

**AST Node Mapping:**

- **SwitchStatementNode**: Contains discriminant expression and list of case statements
- **discriminant**: Expression node for the value to switch on (with optional variable declaration)
- **cases**: List of CaseStatementNode instances
- **CaseStatementNode**: Contains case values, body statements, and default flag
- **values**: List of expression nodes for case patterns (empty for default case)
- **statements**: Statement nodes for the case body
- **isDefault**: Boolean flag for default case (`...` pattern)

**Parser Implementation Notes:**

1. **Discriminant Parsing**: Handle both bare expressions and variable declarations `var x = expr` or `(var x = expr)`
2. **Case Patterns**: Parse expression lists supporting literals, ranges, and complex expressions
3. **Arrow Syntax**: Expect `=>` token between pattern and body
4. **Default Cases**: Handle `...` token for catch-all cases
5. **Body Requirements**: Braces `{}` are always required for switch body (unlike if/while statements)
6. **Parentheses**: Optional around discriminant - both `switch expr {}` and `switch (expr) {}` are valid
7. **Case Ordering**: Default case can appear anywhere but conventionally at end
8. **Expression Lists**: Support comma-separated values with optional trailing comma

**Semantic Constraints:**

1. Switch discriminant must be a comparable expression (integers, strings, enums)
2. Case values must be compile-time constants or ranges
3. Case values must be compatible with discriminant type
4. Multiple values in a case are OR-ed together (any match triggers the case)
5. Range expressions must have compatible start/end types with discriminant
6. Default case is optional but recommended for exhaustiveness
7. Cases are mutually exclusive - first match wins
8. No fall-through behavior - each case is self-contained
9. Variable declaration in discriminant creates a binding available in all case bodies
10. Variable qualifier (var/const/auto) determines mutability within case bodies

### Phase 4.9: Match Statements

Match statements provide type-based pattern matching with variable binding capabilities, supporting type patterns, multiple type alternatives, and wildcard defaults for exhaustive type checking.

**EBNF Productions:**

```ebnf
matchStatement ::= 'match' matchClause matchBody

matchClause ::= '(' matchClauseCore ')'      # Parenthesized clause (optional)
             | matchClauseCore              # Bare clause

matchClauseCore ::= expression

matchBody ::= '{' matchCaseList '}'

matchCaseList ::= matchCase*

matchCase ::= matchPattern '=>' matchCaseBody

matchPattern ::= typePattern (',' typePattern)* ('as' identifier)?  # Multiple types with optional binding
              | '...' ('as' identifier)?                              # Default case with optional binding

typePattern ::= type

matchCaseBody ::= statement                   # Single statement
               | blockStatement              # Block statement
```

**Grammar Examples:**

```cxy
// Basic match with type patterns
match x {
    i32 as a => println("Integer: {}", a)
    string as s => println("String: {}", s)
    f64 as f => println("Float: {}", f)
    ... => println("Unknown type")
}

// Match with multiple types per case
match value {
    i8, u8 as byte => println("8-bit value: {}", byte)
    i16, u16 as word => println("16-bit value: {}", word)
    i32, u32 as dword => println("32-bit value: {}", dword)
    ... => println("Other type")
}

// Match without variable binding (no 'as')
match obj {
    i32 => println("Found integer")
    string => println("Found string")
    bool => println("Found boolean")
    ... => println("Unknown type")
}

// Match with default binding
match data {
    i32 as num => processNumber(num)
    string as text => processText(text)
    ... as other => handleGeneric(other)
}

// Match with block statements
match input {
    i32 as value => {
        result = value * 2
        println("Doubled: {}", result)
    }
    string as text => {
        upper = text.toUpperCase()
        println("Upper: {}", upper)
    }
    ... => {
        println("Cannot process this type")
        return null
    }
}

// Parenthesized match clause
match (getUserInput()) {
    i32 as num => handleNumber(num)
    string as str => handleString(str)
    ... => handleError()
}

// Bare match clause
match getValue() {
    bool as flag => handleFlag(flag)
    ... => handleDefault()
}
```

**AST Node Mapping:**

- **MatchStatementNode**: Contains discriminant expression and list of match cases
- **discriminant**: Expression node for the value to match against
- **cases**: List of MatchCaseNode instances
- **MatchCaseNode**: Contains type patterns, optional variable binding, body statements, and default flag
- **types**: List of type nodes for pattern matching (empty for default case)
- **binding**: Optional identifier for variable binding (null if no 'as' clause)
- **statements**: Statement nodes for the case body
- **isDefault**: Boolean flag for default case (`...` pattern)

**Parser Implementation Notes:**

1. **Discriminant Parsing**: Parse expression that provides the value to type-match
2. **Type Patterns**: Parse type specifications (primitives, user-defined types, etc.)
3. **Multiple Types**: Support comma-separated type lists for OR-style matching
4. **Variable Binding**: Handle optional `as identifier` for capturing matched values
5. **Default Cases**: Handle `...` token for catch-all with optional binding
6. **Arrow Syntax**: Expect `=>` token between pattern and body
7. **Body Requirements**: Braces `{}` are always required for match body
8. **Parentheses**: Optional around discriminant - both `match expr {}` and `match (expr) {}` are valid
9. **Case Ordering**: Default case can appear anywhere but conventionally at end
10. **Trailing Commas**: Support optional trailing commas in type lists

**Semantic Constraints:**

1. Match discriminant must be a runtime value with determinable type
2. Type patterns must be valid, concrete types (no generics without resolution)
3. Multiple types in a pattern are OR-ed together (any type match triggers the case)
4. Variable binding creates a typed variable available in the case body
5. Bound variable type is the union of matched types (or specific type if single)
6. Default case without binding receives the original discriminant value
7. Default case with binding receives the discriminant with its original type
8. Cases are mutually exclusive - first type match wins
9. Type compatibility follows standard type system rules
10. Match should ideally be exhaustive (cover all possible types)
11. Variable bindings follow scope rules - only available within case body
12. Binding identifier must not conflict with existing variables in scope

### Phase 5.0: Attributes

Attributes provide metadata and annotations for declarations and statements, supporting both simple and parameterized forms with literal arguments and named parameters.

**EBNF Productions:**

```ebnf
attributeList ::= attribute+

attribute ::= '@' attributeSpec
           | '@[' attributeListInner ']'

attributeListInner ::= attributeSpec (',' attributeSpec)* ','?

attributeSpec ::= identifier attributeArgs?

attributeArgs ::= '(' attributeArgList? ')'
               | '(' namedAttributeArgs ')'

attributeArgList ::= literal (',' literal)* ','?

namedAttributeArgs ::= namedAttributeArg (',' namedAttributeArg)* ','?

namedAttributeArg ::= identifier ':' literal

literal ::= integerLiteral | floatLiteral | stringLiteral | booleanLiteral | nullLiteral
```

**Grammar Examples:**

```cxy
// Simple attribute
@deprecated
fn oldFunction() {}

// Attribute with literal arguments
@test("integration")
fn testDatabaseConnection() {}

// Attribute with named arguments
@serialize({format: "json", indent: true})
struct Config {
    host: string,
    port: i32,
}

// Multiple attributes - individual syntax
@inline
@deprecated("Use newFunction instead")
@since("1.2.0")
fn legacyFunction() {}

// Multiple attributes - list syntax
@[inline, deprecated("Use newFunction instead"), since("1.2.0")]
fn legacyFunction() {}

// Attributes on statements
@trace
if condition {
    @benchmark
    performExpensiveOperation()
}

// Attributes on variable declarations
@readonly
@validate({min: 0, max: 100})
var score: i32 = 95

// Complex attribute arguments
@cache({
    ttl: 3600,
    key: "user_data",
    strategy: "lru"
})
fn getUserData(id: i32) -> User {}

// Multiple literal arguments
@test("unit", true, 10)
fn quickTest() {}
```

**AST Node Mapping:**

- **AttributeListNode**: Contains list of attribute specifications
- **attributes**: List of AttributeNode instances
- **AttributeNode**: Contains attribute name and optional arguments
- **name**: Identifier for the attribute name
- **arguments**: List of literal expressions (positional args)
- **namedArguments**: Map of identifier to literal expression (named args)
- **LiteralNode**: Base class for all literal values in attribute arguments

**Parser Implementation Notes:**

1. **Attribute Detection**: Parse attributes when encountering `@` token before declarations/statements
2. **Multiple Forms**: Support both `@attr` and `@[attr1, attr2]` syntax
3. **Argument Parsing**: Handle both positional `@attr(1, "test")` and named `@attr({key: value})` arguments
4. **Literal Restriction**: Only allow literal values in attribute arguments (no expressions)
5. **Trailing Commas**: Support optional trailing commas in argument lists
6. **Declaration Integration**: Modify `parseDeclaration()` to check for preceding attributes
7. **Statement Integration**: Modify `parseStatement()` to check for preceding attributes
8. **Attribute Attachment**: Store parsed attributes in declaration/statement AST nodes
9. **Error Handling**: Report errors for invalid attribute syntax or non-literal arguments
10. **List Syntax**: Parse comma-separated attributes within `@[...]` brackets

**Semantic Constraints:**

1. Attributes must precede the declarations or statements they modify
2. Attribute arguments must be compile-time literals (no variables or expressions)
3. Named arguments must have unique names within an attribute
4. Attribute names must be valid identifiers
5. Both positional and named arguments cannot be mixed in the same attribute
6. Attributes can be attached to most declaration types and some statement types
7. Unknown attributes may generate warnings but should not prevent compilation
8. Attribute argument types must match expected types for known attributes
9. Some attributes may be mutually exclusive (enforced during semantic analysis)
10. Attribute lists preserve order for attributes that depend on sequence

### Phase 5.1: Variable Declarations

Variable declarations introduce new variables into the current scope with optional type annotations and initializers. Supports multiple variable names for destructuring assignments and type inference.

**EBNF Productions:**

```ebnf
variableDeclaration ::= declarationKeyword nameList (typeAnnotation initializer? | initializer) ';'?

declarationKeyword ::= 'var' | 'const' | 'auto'

nameList ::= identifier (',' identifier)* ','?

typeAnnotation ::= ':' type

initializer ::= '=' expression
```

**Examples:**

```cxy
// Basic declarations
var x = 42
const PI = 3.14159
auto name = "John"

// With type annotations
var count: i32 = 0
const user: User
auto value: f64 = 100.0

// Multiple variables (destructuring)
var a, b = (10, 20)
var x, y, z = getTuple()
const name, age: (String, i32) = ("Alice", 30)

// Trailing commas (rest ignored)
var first, second, = getLargerTuple()

// Discard patterns (semantic meaning)
var _, important = getResult()
const _, _, value = getTriple()
```

**Semantic Constraints:**

1. **Type Requirements**: Either type annotation OR initializer must be present (enforced at parse time)
   - Valid: `var x: i32`, `var x = 42`, `var x: i32 = 42`
   - Invalid: `var x` (no type, no initializer)

2. **Const Semantics**: `const` declarations create immutable bindings
   - Must be initialized (either explicitly or through type inference)

3. **Auto Inference**: `auto` declarations require initializer for type inference
   - Type annotation with `auto` is redundant but allowed

4. **Discard Patterns**: Identifiers named `_` are treated as discard patterns during semantic analysis
   - Multiple `_` identifiers in same declaration are allowed
   - `_` bindings are not added to symbol table

5. **Multiple Names**: All names in a declaration share the same type annotation
   - `var a, b: i32` means both `a` and `b` have type `i32`
   - Initializer is shared or destructured across all names

**AST Mapping:**

- Maps to `VariableDeclarationNode` with:
  - `names`: Vector of identifier nodes (including `_`)
  - `type`: Optional type expression node
  - `initializer`: Optional expression node
  - `flgConst`: Set for `const` declarations

**Parser Implementation Considerations:**

1. **Keyword Recognition**: Add `var`, `const`, `auto` to statement dispatch in `parseStatement()`
2. **Name Parsing**: Parse comma-separated identifier list with optional trailing comma
3. **Type Parsing**: Parse optional type annotation using existing type parsing infrastructure
4. **Initializer Parsing**: Parse optional initializer expression
5. **Constraint Validation**: Ensure either type or initializer is present during parsing
6. **Semicolon Handling**: Apply same optional semicolon rules as other statements
7. **Error Recovery**: Handle missing types, invalid initializers, malformed name lists

### Phase 5.2: Function Declarations

Function declarations define named functions with parameters, return types, and bodies. Supports expression functions, block functions, variadic parameters, default parameter values, and attributes.

**EBNF Productions:**

```ebnf
functionDeclaration ::= attributeList? 'func' identifier genericParams? parameterList returnType? functionBody

parameterList ::= '(' parameterDeclarations? ')'

parameterDeclarations ::= parameterDeclaration (',' parameterDeclaration)* ','?

parameterDeclaration ::= variadicModifier? identifier type defaultValue?

variadicModifier ::= '...'

defaultValue ::= '=' expression

returnType ::= type

functionBody ::= '=>' expression
              | blockStatement

genericParams ::= '<' genericParamList '>'

genericParamList ::= genericParam (',' genericParam)* ','?

genericParam ::= variadicGenericModifier? identifier genericConstraint? genericDefaultValue?

variadicGenericModifier ::= '...'

genericConstraint ::= ':' typeExpression

genericDefaultValue ::= '=' typeExpression
```

**Examples:**

```cxy
// Expression function with arrow syntax
func add(a i32, b i32) => a + b

// Expression function with return type
func multiply(x i32, y i32) i32 => x * y

// Block function
func say() {
    println("Hello World")
}

// Function with default parameter values
func greet(name string = "World") {
    println("Hello, " + name)
}

// Function with multiple defaults
func connect(host string, port i32 = 8080, timeout i32 = 5000) {
    // connection logic
}

// Variadic function
func println(...args auto) void {
    // print all arguments
}

// Function with explicit return type
func compute() i32 {
    return 100 * global
}

// Function with attributes
@virtual
func calculate() i32 {
    return 42
}

@inline
@deprecated("Use calculateV2 instead")
func legacyCalculate() f64 {
    return 3.14
}

// Basic generic function
func a<T>() {}

// Generic with constraint (must use parseTypeExpression for Constraint)
func a<T:Constraint>() {}

// Generic with default value
func a<X, Y=i32>() {}

// Variadic generic with constraint
func a<...V:isInteger>() {}

// Generic function with parameters
func max<T>(a T, b T) T {
    return if a > b then a else b
}

// Generic with constraints
func sort<T: Comparable>(items []T) {
    // sorting logic
}

// Generic with default values
func process<T, U = i32>(input T, defaultVal U) U {
    // processing logic
}

// Variadic generic with constraint
func combine<...Types: Serializable>(values ...Types) string {
    // combining logic
}

// Complex function with all features
@benchmark
@inline
func processData<T: Serializable, U = ProcessOptions>(
    data []T,
    options U = defaultOptions,
    ...callbacks auto
) Result<[]T> {
    // processing logic
    return Ok(processedData)
}

// Function with no parameters
func getCurrentTime() DateTime {
    return DateTime.now()
}

// Void function (implicit void return)
func initialize() {
    setupGlobals()
}

// Function with variadic parameters and defaults
func log(level LogLevel = Info, format string, ...args auto) {
    // logging implementation
}
```

**Semantic Constraints:**

1. **Parameter Ordering**: Default parameters must come after non-default parameters
   - Valid: `func f(a i32, b i32 = 5, c string = "test")`
   - Invalid: `func f(a i32 = 1, b i32, c string = "test")`

2. **Variadic Position**: Variadic parameters must be the last parameter
   - Valid: `func f(a i32, ...args auto)`
   - Invalid: `func f(...args auto, b i32)`

3. **Return Type Inference**: 
   - Expression functions can infer return type from expression
   - Block functions require explicit return type unless `void`
   - `void` return type is implicit for functions with no return value

4. **Generic Parameters**: Generic parameters must be used in function signature or body
   - Unused generic parameters generate warnings
   - Variadic generic parameters must be last in the generic parameter list
   - Generic default values must use compile-time resolvable types
   - Generic constraints must use `parseTypeExpression` for proper type parsing

5. **Default Values**: Default parameter values must be compile-time constants or simple expressions

6. **Attribute Constraints**: Some attributes are function-specific
   - `@virtual` requires the function to be in a class context
   - `@inline` suggests inlining to the compiler
   - `@pure` indicates no side effects

**AST Mapping:**

- Maps to `FuncDeclarationNode` with:
  - `name`: Function identifier
  - `genericParams`: Optional list of generic parameter declarations
  - `parameters`: List of parameter declaration nodes
  - `returnType`: Optional return type expression
  - `body`: Either expression node (for arrow functions) or block statement
  - `attributes`: List of attribute nodes
  - `isExpression`: Boolean indicating arrow function vs block function

**Parser Implementation Considerations:**

1. **Function Detection**: Recognize `func` keyword in declaration parsing
2. **Generic Parsing**: Handle optional generic parameter lists with:
   - Type constraints using `:` syntax (parsed with `parseTypeExpression`)
   - Default type values using `=` syntax
   - Variadic generics using `...` prefix (must be last)
3. **Parameter Parsing**: Parse parameter list with support for:
   - Variadic parameters (`...name`)
   - Default values (`= expression`)
   - Direct type syntax (`name type`)
4. **Return Type Parsing**: Handle direct type syntax after parameter list
5. **Body Parsing**: Distinguish between `=>` expression and `{` block syntax
6. **Attribute Integration**: Parse preceding attributes and attach to function
7. **Error Recovery**: Handle malformed parameter lists, missing bodies, invalid generics
8. **Constraint Validation**: Enforce parameter ordering rules during parsing
9. **Semicolon Handling**: Functions typically don't require semicolons
10. **Nested Functions**: Consider scope implications for inner function declarations

**Grammar Notes:**

- Arrow functions (`=>`) create single-expression functions
- Block functions use traditional `{}` syntax for multi-statement bodies
- Generic parameters support type constraints with `:` syntax (using `parseTypeExpression`)
- Generic parameters support default values with `=` syntax
- Generic parameters can be variadic with `...` prefix (must be last in list)
- Variadic parameters use `...` prefix and typically have `auto` type for flexibility
- Return type is specified directly after parameter list (no `->` needed)
- Functions without explicit return type default to `void` for blocks, inferred for arrows

### Phase 5.3: Enum Declarations

Enum declarations define named enumeration types with optional backing types and value customization. Supports attributes on enum variants and explicit discriminant values.

**EBNF Productions:**

```ebnf
enumDeclaration ::= attributeList? 'enum' identifier enumBackingType? enumBody

enumBackingType ::= ':' type

enumBody ::= '{' enumOptionList? '}'

enumOptionList ::= enumOption (',' enumOption)* ','?

enumOption ::= attributeList? identifier enumValue?

enumValue ::= '=' expression
```

**Examples:**

```cxy
// Simple enum without backing type
enum Color {
    Red,
    Green,
    Blue
}

// Enum with explicit values
enum StatusCode {
    Ok = 200,
    NotFound = 404,
    InternalError = 500
}

// Enum with backing type
enum Priority : i8 {
    Low = 1,
    Medium = 5,
    High = 10
}

// Enum with attributes on variants
enum HttpMethod {
    @str("GET")
    Get,
    
    @str("POST") 
    Post,
    
    @str("PUT")
    Put = 3,
    
    @deprecated("Use Put instead")
    Update
}

// Simple enum from examples
enum Hello {
   @str('one')
   One,
   Two,
   Three = 3
}

// Empty enum with backing type
enum Flags : i8 {}

// Complex enum with mixed features
@repr("C")
enum TokenKind : u16 {
    // Basic tokens
    Eof = 0,
    
    @doc("Single line comment token")
    Comment = 10,
    
    // Keywords start at 100
    If = 100,
    Else,
    While,
    
    // Operators
    Plus = 200,
    Minus = 201,
    
    @deprecated
    LegacyOperator = 999
}

// Enum without explicit values (auto-increment)
enum Direction {
    North,     // 0
    East,      // 1
    South,     // 2
    West       // 3
}
```

**Semantic Constraints:**

1. **Backing Type Compatibility**: If a backing type is specified, all explicit values must be compatible with that type
   - Valid: `enum Status : i8 { Ok = 1, Error = -1 }`
   - Invalid: `enum Status : u8 { Ok = 1, Error = -1 }` (negative value with unsigned type)

2. **Value Assignment Rules**: 
   - Enum options without explicit values are auto-assigned incrementally starting from 0
   - If previous option has explicit value, auto-increment continues from that value
   - Duplicate values are allowed but generate warnings

3. **Identifier Uniqueness**: Enum option names must be unique within the enum scope

4. **Empty Enums**: Empty enums are allowed and can be useful for marker types or future extension

5. **Attribute Constraints**: Some attributes are enum-specific
   - `@repr("C")` affects memory layout for FFI compatibility
   - `@str(value)` provides string representation for variants
   - `@deprecated` can be applied to individual variants

**AST Mapping:**

- Maps to `EnumDeclarationNode` with:
  - `name`: Enum identifier
  - `base`: Optional backing type expression
  - `options`: List of enum option declaration nodes
  - `attributes`: List of attribute nodes

- Each option maps to `EnumOptionDeclarationNode` with:
  - `name`: Option identifier
  - `value`: Optional discriminant value expression
  - `attributes`: List of attribute nodes

**Parser Implementation Considerations:**

1. **Enum Detection**: Recognize `enum` keyword in declaration parsing
2. **Backing Type Parsing**: Handle optional `: type` syntax after enum name
3. **Option Parsing**: Parse option list with support for:
   - Optional explicit values (`= expression`)
   - Attributes on individual options
   - Trailing comma support
4. **Value Expression Parsing**: Discriminant values can be any compile-time constant expression
5. **Attribute Integration**: Parse preceding attributes for both enum and options
6. **Error Recovery**: Handle malformed option lists, missing values, invalid types
7. **Empty Enum Handling**: Support empty `{}` enum bodies
8. **Semicolon Handling**: Enums typically don't require semicolons

**Grammar Notes:**

- Enum options without explicit values auto-increment starting from 0 or previous explicit value + 1
- Backing types can be any integral primitive type (i8, i16, i32, i64, i128, u8, u16, u32, u64, u128)
- Attributes can be applied to the enum declaration itself and to individual options
- Option values must be compile-time constant expressions
- Empty enums are valid for forward declarations or marker types

### Phase 5.4: Visibility Modifiers

Visibility modifiers control the accessibility and linkage of declarations. They can be applied to any declaration type (variables, functions, enums, etc.) to specify their visibility scope and external linkage.

**EBNF Productions:**

```ebnf
declaration ::= attributeList? visibilityModifier? declarationKind

visibilityModifier ::= 'pub' | 'extern'

declarationKind ::= variableDeclaration
                  | functionDeclaration
                  | enumDeclaration
                  | structDeclaration
                  | classDeclaration
```

**Examples:**

```cxy
// Public declarations - exported from module
pub enum Status { Ok, Error }
pub func calculate(x i32) i32 { return x * 2 }
pub const MAX_SIZE = 1024
pub var globalCounter = 0

// External declarations - defined elsewhere
extern func printf(fmt string, ...args auto) void
extern var errno i32

// Combined with attributes  
@inline pub func fastOperation() { /* ... */ }
@linkname("libc_malloc") extern func malloc(size usize) *void

// Private declarations (default)
enum InternalStatus { Pending, Complete }
func helperFunction() { /* ... */ }
const INTERNAL_LIMIT = 100
```

**Semantic Constraints:**

1. **Visibility Scope**:
   - `pub` declarations are exported from the current module and accessible to other modules
   - `extern` declarations refer to symbols defined in external libraries or modules
   - Declarations without visibility modifiers are private to the current module

2. **External Linkage**:
   - `extern` functions must have compatible signatures with their external definitions
   - `extern` variables refer to global variables defined elsewhere
   - External symbols follow platform-specific calling conventions

3. **Modifier Conflicts**:
   - `pub` and `extern` are mutually exclusive on the same declaration
   - External functions typically cannot have bodies (forward declarations only)
   - External variables cannot have initializers in the declaration

**AST Mapping:**

- Visibility modifiers map to flags on declaration nodes:
  - `pub` sets the `flgPublic` flag
  - `extern` sets the `flgExtern` flag
  - No modifier means private scope (no flags set)

**Parser Implementation Considerations:**

1. **Modifier Detection**: Check for `pub` or `extern` keywords before parsing declaration type
2. **Flag Setting**: Set appropriate visibility flags on the resulting AST node
3. **Validation**: Ensure visibility modifiers are compatible with declaration type
4. **Error Handling**: Report conflicts between `pub` and `extern` on same declaration
5. **Attribute Integration**: Handle visibility modifiers alongside existing attribute parsing

**Grammar Notes:**

- Attributes must appear before visibility modifiers and declaration keywords
- Only one visibility modifier is allowed per declaration
- External declarations are primarily for FFI (Foreign Function Interface) support
- Public declarations enable module-based code organization and reuse

### Phase 5.5: Struct and Class Declarations

Struct and class declarations define composite types with fields, methods, and inheritance relationships. Classes support inheritance and polymorphism, while structs are value types focused on data composition.

**EBNF Grammar:**

```ebnf
structDeclaration ::= attributeList? visibilityModifier? 'struct' identifier genericParameters? inheritance? structBody

classDeclaration ::= attributeList? visibilityModifier? 'class' identifier genericParameters? inheritance? classBody

inheritance ::= ':' typeList

typeList ::= typeExpression (',' typeExpression)*

structBody ::= '{' annotationList? structMember* '}'

classBody ::= '{' annotationList? classMember* '}'

annotationList ::= annotationDeclaration+

structMember ::= fieldDeclaration | methodDeclaration | typeAliasDeclaration

classMember ::= fieldDeclaration | methodDeclaration | typeAliasDeclaration | constructorDeclaration | destructorDeclaration

fieldDeclaration ::= attributeList? visibilityModifier? identifier typeExpression? ('=' expression)? ';'?

methodDeclaration ::= attributeList? visibilityModifier? methodModifiers? 'func' methodName genericParameters? parameterList ('->' typeExpression)? (blockStatement | '=>' expression)? ';'?

methodModifiers ::= ('const' | 'static' | 'virtual' | 'override' | 'final')*

methodName ::= identifier | operatorOverload

operatorOverload ::= '`' (binaryOperator | unaryOperator | callOperator | indexOperator | redirectOperator | rangeOperator | truthyOperator) '`'

callOperator ::= '()'
indexOperator ::= '[]' | '[]='
redirectOperator ::= '&.'
rangeOperator ::= '..'
truthyOperator ::= 'bool'

typeAliasDeclaration ::= '`' identifier '=' typeExpression

annotationDeclaration ::= '`' identifier '=' expression

constructorDeclaration ::= attributeList? visibilityModifier? 'init' parameterList blockStatement

destructorDeclaration ::= attributeList? visibilityModifier? 'deinit' blockStatement

typeAnnotation ::= ':' typeExpression
```

**Examples:**

```cxy
// Basic struct with fields and methods
pub struct Point {
    x f64
    y f64 = 0.0  // field with default value
    
    func distance() f64 => sqrt(x*x + y*y)
    func `+`(other Point) Point => Point{x + other.x, y + other.y}
    func `bool`() bool => x != 0.0 || y != 0.0  // truthy operator
}

// Generic struct with constraints
struct Container<T, U: Numeric = i32> {
    data T
    size U
    capacity U = 10
    
    func get(index U) T => data[index]
    func `bool`() bool => size > 0  // empty container is falsy
}

// Class with inheritance and polymorphism
@serializable
pub class Shape {
    priv origin Point
    
    virtual func area() f64
    virtual func perimeter() f64
    
    func move(offset Point) {
        origin = origin + offset
    }
}

class Circle : Shape {
    radius f64
    
    override func area() f64 => PI * radius * radius
    override func perimeter() f64 => 2 * PI * radius
    
    func `as f64`() f64 => radius  // cast operator overload
}

// Struct with operator overloads and type alias
struct Demo {
    `Annotation = i32  // annotation declaration
    value i32
    value2 = T()  // field with default constructor call
    priv value3 T = T()  // private field with explicit type and default
    
    func say() {}
    const func say() {}  // const method (cannot modify instance)
    priv func done() {}  // private method
    
    @inline
    func who() {}
    
    func `+`(x i32) i32 => x + 10  // binary operator overload
    func `()`() {}  // call operator overload
    func `[]`(x i32) {}  // index operator overload
    func `[]=`(key string, value i32) {}  // index assignment overload
    func `as i32`() i32 {}  // cast operator overload
    func `&.`() {}  // redirect operator overload
    func `..`() {}  // range operator overload
}

// Multiple inheritance and generic constraints
class Print<T, U: isInteger, V = i32> : Base, Serializable {
    data T
    counter U
    metadata V
    
    init(data T, counter U) {
        self.data = data
        self.counter = counter
        self.metadata = V()
    }
    
    deinit {
        cleanup()
    }
    
    static func create() Print<T,U,V> => Print(T(), U())
}

// Extern struct for FFI
extern struct CApiStruct {
    field1 i32
    field2 *char
}
```

**Semantic Rules:**

1. **Struct vs Class Semantics**:
   - **Structs**: Value types, copy semantics, no virtual methods by default
   - **Classes**: Reference types, reference semantics, support virtual methods and inheritance

2. **Field Declarations**:
   - Fields can have explicit types (`name: type`) or inferred types (`name = value`)
   - Fields without initializers must have explicit type annotations
   - Private fields use `priv` visibility modifier

3. **Method Declarations**:
   - Methods can be `const` (cannot modify instance state)
   - Methods can be `static` (no instance access, called on type)
   - Virtual methods enable polymorphism in classes
   - Operator overloads use backtick syntax for special method names

4. **Inheritance**:
   - Structs can inherit from other structs (composition-based)
   - Classes support full inheritance with virtual method dispatch
   - Multiple inheritance is allowed with explicit base type list

5. **Generic Parameters**:
   - Support constraints, defaults, and variadic generics (same as functions)
   - Generic parameters are available in all member declarations

6. **Type Aliases**:
   - Declared with backtick syntax: `` `AliasName = Type``
   - Available within the struct/class scope
   - Can reference generic parameters

**AST Mapping:**

- `StructDeclarationNode` and `ClassDeclarationNode` with:
  - `name`: Identifier for the type name
  - `genericParameters`: Optional list of generic parameters
  - `baseTypes`: List of inherited types (inheritance clause)
  - `members`: List of field, method, and type alias declarations
  - `flags`: Visibility and modifier flags

**Parser Implementation Considerations:**

1. **Disambiguation**: Distinguish between field declarations with and without explicit types
2. **Method Parsing**: Reuse existing function declaration parsing with method-specific modifications
3. **Operator Overloads**: Parse special method names within backticks
4. **Inheritance**: Parse colon-separated base type list
5. **Body Parsing**: Handle mixed member types within struct/class bodies
6. **Generic Integration**: Apply existing generic parameter parsing to struct/class declarations

**Grammar Notes:**

- Semicolons are optional for member declarations (similar to field declarations in other contexts)
- Method bodies can be block statements or expression bodies (`=> expr`)
- Attributes apply to individual members as well as the overall declaration
- Visibility modifiers on members override default struct/class visibility
- Type aliases within structs/classes create scoped type names

### Phase 5.6: Import Declarations

Import declarations enable module composition and external library integration. The syntax supports various import patterns including whole module imports, selective imports, aliasing, and conditional imports for testing.

**EBNF Grammar:**

```ebnf
importDeclaration ::= 'import' ('test')? importClause

importClause ::= 
  | stringLiteral                                    # Whole module import
  | stringLiteral 'as' identifier                    # Module alias import  
  | importList 'from' stringLiteral                  # Named imports (single or multiple)

importList ::= 
  | importItem                                       # Single named import
  | '{' importItem (',' importItem)* '}'             # Multiple named imports

importItem ::= 
  | identifier                                       # Simple named import
  | identifier 'as' identifier                       # Named import with alias
```

**Examples:**

```cxy
// Whole module imports
import "utils.cxy"              // Import all exports from utils module
import "utils.cxy" as Utils     // Import with module alias
import "stdlib.h" as stdlib     // C header import with required alias

// Named imports (single items)
import dump from "utils.cxy"                    // Import specific function
import dump as myDump from "utils.cxy"          // Import with alias (equivalent to { dump as myDump })

// Multiple named imports
import { dump, debug } from "utils.cxy"         // Multiple imports
import { dump, debug as myDebug } from "utils.cxy" // Mixed with alias

// External library imports
import atoi from "stdlib.h"     // Import C function
import printf from "stdio.h"    // Import C library function

// Conditional test imports
import test "test_utils.cxy"                    // Only imported when building for tests
import test { assert, mock } from "test_utils.cxy"  // Test-only named imports
```

**Parser Implementation Considerations:**

1. **Module Path Validation**: Verify string literals contain valid file paths
2. **Alias Requirements**: C header imports require explicit aliases to avoid naming conflicts
3. **Selective Parsing**: Named imports create specific symbol bindings
4. **Test-Only Imports**: Imports prefixed with `test` are only processed in test builds
5. **Dependency Tracking**: Track import relationships for build ordering

**Grammar Notes:**

- Module paths are string literals supporting both `.cxy` and external formats
- The `as` keyword is required for C header whole-module imports
- Braces `{}` are required for multiple named imports
- Trailing commas are allowed in import lists
- The `test` keyword prefix indicates test-only imports
- Import declarations must appear at file scope (not within functions/classes)

### Phase 5.7: Type Declarations

Type declarations define type aliases that create new names for existing types. They support generic parameters, union types, and complex type expressions. Type declarations can be marked as public when used at module scope.

**EBNF Productions:**

```ebnf
typeDeclaration ::= attributeList? 'pub'? 'type' identifier genericParams? '=' typeExpression

genericParams ::= '<' genericParamList '>'

genericParamList ::= genericParam (',' genericParam)* ','?

genericParam ::= identifier genericConstraint? genericDefaultValue?

genericConstraint ::= ':' typeExpression

genericDefaultValue ::= '=' typeExpression
```

**Examples:**

```cxy
// Simple type alias
type Number = i32

// Union type alias
type Number = i32 | u32

// Function type alias
type Func = func() -> void

// Generic type alias with tuple
type Custom<T> = (T, i32)

// Public type declaration
pub type Result<T> = T | Error

// Generic type with constraints
type Comparable<T: Ord> = T

// Generic type with default parameter
type Optional<T = string> = T | null

// Complex nested type
type Handler<T> = func(T) -> Result<(), Error>

// Multiple generic parameters
type Map<K, V> = [(K, V)]

// Generic with constraints and defaults
type Container<T: Clone, U = i32> = (T, U)
```



**Parser Implementation Considerations:**

1. **Visibility Parsing**: Handle optional `pub` modifier at declaration start
2. **Generic Parameter Parsing**: Support constraints and default values in generic parameters
3. **Type Expression Integration**: Leverage existing complex type expression parser
4. **Scope Handling**: Type declarations create new type names in current scope
5. **Forward References**: Allow type aliases to reference other type aliases

**Grammar Notes:**

- Type declarations can only appear at file or module scope
- The `pub` modifier makes the type alias visible to other modules
- Generic parameters support both constraints (`:`) and default values (`=`)
- Type expressions on the right-hand side can be arbitrarily complex
- Recursive type definitions are validated during semantic analysis
- Type aliases do not create new types, only new names for existing types

### Phase 5.8: Module Declarations

Module declarations define structured compilation units with separate sections for top-level declarations (imports, type definitions) and main content (implementation code).

**EBNF Grammar:**

```ebnf
moduleDeclaration ::= 'module' identifier moduleBody

moduleBody ::= topLevelSection? mainSection?

topLevelSection ::= importDeclaration*

mainSection ::= declaration*
```

**Examples:**

```cxy
// Complete module with both sections  
module utils
import "core.cxy" as core
import "std/io.cxy" as io

type Result<T> = T | Error
func process(data: string) -> Result<i32> {
    // implementation here
}

// Module with only imports
module client
import "http.cxy" as http
import "json.cxy" as json

// Module with only main content
module impl
func helper() -> i32 { return 42 }
type LocalType = string

// Minimal module with single function
module hello
func greet() {
    println("Hello from module!")
}
```

**Parser Implementation Considerations:**

1. **File-Level Scope**: Module declarations can only appear at the top level of a file
2. **Single Declaration**: Only one module declaration is allowed per file
3. **Module Structure**: Module body contains top-level imports followed by main content (types, functions, implementations)
4. **Declaration Organization**: Top-level section is for imports only, main content contains all other declarations
5. **Identifier Validation**: Module names must follow standard identifier rules
6. **Namespace Creation**: Creates a namespace scope for all declarations in the file

**Grammar Notes:**

- Module declarations are optional - files without them use implicit naming
- Module names must be valid identifiers (no dots, dashes, or special characters)
- The module name becomes the namespace for all public exports from the file
- Top-level imports come before main content (all other declarations)
- Module structure provides logical organization without requiring explicit section markers
- Multiple files can contribute to the same module through build system configuration

### Phase 6.1: Qualified Paths

Qualified paths enable module-scoped type references and generic type instantiation. To disambiguate between generic type instantiation and comparison operators, the `::` prefix is required when referencing generic types in expression contexts.

**EBNF Grammar:**

```ebnf
qualifiedPath ::= pathSegment ('.' pathSegment)*

pathSegment ::= identifier genericArguments?

genericArguments ::= '<' typeList '>'

typeList ::= typeExpression (',' typeExpression)*

typeExpression ::= primitiveType | qualifiedPath

qualifiedTypeExpression ::= '::' qualifiedPath

primaryExpression ::= ... | qualifiedTypeExpression | ...
```

**Context-Based Parsing:**

1. **Type Contexts** (generics allowed without `::` prefix):
   - Type annotations: `var x: Vector<i32>`
   - Function parameters: `func process(items: List<string>)`
   - Return types: `func create() -> Map<string, i32>`
   - Inheritance clauses: `class MyClass : Base<T>`

2. **Expression Contexts** (require `::` prefix for generics):
   - Constructor calls: `var a = ::Vector<i32>()`
   - Generic function calls: `let result = ::convert<f64>(value)`
   - Type instantiation: `items.push(::Option<string>("hello"))`

**Examples:**

```cxy
// Type context - no :: needed
var items: Vector<string>
func process(data: Map<string, i32>) -> Result<(), Error>
class Container<T> : Collection<T>

// Expression context - :: required for generics
var a = ::Vector<i32>()                    // Generic constructor
let result = ::convert<f64>(value)         // Generic function call
let map = ::HashMap<string, i32>::new()    // Qualified generic constructor

// Regular expressions work without ::
if a < b && c < d {                        // Always comparisons
    let x = value < threshold              // Always comparisons
}

// Mixed contexts
func create() -> Vector<i32> {             // Return type context
    return ::Vector<i32>()                 // Expression context
}

// Complex nested generics
let complex = ::Map<string, ::Vector<::Option<i32>>>()
```

**Grammar Integration:**

The `::` disambiguation enables clean separation:
- `Vector<i32>` in type contexts (no ambiguity with comparisons)
- `::Vector<i32>()` in expression contexts (explicit type reference)
- `a < b` always parsed as comparison in expressions

**AST Mapping:**

- `QualifiedPathNode` with:
  - `segments`: Vector of path segments
  - `isGeneric`: Boolean indicating if any segment has generic arguments
  - `isExpressionContext`: Boolean indicating if parsed with `::` prefix
- `PathSegmentNode` with:
  - `name`: Identifier for the segment
  - `genericArgs`: Optional list of type arguments

**Parser Implementation Considerations:**

1. **Context Tracking**: Parser knows whether it's in type or expression context
2. **Simple Disambiguation**: `::` prefix immediately indicates qualified type in expression
3. **No Backtracking**: LL(3) friendly - decision is immediate based on context and `::` presence
4. **Generic Parsing**: Only attempt `<>` generic parsing in type contexts or after `::`
5. **Comparison Fallback**: `<` in expression contexts defaults to comparison operators

**Grammar Notes:**

- Generic arguments use angle bracket syntax: `Type<Args>`
- Multiple type arguments are comma-separated: `Map<K, V>`
- The `::` prefix is only required in expression contexts for disambiguation
- Empty generic arguments are not allowed: `Type<>` is invalid
- Regular qualified paths (without generics) don't require `::`: `mod.function()`

### Phase 6.2: Complex Type Expressions

Complex type expressions provide rich type system capabilities including arrays, tuples, unions, optionals, results, and function types. These enable precise type specifications for memory management, error handling, and functional programming patterns.

**EBNF Grammar:**

```ebnf
typeExpression ::=
  | primitiveType
  | qualifiedPath
  | arrayType
  | tupleType
  | unionType
  | optionalType
  | resultType
  | functionType
  | '(' typeExpression ')'

arrayType ::= '[' expression? ']' typeExpression

tupleType ::= '(' typeExpression (',' typeExpression)* ')'

unionType ::= typeExpression ('|' typeExpression)+

optionalType ::= '?' typeExpression

resultType ::= '!' typeExpression

functionType ::= 'func' '(' parameterTypes? ')' '->' typeExpression

parameterTypes ::= typeExpression (',' typeExpression)*
```

**Examples:**

```cxy
// Array types
[10]i32        // Fixed-size array of 10 integers
[]string       // Dynamic array of strings
[N]T           // Generic fixed-size array

// Tuple types
(i32, string)           // Two-element tuple
(i32, string, bool)     // Three-element tuple
()                      // Empty tuple (unit type)

// Union types
i32|string              // Union of integer or string
i32|string|null         // Nullable union
User|Error              // Result-like union

// Optional types
?i32           // Optional integer (equivalent to i32|null)
?string        // Optional string
?User          // Optional user type

// Result types
!i64           // Function that returns i64 or throws
!string        // Function that returns string or throws
!()            // Function that returns unit or throws

// Function types
func(i32, string) -> bool          // Function taking i32 and string, returning bool
func() -> i32                      // Function with no parameters
func([3]i32, (i32, string), i32|string) -> ?T  // Complex function signature
func(callback: func(i32) -> bool) -> []i32     // Higher-order function
```



**Parser Implementation Considerations:**

1. **Precedence Handling**: Union types have lower precedence than array/optional types
2. **Array Size Parsing**: Size expressions are parsed as general expressions
3. **Function Type Context**: `func` keyword disambiguates from function calls
4. **Optional vs Result**: `?` for nullable, `!` for error-returning
5. **Nested Types**: All type expressions can be arbitrarily nested
6. **Reference vs Pointer**: `&T` for references, `T` for raw pointers

**Grammar Notes:**

- Array types support both fixed-size `[N]T` and dynamic `[]T` variants
- Tuple types require parentheses and support empty tuples `()`
- Union types use `|` operator and require at least two members
- Optional types `?T` are sugar for `T|null`
- Result types `!T` indicate functions that may throw exceptions
- Function types use `func` keyword to avoid ambiguity with function calls
- Precedence: `?`, `!`, `[]` > `|` > `,` (in tuples) > `->` (in functions)
- All complex types can contain other complex types recursively

## Grammar Evolution Plan

This grammar will be extended in phases:

- **Phase 1**: Literals and identifiers ✅
- **Phase 2**: Basic expressions with operators ✅
- **Phase 3**: Function calls and member access ✅
- **Phase 3.1**: Cast expressions ✅
- **Phase 3.2**: Struct literals ✅
- **Phase 3.3**: Range expressions ✅
- **Phase 3.4**: Closure expressions (deferred - requires block statements)
- **Phase 3.5**: Advanced expression features ✅
- **Phase 4**: Statements and control flow
- **Phase 4.1**: Expression statements ✅
- **Phase 4.2**: Break and continue statements ✅
- **Phase 4.3**: Block statements ✅
- **Phase 4.4**: Defer, return, and yield statements ✅
- **Phase 4.5**: If statements ✅
- **Phase 4.6**: While statements ✅
- **Phase 4.7**: For statements  ✅
- **Phase 4.8**: Switch statements  ✅
- **Phase 4.9**: Match statements  ✅
- **Phase 5**: Declarations and definitions
- **Phase 5.0**: Attributes ✅
- **Phase 5.1**: Variable declarations ✅
- **Phase 5.2**: Function declarations ✅
- **Phase 5.3**: Enum declarations ✅
- **Phase 5.4**: Visibility modifiers ✅
- **Phase 5.5**: Struct and class declarations ✅
- **Phase 5.6**: Import Declarations (Deferred)
- **Phase 6**: Advanced features
- **Phase 6.1**: Qualified paths and generic instantiation ✅
- **Phase 6.2**: Complex type expressions (arrays, tuples, unions, optionals, results, functions)

Each phase will add new productions to this file as the parser capabilities expand.

### Notes on Phase 2 Implementation

The expression grammar follows traditional precedence hierarchy:

1. **Assignment** (lowest precedence)
2. **Conditional** (ternary `?:` - not yet implemented)
3. **Logical OR** (`||`)
4. **Logical AND** (`&&`)
5. **Bitwise OR** (`|`)
6. **Bitwise XOR** (`^`)
7. **Bitwise AND** (`&`)
8. **Equality** (`==`, `!=`)
9. **Relational** (`<`, `<=`, `>`, `>=`)
10. **Shift** (`<<`, `>>`)
11. **Additive** (`+`, `-`)
12. **Multiplicative** (`*`, `/`, `%`)
13. **Unary** (`++`, `--`, `+`, `-`, `!`, `~`, `&`, `&&` prefix)
14. **Cast** (`as`, `!as`)
15. **Postfix** (`++`, `--` postfix, `()`, `[]`, `.`, `&.`)
16. **Primary** (highest precedence)

This structure ensures correct operator precedence and associativity during parsing using the recursive descent approach.

### Notes on Phase 3 Implementation

The Phase 3 grammar adds complex postfix operations and collection literals:

1. **Postfix Operation Chaining**: All postfix operations (calls, indexing, member access) have the same precedence and are left-associative, allowing natural chaining like `obj.method()[0].field`.

2. **Collection Literal Disambiguation**:
   - `(expr)` - parenthesized expression
   - `(expr, expr)` - tuple literal (requires at least 2 elements)
   - `[expr, expr]` - array literal

3. **Function Call Syntax**: Supports both empty `()` and argument lists with proper comma separation.

4. **Member Access Operations**: Both dot (`.`) and arrow (`->`) operators for different access patterns (value vs pointer semantics).

5. **Precedence Integration**: Complex expressions integrate properly with the existing operator precedence hierarchy.

### Notes on Phase 3.1 Implementation

The Phase 3.1 grammar adds cast expressions to the language:

1. **Cast Precedence**: Cast expressions sit between unary and postfix operations in precedence, allowing natural expressions like `ptr.field as i32` and `-x as f64`.

2. **Two Cast Types**:
   - **Normal Cast** (`as`): Safe type conversion with runtime checks
   - **Unsafe Retype** (`!:`): Unsafe pointer reinterpretation, no runtime checks

3. **Type Expression Parsing**: Supports primitive types only for now (i8, i16, i32, i64, i128, u8, u16, u32, u64, u128, f32, f64, bool, char, void).

4. **Left Associative**: Multiple casts chain left-to-right: `x as i32 as f64` becomes `((x as i32) as f64)`.

5. **Integration Examples**:
   - `obj.field as bool` - cast member access result
   - `func() as i32` - cast function return value
   - `arr[i] !: u64` - unsafe retype array element to integer

### Notes on Phase 3.3 Implementation

The Phase 3.3 grammar adds range expressions to the language:

1. **Range Precedence**: Range expressions have precedence between relational and additive operators, allowing natural expressions like `x + 1..10` and `arr[0..len-1]`.

2. **Multiple Range Types**:
   - **Inclusive Range** (`..`): Both endpoints included (`1..10` = 1,2,3,...,10)
   - **Exclusive Range** (`..<`): End excluded (`1..<10` = 1,2,3,...,9)
   - **Open Ranges**: Missing start (`..10`), end (`5..`), or both (`..`)

3. **Function-Style Ranges**: Alternative syntax using `range()` function calls with 1-3 arguments for max, min/max, or min/max/step.

4. **Use Cases**: Iteration (`for i in 0..10`), slicing (`arr[1..5]`), pattern matching, and collection initialization.

5. **Integration Examples**:
   - `for i in 0..<count { }` - exclusive range iteration
   - `array[start..end]` - array slicing with range
   - `range(0, 100, 2)` - function-style with step

### Notes on Phase 3.5 Implementation

The Phase 3.5 grammar adds advanced expression features to the language:

1. **Spread Expression Precedence**: Spread expressions (`...expr`) are primary expressions that can be used in collection literals and function argument lists.

2. **String Interpolation Syntax**: Enhanced string literals with embedded expressions:
   - **Expression Interpolation**: `"Hello {name}!"` - any expression can be embedded

3. **Macro Call Varieties**: Two different macro invocation syntaxes:
   - **Bare**: `macro!` - equivalent to `macro!()`
   - **Function-like**: `macro!(args)` - similar to function calls

4. **Expression Integration**: All advanced features integrate naturally with existing expressions:
   - Spread in collections: `[1, ...middle, 3]`
   - Interpolation in assignments: `let msg = "Result: {value}"`
   - Macro chaining: `macro!().method()` or `macro!.method()`

5. **Parsing Considerations**:
   - Spread operator must be distinguished from range operator (`...` vs `..`)
   - String interpolation requires nested expression parsing within string context
   - Bare macro calls (`macro!`) are distinguished from factorial operator context

6. **Integration Examples**:
   - `func(a, ...spread_args, b)` - spread in function arguments
   - `println!("Processing {count} of {total} items")` - interpolated string in macro
   - `[...first_half, separator, ...second_half]` - multiple spreads in array
   - `debug!` - bare macro call for simple debugging

### Notes on Phase 5.5 Implementation

The Phase 5.5 grammar adds struct and class declarations to the language:

1. **Unified Member Parsing**: Both structs and classes share common member types (fields, methods, type aliases) with class-specific additions (constructors, destructors).

2. **Field Declaration Flexibility**: 
   - Fields can omit type annotations when initializers are present (`field = value`)
   - Fields can omit initializers when explicit types are provided (`field: Type`)
   - Both syntax forms can be combined (`field: Type = value`)

3. **Method Declaration Integration**:
   - Reuses existing function declaration parsing with method-specific modifiers
   - Operator overloads use backtick syntax to distinguish from regular identifiers
   - Method bodies support both block statements and expression syntax (`=> expr`)

4. **Inheritance Syntax**:
   - Colon-prefixed base type list supports multiple inheritance
   - Base types are parsed as type expressions (allowing generic instantiations)
   - Inheritance is optional for both structs and classes

5. **Generic Parameter Integration**:
   - Struct and class declarations can have generic parameters with same syntax as functions
   - Generic parameters are available throughout all member declarations
   - Constraints and defaults work identically to function generics

6. **Visibility and Modifier Handling**:
   - Members can have individual visibility modifiers that override container defaults
   - Method modifiers (`const`, `static`, `virtual`, etc.) are parsed and validated
   - External struct declarations are supported for FFI integration

7. **Type Alias Scoping**:
   - Type aliases within structs/classes create scoped type names
   - Backtick syntax (`` `AliasName = Type``) distinguishes from field declarations
   - Aliases can reference generic parameters and other members

8. **Parser Implementation Considerations**:
   - Member parsing requires lookahead to distinguish field vs method declarations
   - Semicolons are optional for member declarations (following variable declaration patterns)
   - Constructor/destructor parsing reuses block statement parsing for bodies
   - Attribute parsing applies to both container declarations and individual members

### Notes on Phase 5.6 Implementation

The Phase 5.6 grammar adds import declarations for module composition and external library integration:

1. **Import Pattern Recognition**:
   - Parser must distinguish between different import patterns using lookahead
   - String literal followed by `as` indicates module alias import
   - Identifier followed by `from` indicates named import
   - Braces `{}` immediately indicate multiple named imports

2. **Module Path Processing**:
   - String literals must be validated as proper file paths during parsing
   - Relative paths are resolved relative to importing file's directory
   - C header imports (`.h` extension) have different semantics than `.cxy` modules

3. **Conditional Import Handling**:
   - The `...` syntax creates conditional compilation dependencies
   - Parser should mark these imports with special flags for build system processing
   - Test imports are only resolved when building with test configuration

4. **Alias Requirement Validation**:
   - C header whole-module imports must include `as` clause to avoid naming conflicts
   - Parser should enforce this requirement and report errors for missing aliases
   - Named imports from C headers don't require aliases (functions are imported directly)

5. **Symbol Binding Creation**:
   - Each import creates specific symbol bindings in the current scope
   - Whole module imports create a single namespace binding
   - Named imports create individual symbol bindings
   - Aliases create renamed bindings to avoid conflicts

6. **Dependency Graph Construction**:
   - Parser should track import relationships for dependency ordering
   - Circular import detection can be performed during parsing phase
   - Import ordering affects symbol resolution in later compilation phases

7. **Integration with Qualified Paths**:
   - Imported module aliases can be used in qualified path expressions
   - `import "utils.cxy" as Utils` enables `Utils.dump()` syntax
   - Named imports enable direct usage without qualification

8. **Parser Implementation Considerations**:
   - Import declarations must only appear at file scope (not within functions/classes)
   - Multiple import clauses can be combined with comma separation for the same module
   - Trailing commas are allowed in import lists following collection literal patterns
   - Import parsing should integrate with existing string literal and identifier parsing

### Notes on Phase 5.7 Implementation

The Phase 5.7 grammar adds type declarations for creating type aliases with optional generic parameters:

1. **Type Alias Semantics**:
   - Type declarations create new names for existing types, not new types themselves
   - Aliases are resolved during semantic analysis phase, not during parsing
   - Generic type aliases create parameterized type templates
   - Public type aliases are exportable to other modules via `pub` modifier

2. **Generic Parameter Integration**:
   - Type declarations reuse existing generic parameter parsing from function declarations
   - Generic constraints (`:` syntax) limit acceptable type arguments
   - Default generic values (`=` syntax) provide fallback types when arguments omitted
   - Generic parameters are available in the aliased type expression scope

3. **Type Expression Integration**:
   - Right-hand side of type declarations can be any valid type expression
   - Union types, function types, arrays, tuples, and complex nested types all supported
   - Type aliases can reference other type aliases, enabling compositional type design
   - Forward references to other type aliases are resolved during semantic analysis

4. **Visibility and Scoping**:
   - `pub` modifier makes type aliases visible to importing modules
   - Type aliases create bindings in the current scope (file or module level)
   - Generic type aliases require explicit instantiation when used in expressions
   - Type alias names follow same scoping rules as other declarations

5. **Parser Implementation Considerations**:
   - Type declarations can only appear at file scope, not within functions or classes
   - Generic parameter parsing reuses existing function generic parameter infrastructure
   - Type expression parsing leverages Phase 6.2 complex type expression parser
   - Visibility modifier parsing integrates with existing `pub` keyword handling

6. **Integration Examples**:
   - `type Result<T> = T | Error` - generic union type for error handling
   - `type Handler = func(Request) -> Response` - function type alias for web handlers
   - `type Matrix<T: Numeric> = [[T]]` - constrained generic for 2D arrays
   - `pub type UserId = i64` - public primitive type alias for domain modeling

7. **Semantic Analysis Considerations**:
   - Recursive type alias detection must prevent infinite type expansion
   - Generic constraint validation ensures type arguments satisfy requirements
   - Default generic value substitution occurs when type arguments are omitted
   - Type alias resolution must handle transitive dependencies correctly

### Notes on Phase 5.8 Implementation

The Phase 5.8 grammar adds module declarations for organizing code into named modules:

1. **File-Level Organization**:
   - Module declarations provide a namespace identifier for the current compilation unit
   - Must appear at the very top of the file, before any imports or other declarations
   - Only one module declaration is allowed per file to maintain clarity
   - Files without module declarations use implicit naming based on file path

2. **AST Structure Alignment**:
   - `topLevel` field in `ModuleDeclarationNode` contains imports only
   - `mainContent` field contains all other declarations (types, functions, classes, etc.)
   - This separation reflects the logical organization where imports establish dependencies first
   - Parser must route import declarations to `topLevel` and other declarations to `mainContent`

3. **Import-First Organization**:
   - Top-level section is exclusively for import declarations
   - All dependency relationships must be established before main module content
   - Enables clear separation between external dependencies and module implementation
   - Supports dependency analysis and build ordering requirements

4. **Parser Implementation Considerations**:
   - Module declarations should be parsed early in the compilation unit processing
   - Parser must distinguish between import declarations (topLevel) and other declarations (mainContent)
   - Import parsing uses existing `parseImportDeclaration()` infrastructure
   - Main content parsing uses standard `parseDeclaration()` for all other declaration types

5. **Build System Integration**:
   - Module names are used for dependency resolution and import path calculation
   - Import-first structure enables dependency graph construction before processing implementations
   - Multiple files can contribute to the same module through build configuration
   - Module declarations enable explicit control over module structure vs implicit file-based naming

6. **Semantic Analysis Integration**:
   - Module names are registered in the module registry during parsing
   - Import resolution processes `topLevel` declarations first to establish symbol table
   - `mainContent` declarations are processed with full import context available
   - Module scoping affects visibility and access control for declarations

### Notes on Phase 6.2 Implementation

Complex type expressions represent the core of CXY's type system, enabling precise specification of data structures, memory layout, and function signatures.

1. **Type Expression Parsing**:
   - Type expressions are parsed in type contexts (variable declarations, function parameters, return types)
   - Expression contexts require careful disambiguation between `<` (comparison) and `<` (generics)
   - Union types have lower precedence than other type operators to enable `?i32|string` parsing correctly

2. **Array Type Handling**:
   - Fixed-size arrays `[N]T` parse size as general expressions (constants, variables, generics)
   - Dynamic arrays `[]T` have null size field in AST
   - Array element types can be any valid type expression, enabling `[10][20]i32` for 2D arrays

3. **Function Type Context**:
   - `func` keyword clearly disambiguates function types from function calls
   - Parameter types are parsed as comma-separated type expressions
   - Return type follows `->` arrow operator consistent with function declarations
   - Higher-order functions supported: `func(func(i32) -> bool) -> []i32`

4. **Union Type Precedence**:
   - Union operator `|` has lower precedence than prefix operators (`?`, `!`, array types)
   - This enables `?i32|string` to parse as `(?i32)|string` not `?(i32|string)`
   - Parentheses can override precedence when needed: `?(i32|string)`

5. **Optional and Result Type Sugar**:
   - `?T` is syntactic sugar for `T|null` but creates distinct AST nodes for semantic analysis
   - `!T` represents error-returning functions, not union with error types
   - Both are prefix operators with high precedence

6. **Tuple vs Function Parameter Disambiguation**:
   - Tuple types: `(i32, string)` - comma-separated types in parentheses
   - Function parameters: `func(i32, string) -> bool` - preceded by `func` keyword
   - Empty tuple `()` represents unit type, distinct from `func() -> T`

7. **Reference and Pointer Types**:
   - `&T` for borrowed references (managed lifetime)
   - `*T` for raw pointers (manual memory management)
   - Both are prefix operators that can be chained: `&*T`, `*&T`

8. **Recursive Type Support**:
   - All complex types can contain other complex types arbitrarily
   - Examples: `?[10](i32|string)`, `func(?T) -> ![](U|V)`
   - Parser must handle deep nesting without stack overflow
