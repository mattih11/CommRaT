<div align="center">
  <img src="docs/CommRaT.png" alt="CommRaT Logo" width="400"/>
</div>

# CommRaT - Modern C++ Real-Time Communication Framework

A modern C++20 communication framework that combines **RACK's TiMS IPC** message service with **SeRTial's** zero-allocation serialization, providing compile-time safe, real-time capable messaging with templated message types and a powerful mailbox interface for efficient type dispatch.

## Features

- **Modern C++20**: Full template metaprogramming with concepts, `std::span`, and type safety
- **Zero-Allocation Serialization**: Stack-allocated `std::byte` buffers via SeRTial with compile-time size computation
- **Compile-Time Type Safety**: Templated message types with static validation
- **Mailbox API**: Modern C++20 RAII-based mailbox with blocking/non-blocking/timeout receive patterns
- **Message Registry**: Compile-time type registry for zero-overhead message dispatch
- **Runtime Visitor Pattern**: Efficient runtime dispatch without virtual functions (receive_any)
- **SeRTial Integration**: Automatic serialization using `fixed_vector`, `fixed_string`, and `buffer_type`
- **TiMS IPC Backend**: Socket-based real-time messaging from RACK (casting at C API boundary only)
- **RT-Capable**: No dynamic allocation in message paths, deterministic behavior
- **Clean Interfaces**: `std::span<std::byte>` throughout, pointer+size only at TiMS boundary

## Quick Start

### Installation

```bash
# Clone the repository
git clone <repository-url>
cd CommRaT

# Set up dependencies (SeRTial and RACK TiMS)
ln -s /path/to/SeRTial SeRTial
ln -s /path/to/rack/tims tims

# Build
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Simple Example

```cpp
#include "commrat/commrat.hpp"

int main() {
    using namespace commrat;
    
    // Define message registry (compile-time)
    using MyRegistry = MessageRegistry<StatusMessage, CommandMessage>;
    
    // Create mailbox
    TimsConfig config{
        .mailbox_name = "my_mailbox",
        .mailbox_id = 100,
        .max_msg_size = MyRegistry::max_message_size,
        .priority = 10
    };
    
    Mailbox<MyRegistry> mailbox(config);
    mailbox.start();
    
    // Send a message
    StatusMessage status;
    status.payload.status_code = 200;
    status.payload.cpu_load = 0.65f;
    status.payload.description = "System OK";
    
    mailbox.send(status, 200); // Send to mailbox ID 200
    
    // Receive with timeout
    auto result = mailbox.receive_for<StatusMessage>(
        std::chrono::milliseconds(1000)
    );
    
    if (result) {
        std::cout << "Status: " << result->message.payload.status_code << "\n";
    }
}
```

## Architecture

### Mailbox-Based Communication

The core abstraction is the type-safe `Mailbox` template:

```cpp
// Define your message types
using MyMailbox = Mailbox<
    StatusMessage,
    CommandMessage,
    SensorMessage
>;

// Create and start
TimsConfig config = /* ... */;
MyMailbox mailbox(config);
mailbox.start();

// Type-safe send
CommandMessage cmd;
mailbox.send(cmd, dest_id);

// Blocking receive
auto msg = mailbox.receive<StatusMessage>();

// Non-blocking receive
auto msg = mailbox.try_receive<CommandMessage>(
    std::chrono::milliseconds(0)
);

// Receive any registered type (visitor pattern)
mailbox.receive_any([](auto&& received_msg) {
    using MsgType = std::decay_t<decltype(received_msg.message)>;
    // Handle message based on type
    if constexpr (std::is_same_v<MsgType, StatusMessage>) {
        std::cout << "Status: " << received_msg.message.payload.status_code;
    }
});
```

### Templated Message System

Messages use a template-based design with type-safe payloads:

```cpp
// TIMS-compatible header (automatically managed)
struct TimsHeader {
    uint32_t msg_type;
    uint32_t msg_size;      // Set during serialization
    uint64_t timestamp;     // Set by send()
    uint32_t seq_number;    // Set by send()
    uint32_t flags;
};

// Generic message wrapper
template<typename PayloadT>
struct TimsMessage {
    TimsHeader header;
    PayloadT payload;
    
    // Message type automatically derived from payload type
    static constexpr MessageType message_type = message_type_for<PayloadT>::value;
};
```

### Message Registry for Compile-Time Dispatch

The message type-to-ID mapping is automatic via type traits:

```cpp
// Define your payload
struct MyPayload {
    uint32_t data;
    float value;
};

// Register it with a message type ID
template<>
struct message_type_for<MyPayload> {
    static constexpr MessageType value = MessageType::USER_DEFINED;
};

// Use it - just one template parameter!
using MyMessage = TimsMessage<MyPayload>;

// Type ID is automatically set during serialization
MyMessage msg;
auto result = serialize(msg);  // msg.header.msg_type automatically set
```

**Key Benefits:**
- Single template parameter: `TimsMessage<PayloadT>` instead of `TimsMessage<PayloadT, MsgType>`
- Type-to-ID mapping via `message_type_for` trait
- Compile-time validation - won't compile if trait is missing
- Zero runtime overhead
- RAII lifecycle management with `Mailbox`
- Modern C++20 interfaces: `std::span<std::byte>`, concepts, type traits
- Buffer sizes from `MessageRegistry::max_message_size` (no hardcoded values)

## Running Examples and Tests

### Registry Demo

Demonstrates compile-time message registry and runtime dispatch:

```bash
cd build
./example_registry_demo
```

Shows:
- Default registry with 6 built-in message types
- Runtime dispatch with visitor pattern
- Custom registry creation
- Compile-time type information and max buffer sizes

### Mailbox Example

Complete sender/receiver demo with type-safe mailbox:

```bash
cd build
./example_mailbox
```

Demonstrates:
- Simple bidirectional communication
- Visitor pattern (receive_any with different types)
- Non-blocking and timeout receive
- Type safety with restricted registries
- Standard mailbox usage

### Non-Blocking Test

Validates non-blocking receive behavior:

```bash
cd build
./test_nonblocking
```

Verifies:
- Non-blocking receive is truly non-blocking (0ms for 10 calls)
- Timeout receive properly times out (~1000ms due to TiMS quirk)

### Simple Usage

Basic serialization/deserialization example:

```bash
cd build
./example_simple_usage
```

### Simple Usage

Basic serialization/deserialization example:

```bash
cd build
./example_simple_usage
```

Shows:
- Serialize/deserialize with automatic type IDs
- Multiple message types
- Compile-time type information
- Type traits validation

## API Overview

### Core Components

1. **Mailbox<MessageTypes...>**: Type-safe mailbox with RAII lifecycle
   - `start()` / `stop()`: Lifecycle management
   - `send<T>(msg, dest_id)`: Type-safe send
   - `receive<T>()`: Blocking receive
   - `try_receive<T>(timeout)`: Non-blocking receive
   - `receive_for<T>(timeout)`: Timeout receive
   - `receive_any(visitor)`: Visitor pattern for runtime dispatch
   - `clean()`: Clear pending messages

2. **MessageRegistry<MessageTypes...>**: Compile-time type registry
   - `serialize<T>(msg)`: Type-safe serialization
   - `deserialize<T>(data)`: Type-safe deserialization
   - `visit(msg_type, data, visitor)`: Runtime dispatch
   - `max_message_size`: Maximum buffer size for all types

3. **TimsWrapper**: Low-level TiMS IPC wrapper
   - Modern C++ interface with `std::span<std::byte>`
   - Casting to void* only at TiMS C API boundary
   - RAII resource management

See full examples and API documentation in the `examples/` directory and `docs/MAILBOX_DESIGN.md`.

## License

See LICENSE file for details.

## References

- [RACK Project](https://github.com/smolorz/RACK) - Robotics Application Construction Kit
- [SeRTial Library](https://github.com/mattih11/SeRTial) - Reflective C++ serialization
