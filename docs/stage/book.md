# The Cxy Programming Language

Cxy is a modern compiled programming language that combines the performance and control of systems programming languages with high-level features and safety guarantees.

## Getting Started

### Installation

First, ensure you have the necessary build tools:

- CMake (version 3.10 or higher)
- A C compiler (GCC or Clang)
- Make

Clone and build the Cxy compiler:

```bash
git clone https://github.com/example/cxy.git
cd cxy
cmake -B build
make -C build -j$(nproc)
```

### Hello World

Create a file called `hello.cxy`:

```cxy
func main() {
    println("Hello, World!")
}
```

Compile and run:

```bash
cxy hello.cxy
./hello
```

## Basic Syntax

### Variables

Variables are declared with `var` for mutable variables or `const` for constants:

```cxy
func main() {
    var age = 25          // Mutable variable
    const name = "Alice"  // Constant
    var height: f64 = 5.8 // Explicit type annotation
    
    age = 26  // OK, age is mutable
    // name = "Bob"  // Error: name is constant
}
```

### Basic Types

Cxy provides several built-in types:

```cxy
func main() {
    // Integer types
    var small: i8 = 127
    var medium: i32 = 100000
    var large: i64 = 9223372036854775807
    
    // Unsigned integers
    var byte: u8 = 255
    var word: u16 = 65535
    var dword: u32 = 4294967295
    var qword: u64 = 18446744073709551615
    
    // Floating point
    var pi: f32 = 3.14159
    var precise: f64 = 3.141592653589793
    
    // Boolean
    var isTrue: bool = true
    var isFalse: bool = false
    
    // Character
    var letter: char = 'A'
    
    // String
    var message: string = "Hello"
}
```

### Arrays and Slices

Arrays have fixed sizes, while slices are dynamic views:

```cxy
func main() {
    // Fixed-size array
    var numbers: [i32, 5] = [1, 2, 3, 4, 5]
    var matrix: [i32, 3, 3] = [[1, 2, 3], [4, 5, 6], [7, 8, 9]]
    
    // Array literals (size inferred)
    var fruits = ["apple", "banana", "cherry"]
    
    // Accessing elements
    println("First number: ", numbers[0])
    println("Matrix element: ", matrix[1][2])  // Element at row 1, col 2
    
    // Array length
    println("Array length: ", numbers.len)
}
```

### String Operations

Cxy provides multiple string types for different use cases:

```cxy
func main() {
    var str1: string = "Hello"        // Basic string literal
    var str2 = String("World")        // String object
    var cstr: __string = "Native"     // Native string type
    
    // String concatenation
    var greeting = str1 + " " + "World"
    
    // String methods
    println("Length: ", str2.length())
    println("Uppercase: ", str2.upper())
    println("Contains: ", str2.contains("rl"))
}
```

## Functions

### Basic Functions

Functions are defined with the `func` keyword:

```cxy
func add(a: i32, b: i32): i32 {
    return a + b
}

func greet(name: string) {
    println("Hello, ", name, "!")
}

func main() {
    var result = add(5, 3)
    println("5 + 3 = ", result)
    greet("Alice")
}
```

### Function Overloading

Functions can be overloaded based on parameter types:

```cxy
func process(value: i32) {
    println("Processing integer: ", value)
}

func process(value: string) {
    println("Processing string: ", value)
}

func main() {
    process(42)        // Calls the integer version
    process("hello")   // Calls the string version
}
```

### Default Parameters

Functions can have default parameter values:

```cxy
func createUser(name: string, age: i32 = 18, active: bool = true) {
    println("User: ", name, ", Age: ", age, ", Active: ", active)
}

func main() {
    createUser("Alice")           // Uses defaults for age and active
    createUser("Bob", 25)         // Uses default for active
    createUser("Charlie", 30, false)  // All parameters specified
}
```

### External Functions

You can declare external functions using `extern`:

```cxy
extern func printf(format: string, ...): i32

func main() {
    printf("Hello from C printf: %d\n", 42)
}
```

## Control Flow

### If Statements

If statements provide flexible conditional logic:

- **Optional parentheses**: Parentheses around the condition are optional, but when omitted, the body must be enclosed in braces and the condition expression cannot contain struct expressions
- **Variable conditions**: Conditions can be variables that are implicitly checked for truthiness
- **Variable declarations**: Conditions can declare new variables with `const` or `var`

#### Basic If Statement

```cxy
func main() {
    var age = 18
    
    if (age >= 18) {
        println("You are an adult")
    } else if (age >= 13) {
        println("You are a teenager")
    } else {
        println("You are a child")
    }
}
```

#### Optional Parentheses

```cxy
func check_age() {
    var age = 20
    
    // With parentheses (recommended)
    if (age >= 18) {
        println("You are an adult")
    }

    // Without parentheses - body must be in braces
    if age >= 18 {
        println("You are an adult")
    }
}
```

#### Struct Expressions in Conditions

```cxy
func check_point() {
    // This would be invalid without parentheses:
    // if Point{x: 1, y: 2}.x > 0 {  // Error: struct expression
    //     println("Valid point")
    // }

    // Use parentheses for struct expressions:
    if (Point{x: 1, y: 2}.x > 0) {
        println("Valid point")
    }
}
```

#### Variable Declaration in Condition

```cxy
func process_user() {
    if const user = get_current_user() {
        println("Hello, " + user.name)
        // 'user' is available in this scope
    } else {
        println("No user logged in")
    }
    // 'user' is not available here
}
```

### Switch Statements

Switch statements provide multi-way branching with flexible syntax:

- **Optional parentheses**: Parentheses around the switch expression are optional, but when omitted, struct expressions cannot be used directly in the switch expression
- **Optional case keyword**: The `case` keyword is optional (may be deprecated in the future)
- **Default cases**: The `default` keyword is the same as `...` (`default` may be deprecated soon)
- **Multiple cases**: Multiple cases can be separated by commas
- **Fall-through**: If a case has no body, it falls through to the next case

#### Basic Switch Statement

```cxy
func describeDay(day: i32): string {
    switch (day) {
        case 1 => return "Monday"
        case 2 => return "Tuesday"
        case 3 => return "Wednesday"
        case 4 => return "Thursday"
        case 5 => return "Friday"
        case 6, 7 => return "Weekend"
        default => return "Invalid day"
    }
}
```

#### Advanced Switch Features

```cxy
func main() {
    println(describeDay(1))  // Monday
    println(describeDay(6))  // Weekend
    
    // Optional parentheses and various case types
    switch get() {
        0..10 => println("Valid range")
        16, 17 => println("Unsupported numbers")
        21 => // falls through
        22 => println("Special numbers")
        89 => {} // empty block, does not fall through
        ... => println("Invalid range") // default case
    }
}
```

### For Loops

For loops support multiple flexible syntax forms:

- **Optional parentheses**: Parentheses around the loop expression are optional, but when omitted, the body must be enclosed in braces and the loop expression cannot contain struct expressions
- **Optional keywords**: The `const` and `var` keywords are optional in loop variable declarations
- **Alternative syntax**: `in` keyword can be used in place of the `:` token
- **Value ignoring**: `_` can be used to ignore values when destructuring variables in the header

#### Basic For Loops

```cxy
func main() {
    // Range-based for loop
    for (const i: 0..10) {
        println("i = ", i)
    }
    
    // Array iteration
    var numbers = [1, 2, 3, 4, 5]
    for (const num: numbers) {
        println(num)
    }
    
    // Index and value iteration
    for (const value, i: numbers) {
        println(f"numbers[{i}] = {value}")
    }
}
```

#### Alternative Syntax

```cxy
func demo_syntax() {
    var numbers = [1, 2, 3, 4, 5]
    
    // Using 'in' keyword instead of ':'
    for (const i in 0..10) {
        println("i = ", i)
    }
    
    // Optional keywords and parentheses
    for i, value in numbers {
        println(f"numbers[{i}] = {value}")
    }
    
    // Ignoring values with underscore
    for (_, value: numbers) {
        println("Value: " + value.str())
    }
}
```

#### Struct Expression Limitations

```cxy
func struct_examples() {
    // Valid: struct expression with parentheses
    for (x: Numbers{}.items) {
        println(x)
    }
    
    // Error: Cannot use struct expression when parentheses are omitted
    // for x in Numbers{} {
    //     println(x)
    // }
}
```

### While Loops

While loops provide several flexible features:

- **Infinite loops**: `while` without a condition creates an infinite loop
- **Optional parentheses**: Parentheses around the condition are optional, but when omitted, struct expressions cannot be used in the condition and the body must be a block
- **Variable conditions**: Conditions can be variables that are implicitly checked for truthiness
- **Variable declarations**: Conditions can declare new variables with `const` or `var`

#### Basic While Loop

```cxy
func main() {
    var count = 0
    while (count < 5) {
        println("Count: " + count.str())
        count = count + 1
    }
}
```

#### Infinite Loop

```cxy
func server_loop() {
    while {
        // Process requests forever
        handle_request()
    }
}
```

#### Optional Parentheses

```cxy
func countdown() {
    var i = 10
    while i > 0 {  // No parentheses needed
        println(i.str())
        i = i - 1
    }
}
```

#### Variable as Condition

```cxy
func process_items(items: []string) {
    var item = items.next()
    while item {  // Variable checked for truthiness
        process(item)
        item = items.next()
    }
}
```

#### Variable Declaration in Condition

```cxy
func process_data() {
    while const data = get_next_data() {  // Declare and check in one step
        process(data)
        // 'data' is available in the loop body
    }
    // 'data' is not available here
}
```

### Match Statements

Match statements provide powerful pattern matching with flexible syntax:

- **Optional parentheses**: Parentheses around the match expression are optional
- **Optional case keyword**: The `case` keyword is optional (might be deprecated in the future)
- **Default cases**: The `else` keyword for default match can be replaced with `...`
- **Multiple type matching**: Matching multiple types is supported but `as` binding cannot be used
- **Fall-through**: Fall-through matches are supported but `as` binding cannot be used

#### Basic Match Statement

```cxy
func process_value(value: i32|string|bool) {
    match (value) {
        case i32 as num => {
            println("Number: " + num.str())
        }
        case string as text => {
            println("Text: " + text)
        }
        case bool as flag => {
            println("Boolean: " + flag.str())
        }
    }
}
```

#### Optional Syntax Features

```cxy
func flexible_matching(value: i32|string|bool) {
    // Optional parentheses and case keyword
    match value {
        i32 as num => println("Number: " + num.str())
        string as text => println("Text: " + text)
        else => println("Other type")
    }
    
    // Using ... for default case
    match (value) {
        case i32 as num => println("Number: " + num.str())
        case string as text => println("Text: " + text)
        ... => println("Default case")
    }
}
```

#### Multiple Type and Fall-through Matching

```cxy
func advanced_matching(value: i32|string|bool|f64) {
    match (value) {
        // Multiple types (no 'as' binding allowed)
        case i32|f64 => println("Numeric type")
        case string => println("Text type")
        case bool => // Fall-through (no 'as' binding allowed)
        default => println("Boolean or other type")
    }
}
```

## Structs

Structs group related data together with flexible initialization and visibility:

- **Field visibility**: Fields can be public (default) or private (prefixed with `-`)
- **Default values**: Fields can have default values using `=` assignment
- **Initialization**: Structs are initialized using `{}` braces with field assignments
- **Methods**: Structs can have member functions and operators

#### Basic Struct Definition

```cxy
struct Point {
    x: f64
    y: f64
}

struct Person {
    name: String
    age: i32
    - isActive: bool = true  // Private field with default value
}

func main() {
    var p1 = Point{x: 3.0, y: 4.0}
    var person = Person{
        name: String("Alice"),
        age: 30
        // isActive uses default value
    }
    
    println("Point: (", p1.x, ", ", p1.y, ")")
    println("Person: ", person.name.str(), ", ", person.age)
}
```

#### Default Values and Private Fields

```cxy
pub struct Config {
    host: String = String("localhost")
    port: i32 = 8080
    - debug: bool = false  // Private field
    - maxConnections: i32 = 100
}

func main() {
    var config1 = Config{}  // Uses all defaults
    var config2 = Config{port: 3000}  // Override port only
    var config3 = Config{
        host: String("example.com"),
        port: 443
    }
}
```

#### Struct Methods

```cxy
struct Rectangle {
    width: f64
    height: f64
    
    func area() {
        return width * height
    }
    
    func `str`(os: &OutputStream) {
        os << "Rectangle(" << width << "x" << height << ")"
    }
    
    func scale(factor: f64) {
        width = width * factor
        height = height * factor
    }
}

func main() {
    var rect = Rectangle{width: 10.0, height: 5.0}
    println("Area: ", rect.area())
    rect.scale(2.0)
    println("After scaling: ", rect.str())
}
```

#### Advanced Struct Features

```cxy
struct FileUpload {
    - _name: String         // Private field
    - _contentType: __string
    - _size: u64 = 0
    
    @inline
    func `init`(name: String, contentType: __string) {
        _name = &&name
        _contentType = contentType
    }
    
    const func name() => _name.str()
    const func size() => _size
    
    func setSize(size: u64) {
        _size = size
    }
}

func main() {
    var upload = FileUpload{
        _name: String("document.pdf"),
        _contentType: "application/pdf".s
    }
    upload.setSize(1024)
    println("File: ", upload.name(), " Size: ", upload.size())
}
```

#### Operator Overloading

```cxy
pub struct Address {
    - addr: [i8, 32]
    
    func `init`() {
        // Initialize with default values
    }
    
    func `init`(ip: string, port: u16) {
        // Custom initialization
    }
    
    @inline
    const func `!!`() => addr.[0] != 0
    
    @inline
    const func `==`(other: &const This) => memcmp(addr, other.addr, 32) == 0
    
    @inline
    const func `!=`(other: &const This) => !(this == other)
    
    @inline
    const func `hash`() => hashBytes(addr, 32)
    
    const func `str`(os: &OutputStream) {
        os << "Address(" << port() << ")"
    }
    
    const func port() {
        // Extract port from addr
        return 8080  // simplified
    }
}
```

## Enums

Enums define named constants with optional backing types:

```cxy
pub enum Method : u32 {
    @str("DELETE")
    Delete,
    @str("GET")
    Get,
    @str("HEAD")
    Head,
    @str("POST")
    Post,
    @str("PUT")
    Put,
    @str("CONNECT")
    Connect,
    @str("OPTIONS")
    Options,
    @str("TRACE")
    Trace,
    Unknown
}

pub enum Status : i32 {
    Continue = 100,
    OK = 200,
    NotFound = 404,
    InternalError = 500
}

func main() {
    var method = Method.Get
    var status = Status.OK
    
    println("Method: ", method as u32)
    println("Status: ", status as i32)
}
```

## Type Aliases and Unions

Type aliases create alternative names for existing types:

```cxy
pub type PathLike = string | __string | String | Path
pub type time_t = i64
type sockaddr = socket.sockaddr

// Union types with pattern matching
func processValue(value: PathLike) {
    match (value) {
        case string as s => println("String: ", s)
        case __string as s => println("Native string: ", s.str())
        case String as s => println("String object: ", s.str())
        case Path as p => println("Path: ", p.str())
    }
}
```

## Modules and Imports

Modules organize code into reusable units with explicit imports:

```cxy
// In stdlib/path.cxy
module path

import { Stat, stat, fstat, lstat, getenv, IOError } from "stdlib/os.cxy"
import "unistd.h" as unistd
import "stdlib.h" as stdlib

pub struct Path {
    - path: String = null
    
    @inline
    func `init`() {
    }
    
    @inline
    func `init`(s: string) {
        this.path = String(s)
    }
    
    const func exists() {
        return unistd.access(path.str() !: ^const char, F_OK!) == 0`i32
    }
}

pub func cwd(): Path {
    var buffer: [char, 1024] = []
    var str = unistd.getcwd(buffer !: ^char, sizeof!(buffer))
    if (str != null)
        return Path(str !: string)
    return Path()
}
```

### Import Syntax

Different import patterns for different needs:

```cxy
// Import specific symbols
import { Time } from "./time.cxy"
import { Address, BufferedSocketOutputStream } from "./net.cxy"

// Import C headers with aliases
import "unistd.h" as unistd
import "time.h" as ctime

// Import entire modules
import "./log.cxy"

// Conditional imports
##if (defined MACOS) {
    import "netinet6/in6.h" as inet6
}

func main() {
    var currentTime = Time()
    var addr = Address("localhost", 8080)
}
```

## Generics and Templates

Generics enable type-safe code reuse with compile-time type checking:

```cxy
// Generic function with type parameters
pub func cast[T](path: &PathLike): T {
    #if (#T == #__string) {
        match (path) {
            case string as s => return __string(s)
            case __string as s => return __copy!(s)
            case String as s => return s.__str()
            case Path as &p => return p.__str()
        }
    }
    else #if (#T == #String) {
        var str = String()
        str << path
        return str
    }
    else #if (#T == #Path) {
        match (path) {
            case string as s => return <T> Path(s)
            case __string as s => return <T> Path(__copy!(s))
            case String as s => return <T> Path(__copy!(s))
            case Path as p => return __copy!(p)
        }
    }
}

// Generic type with constraints
type HashMap[K, V, Hash = DefaultHash, Equals = DefaultEquals] = struct {
    - buckets: [Bucket[K, V], N]
    - size: u64
    
    func get(key: &K): V? {
        // Implementation using Hash and Equals
    }
}

// Compile-time type checks
func withNullTermination[T](s: &const __string, fn: func(p: ^const char) -> T) {
    if (s.isnt()) {
        var p: [char, 1024] = []
        s.copyto(p, sizeof!(p))
        return fn(p !: ^const char)
    }
    else {
        return fn(s.data())
    }
}
```

## Memory Management and Pointers

Cxy provides explicit memory management with safety features:

### Pointers and References

```cxy
func processData() {
    var value = 42
    var ptr = ptrof value        // Get pointer to value
    var ref = &value             // Get reference to value
    
    println("Value: ", *ptr)     // Dereference pointer
    *ptr = 100                   // Modify through pointer
    println("New value: ", value)
}

// Working with C pointers
func cInterop() {
    var buffer: [char, 1024] = []
    var cPtr = buffer !: ^char   // Cast to C pointer
    var status = unistd.getcwd(cPtr, sizeof!(buffer))
}
```

### RAII and Automatic Cleanup

```cxy
struct FileHandle {
    - fd: i32 = -1
    
    func `init`(filename: string) {
        fd = open(filename)
    }
    
    func `deinit`() {
        if (fd != -1) {
            close(fd)
        }
    }
    
    func read(buffer: ^void, size: u64) {
        // Use fd for operations
    }
}

func main() {
    var file = FileHandle("test.txt")
    // file.`deinit`() called automatically when file goes out of scope
}
```

### Defer for Resource Cleanup

```cxy
func processFile(filename: string) {
    var fd = open(filename)
    defer close(fd)  // Ensure cleanup happens
    
    var d = opendir(dirname)
    defer closedir(d)
    
    // Process file and directory
    // Cleanup happens automatically in reverse order
}
```

## Error Handling

Cxy uses exceptions and optional types for error handling:

### Exceptions

```cxy
pub exception HttpError(msg: String) => msg == null? "" : msg.str()
pub exception IOError(msg: String) => msg.str()

func mkdir(path: PathLike, mode: u16 = 0o777): !void {
    var spath = cast[__string](&path)
    const status = withNullTermination[i32](
        &spath, (p: ^const char) => sysstat.mkdir(p, mode))
    if (status != 0)
        raise IOError(f"creating directory {spath} failed: {strerr()}")
}

func main() {
    mkdir("/tmp/test") catch |err| {
        println("Failed to create directory: ", err)
    }
}
```

### Optional Types and Pattern Matching

```cxy
func getFileSize(path: PathLike): !u64 {
    var spath = cast[__string](&path)
    var result = withNullTermination[bool|u64](&spath, (p: ^const char): bool|u64 => {
        var s = Stat{}
        if (stat(p !: string, ptrof s) != 0) {
            return false
        }
        return <u64>s.st_size
    })

    if (result is #bool)
        raise IOError(f"Getting file '${path}' size failed: ${strerr()}")
    return result as u64
}

func processFile(path: string) {
    var size = getFileSize(path) catch return
    println("File size: ", size)
}
```

## Attributes and Annotations

Attributes provide metadata and compiler directives:

### Function Attributes

```cxy
@inline
func fastOperation() {
    // Function will be inlined
}

@poco  // Plain Old C Object
pub struct OptionDesc {
    name: __string
    desc: __string
}

@json  // Enable JSON serialization
pub struct Config {
    - address = Address("0.0.0.0", 8100)
    - serverName = String("cxy")
    - maxConnections = 1000`u64
}

@align(16)
struct AlignedStruct {
    data: [u8, 64]
}

@__cc "native/dns/dns.c"  // Link C source file
```

### Conditional Compilation

```cxy
#if (defined UNIX) {
    macro DIR_SEPARATOR "/"
}
else {
    macro DIR_SEPARATOR "\\"
}

##if (defined MACOS) {
    import "netinet6/in6.h" as inet6
    type sockaddr_in6 = inet6.sockaddr_in6
}
else {
    type sockaddr_in6 = inet.sockaddr_in6
}

// Compile-time type checking
func byteSwap(x: u16) : u16 {
    #if (BYTE_ORDER! == BIG_ENDIAN!) {
        return x
    }
    else #if (BYTE_ORDER! == LITTLE_ENDIAN!) {
        return __bswap16(x)
    }
    else {
        error!("Unknown byte order")
    }
}
```

### Macros and Constants

```cxy
macro HTTP_TIME_FMT "%a, %d %b %Y %T GMT"
macro LOG_TIME_FMT  "%Y-%m-%d %H:%M:%S"
macro INADDR_ANY 0x00000000`u32

// Compile-time checks
func processType[T](value: T) {
    #if (T.isString) {
        println("Processing string: ", value)
    }
    else #if (T.isSlice) {
        println("Processing slice with ", value.count, " elements")
    }
    else {
        error!("Unsupported type")
    }
}
```

## Advanced Features

### Compile-time Programming

Cxy provides powerful compile-time programming capabilities through comptime expressions and builtin macros:

#### Comptime Variables and Control Flow

```cxy
func buildOptions[T](opts: &T) {
    #const hasCommand = false
    #for (const member: T.members) {
        #const M = typeof!(#{member})
        #if (#M == #Param) {
            require!(
                !#{hasCommand}, 
                "all global arguments must be added before commands"
            )
            addParam(&& #{member})
        }
        else #if (is_base_of!(#Command, #M)) {
            #{hasCommand = true}
            addCommand(&& #{member})
        }
    }
}
```

#### Type Reflection and Transformation

```cxy
func bindOption[T](opts: &T, name: __string, value: __string): !void {
    #for (const M: T.members) {
        #if (M.isField) {
            if (name == #{M.name}) {
                opts.#{mk_ident!(M.name)}.set(value)
                return
            }
        }
    }
    raise Error("Unknown option: " + name.str())
}

// Tuple type transformations
#const T = #(bool, string, String)
#const Refs = #`T as M,i => &M`  // Transform to reference types
#const Strs = #`T as M,i => M, M.isString`  // Filter string types
```

#### Builtin Macros for Metaprogramming

```cxy
func dynamicStruct() {
    #const list = mk_ast_list!()
    ast_list_add!(list, mk_field_expr!(:name, "Alice"))
    ast_list_add!(list, mk_field_expr!(:age, 30))
    
    var person = mk_struct_expr!(list)
    // Equivalent to: var person = {name: "Alice", age: 30}
    
    #const typeName = mk_str!("User", "Type")
    info!("Creating type: {s}", typeName)
}
```

### Inline Assembly

Cxy supports inline assembly for low-level programming:

```cxy
macro __cxy_coro_setjmp(ctx) =({
    @volatile var ret: i32 = 0
    @volatile asm("""
        lea LJMPRET$=(%rip), %rcx
        xor $0, $0
        movq %rbx,   ($1)
        movq %rbp,  8($1)
        movq %r12, 16($1)
        movq %r13, 24($1)
        movq %r14, 32($1)
        movq %r15, 40($1)
        movq %rsp, 48($1)
        movq %rcx, 56($1)
        LJMPRET$=:
        """ :
        "=&r"(ret) :
        "r" (ctx!) :
        "memory", "rcx", "rsi", "rdi", "r8", "r9", "r10", "r11", "cc"
    )
    ret
})

macro __cxy_coro_setsp(x) {
    @volatile asm(""::"r"(alloca!(#u64)))
    @volatile asm("leaq (%rax), %rsp" :: "rax"(x!))
}
```

### Async/Await and Coroutines

Cxy provides first-class support for asynchronous programming:

```cxy
pub async func getRemoteAddress(name: string, port: u16, mode: IPVersion = IPVersion.Any) {
    var addr = Address(name, port, mode)
    if (addr)
       return addr

    // Asynchronous DNS lookup
    var resolver = dns.dns_res_open(cxy_DNS_conf, cxy_DNS_hosts, cxy_DNS_hints, null, null, ptrof rc)
    
    while {
        rc = dns.dns_ai_nextent(ptrof it, ai)
        if (rc == EAGAIN!) {
            var fd = dns.dns_ai_pollfd(ai)
            assert!(fd >= 0)
            fdWaitRead(fd)  // Yield to other coroutines
            continue
        }
        break
    }
    
    return addr
}

// Coroutine synchronization
pub struct Mutex {
    waiting = List[Coroutine]{}
    holder: ^Coroutine = null

    func lock() {
        if (holder == null) {
            holder = running()
            return
        }
        waiting.push(running())
        suspend()
    }

    func unlock() {
        var next = waiting.pop()
        if (next == null) {
            return
        }
        holder = next
        resume(next, 0)
    }
}
```

### Testing

Built-in testing support for unit tests:

```cxy
test {
    struct Item {
        link: ^This = null
        num: i32
    }
    
    var item1 = Item{num: 42}
    var item2 = Item{num: 24}
    
    assert!(item1.num == 42)
    assert!(item2.num == 24)
    
    // Test list operations
    var list = List[Item]{}
    list.push(&item1)
    list.push(&item2)
    
    assert!(list.count == 2)
    assert!(list.front().num == 42)
}

func main() {
    // Tests run automatically in debug builds
    println("All tests passed!")
}
```

### Foreign Function Interface

Cxy provides seamless C interop through header imports and external linkage:

```cxy
// Import C headers directly
import "unistd.h" as unistd
import "sys/socket.h" as socket
import "netinet/in.h" as inet

// Link external C source files
@__cc "native/evloop/ae.c"
@__cc "native/dns/dns.c"

// External functions are imported automatically
func networkOperation() {
    var fd = socket.socket(AF_INET!, SOCK_STREAM!, 0)
    defer unistd.close(fd)
    
    var addr = inet.sockaddr_in{}
    addr.sin_family = AF_INET! as u8
    addr.sin_port = htons(8080)
    
    var result = socket.connect(fd, &addr !: ^socket.sockaddr, sizeof!(addr))
}

// Thread-local and external linkage
@[thread, linkage("External")]
var global_scheduler: CoroutineScheduler = null
```

This documentation covers the comprehensive features of the Cxy programming language, from basic syntax to advanced metaprogramming capabilities. The language combines systems programming control with high-level abstractions, making it suitable for performance-critical applications while maintaining developer productivity.