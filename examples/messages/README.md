# Example Message Definitions

This directory contains shared message type definitions for inter-module communication examples.

## Purpose

Message definitions are kept in a **shared header** separate from module implementations to:

1. **Ensure consistency** - All modules use the same data structures
2. **Enable type safety** - Compile-time checking of message types
3. **Promote reusability** - Messages can be shared across multiple modules
4. **Clarify architecture** - Separates data contracts from business logic
5. **Support evolution** - Message definitions can be versioned independently

## Structure

### `common_messages.hpp`

Contains message data structures and their CommRaT type registrations.

**Message Categories:**

- **Status Messages** - System health and monitoring data
- **Counter Messages** - Simple counters for testing/benchmarking
- **Sensor Messages** - Sensor readings (temperature, etc.)
- **Kinematic Messages** - Position, velocity, pose data

Each message type includes:
- Data structure definition with documented fields
- Registration with `commrat::message_type_for<>` template specialization
- Mapping to appropriate `MessageType` enum value

## Usage Pattern

### 1. Define Your Message Structure

```cpp
namespace example_messages {

struct MyData {
    uint64_t timestamp;
    float value;
    // ... more fields
};

} // namespace example_messages
```

### 2. Register with CommRaT

```cpp
template<>
struct commrat::message_type_for<example_messages::MyData> {
    static constexpr commrat::MessageType value = commrat::MessageType::DATA;
};
```

### 3. Use in Modules

```cpp
#include "messages/common_messages.hpp"

using namespace example_messages;

class MyModule : public Module<MyData, PeriodicInput> {
    void process(MyData& output) override {
        output.timestamp = get_timestamp();
        output.value = compute_value();
    }
};
```

## Available Message Types

From `commrat::MessageType` enum:

- `DATA` - Generic data messages
- `COMMAND` - Command/control messages  
- `STATUS` - Status/health information
- `ERROR` - Error reports
- `ACK` - Acknowledgments
- `SENSOR_DATA` - Sensor readings
- `ROBOT_STATE` - Robot kinematic state
- `USER_DEFINED` (≥1000) - Custom application types

## Best Practices

### ✅ DO

- Keep message structures simple and focused
- Document all fields with comments
- Use consistent naming conventions (e.g., `_data` suffix)
- Group related messages in namespaces
- Use standard integer types (`uint64_t`, `float`, etc.)
- Include timestamps for temporal data
- Version your message definitions

### ❌ DON'T

- Define messages inside module classes
- Use pointers or dynamic allocations in message structs
- Mix business logic with message definitions
- Create circular dependencies between message types
- Use platform-specific types (use `uint32_t` not `unsigned int`)

## Example: Producer-Consumer Pattern

```cpp
// In common_messages.hpp
struct SensorReading {
    uint64_t timestamp;
    uint32_t sensor_id;
    float value;
};

template<>
struct commrat::message_type_for<SensorReading> {
    static constexpr commrat::MessageType value = commrat::MessageType::SENSOR_DATA;
};

// Producer module
class SensorModule : public Module<SensorReading, PeriodicInput> {
    void process(SensorReading& output) override {
        output.sensor_id = 42;
        output.value = read_sensor();
        output.timestamp = get_timestamp();
    }
};

// Consumer module
class ProcessorModule : public Module<ProcessedData, ContinuousInput<SensorModule>> {
    void process(const SensorReading& input, ProcessedData& output) override {
        output.filtered_value = apply_filter(input.value);
    }
};
```

## Adding New Messages

1. Define structure in appropriate namespace
2. Add `message_type_for<>` specialization
3. Document the message purpose and fields
4. Update this README if adding new categories
5. Rebuild dependent modules

## See Also

- `../module_example.cpp` - Examples using these messages
- `../../include/commrat/messages.hpp` - Base message types
- `../../include/commrat/module.hpp` - Module framework
- `../../docs/MODULE_FRAMEWORK.md` - Module system documentation
