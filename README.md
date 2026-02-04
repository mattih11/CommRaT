<div align="center">
  <img src="docs/CommRaT.svg" alt="CommRaT Logo" width="400"/>
</div>

# CommRaT - Modern C++ Real-Time Communication Framework

A modern C++20 communication framework that combines **RACK's TiMS IPC** message service with **SeRTial's** zero-allocation serialization, providing compile-time safe, real-time capable messaging with templated message types and a powerful message registry for efficient type dispatch.

## Features

- **Modern C++20**: Full template metaprogramming with concepts and type safety
- **Zero-Allocation Serialization**: Stack-allocated buffers via SeRTial with compile-time size computation
- **Compile-Time Type Safety**: Templated message types with static validation
- **Message Registry**: Compile-time type registry for zero-overhead message dispatch
- **Runtime Visitor Pattern**: Efficient runtime dispatch without virtual functions
- **SeRTial Integration**: Automatic serialization using `fixed_vector` and `fixed_string` containers
- **TiMS IPC Backend**: Socket-based real-time messaging from RACK
- **RT-Capable**: No dynamic allocation in message paths, deterministic behavior
- **User-Friendly API**: Define custom payloads, get type-safe messages automatically

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
    
    // Create a status message
    StatusMessage status;
    status.payload.status_code = 200;
    status.payload.cpu_load = 0.65f;
    status.payload.description = "System OK";
    
    // Serialize - type ID automatically set from template parameter
    auto result = serialize(status);
    
    // Deserialize with compile-time type knowledge
    auto msg = deserialize<StatusMessage>(
        std::span{result.buffer.data(), result.size}
    );
    
    if (msg) {
        std::cout << "Status: " << msg->payload.status_code << "\n";
    }
}
```

## Architecture

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

## Running the Tests

The test applications demonstrate complete message exchange with proper type identification:

### Terminal 1: Start Receiver

```bash
cd build
./test_receiver 200
```

### Terminal 2: Send Messages

```bash
cd build
./test_sender 200
```

### Example Output

```
[StatusMessage] Received:
  Timestamp: 113852048385
  Seq: 0
  Status code: 100
  CPU load: 45%
  Memory: 30%
  Description: Test status message #0

[CommandMessage] Received:
  Timestamp: 113852548546
  Seq: 1
  Command ID: 1000
  Target ID: 5
  Parameters (3): 0, 0, 0
```

### Run Simple Usage Example

```bash
cd build
./example_simple_usage
```

This demonstrates the clean user-facing API with automatic type-to-ID mapping.

### Run Registry Demo

```bash
cd build
./example_registry_demo
```

This shows the internal registry implementation (for advanced users only).

See full examples and API documentation in the `examples/` directory.

## License

See LICENSE file for details.

## References

- [RACK Project](http://rack.sourceforge.net/)
- SeRTial Library - Reflective C++ serialization
