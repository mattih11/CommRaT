# CommRaT Mailbox Design and Implementation

## Implementation Status

**Status**: ✅ Phase 1 COMPLETED  
**Last Updated**: February 4, 2026  
**Location**: `include/commrat/mailbox.hpp`

### What's Working

- ✅ Core `Mailbox<MessageTypes...>` class with template-based type safety
- ✅ Lifecycle management (`start()`, `stop()`, RAII cleanup)
- ✅ Type-safe sending (`send<T>(message, dest)`)
- ✅ Blocking receive (`receive<T>()`)
- ✅ **Non-blocking receive** (`try_receive<T>()` - uses TIMS_NONBLOCK)
- ✅ Timeout receive (`receive_for<T>(timeout)`)
- ✅ Visitor pattern (`receive_any(visitor)` for runtime dispatch)
- ✅ C++20-compatible error handling (`MailboxResult<T>`)
- ✅ Comprehensive test suite validating behavior

### Implementation Notes

1. **Non-blocking Mode**: Uses `timeout = -1ms` which converts to `TIMS_NONBLOCK (-1ns)` in TiMS
   - Matches RACK's `recvMsgIf()` approach
   - Returns immediately if no message available
   - Tested: 0ms for 10 non-blocking calls ✓

2. **Timeout Handling**: TiMS implementation adds ~1 second to all non-zero timeouts
   - This is a TiMS quirk: `timeout.tv_sec = (timeout_ns / 1e9) + 1`
   - Documented in test code
   - Not a bug in our implementation

3. **RACK Compatibility**: Design follows RACK patterns while modernizing the interface
   - RACK: `recvMsgIf()` → CommRaT: `try_receive()`
   - RACK: `recvMsgTimed(ns)` → CommRaT: `receive_for(ms)`
   - RACK: void* data → CommRaT: `ReceivedMessage<T>`

4. **Modern C++ Interfaces**: Type-safe throughout the stack
   - Uses `std::span<std::byte>` for buffer passing (not pointer+size)
   - SeRTial's `buffer_type` (compile-time sized `std::array<std::byte, N>`)
   - Buffer sizes from `MessageRegistry::max_message_size` (no hardcoded values)
   - Casting to `void*` only at TiMS C API boundary in `TimsWrapper`
   - Clean interfaces: modern C++ everywhere, old-school C only at system boundary

---

## Original Design Document

Design for a modern C++20 strongly-typed mailbox interface that builds on top of our existing TiMS wrapper and message system, providing a clean, type-safe API for message-based communication.

## Goals

1. **Strong Type Safety**: Compile-time type checking for all message operations
2. **Zero Runtime Overhead**: All type dispatch resolved at compile time
3. **Modern C++ Interface**: RAII, move semantics, concepts, and ranges
4. **Backward Compatible**: Works with existing TiMS infrastructure
5. **User Friendly**: Simple, intuitive API that hides complexity
6. **RT-Capable**: No dynamic allocation in hot paths

## Key Differences from RACK Mailbox

| Feature | RACK Mailbox | CommRaT Mailbox |
|---------|--------------|-----------------|
| Type System | Void pointers, manual casting | Template-based, compile-time types |
| Message Handling | Manual serialization | Automatic via SeRTial |
| API Style | C-style with classes | Modern C++20 with concepts |
| Error Handling | Return codes | `MailboxResult<T>` (std::expected-like) |
| Thread Safety | Manual mutex locking | RAII lock guards, atomic operations |
| Memory Management | Manual buffer management | RAII with stack allocation |
| Buffer Passing | `void* + size_t` | `std::span<std::byte>` |
| Buffer Sizing | Hardcoded (e.g., 4096) | `MessageRegistry::max_message_size` |
| Type Safety | Runtime casting | Compile-time type validation |

## Architecture

### 1. Mailbox Template Class

```cpp
template<typename... MessageTypes>
class Mailbox {
    // Statically knows about all message types it can handle
    // Provides compile-time validation
};
```

**Key Features:**
- Template parameter pack of message types
- Compile-time validation of supported types
- Automatic type registration
- Zero overhead type dispatch

### 2. Core Operations

#### Send Operations
```cpp
// Send any registered message type
template<typename T>
auto send(const T& message, uint32_t dest_mailbox) -> Result;

// Send with callback on completion
template<typename T>
auto send_async(T&& message, uint32_t dest_mailbox, Callback cb) -> Future<Result>;

// Broadcast to multiple destinations
template<typename T>
auto broadcast(const T& message, std::span<uint32_t> destinations) -> Result;
```

#### Receive Operations
```cpp
// Blocking receive with type
template<typename T>
auto receive() -> Expected<T, Error>;

// Non-blocking receive
template<typename T>
auto try_receive() -> std::optional<T>;

// Timed receive
template<typename T>
auto receive_for(Duration timeout) -> Expected<T, Error>;

// Receive any registered type using visitor
auto receive_any(Visitor auto&& visitor) -> Result;
```

#### Peek Operations (Non-destructive)
```cpp
// Peek at next message without removing it
template<typename T>
auto peek() -> Expected<const T&, Error>;

// Check if message of type is available
template<typename T>
auto has_message() const -> bool;

// Get count of pending messages
auto pending_count() const -> size_t;
```

### 3. Message Queue Interface

```cpp
// Internal queue for typed messages
template<typename T, size_t Capacity>
class MessageQueue {
    // Lock-free SPSC queue for RT performance
    // Fixed capacity for deterministic behavior
    // RAII semantics
};
```

### 4. Type Registry Integration

Uses our existing `message_type_for<T>` trait system:
```cpp
// Compile-time validation
static_assert(Mailbox<StatusMessage, CommandMessage>::is_registered<StatusMessage>);

// Automatic serialization/deserialization
auto result = serialize(message);  // Already have this
auto msg = deserialize<T>(buffer); // Already have this
```

### 5. Error Handling

Use `std::expected` for modern error handling:
```cpp
enum class MailboxError {
    NotInitialized,
    InvalidMessage,
    Timeout,
    QueueFull,
    QueueEmpty,
    SerializationError,
    NetworkError
};

template<typename T>
using MailboxResult = std::expected<T, MailboxError>;
```

### 6. Thread Safety Model

- **Send**: Thread-safe with internal locking (inherited from TimsWrapper)
- **Receive**: Single receiver thread (typical mailbox pattern)
- **Peek**: Read-only, lock-free where possible
- **Statistics**: Lock-free atomic counters

## Interface Design

### Basic Mailbox

```cpp
template<typename... MessageTypes>
class Mailbox {
public:
    // Configuration
    struct Config {
        uint32_t mailbox_id;
        size_t message_slots = 10;
        size_t max_message_size = 4096;
        uint8_t send_priority = 10;
        bool realtime = false;
    };
    
    // Construction
    explicit Mailbox(const Config& config);
    ~Mailbox();
    
    // Non-copyable, movable
    Mailbox(const Mailbox&) = delete;
    Mailbox& operator=(const Mailbox&) = delete;
    Mailbox(Mailbox&&) noexcept;
    Mailbox& operator=(Mailbox&&) noexcept;
    
    // Lifecycle
    auto start() -> MailboxResult<void>;
    void stop();
    auto is_running() const -> bool;
    
    // Send operations (any registered type)
    template<typename T>
        requires is_registered<T>
    auto send(const T& message, uint32_t dest) -> MailboxResult<void>;
    
    template<typename T>
        requires is_registered<T>
    auto send_reply(const T& message, const ReceivedMessage& original) -> MailboxResult<void>;
    
    // Receive operations
    template<typename T>
        requires is_registered<T>
    auto receive() -> MailboxResult<T>;
    
    template<typename T>
        requires is_registered<T>
    auto try_receive() -> std::optional<T>;
    
    template<typename T>
        requires is_registered<T>
    auto receive_for(std::chrono::milliseconds timeout) -> MailboxResult<T>;
    
    // Generic receive with visitor (runtime dispatch)
    auto receive_any(auto&& visitor) -> MailboxResult<void>;
    
    // Peek operations (non-destructive)
    template<typename T>
        requires is_registered<T>
    auto peek() -> MailboxResult<const T*>;
    
    auto peek_any(auto&& visitor) -> MailboxResult<void>;
    
    // Utilities
    auto clean() -> MailboxResult<void>;  // Clear all pending messages
    auto pending_count() const -> size_t;
    auto mailbox_id() const -> uint32_t;
    
    // Statistics
    auto messages_sent() const -> uint64_t;
    auto messages_received() const -> uint64_t;
    
private:
    TimsWrapper tims_;
    // Internal implementation...
};
```

### Received Message Wrapper

```cpp
template<typename T>
struct ReceivedMessage {
    T payload;
    
    // Metadata from TIMS header
    uint32_t source_mailbox;
    uint64_t timestamp;
    uint32_t sequence_number;
    uint8_t priority;
    
    // Helper for replies
    auto reply(auto&& response_message) -> MailboxResult<void>;
};
```

### Convenience Aliases

```cpp
// Default mailbox with common message types
using StandardMailbox = Mailbox<
    CommandMessage,
    StatusMessage,
    ErrorMessage,
    AckMessage,
    SensorMessage
>;

// Custom mailbox type
template<typename... CustomTypes>
using CustomMailbox = Mailbox<
    CommandMessage,
    StatusMessage,
    CustomTypes...
>;
```

## Usage Examples

### Basic Send/Receive

```cpp
// Create mailbox
Mailbox<StatusMessage, CommandMessage>::Config config{
    .mailbox_id = 200,
    .message_slots = 10
};
Mailbox<StatusMessage, CommandMessage> mbx(config);
mbx.start();

// Send
StatusMessage status;
status.payload.status_code = 100;
status.payload.description = "OK";
mbx.send(status, 201);

// Receive (blocking)
auto result = mbx.receive<CommandMessage>();
if (result) {
    auto& cmd = *result;
    std::cout << "Command: " << cmd.payload.command_id << "\n";
}
```

### Non-blocking Receive

```cpp
// Try receive without blocking
auto msg = mbx.try_receive<StatusMessage>();
if (msg) {
    std::cout << "Got status: " << msg->payload.status_code << "\n";
} else {
    std::cout << "No message available\n";
}
```

### Receive with Timeout

```cpp
using namespace std::chrono_literals;
auto result = mbx.receive_for<CommandMessage>(500ms);
if (result) {
    // Process command
} else if (result.error() == MailboxError::Timeout) {
    // Handle timeout
}
```

### Generic Receive (Runtime Dispatch)

```cpp
auto result = mbx.receive_any([](auto&& msg) {
    using T = std::decay_t<decltype(msg)>;
    if constexpr (std::is_same_v<T, StatusMessage>) {
        std::cout << "Status: " << msg.payload.status_code << "\n";
    } else if constexpr (std::is_same_v<T, CommandMessage>) {
        std::cout << "Command: " << msg.payload.command_id << "\n";
    }
});
```

### Reply Pattern

```cpp
auto result = mbx.receive<CommandMessage>();
if (result) {
    auto received = *result;
    
    // Process command...
    
    // Send reply
    StatusMessage reply;
    reply.payload.status_code = 200;
    mbx.send_reply(reply, received);
}
```

### Peek Without Consuming

```cpp
// Check if message is available
if (auto msg_ptr = mbx.peek<StatusMessage>()) {
    std::cout << "Next status code: " << (*msg_ptr)->payload.status_code << "\n";
    // Message still in queue
}
```

## Implementation Strategy

### Phase 1: Core Mailbox Class ✅ COMPLETED

**Status**: Fully implemented and tested  
**Location**: `include/commrat/mailbox.hpp`  
**Examples**: `examples/mailbox_example.cpp`, `examples/test_nonblocking.cpp`

#### Implemented Components

1. **Template class definition** ✅
   ```cpp
   template<typename... MessageTypes>
   class Mailbox {
       using Registry = MessageRegistry<MessageTypes...>;
       // Validates all types are CommRaT messages at compile time
   };
   ```

2. **RAII initialization** ✅
   - Constructor takes `MailboxConfig`
   - Automatic `start()` connects to TiMS
   - Destructor calls `stop()` and cleans up
   - Move semantics implemented (non-copyable)

3. **Send operations** ✅
   ```cpp
   template<typename T>
       requires is_registered<T>
   auto send(T& message, uint32_t dest_mailbox) -> MailboxResult<void>;
   ```
   - Compile-time type validation
   - Automatic serialization via MessageRegistry
   - Type ID automatically set from `message_type_for<T>`

4. **Receive operations** ✅
   - **Blocking**: `receive<T>()` - waits indefinitely
   - **Non-blocking**: `try_receive<T>()` - returns immediately (uses TIMS_NONBLOCK)
   - **Timeout**: `receive_for<T>(timeout)` - waits up to timeout
   - **Visitor**: `receive_any(visitor)` - runtime dispatch to typed handler

5. **Error handling** ✅
   - Custom `MailboxResult<T>` type (C++20-compatible alternative to std::expected)
   - Comprehensive error codes (NotRunning, Timeout, NetworkError, etc.)
   - `to_string(error)` for user-friendly messages

6. **ReceivedMessage wrapper** ✅
   ```cpp
   template<typename T>
   struct ReceivedMessage {
       T message;
       uint64_t timestamp;
       uint32_t sequence_number;
   };
   ```

#### Implementation Details

**Non-blocking Receive**:
```cpp
// Uses -1ms timeout which becomes TIMS_NONBLOCK (-1ns) in TiMS
auto try_receive() -> std::optional<ReceivedMessage<T>> {
    auto result = receive_for<T>(std::chrono::milliseconds(-1));
    return result ? std::optional(*result) : std::nullopt;
}
```

**TiMS Integration**:
- Special handling in `TimsWrapper::receive_raw()`:
  ```cpp
  if (timeout.count() == -1) {
      timeout_ns = -1;  // TIMS_NONBLOCK
  } else {
      timeout_ns = timeout.count() * 1000000;  // ms to ns
  }
  ```

**RACK Compatibility Mapping**:
- `RackMailbox::recvMsgIf()` → `Mailbox::try_receive<T>()`
- `RackMailbox::recvMsgTimed(ns)` → `Mailbox::receive_for<T>(ms)`
- `RackMailbox::recvMsg()` → `Mailbox::receive<T>()`

#### Test Results

From `test_nonblocking.cpp`:
```
✓ Non-blocking receive: 0ms for 10 calls
✓ Blocking timeout: ~1000ms (TiMS adds 1 second to timeouts)
```

#### Known Issues/Quirks

1. **TiMS Timeout Behavior**: TiMS implementation adds 1 second to all non-zero timeouts
   - Root cause: `timeout.tv_sec = (timeout_ns / 1e9) + 1` in `tims_api_linux.c`
   - This is not a bug in CommRaT
   - Documented in code and tests

2. **TiMS Peek Not Implemented**: `tims_peek_timed()` is not available
   - RACK's `peekIf()` cannot be implemented
   - Workaround: use `try_receive()` which is functionally equivalent

### Phase 2: Type System Integration (PLANNED)

**Status**: Not started - infrastructure already exists

1. **Custom type registration helpers**
   - Macro for easy type registration
   - Validation of custom payloads
   - Integration with existing `message_type_for<T>` system

2. **Advanced serialization**
   - Custom serializer support
   - Compression options
   - Schema versioning

### Phase 3: Advanced Features (PLANNED)

**Status**: Design phase

1. **Priority handling**
2. Timeout support
3. Peek operations
4. Queue statistics

### Phase 4: Optimization
1. Lock-free data structures where possible
2. Cache-friendly memory layout
3. Branch prediction hints
4. RT profiling and tuning

## Benefits Over RACK Mailbox

1. **Type Safety**: Compile-time errors instead of runtime crashes
2. **No Manual Casting**: Types automatically handled
3. **Modern Error Handling**: `std::expected` instead of error codes
4. **RAII Everything**: No manual cleanup needed
5. **Zero Copy**: Where possible with move semantics
6. **Better Documentation**: Self-documenting with types
7. **Easier Testing**: Mock friendly interface
8. **Performance**: Compile-time dispatch, no virtual calls

## Backward Compatibility

Can interoperate with RACK mailboxes through:
- Same underlying TiMS protocol
- Compatible message format
- Conversion utilities if needed

## Future Enhancements

1. **Async/Await**: C++20 coroutine support
2. **Priority Queues**: Multiple priority levels
3. **Message Filtering**: Selective receive
4. **Distributed Tracing**: Message flow tracking
5. **Hot Reloading**: Dynamic type registration
6. **Zero-Copy IPC**: Shared memory optimization
