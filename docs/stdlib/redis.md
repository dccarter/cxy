# Redis Client

The Cxy standard library provides a full-featured Redis client through `stdlib/redis.cxy`. This implementation supports connection pooling, automatic keep-alive management, transactions, and a comprehensive set of Redis commands.

## Overview

The Redis client provides:

- **Connection pooling** - Automatic connection reuse with configurable keep-alive
- **Async operations** - Non-blocking I/O using Cxy's coroutine system
- **Transaction support** - MULTI/EXEC/DISCARD for atomic operations
- **Type-safe responses** - Generic methods for type-safe value retrieval
- **Retry logic** - Configurable retry attempts with exponential backoff
- **RESP protocol** - Full Redis Serialization Protocol (RESP) implementation
- **Comprehensive commands** - Support for strings, lists, sets, hashes, and more
- **Error handling** - Clear exceptions with detailed error messages

## Getting Started

### Basic Usage

Connect to Redis and perform simple operations:

```cxy
import { RedisDb } from "stdlib/redis.cxy"

func main(): !void {
    // Create a database instance with default configuration
    var db = RedisDb()
    
    // Get a connection from the pool
    var conn = db.connection()
    
    // Perform operations
    conn&.set("user:1:name", "Alice")
    var name = conn&.get("user:1:name")
    println("User name: ", name)
    
    // Connection automatically returns to pool when scope ends
}
```

### Custom Configuration

Configure Redis connection settings:

```cxy
import { RedisDb, RedisConfig } from "stdlib/redis.cxy"

func main(): !void {
    var db = RedisDb()
    
    // Configure the database connection
    db.setup({
        host: "redis.example.com".S,
        port: 6380 as u16,
        password: "secret".S,
        database: 2 as i32,
        timeout: 3000 as i64,      // 3 seconds
        keepAlive: 60000 as i64,   // 60 seconds
        maxRetries: 5 as i32,
        retryDelay: 200 as i64     // 200ms between retries
    })
    
    var conn = db.connection()
    conn&.ping()  // Test connection
}
```

## Configuration

### RedisConfig Structure

All configuration options with their defaults:

- **`host`** (default: `"127.0.0.1"`) - Redis server hostname or IP address
- **`port`** (default: `6379`) - Redis server port number
- **`password`** (default: `null`) - Authentication password, null if no auth required
- **`database`** (default: `0`) - Database number to select (0-15 typically)
- **`timeout`** (default: `5000`) - Network timeout in milliseconds for send/receive operations
- **`keepAlive`** (default: `0`) - Connection keep-alive duration in milliseconds. Set to 0 to disable pooling
- **`maxRetries`** (default: `3`) - Maximum number of retry attempts on connection failures
- **`retryDelay`** (default: `100`) - Delay in milliseconds between retry attempts

### Connection Pooling

When `keepAlive` is greater than 0, connections are automatically pooled and reused:

```cxy
var db = RedisDb()
db.setup({
    keepAlive: 30000 as i64  // Keep connections alive for 30 seconds
})

// First connection creates a new TCP socket
var conn1 = db.connection()
conn1&.set("key1", "value1")
// Connection returns to pool when conn1 goes out of scope

// Second connection reuses the pooled connection
var conn2 = db.connection()
conn2&.get("key1")
// Connection returns to pool again
```

**Benefits of connection pooling:**
- Reduces TCP connection overhead
- Improves performance for frequent operations
- Automatic cleanup of expired connections
- Thread-safe connection management

**How it works:**
1. When a connection is released, it's marked with an expiration timestamp
2. Background cleanup routine removes expired connections
3. The cleanup process yields periodically to avoid blocking
4. Connections are reused in FIFO order

## Core Operations

### Connection Management

Get a connection from the pool:

```cxy
// Get connection to default database (0)
var conn = db.connection()

// Get connection to specific database
var conn2 = db.connection(1)  // Connect to database 1
```

Connections implement RAII (Resource Acquisition Is Initialization):
- Automatically returned to pool when variable goes out of scope
- No manual cleanup required
- Safe to use in exception-throwing code

### Testing Connectivity

Verify the connection is alive:

```cxy
var conn = db.connection()

// PING returns true on success
if conn&.ping() {
    println("Redis is alive!")
}
```

Get server information:

```cxy
var info = db.info()
if info {
    println("Redis version: ", info.version)
    
    // Access other server properties
    var uptime = info.data.["uptime_in_seconds"]
    var memory = info.data.["used_memory_human"]
}
```

## String Operations

Redis strings are the most basic value type. They can hold any data including binary data.

### Set and Get

Store and retrieve string values:

```cxy
var conn = db.connection()

// Set a key-value pair
conn&.set("username", "alice")

// Get the value
var username = conn&.get("username")
println(username)  // "alice"

// Get non-existent key returns empty string
var missing = conn&.get("nonexistent")
println(missing.empty())  // true
```

### Numeric Operations

Increment and decrement numeric values atomically:

```cxy
// Increment by 1
conn&.set("counter", "10")
var newVal = conn&.incr("counter")  // Returns 11

// Decrement by 1
newVal = conn&.decr("counter")  // Returns 10

// Increment by specific amount
newVal = conn&.incrby("counter", 5)  // Returns 15

// Decrement by specific amount
newVal = conn&.decrby("counter", 3)  // Returns 12

// Floating point increment
conn&.set("price", "19.99")
var price = conn&.incrbyfloat("price", 5.01)  // Returns 25.00
```

**Use cases:**
- Rate limiting counters
- Page view counts
- Like/vote tallies
- Statistics tracking

### Key Management

```cxy
// Check if key exists
if conn&.exists("username") {
    println("User exists")
}

// Delete a key
conn&.del("old_key")

// Set expiration (in seconds)
conn&.set("session:123", "data")
conn&.expire("session:123", 3600)  // Expires in 1 hour

// Check time-to-live
var ttl = conn&.ttl("session:123")  // Returns seconds remaining
if ttl < 0 {
    println("Key has no expiration or doesn't exist")
}

// Get substring of value
var sub = conn&.substr("username", 0, 2)  // Get first 3 characters
```

### Pattern Matching

Find keys matching a pattern:

```cxy
// Set some keys
conn&.set("user:1:name", "Alice")
conn&.set("user:2:name", "Bob")
conn&.set("user:3:name", "Charlie")

// Find all user name keys
var userKeys = conn&.keys("user:*:name")
for key, _ in userKeys {
    println("Found key: ", key)
}

// Be careful: KEYS scans entire keyspace - avoid in production
// Better to use Redis SCAN command for large datasets
```

## List Operations

Redis lists are ordered collections of strings, implemented as linked lists.

### Basic List Operations

Add and retrieve items:

```cxy
// Push items to the right (end) of list
conn&.rpush("tasks", "task1", "task2", "task3")

// Push items to the left (beginning) of list
conn&.lpush("tasks", "urgent_task")

// Get list length
var length = conn&.llen("tasks")  // Returns 4

// Get range of items (0-based indexing)
var allTasks = conn&.lrange[String]("tasks", 0, -1)  // -1 means end of list
for task, _ in allTasks {
    println("Task: ", task)
}

// Get specific range
var firstThree = conn&.lrange[String]("tasks", 0, 2)  // Items 0, 1, 2
```

### List Manipulation

```cxy
// Get item at specific index
var firstTask = conn&.lindex[String]("tasks", 0)

// Trim list to specific range (remove elements outside range)
conn&.ltrim("tasks", 0, 9)  // Keep only first 10 items
```

**Common patterns:**

**Task queue (FIFO):**
```cxy
// Producer adds to right
conn&.rpush("queue", "job1")

// Consumer pops from left
var job = conn&.lpop("queue")
```

**Activity feed:**
```cxy
// Add new activity to beginning
conn&.lpush("feed:user:123", "User posted a photo")

// Get recent 10 activities
var recent = conn&.lrange[String]("feed:user:123", 0, 9)

// Keep only recent 100 items
conn&.ltrim("feed:user:123", 0, 99)
```

## Hash Operations

Redis hashes are maps between string fields and string values, perfect for representing objects.

### Basic Hash Operations

```cxy
// Set hash field
conn&.hset[String]("user:1", "name", "Alice")
conn&.hset[i64]("user:1", "age", 30)
conn&.hset[String]("user:1", "email", "alice@example.com")

// Get hash field
var name = conn&.hget[String]("user:1", "name")
var age = conn&.hget[i64]("user:1", "age")

// Check if field exists
if conn&.hexists("user:1", "email") {
    println("Email is set")
}

// Get all field names
var fields = conn&.hkeys("user:1")
for field, _ in fields {
    println("Field: ", field)
}

// Get number of fields
var fieldCount = conn&.hlen("user:1")

// Delete field
conn&.hdel("user:1", "age")
```

**Use cases:**
- User profiles with multiple attributes
- Product information (name, price, stock, description)
- Session data with multiple fields
- Configuration settings grouped by category

**Example - User profile:**
```cxy
// Store user data
conn&.hset[String]("profile:alice", "name", "Alice Smith")
conn&.hset[String]("profile:alice", "bio", "Software Engineer")
conn&.hset[i64]("profile:alice", "followers", 1523)
conn&.hset[String]("profile:alice", "location", "San Francisco")

// Retrieve user data
var bio = conn&.hget[String]("profile:alice", "bio")
var followers = conn&.hget[i64]("profile:alice", "followers")
```

## Set Operations

Redis sets are unordered collections of unique strings.

### Basic Set Operations

```cxy
// Add member to set
conn&.sadd[String]("tags:article:1", "redis")
conn&.sadd[String]("tags:article:1", "database")
conn&.sadd[String]("tags:article:1", "nosql")

// Get all members
var tags = conn&.smembers[String]("tags:article:1")
for tag, _ in tags {
    println("Tag: ", tag)
}

// Get set size
var count = conn&.scard("tags:article:1")  // Returns 3

// Remove and return random member
var randomTag = conn&.spop[String]("tags:article:1")
```

**Use cases:**
- Unique visitor tracking
- Tag systems
- Social graph relationships (followers, following)
- Access control lists

**Example - Tracking unique visitors:**
```cxy
// Add visitor
conn&.sadd[String]("visitors:2024-01-15", "user:123")
conn&.sadd[String]("visitors:2024-01-15", "user:456")
conn&.sadd[String]("visitors:2024-01-15", "user:123")  // Duplicate ignored

// Get unique visitor count
var uniqueVisitors = conn&.scard("visitors:2024-01-15")  // Returns 2
```

## Transactions

Redis transactions group multiple commands into a single atomic operation using MULTI/EXEC.

### Basic Transaction

```cxy
var conn = db.connection()

// Create transaction
var tx = RedisTransaction(conn&)

// Queue commands (they don't execute yet)
tx("SET", "key1", "value1")
tx("SET", "key2", "value2")
tx("INCR", "counter")

// Execute all commands atomically
var result = tx.exec()
if result {
    println("Transaction successful")
    // Access results if needed
}

// Transaction auto-discards if not executed (via deinit)
```

### Transaction with Rollback

```cxy
var tx = RedisTransaction(conn&)

tx("SET", "account:1:balance", "1000")
tx("SET", "account:2:balance", "500")

// Conditionally discard
if someCondition {
    tx.Discard()  // Cancel transaction
    println("Transaction cancelled")
} else {
    tx.exec()
    println("Transaction committed")
}
```

**Important notes:**
- All commands are queued (not executed) until `exec()` is called
- Commands execute atomically - no other client can execute commands in between
- If `exec()` is not called, transaction is automatically discarded via destructor
- Individual command errors don't prevent other commands from executing
- Transactions don't support rollback on application logic errors

## Advanced Usage

### Custom Commands

Execute any Redis command using the generic interface:

```cxy
var conn = db.connection()

// Using the function call operator
var response = (*conn)("COMMAND", "arg1", "arg2")

// Or using send method
var response = conn&.send("COMMAND", "arg1", "arg2")

// Example: APPEND command
(*conn)("APPEND", "message", " world")
```

### Type-Safe Responses

Most methods support generic type parameters for type-safe value retrieval:

```cxy
// Get as String
var name = conn&.get("name")  // Returns String

// Hash fields with different types
var userAge = conn&.hget[i64]("user:1", "age")
var userName = conn&.hget[String]("user:1", "name")

// List items
var stringList = conn&.lrange[String]("items", 0, -1)
var numberList = conn&.lrange[i64]("scores", 0, -1)

// Set members
var tags = conn&.smembers[String]("tags")
var ids = conn&.smembers[i64]("user_ids")
```

### Error Handling

Redis operations throw `RedisError` exceptions on failures:

```cxy
import { RedisError } from "stdlib/redis.cxy"

var value = conn&.get("key") catch (err: RedisError) {
    stderr << "Failed to get key: " << err << "\n"
    return "default_value".S
}

// Handle connection errors
var conn = db.connection() catch (err: RedisError) {
    stderr << "Cannot connect to Redis: " << err << "\n"
    return -1
}
```

Common error scenarios:
- Connection timeout
- Authentication failure
- Network errors
- Invalid command syntax
- Type conversion errors

### Long-Running Connections

For applications that need persistent connections:

```cxy
var db = RedisDb()
db.setup({
    keepAlive: 0 as i64  // Disable automatic pooling
})

// Connection stays open for entire scope
var conn = db.connection()

// Perform many operations
for i in 0..1000 {
    conn&.set(f"key:{i}", f"value:{i}")
}

// Connection closes when conn goes out of scope
```

### Multiple Databases

Redis supports multiple databases (0-15 by default):

```cxy
var db = RedisDb()

// Use different databases for different purposes
var sessionConn = db.connection(0)  // Database 0 for sessions
var cacheConn = db.connection(1)    // Database 1 for cache
var queueConn = db.connection(2)    // Database 2 for queues

sessionConn&.set("session:abc", "user_data")
cacheConn&.set("cache:key", "cached_data")
queueConn&.lpush("jobs", "process_this")
```

## Performance Considerations

### Connection Pooling Best Practices

Enable pooling for applications with frequent short-lived operations:

```cxy
// Good for web applications, API servers
db.setup({
    keepAlive: 30000 as i64  // 30 seconds
})

// Each request gets a connection
func handleRequest(db: &RedisDb): !String {
    var conn = db.connection()
    return conn&.get("data")
    // Connection automatically returned to pool
}
```

Disable pooling for long-running workers:

```cxy
// Good for background workers, batch processors
db.setup({
    keepAlive: 0 as i64  // No pooling
})

func processJobs(db: &RedisDb): !void {
    var conn = db.connection()
    while true {
        // Use same connection for all operations
        var job = conn&.lpop("jobs")
        if job.empty() {
            break
        }
        processJob(job)
    }
}
```

### Batch Operations

Use transactions or pipelining for bulk operations:

```cxy
// Instead of multiple round-trips
for i in 0..100 {
    conn&.set(f"key:{i}", f"value:{i}")  // 100 round-trips
}

// Use transaction (1 round-trip)
var tx = RedisTransaction(conn&)
for i in 0..100 {
    tx("SET", f"key:{i}", f"value:{i}")
}
tx.exec()
```

### Memory Management

Be mindful of large responses:

```cxy
// Avoid loading entire large lists
// Bad: Gets entire list into memory
var all = conn&.lrange[String]("huge_list", 0, -1)

// Good: Process in chunks
var page = 0
while true {
    var items = conn&.lrange[String]("huge_list", page * 100, (page + 1) * 100 - 1)
    if items.empty() {
        break
    }
    processItems(items)
    page += 1
}
```

## Complete Example

Here's a complete example demonstrating common patterns:

```cxy
import { RedisDb } from "stdlib/redis.cxy"

struct User {
    id: i64
    name: String
    email: String
    score: i64
}

func main(): !void {
    // Setup Redis
    var db = RedisDb()
    db.setup({
        host: "127.0.0.1".S,
        keepAlive: 30000 as i64
    })
    
    // Store user data
    var userId = 1 as i64
    var conn = db.connection()
    
    // Store user as hash
    conn&.hset[i64]("user:1", "id", userId)
    conn&.hset[String]("user:1", "name", "Alice")
    conn&.hset[String]("user:1", "email", "alice@example.com")
    conn&.hset[i64]("user:1", "score", 100)
    
    // Add user to active users set
    conn&.sadd[i64]("active_users", userId)
    
    // Track user activity
    conn&.lpush("activity:user:1", "logged_in")
    conn&.lpush("activity:user:1", "viewed_dashboard")
    
    // Increment view counter
    conn&.incr("stats:total_views")
    
    // Set session with expiration
    conn&.set("session:abc123", "user:1")
    conn&.expire("session:abc123", 3600)  // 1 hour
    
    // Retrieve user data
    var userName = conn&.hget[String]("user:1", "name")
    var userScore = conn&.hget[i64]("user:1", "score")
    
    println(f"User: {userName}, Score: {userScore}")
    
    // Get recent activity (last 5 items)
    var activity = conn&.lrange[String]("activity:user:1", 0, 4)
    println("Recent activity:")
    for action, _ in activity {
        println("  - ", action)
    }
}
```

## Limitations and Future Enhancements

**Current limitations:**
- No support for Redis Cluster
- No pub/sub implementation yet
- No support for Redis Streams
- Pipelining not explicitly exposed (use transactions)
- No support for Lua scripts (EVAL/EVALSHA)
- Limited support for sorted sets (ZADD, ZRANGE, etc.)

**Planned enhancements:**
- Cluster support
- Pub/Sub messaging
- Redis Streams API
- Geospatial commands
- HyperLogLog commands
- Sorted set operations
- Explicit pipeline support

These features may be added in future versions based on user needs.

## Best Practices

1. **Use connection pooling** for applications with many short operations
2. **Use transactions** for atomic multi-command operations
3. **Set appropriate timeouts** based on your network environment
4. **Handle errors gracefully** with try-catch blocks
5. **Use type parameters** for type-safe value retrieval
6. **Avoid KEYS in production** - use SCAN for pattern matching instead
7. **Set expiration on session keys** to prevent memory leaks
8. **Use hashes for objects** with multiple fields instead of multiple keys
9. **Choose appropriate data structures** based on access patterns
10. **Monitor connection pool** health in production

## Troubleshooting

**Connection timeouts:**
- Increase `timeout` value in configuration
- Check network connectivity to Redis server
- Verify Redis server is running and accessible

**Memory issues with large responses:**
- Use pagination with LRANGE, avoid getting entire lists
- Consider using SCAN instead of KEYS
- Process data in chunks

**Connection pool exhaustion:**
- Increase `keepAlive` duration
- Ensure connections are properly released (use RAII pattern)
- Check for connection leaks in error handling paths

**Performance degradation:**
- Enable connection pooling with appropriate `keepAlive`
- Use transactions for bulk operations
- Monitor Redis server performance (use INFO command)
- Consider Redis pipelining for high-throughput scenarios
