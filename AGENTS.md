# AI Agent Instructions for Cxy Programming Language

## CMake Project Overview

Cxy is a modern compiled programming language implemented in C. This project uses a carefully designed architecture with
arena-based memory management, comprehensive diagnostic reporting, and a multi-phase parser development approach.

## Development Philosophy

### Incremental Development

- **Feature development must be incremental and unit-testable**
- Each component should be implementable and testable in isolation
- New features require corresponding test cases before implementation
- Avoid large monolithic changes - break work into small, verifiable steps

### Building

- Build prerequisites are `cmake >= 3.16`, Clang, and LLVM (see `README.md` and required `find_package(Clang/LLVM)` in `CMakeLists.txt`).
- Configure with CMake and build with a generator-agnostic command:
    - `cmake -S . -B build`
    - `cmake --build build --parallel`
- `make -j $(nproc)` is valid in Makefile-based builds, but prefer `cmake --build` for portability (including macOS).

### Testing

- CMake/CTest is enabled by default (`ENABLE_TESTS=ON`, `ENABLE_UNIT_TESTS=ON` in `CMakeLists.txt`).
- Run unit and stdlib tests from the build directory with `ctest --output-on-failure`.
- Snapshot/lang tests use `tests/runner.sh` (see `tests/lang`, `tests/package`, and per-directory `run.cfg` behavior in
  `tests/runner.sh`).
- On macOS, `tests/runner.sh` expects Bash >= 4 and `coreutils` (`gdate`), matching `tests/README.md`.

# Agent Guidelines for Cxy

Quick reference for AI agents working with Cxy code.

## Module and Package Declarations

```cxy
// Regular module file
module my_module

// File with main() - NO module declaration
func main() {     // ✅ no module declaration
    println("Hello")
}

// Index file (index.cxy) - uses package
package my_package

export { MyClass, MyFunc } from "./src/file.cxy"
// Only export statements allowed in package files

// NEVER use
module main       // ❌ invalid in main files
```

## Top-Level Declarations

Top-level declarations can only appear at the module/file level (not inside functions or classes):

```cxy
// Import declarations
import "stdio.h" as stdio
import { Vector } from "stdlib/vector.cxy"
export { MyClass, myFunc } from "./src/module.cxy"

// C build configuration
@__cc "src/native/implementation.c"
@__cc "src/native/helper.c"
@__cc:lib "pthread"
@__cc:lib "m"

// Conditional compilation
##if defined MACOS {
    import "include/darwin/specific.h" as dws
    @__cc:lib "darwin_specific"
}
else ##if defined LINUX {
    import "include/linux/specific.h" as linux
    @__cc:lib "clib"
}
else {
    import "include/generic.h"
}

// These MUST be at top level:
// - import, export statements
// - @__cc and @__cc:lib annotations
// - ##if conditional compilation blocks
```

## Reserved Keywords

All Cxy keywords are reserved and cannot be used as variable names:

```cxy
// Common keywords that might conflict with variable names
package     // ❌ use 'pkg' instead
type        // ❌ use 'typ' instead  
string      // ❌ use 'str' instead
range       // ❌ use 'rng' instead
class       // ❌ use 'cls' instead
interface   // ❌ use 'iface' instead (reserved but not yet implemented)
module      // ❌ use 'mod' instead
default     // ❌ use 'defaultValue' instead

// Other reserved keywords
// Control flow: if, else, match, switch, case, for, in, while, break, continue, return, yield
// Declarations: func, var, const, struct, enum, exception, extern, native, opaque
// Modifiers: pub, virtual, async
// Operators: is, as, new, delete, await, launch, defer, raise, catch, discard
// Imports: import, export, from, include
// OOP: this, This, super
// Other: auto, void, true, false, null, macro, asm, defined, test, plugin
```

## Comments

```cxy
// Single line comment

/* Multi-line comment
   Nesting supported:
   /* nested comment */
*/
```

## Literals

```cxy
// Null literal
null                    // nullify pointers, invalidate optionals, drop class/struct refs

// Boolean literals
true
false

// Character literals (default type: wchar)
'a'
'\n'                    // escaped character
'☺️'                    // wide character
'!'`char                // bound to type char

// Integer literals (type inferred from smallest fit)
1                       // decimal
0b01                    // binary
0o77                    // octal
0xaf                    // hex
100`i64                 // bound to i64

// Float literals (default type: f64)
0.00
1e10                    // exponential
1e-10
1.32`f32                // bound to f32

// String literals (type: string, null terminated char arrays)
"Hello World"
// - nullable
// - enumerable with for loops
// - indexable with .[]
// - len!(s) for length
```

## Built-in Types and Standard Library

These types are built into the language and don't need imports:

```cxy
String       // owned string
__string     // borrowed string slice
Optional     // optional values (Some/None)
Slice        // array slice view
OutputStream // output stream interface

// No import needed!
func process(s: String, out: &OutputStream): void { }
```

**Note:** `Vector` is NOT built-in - requires `import { Vector } from "stdlib/vector.cxy"`

### Standard Library Location

stdlib can be found at `$CXY_ROOT/lib/cxy/std/stdlib/*`

Always check API functions in stdlib before assuming:

```cxy
import { Database } from "stdlib/sqlite.cxy"
// Check: $CXY_ROOT/lib/cxy/std/stdlib/sqlite.cxy

import { Time } from "stdlib/time.cxy"
// Check: $CXY_ROOT/lib/cxy/std/stdlib/time.cxy
```

## String Types

```cxy
__string     // borrowed string slice (no allocation)
String       // owned string (allocated)
string       // native string (C-style)

// String literal shortcuts
"Hello World".S    // creates String, same as String("Hello World")
"hello world".s    // creates __string, same as __string("hello world")
"hello world"      // native string

// Interpolated strings are already String type
var name = "World"
var msg = f"Hello {name}"  // msg is String, no conversion needed

// Conversions
var s = "hello".S
var slice = s.__str()      // String.__str() returns __string
var native = s.str()       // String.str() returns native string

var borrowed = "test".s
var native2 = borrowed.str()  // __string.str() returns native string
                              // Warning: may not be null-terminated!

var slice2 = s.substr(0, 3)   // substring slice (no allocation)
```

## String Interpolation

```cxy
// String interpolation with f"${...}" syntax
var a = 10
var b = 20
println(f"${a} + ${b} = ${a + b}")  // prints: 10 + 20 = 30

// Works with primitive types
var name = "Alice"
var age = 25
var msg = f"${name} is ${age} years old"

// Works with tuples
var pair = (10, "hello")
println(f"Tuple: ${pair}")  // prints: Tuple: (10, hello)

// Custom types with str operator
struct Message {
    text: String
    
    func `str`(os: &OutputStream) {
        os << "Message: " << text
    }
}

var m = Message("Hello".S)
println(f"${m}")  // prints: Message: Hello

// Tuples and unions with str implementations
var data = ("Jane", Message("World".S))
println(f"Data: ${data}")  // prints: Data: (Jane, Message: World)

// Rules:
// - Use f"${expr}" syntax for interpolation
// - Supports primitives, tuples, unions out of the box
// - Custom types need `str` operator implementation
// - `str` operator takes OutputStream reference
```

## Optionals

```cxy
// Optional type: Type?
// Can hold a value or be invalid (null)

// Create invalid optional
var x: i32? = null
var y: i32? = None[i32]()  // same as above

// Create valid optional
var z: i32? = 10
var w: i32? = Some(10)  // same as above

// Check if has value with truthy operator !!
if (!!opt) {
    // has value
}

if (!opt) {
    // no value
}

// Extract value with dereference operator *
if (!!z) {
    var val = *z  // get the i32 value
}

// Access fields/methods directly without unpacking
opt&.field          // access field if valid
opt&.method()       // call method if valid

// Invalidate optional
z = null  // z is now invalid

// Complete example
func divide(a: i32, b: i32): i32? {
    if b == 0
        return None[i32]()  // return invalid optional
    return Some(a / b)      // return valid optional
}

var result = divide(10, 2)
if (!!result) {
    println("Result: ", *result)
} else {
    println("Division failed")
}

// NEVER use
opt.hasValue()      // ❌ doesn't exist
opt.value()         // ❌ doesn't exist
opt == null         // ❌ cannot compare to null
```

## Copy Semantics

```cxy
// References are NOT implicitly copied
func process(val: T) { }

var x: &T = returnsReference()
process(__copy!(x))  // ✅ explicit copy required

// Without __copy!:
process(x)           // ❌ may fail - no implicit copy

// Copy behavior:
// - For value types (struct): memberwise copy
// - For class types: ref count ++, same object
// - For String (class): ref count ++

// Key principle: no silent copies, user must be explicit
```

## Variables and Declarations

```cxy
// Variable declarations
var x: i32 = 10         // mutable variable
var y = 20              // type inferred from initializer
var z: i32              // uninitialized (use with caution!)

// Type inferred from first assignment
var w
w = 42                  // now w is i32

// Constants (immutable, initializer required)
const PI: f64 = 3.14159
const MAX_SIZE: i32 = 1000

// Auto type
var result: auto = someFunction()  // type inferred from return value

// Global variables are supported
var globalCounter: i32 = 0

// Multiple variable declarations with tuple unpacking
const a, _, b = getTuple()           // _ discards value
const x, _ = (true, "Hello")         // x = true, string discarded
var first, second, third = (1, 2, 3)

// Rules:
// - var = mutable, const = immutable
// - Type optional if initializer provided or first assignment made
// - const variables require initializer
// - Multiple declarations need tuple initializer
```

## Type Aliases and Enums

```cxy
// Type aliases
type UserId = i64
type Callback = func(i32): void
type Handler = func(req: Request, res: Response): void

// Compound type aliases
type Unsigned = u8 | u16 | u32 | u64  // union type
type OptionalInt = i32?                 // optional type
type IntPtr = ^i32                      // pointer type
type IntRef = &i32                      // reference type

// Generic type aliases
type Result[T] = Error | T

var id: UserId = 123

// Simple enum
pub enum Color {
    Red,
    Green,
    Blue
}

var color = Color.Red

// Enum with values
pub enum Status {
    Pending = 0,
    Active = 1,
    Inactive = 2
}
```

## Types

### Arrays

```cxy
// Fixed-size arrays: [Type, Size]
var a: [i32, 3]
var b: [i32, 10] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]

// Indexing (note .[] syntax)
a.[0] = 1
a.[1] = 2
a.[2] = 3

// Enumerable
for x in a {
    println(x)
}

// Get length
var size = len!(a)  // returns 3

// Arrays are indexable, enumerable, and support len! macro
```

### Slices

```cxy
// Unsized arrays: [Type] (size omitted)
func show(nums: [i32]) {
    for n in nums {
        println(n)
    }
}

// Create slice from array
var arr: [i32, 3] = [0, 1, 2]
show(arr)  // compiler creates slice automatically

// Create slice using Slice constructor
var msgs: ^Message = alloc[Message](10)
var slice = Slice[Message](msgs, 10)

// Slices are enumerable and indexable
// Internally represented by builtin Slice type
```

### Tuples

```cxy
// Group multiple types: (Type1, Type2, ...)
var a: (i32, string) = (10, "Hello")

// Access members with integral expressions
var num = a.0      // 10
var text = a.1     // "Hello"

// Unpack with ... operator
var first, second = a  // first = 10, second = "Hello"

// Tuple types in function returns
func getPair(): (i32, string) {
    return (42, "answer")
}
```

### Pointers

```cxy
// Pointer type: ^Type (stores memory address)
var x = 100 as u32
var y: ^u32 = ptrof x  // get pointer with ptrof

// Null assignment
var ptr: ^i32 = null

// Pointer arithmetic with ptroff! macro
var s = "hello"
var p = s !: ^char         // retype string to pointer
var c = ptroff!(p + 2)     // pointer offset

// Dereference
var d = *c                 // equivalent to c.[0]

// Index into pointer
var val = p.[0]

// Arrays and strings are pointer types
```

### References and Moves

```cxy
// Reference type: &Type (like pointers but for structural types)
// - Used with struct, class, tuple, union
// - Compiler handles dereferencing automatically

func processMessage(msg: &Message) {
    // msg works like normal variable
    msg.send()
}

var x = Message("Hello".S)
processMessage(&x)  // take reference with &

// References work like normal types, no manual dereferencing needed

// Take reference (lvalues only)
var ref = &value    // borrow reference
func(&arg)          // pass by reference

// Move ownership (lvalues only)
return &&value      // move value
func(&&arg)         // move argument

// ⚠️  WARNING: Moving variables in loops
// The compiler cannot detect if you move a variable declared outside a loop
// inside the loop body. This will cause runtime errors on the second iteration.
// BAD - will fail at runtime:
// var data = Vector[i32]()
// for i in 0..10 {
//     process(&&data)  // ❌ moves data on first iteration, error on second!
// }
// GOOD - declare inside loop or use references:
// for i in 0..10 {
//     var data = Vector[i32]()
//     process(&&data)  // ✅ new data each iteration
// }
// OR:
// var data = Vector[i32]()
// for i in 0..10 {
//     process(&data)   // ✅ borrow reference instead
// }
// OR (if function doesn't accept references):
// var data = Vector[i32]()
// for i in 0..10 {
//     process(__copy!(data))  // ✅ explicit copy each iteration
// }

// Variadic arguments
func process(...args: auto) { }

// Move variadic args
func forward(...args: auto) {
    other(...&&args)  // forward with move
}

// Reference variadic args
func forward(...args: auto) {
    other(...&args)   // forward by reference
}
```

### Function Types

```cxy
// Function type syntax: func(params) -> return_type
type Handler = func(msg: string): void
type Processor = func(x: i32, y: i32): i32
type Callback = func(data: string, code: i32): void

// Use in function parameters
func execute(handler: func(msg: string): void) {
    handler("Hello")
}

// Generic function types
type Mapper[T, R] = func(value: T): R

// Function types can be used as parameters or return values
```

## Operators

```cxy
// Arithmetic: +, -, *, /, %
var sum = a + b
var diff = a - b
var product = a * b
var quotient = a / b
var remainder = a % b

// Comparison: ==, !=, <, >, <=, >=
if a == b { }
if a != b { }
if a < b { }

// Logical: &&, ||, !
if a && b { }
if a || b { }
if !condition { }

// Bitwise: &, |, ^, ~, <<, >>
var result = a & b      // bitwise AND
var result = a | b      // bitwise OR
var result = a ^ b      // bitwise XOR
var result = ~a         // bitwise NOT
var result = a << 2     // left shift
var result = a >> 2     // right shift

// Compound assignment: +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=
x += 5
x -= 3
x *= 2
x &= mask
x <<= 1

// Increment/Decrement
x++     // post-increment
++x     // pre-increment
x--     // post-decrement
--x     // pre-decrement

// Range operator
0..10   // range from 0 to 9 (exclusive end)

// Special operators
&.      // redirect operator (safe member access)
&&      // move operator
&       // reference operator
*       // dereference operator
...     // spread operator
```

## Control Flow Syntax

```cxy
// Prefer simple syntax without parens
if cond { }           // ✅ preferred
while cond { }        // ✅ preferred
switch expr { }       // ✅ preferred
match expr { }        // ✅ preferred
for x in range { }    // ✅ preferred

// Instead of
if (cond) { }         // works but verbose
while (cond) { }      // works but verbose
for (x in range) { }  // works but verbose

// Parens OK for one-liners without blocks
if (cond)
    return 10

while (cond)
    doSomething()
```

## For Loops

```cxy
// Range iteration
for i in 0..10 { }
for i : 0..10 { }   // same

// Collection iteration (value is tuple)
for v in collection {
    v.0  // value reference (&T)
    v.1  // index
}

// Collection with unpacking
for val, idx in collection {
    val  // unpacked value
    idx  // index
}

// Ignore with underscore
for val, _ in collection { }  // ignore index

// Loop control
for i in 0..10 {
    if i == 5
        continue    // skip to next iteration
    if i == 8
        break       // exit loop
}

// NEVER use
for (var i = 0; i < n; i++) { }  // ❌ C-style not supported
```

## Switch Statements

Discriminates on **expression values** (not types):

```cxy
switch x {
    0 => println("Zero")
    1..9 => println("Range 1-9")
    10, 20, 30 => println("Multiple values")
    
    // Fall-through to next case with body
    15..18 =>        // falls through
    case 25 =>       // case keyword required after fall-through
    case 35 => println("15-18, 25, or 35")
    
    ... => { }       // default case (must have body)
}

// Rules:
// - Cases are values, not types
// - Ranges supported: 0..10
// - Comma-separated values: 10, 20, 30
// - Fall-through: case without body shares next case's body
// - 'case' keyword required on continuation cases
// - No break/fallthrough statements (yet)
```

## Match Expressions

Discriminates on **types** (for union types):

```cxy
match x {
    i32, i16, i8 => println("Integer types")
    f64 as num => println("Float: ", num)
    String as s => println("String: ", s)
    ... => { }
}

// Rules:
// - Matches on types, not values
// - Use 'as' to create alias (only on single type)
// - Cannot use 'as' with multiple types: i32, i16 as x ❌
// - 'as' creates temporary if needed (x is obj.hello → temp = obj.hello cast to type)
// - No 'case' keyword needed (prefer without)
```

## Exceptions

Exception declarations define the structure and message of errors that can be raised.

```cxy
// Simple exception (no parameters, no body)
exception Error1

// Exception with expression body
exception Error2 => "Error2 message"

// Exception with block body (must return string)
exception Error3 {
    return "Error3 message"
}

// Exception with parameters
exception Error4(msg: string) => msg

// Exception with parameters and defaults
pub exception Error5(id: i32, msg: String = null) {
    if (msg == null) {
        msg = f"Error number {id}".S
    }
    return msg.str()  // must return string type
}

// Raise exceptions
raise Error1()
raise Error4("something failed")
raise Error5(100)

// Rules:
// - ONLY declared exceptions can be raised
// - Exception body MUST return string type
// - Parameters follow function parameter syntax
// - Use String type (not string) for stateful exceptions

// NEVER use
panic()       // ❌ unsupported
```

## Defer Statements

```cxy
// Execute code when scope exits (like Go's defer)
func processFile(path: string): !void {
    var file = openFile(path)
    defer file.close()  // called when function returns
    
    // ... work with file ...
    
    // file.close() called automatically before return
}

// Multiple defers execute in reverse order (LIFO)
func example() {
    defer println("First")   // executes last
    defer println("Second")  // executes second
    defer println("Third")   // executes first
    
    // Output: Third, Second, First
}
```

## Class Semantics

```cxy
// Classes are reference-counted and nullable
class MyClass { }

var obj: MyClass = null     // ✅ can be null
if (obj == null) { }        // ✅ can compare to null
if (obj != null) { }        // ✅ valid check

// String is a class, so it's reference-counted
var s1 = "hello".S
var s2 = s1                 // ref count ++, same object

// Caution: reference counting has overhead
// Use structs for simple value types instead
```

### Class Member Access

```cxy
pub class MyClass {
    name: String
    
    func `init`(name: String) {
        this.name = &&name      // 'this' is a reference to current instance (in classes)
    }
    
    func getName(): String {
        return this.name        // access instance member
    }
    
    func copy(): This {         // 'This' = current type (MyClass)
        return MyClass(this.name)
    }
}

pub struct MyStruct {
    value: i32
    
    func `init`(value: i32) {
        this.value = value      // 'this' is a pointer to current instance (in structs)
    }
    
    func getValue(): i32 {
        return this.value
    }
}

pub class Derived: Base {
    func method(): !void {
        super.method()          // 'super' calls parent method
        // ... additional logic
    }
    
    func `init`() {
        super()                 // call parent constructor
    }
}

// Notes:
// - 'this' is a reference (&) in classes
// - 'this' is a pointer (^) in structs
// - 'This' refers to the current type
// - Use super() to call parent initializer
```

## Error Handling Patterns

Functions that can raise exceptions MUST have `!` prefix on return type:

```cxy
exception IOError
exception InvalidOperationError

// Function that raises exceptions (note ! prefix)
func crash(x: i32): !i32 {
    if (x == 0)
        raise IOError()
    return x
}

// Void function that raises (note !void)
func doSomething(): !void {
    raise InvalidOperationError()
}

// Default handling: bubble up to caller
func caller(): !void {
    crash(0)  // no catch, exception bubbles up
}

// Discard errors (for void-returning expressions)
doSomething() catch discard

// Default value (for non-void expressions)
var count = getCount() catch 0
var name = getName() catch "unknown"

// Handle with block and yield default value
var result = compute() catch {
    logError()
    yield defaultValue  // exits catch block, not function
}

// Handle and exit function
func process(): !i64 {
    var stmt = db.exec("...") catch {
        return 0  // exits entire function
    }
    return stmt.column[i64](0)
}

// Re-raise caught exception
func wrapper(): !void {
    crash(0) catch {
        println("Caught error, re-raising")
        raise  // re-raise without argument
    }
}

// Access caught exception with ex! macro
func handler(): void {
    crash(0) catch {
        println("Exception: ", ex!)  // ex! is the caught exception
    }
}

// Stack traces are automatically included
// Example output:
// IOError("something failed") at:
//  crash (/path/to/file.cxy:10)
//  main (/path/to/file.cxy:20)

// NEVER use
catch {}      // ❌ empty object, not a block
```

### Complete Exception Example

```cxy
exception NullPointerError
exception InvalidOperationError
exception AgeError(msg: String) => msg.str()

struct User {
    name: String
    age: i32
    
    func `init`(name: String, age: i32) {
        this.name = &&name
        this.age = age
    }
    
    func drink(): !void {
        if age < 21
            raise AgeError(f"{name} is under age".S)
        println("Enjoying drink")
    }
}

func validate(msg: string): !void {
    if !msg
        raise NullPointerError()
    println(msg)
}

func process(id: i32, msg: string): !i32 {
    if id != 2
        raise InvalidOperationError()
    
    // Exception bubbles up if not caught
    validate(msg)
    
    if id == 0
        return 100
    
    // Catch and re-raise
    validate(null) catch {
        println("Caught null pointer")
        raise
    }
}

func main() {
    // Discard exception
    validate("Hello") catch discard
    
    // Handle with block
    validate("World") catch {
        println("Unhandled exception: ", ex!)
    }
    
    // Default value if exception raised
    var id = process(2, "test") catch 0
    
    // Yield from catch block
    id = process(2, "ok") catch {
        if id == 0
            raise
        yield 100
    }
    
    // User example with stack trace
    var user = User("Carter".S, 16)
    user.drink() catch {
        println("Error: ", ex!)
        // Stack trace automatically included:
        // AgeError("Carter is under age") at:
        //   User::drink (file.cxy:15)
        //   main (file.cxy:58)
    }
}
```

## Visibility Modifiers

```cxy
// Public class/struct/function
pub class MyClass { }
pub struct MyStruct { }
pub func myFunc() { }

// Member visibility
pub class MyClass {
    _privateName: String       // ✅ _ prefix = private
    publicName: String          // ✅ no prefix = public
    
    @private
    secretField: String         // ✅ @private = private
    
    func publicMethod() { }     // ✅ public (no pub prefix on members!)
    
    @private
    func privateMethod() { }    // ✅ private method
}

// NEVER use
pub class MyClass {
    pub func method() { }       // ❌ no 'pub' on member functions
    - privateName: String       // ❌ avoid '- ' prefix, use '_' instead
}

// Private methods (like operator overloads)
pub class MyClass {
    _data: String
    
    @private
    func `init`(data: String) {  // private constructor
        _data = &&data
    }
}
```

## Virtual Methods

```cxy
// Abstract class (has virtual method without body)
pub class Base {
    virtual func method(): !void  // no body = abstract
}

// Concrete class (virtual with body)
pub class Base {
    virtual func method(): !void {
        // default implementation
    }
}

// Derived class
pub class Derived: Base {
    @override
    func method(): !void {
        // override implementation
    }
}

// Note: If ANY virtual method has no body, class becomes implicitly abstract
```

## Static Methods

```cxy
// Factory methods use @static
pub class MyClass {
    _data: String
    
    func `init`(data: String) {
        _data = &&data
    }
    
    @static
    func create(value: string): !MyClass {
        var obj = MyClass(String(value))
        return &&obj
    }
}

// Usage
var instance = MyClass.create("test")
```

## Abstract Classes (Interfaces Not Yet Supported)

```cxy
// Abstract class with virtual methods (no body = abstract)
pub class Drawable {
    virtual func draw(out: &OutputStream): !void
    virtual func getWidth(): i32
}

// Class implementing abstract class
pub class Rectangle: Drawable {
    width: i32
    height: i32
    
    func `init`(w: i32, h: i32) {
        width = w
        height = h
    }
    
    // Implement virtual methods
    @override
    func draw(out: &OutputStream): !void {
        out << "Drawing rectangle\n"
    }
    
    @override
    func getWidth(): i32 {
        return width
    }
}

// Abstract class as parameter type
func render(item: &Drawable) {
    item.draw(&stdout)
}

// Note: Interfaces are not yet supported in Cxy
// Use abstract classes with virtual methods instead
```

## Array/Collection Access

```cxy
// Index access syntax
arr.[idx]      // ✅ correct
arr[idx]       // ❌ wrong

var vec = Vector[i32]()
vec.push(42)
var val = vec.[0]  // access first element
```

## Operator Overloading

```cxy
pub class Vector2 {
    x: f64
    y: f64
    
    // Binary operators
    func `+`(other: &Vector2): Vector2 {
        return Vector2(x + other.x, y + other.y)
    }
    
    func `-`(other: &Vector2): Vector2 {
        return Vector2(x - other.x, y - other.y)
    }
    
    // Comparison
    func `==`(other: &Vector2): bool {
        return x == other.x && y == other.y
    }
    
    // Index access
    func `[]`(index: i32): f64 {
        if index == 0
            return x
        return y
    }
    
    // Index assignment
    func `=[]`(index: i32, value: f64): void {
        if index == 0
            x = value
        else
            y = value
    }
    
    // Call operator
    func `()`(scale: f64): Vector2 {
        return Vector2(x * scale, y * scale)
    }
    
    // String conversion
    func `str`(): string {
        return f"({x}, {y})".str()
    }
    
    // Hash function
    func `hash`(): u64 {
        return hashCombine(hash(x), hash(y))
    }
}

// Overloadable operators:
// Arithmetic: +, -, *, /, %
// Comparison: ==, !=, <, >, <=, >=
// Bitwise: &, |, ^, <<, >>
// Logical: &&, ||
// Special: [], =[], (), str, init, deinit, copy, hash
```

## Union Types (Tagged Unions)

```cxy
// Union types: list of types separated by |
// Compiler generates tags to track current type
type Result = i32 | String | Error
type Unsigned = u8 | u16 | u32 | u64

var a: i32 | string = 10
a = "Hello World"  // can change to different union member

// Casting to concrete type
var b = a as string        // ✅ OK, string is in union
var c = a as i32          // ⚠️  Compiles, but crashes at runtime if wrong type

// Type checking with 'is' operator
if a is string {
    // true because a currently holds string
    var text = a as string  // safe cast here
}

if a is i32 {
    // false in this case
}

// Match statement for type-safe handling
func process(result: Result) {
    match result {
        i32 as num => println("Got number: ", num)
        String as str => println("Got string: ", str)
        Error as err => println("Got error: ", err)
        ... => { }  // default case
    }
}

// Rules:
// - Tagged unions track current type at runtime
// - Cast to concrete type only if it's a union member
// - Use 'is' operator for safe type checking
// - Match statements provide type-safe dispatch
```

## SQLite API

```cxy
import { Database, Statement } from "stdlib/sqlite.cxy"

// Open database
var db = Database.open("path.db")

// Execute query (variadic args)
var stmt = db.exec("SELECT * FROM users WHERE id = ?", userId)

// Iterate results
while (stmt.next()) {           // next() doesn't throw
    var id = stmt.column[i64](0)
    var name = stmt.column[string](1)
}

// Read into vector
var users = Vector[User]()
stmt >> users

// Note: stmt.column[T]() throws - handle based on caller signature
// If caller returns !T, error propagates automatically
func getUserId(name: string): !i64 {
    var stmt = db.exec("SELECT id FROM users WHERE name = ?", name)
    if (stmt.next()) {
        return stmt.column[i64](0)  // error propagates
    }
    return 0
}

// If caller returns T (no !), must catch the error
func getCount(): i64 {
    var stmt = db.exec("SELECT COUNT(*) FROM t") catch {
        return 0
    }
    if (stmt.next()) {
        return stmt.column[i64](0) catch 0  // must handle!
    }
    return 0
}

// NEVER use
db.prepare()                     // ❌ use db.exec()
stmt.bind()                      // ❌ use variadic args in exec()
stmt.step()                      // ❌ use stmt.next()
stmt.columnText()                // ❌ use stmt.column[string]()
stmt.columnInt64()               // ❌ use stmt.column[i64]()
```

## Async and Launch (Coroutines and Threads)

```cxy
// 'async' launches a coroutine
async fetchData("https://example.com")  // runs in coroutine

// Async block
async {
    println("Running in coroutine")
    // coroutine work
}

// 'launch' launches a thread
launch processData()  // runs in separate thread

// Launch block in thread
launch {
    println("Running in separate thread")
    // thread work
}

// Note: 
// - 'async' = coroutine (lightweight concurrency)
// - 'launch' = thread (OS-level concurrency)
// - 'await' keyword exists but is currently unused
```

## C Interop and Native Declarations

```cxy
// Import C headers
import "stdio.h" as stdio

// Call C functions
stdio.printf("Hello from C\n")

// Declare native functions (implemented externally in C)
native func my_c_function(x: i32): i32

// Extern functions (defined elsewhere in Cxy)
extern func helper(data: string): !void

// Build C sources alongside Cxy (top-level declaration)
@__cc "src/native/mylib.c"
@__cc "src/native/helper.c"

// Link against C libraries (top-level declaration)
@__cc:lib "pthread"
@__cc:lib "m"  // math library

// Pass pointers to C functions
var buffer: [u8; 1024]
stdio.fwrite(ptrof buffer, 1, 1024, file_ptr)
```

## Conditional Compilation

```cxy
// Top-level conditional compilation with ##if
##if defined MACOS {
    import "include/darwin/specific.h" as dws
}
else ##if defined LINUX {
    import "include/linux/specific.h" as linux
    @__cc:lib "clib"
}
else {
    import "include/generic.h"
}

// Conditional imports and build configuration
##if defined DEBUG {
    @__cc "debug/logger.c"
}

// Multiple conditions
##if defined WINDOWS {
    @__cc:lib "ws2_32"
    native func win32_init(): void
}
```

## Functions

```cxy
// Basic function declaration
func greet(name: string): void {
    println("Hello, ", name)
}

// Return type is optional (inferred by compiler)
func add(x: i32, y: i32) {
    return x + y  // return type inferred as i32
}

// Return type required for:
// - Recursive functions
// - Functions called before declaration
func factorial(n: i32): i32 {
    if n <= 1
        return 1
    return n * factorial(n - 1)  // recursive
}

// Expression body with =>
func multiply(x: i32, y: i32): i32 => x * y

// Public functions (private by default)
pub func publicAPI() {
    // accessible outside module
}

func privateHelper() {
    // only accessible within module
}

// Attributes
@inline
func fastCompute(x: i32): i32 => x * 2

// Function with function type parameter
func execute(handler: func(msg: string): void) {
    handler("test")
}
// By default, only closures allowed:
execute((msg: string) => { println(msg) })

// @pure attribute removes closure requirement
@[inline, pure]
func execute(handler: func(msg: string): void) {
    handler("test")
}
// Now native functions work too

// Multiple attributes
@[inline, pure]
func process(x: i32): i32 => x + 1

// Variadic parameters
func sum(...nums: i32): i32 {
    var total = 0
    for num, _ in nums {
        total += num
    }
    return total
}
```

### External Functions

```cxy
// Declare functions implemented in C or other libraries
extern func strlen(s: string): u64
extern func sprintf(s: ^char, fmt: string, ...args: auto): i64

// Public external functions
pub extern func malloc(size: u64): ^void
pub extern func free(ptr: ^void): void

// External functions have no body
// Must be provided at link time (via library or C source)

// Use with C interop
@__cc:lib "m"  // link math library
extern func sin(x: f64): f64
extern func cos(x: f64): f64
```

## Unit Testing

```cxy
// Test-only imports (only available when running `cxy test`)
import test { Vector } from "stdlib/vector.cxy"
import test { Database } from "stdlib/sqlite.cxy"

// Test-only code blocks (not compiled into regular builds)
test {
    // Helper structs for testing
    struct TestUser {
        id: i32
        name: String
    }
    
    // Helper functions for tests
    func createTestUser(id: i32): TestUser {
        return TestUser(id, "TestUser".S)
    }
    
    // Test utilities
    func assertContains(haystack: string, needle: string): bool {
        // implementation
        return true
    }
}

// Multiple test blocks are allowed
test {
    const TEST_TIMEOUT: i32 = 5000
    const TEST_DATA_PATH: string = "/tmp/test_data"
}

// Declare tests with test "name" { }
test "basic arithmetic works" {
    var sum = 2 + 2
    ok!(sum == 4)
    
    var product = 3 * 4
    ok!(product == 12)
}

test "vector operations" {
    var vec = Vector[i32]()
    vec.push(1)
    vec.push(2)
    vec.push(3)
    
    ok!(vec.size() == 3)
    ok!(vec.[0] == 1)
    ok!(vec.[2] == 3)
}

test "optional handling" {
    var some = Some(42)
    var none = None[i32]()
    
    ok!(!!some)
    ok!(!none)
    ok!(*some == 42)
}

test "string operations" {
    var s = "Hello World".S
    var upper = s.toUpper()
    
    ok!(s.length() == 11)
    ok!(assertContains(upper.str(), "HELLO"))
}

test "user creation with test helpers" {
    var user = createTestUser(1)
    
    ok!(user.id == 1)
    ok!(user.name.str() == "TestUser")
}

test "exception handling" {
    exception TestError
    
    func mayFail(): !i32 {
        raise TestError()
    }
    
    var caught = false
    mayFail() catch {
        caught = true
    }
    
    ok!(caught)
}

// Run tests with: cxy test path/to/file.cxy
// Run specific test: cxy test path/to/file.cxy --filter "vector operations"

// Notes:
// - test imports only available during test runs
// - test {} blocks not compiled into production code
// - ok! macro asserts condition is true
// - Tests run in declaration order
// - Each test runs in isolation
```

## Macros

```cxy
// Simple value macro
macro DEVICE_ID = 0x10
println(DEVICE_ID!)  // Access with !

// Simple macro without =
macro MAX_SIZE 1024
var buffer: [u8; MAX_SIZE!]

// Macro with arguments
macro ADD(X) X! + 20
println(ADD!(10))  // prints 30

// Multiple arguments
macro MUL(A, B) A! * B!
var result = MUL!(5, 3)  // 15

// Block macro (multiple statements)
macro BLOCK(ID) {
    println("This is my BLOCK ", ID!)
    println("Welcome")
}
BLOCK!(42)

// Expression block macro (returns a value)
macro EXPR_BLOCK(A, B) =({
    println(A! + B!)
    A! + B!  // returned value
})
var sum = EXPR_BLOCK!(10, 20)  // prints 30, returns 30

// Macro arguments accessed with <name>!
macro REPEAT(N, STMT) {
    for i in 0..N! {
        STMT!
    }
}

// Variadic macro arguments
macro MANY(...ARGS) println(ARGS!)
MANY!(1, 2, 3, "hello")

macro LOG_ALL(...VALUES) {
    println("Logging: ", VALUES!)
}
LOG_ALL!("error", 404, "not found")
```

## Generics

```cxy
// Generic functions
func say[T](value: T) {
    println(value)
}

say[i32](10)       // explicit type
say(10)            // T inferred from parameter

// Multiple generic parameters
func process[K, V](key: K, value: V) {
    println("Key: ", key, ", Value: ", value)
}

// Default generic types (only on last arguments)
func say[N, T = i32](name: N, age: i8): T {
    println(name, " is ", age, " years old")
    return 0 as T
}

say("John", 8)                  // N inferred, T defaulted to i32
say[String]("John", 8)          // N explicit, T defaulted
say[String, i64]("John", 8)     // Both explicit

// Generic functions with variadic parameters (NOT variadic generics)
func say[T](age: T, ...names: auto) {
    println("Age: ", age)
    for name, _ in names {
        println("Name: ", name)
    }
}
say(25, "Alice", "Bob", "Charlie")

// ❌ Variadic generic arguments NOT supported on functions
func say[...Names]() { }  // ❌ not allowed

// Generic classes
pub class Container[T] {
    value: T
    
    func `init`(value: T) {
        this.value = &&value
    }
    
    func getValue(): T {
        return this.value
    }
}

var intContainer = Container[i32](42)
var strContainer = Container[String]("hello".S)

// Generic structs
pub struct Pair[K, V] {
    key: K
    value: V
}

var pair = Pair[String, i32]("age".S, 25)

// Variadic generic arguments (classes/structs only)
pub struct Printer[...Types] {
    values: Types  // tuple of all Types
}

var printer = Printer[i32, String, f64]()
// printer.values is a tuple: (i32, String, f64)

pub class Storage[...Items] {
    data: Items
    
    func `init`(data: Items) {
        this.data = &&data
    }
}

// Generic constraints and bounds
// Note: Check current syntax for constraints if supported
```

## Compile-Time Evaluation (Comptime)

```cxy
// Comptime declarations prefixed with #
#const num = 1
#const T = typeof!(true)

// Comptime expressions enclosed in #{ }
var x = #{num + 10}  // evaluates at compile time

// Access comptime variables
#const count = 5
#{count = count + 1}  // update at comptime

// NEVER use
#var x = 10  // ❌ must use 'const', not 'var'
```

### Comptime If

```cxy
// Conditional compilation based on comptime values
#const T = #i32
#if (T == #i32) {
    info!("type is i32")  // this block is compiled
}
else {
    info!("type is not i32")  // this block is discarded
}

// Chained comptime conditionals
#if (sizeof!(T) == 2) {
    info!("Type is 2 bytes")
}
else #if (sizeof!(T) == 4) {
    info!("Type is 4 bytes")
}
else {
    info!("Type is {u64} bytes", sizeof!(T))
}

// Note: condition must evaluate to boolean literal at compile time
```

### Comptime For (Loop Unrolling)

```cxy
// Unroll loops at compile time
#for(const i: 0..3) {
    println(#{i})
}
// Expands to:
// println(0)
// println(1)
// println(2)

// Iterate over tuple members
#const T = #(bool, i32, string)
#for(const i: 0..T.membersCount) {
    #const M = typeat!(T, i)
    info!("member at {u64} is {t}", i, #M)
}

// For loops can be nested
#for(const i: 0..3) {
    #for(const j: 0..2) {
        println(#{i}, ", ", #{j})
    }
}
```

### Tuple Type Transformations

```cxy
// Transform tuple types with: T as M,i => transform(T, i), condition?
#const T = #(bool, string, String)

// Transform to references
#const Refs = #`T as M,i => &M`
// Result: #(&bool, &string, &String)

// Filter string types
#const Strs = #`T as M,i => M, M.isString`
// Result: #(string, String)

// Reverse tuple
#const Reversed = #`T as M,i => typeat!(T, T.membersCount - i - 1)`
// Result: #(String, string, bool)
```

### Builtin Macros

```cxy
// Type introspection
sizeof!(i32)           // size of type
typeof!(variable)      // get type of expression
typeat!(T, 0)         // get type at index in tuple

// Runtime assertions
assert!(condition)     // runtime assertion with location info

// AST building
#const list = mk_ast_list!()
ast_list_add!(list, true)
ast_list_add!(list, "Hello")
println(#{list})  // same as println(true, "Hello")

// Create identifiers and strings
mk_ident!("prefix", "_", "suffix")  // creates identifier
mk_str!("Hello ", name, ", age: ", age)  // concatenate to string
mk_integer!(42)  // create integer literal

// Struct/tuple building
mk_field!("name", #String, "default")  // struct field
mk_field_expr!(:name, "value")         // field expression
mk_struct_expr!(fields)                // struct expression
mk_tuple_expr!(members)                // tuple expression

// Type queries
has_member!(#Cat, "sound", #func() -> string)  // check for member
has_type!(#Cat, :Id)                            // check for type alias
is_base_of!(#Base, #Derived)                   // check inheritance
indexof!(#(i32, string), #string)              // get index in tuple
base_of!(#Derived)                             // get base type
lambda_of!(#func(i32) -> i32)                  // get lambda type

// String operations
cstr!("hello")         // convert to C-style string (^const char)
len!("hello")          // length of string/array/collection

// Diagnostics
error!("Type {t} not supported", #T)   // compilation error
warn!("Deprecated: {s}", "old_func")   // compilation warning
info!("Compiling {t}", #T)             // compilation info message

// Source location
file!                  // current file path
line!                  // current line number
column!                // current column number

// Exception handling
doSomething() catch {
    println(ex!)       // ex! is the caught exception
}
```

### AST Properties

```cxy
// Access AST node properties at compile time
struct User {
    name: String
    age: i32
    `id = 42          // annotation
}

// Type information
#const members = User.members        // list of struct members
#const count = User.membersCount     // number of members
#const attrs = User.attributes       // attributes list
#const annots = User.annotations     // annotations list

// Iterate over members
#for(const member: #{User.members}) {
    #const M = member.Tinfo          // get member type info
    #const name = member.name        // get member name
}

// Type checking properties
.isInteger      // true if integer type
.isSigned       // true if signed integer
.isUnsigned     // true if unsigned integer
.isFloat        // true if float type
.isNumber       // true if integer or float
.isBoolean      // true if boolean type
.isChar         // true if char or wchar
.isPointer      // true if pointer type
.isReference    // true if reference type
.isOptional     // true if optional type
.isStruct       // true if struct type
.isClass        // true if class type
.isTuple        // true if tuple type
.isUnion        // true if union type
.isArray        // true if array type
.isSlice        // true if slice type
.isEnum         // true if enum type
.isVoid         // true if void type
.isFunction     // true if function type
.isClosure      // true if closure type
.isResultType   // true if result type
.isString       // true if string type

// Structural properties
.name           // name of entity
.value          // value of annotation
.elementType    // element type of array
.pointedType    // pointed type of pointer
.strippedType   // stripped type (pointed or referred)
.targetType     // target type for optional/result
.returnType     // return type of function
.baseType       // base type of class
.params         // list of parameters
.callable       // callable component of lambda

// State properties
.hasBase        // true if class has base type
.hasDeinit      // true if has deinitializer
.hasVoidReturnType      // true if function returns void
.hasReferenceMembers    // true if has reference members
.isDestructible         // true if has destructor

// Index operator for comptime collections
#const json = T.attributes.["json"]      // lookup by name
#const id = attr.[0]                      // lookup by index
#const name = attr.["name"]               // lookup named param
```

## Common Patterns

```cxy
// Move ownership
return &&value

// Pass by reference
func process(item: &Item)

// String formatting
f"Hello {name}"     // interpolation works
f"""multi
line"""             // ❌ may not work, use << concatenation

// Build SQL with ORDER BY
var sql = "SELECT * FROM t ORDER BY ".S
sql << orderByClause  // can't parameterize ORDER BY
sql << " LIMIT ?"
db.exec(sql.str(), limit)

// Type checking with 'is' operator
if obj is MyClass {
    // obj is of type MyClass
}

var value: i32 | String = getValue()
if value is i32 {
    // value is i32 type
}

// Delete operator (for manual memory management - rarely needed)
var obj = new MyClass()
delete obj  // manually free memory

// Spread operator
func sum(...nums: i32): i32 {
    var total = 0
    for num, _ in nums {
        total += num
    }
    return total
}

var arr = [1, 2, 3]
sum(...arr)  // spread array as arguments

// Inline assembly (advanced)
asm {
    "mov rax, 1"
    "ret"
}
```

## Code Formatting for Markdown

When showing code in markdown responses, use path-based syntax:

```
```path/to/file.cxy#L10-20
code here
```

```

NEVER use language tags like ```cxy or bare ``` blocks.
