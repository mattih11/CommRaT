<div align="center">
  <img src="docs/CommRaT.png" alt="CommRaT Logo" width="400"/>
</div>

# CommRaT - Modern C++ Real-Time Communication Framework

A modern C++20 communication framework that combines **RACK's TiMS IPC** message service with **SeRTial's** zero-allocation serialization, providing compile-time safe, real-time capable messaging with templated message types and a powerful mailbox interface for efficient type dispatch.

## Features

- **Ultra-Clean User Interface**: Define messages ONCE, use payload types everywhere (no MessageDefinition in user code)
- **Payload-Only API**: Module<OutputData, InputMode> - users never see TimsMessage wrappers or message IDs
- **Auto-Subscription**: ContinuousInput<PayloadT> automatically handles subscription protocol
- **Variadic Commands**: Module<..., Cmd1, Cmd2, Cmd3> with type-safe on_command() handlers
- **System Messages Auto-Included**: CombinedRegistry automatically adds subscription protocol messages
- **Compile-Time Message IDs**: 0xPSMM format (Prefix, SubPrefix, Message ID) with auto-increment
- **Modern C++20**: Full template metaprogramming with concepts, `std::span`, and type safety
- **Zero-Allocation Serialization**: Stack-allocated `std::byte` buffers via SeRTial with compile-time size computation
- **Compile-Time Type Safety**: Templated message types with static validation and collision detection
- **Module Framework**: RAII-based Module<> with PeriodicInput/LoopInput/ContinuousInput modes
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

### Ultra-Clean Interface (Recommended)

**Step 1**: Define your messages ONCE
```cpp
// messages/user_messages.hpp
#include <commrat/system_registry.hpp>

struct TemperatureData {
    float temperature_celsius{0.0f};
    uint64_t timestamp_ms{0};
};

// Registry with system messages auto-included
using AppRegistry = commrat::CombinedRegistry<
    commrat::MessageDefinition<TemperatureData, 
        commrat::MessagePrefix::UserDefined, 
        commrat::UserSubPrefix::Data>
>;

// Create clean alias (hides registry)
template<typename OutputData, typename InputMode, typename... CommandTypes>
using Module = commrat::Module<AppRegistry, OutputData, InputMode, CommandTypes...>;
```

**Step 2**: Write your module using payload types only
```cpp
#include "messages/user_messages.hpp"

// Ultra-clean: Module<OutputType, InputMode>
class SensorModule : public Module<TemperatureData, PeriodicInput> {
protected:
    TemperatureData process() override {
        return TemperatureData{.temperature_celsius = read_sensor()};
    }
};

int main() {
    ModuleConfig config{
        .mailbox_id = 100,
        .period = std::chrono::milliseconds(100)
    };
    
    SensorModule sensor(config);
    sensor.start();  // Auto-publishes to subscribers
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    sensor.stop();
}
```

**What You DON'T Write:**
- ❌ MessageDefinition in module code (only in registry)
- ❌ TimsMessage wrappers (automatic)
- ❌ Manual message IDs (auto-increment)
- ❌ Subscription protocol (automatic)
- ❌ Registry parameter in Module<> (hidden in alias)

### Legacy Mailbox API (Still Supported)

```cpp
#include "commrat/commrat.hpp"

using MyRegistry = MessageRegistry<
    MessageDefinition<StatusData, ...>,
    MessageDefinition<CommandData, ...>
>;

RegistryMailbox<MyRegistry> mailbox(config);
mailbox.start();

// Send payload directly
StatusData status{.status_code = 200, .cpu_load = 0.65f};
mailbox.send(status, dest_mailbox_id);

// Receive payload directly
auto result = mailbox.receive<StatusData>(std::chrono::milliseconds(1000));
if (result) {
    std::cout << "Status: " << result->message.status_code << "\n";
}
```
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

### Ultra-Clean Interface Examples (Runtime Validated ✅)

#### Continuous Input Example
Producer→Consumer with automatic subscription protocol:
```bash
cd build
./example_continuous_input
```

Demonstrates:
- Producer publishing TemperatureData @ 100ms (PeriodicInput)
- Consumer auto-subscribing to producer (ContinuousInput<TemperatureData>)
- Data flowing continuously between modules
- Ultra-clean Module<PayloadT, InputMode> interface
- No MessageDefinition in user code

#### Clean Interface Example
Minimal boilerplate with payload-only API:
```bash
cd build
./example_clean_interface
```

Shows:
- Module<TemperatureData, PeriodicInput> (no registry parameter)
- process() returns payload type directly
- System messages automatically included
- One include: `messages/messages.hpp`

#### Command Example
Variadic command handling with type-safe dispatch:
```bash
cd build
./example_commands
```

Demonstrates:
- Module<TempData, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd>
- Type-safe on_command(const CmdT&) handlers
- No manual command ID checking
- Compile-time command validation

### Legacy Examples

#### Registry Demo
Compile-time message registry and runtime dispatch:
```bash
cd build
./example_registry_demo
```

#### Mailbox Example
Type-safe mailbox with visitor pattern:
```bash
cd build
./example_mailbox
```

#### Non-Blocking Test
Validates receive behavior:
```bash
cd build
./test_nonblocking
```

#### Simple Usage
Basic serialization/deserialization:
```bash
cd build
./example_simple_usage
```

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
