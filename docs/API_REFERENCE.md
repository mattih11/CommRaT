# CommRaT API Reference

**Version**: 1.0.0  
**Last Updated**: February 12, 2026

This document provides a comprehensive reference for the CommRaT API. For detailed Doxygen documentation, run `make docs` and open `docs/api/html/index.html`.

---

## Table of Contents

1. [Application Template](#application-template)
2. [Module Base Class](#module-base-class)
3. [I/O Specifications](#io-specifications)
4. [Metadata Accessors](#metadata-accessors)
5. [Configuration](#configuration)
6. [Threading Abstractions](#threading-abstractions)
7. [Timestamp Abstractions](#timestamp-abstractions)
8. [Mailbox Interface](#mailbox-interface)
9. [Message Definitions](#message-definitions)
10. [Introspection System](#introspection-system)

---

## Application Template

### CommRaT<MessageDefs...>

The main application template that defines your messaging system.

```cpp
template<typename... MessageDefs>
class CommRaT;
```

**Usage:**
```cpp
using MyApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<PressureData>,
    commrat::Message::Command<ResetCmd>
>;
```

**Provides:**
- `MyApp::Module<OutputSpec, InputSpec, ...Commands>` - Module template
- `MyApp::Mailbox<T>` - Mailbox template for type T
- `MyApp::HistoricalMailbox<HistorySize>` - Buffered mailbox with getData()
- `MyApp::Introspection` - Schema export helper
- `MyApp::get_message_id<T>()` - Compile-time message ID lookup
- `MyApp::is_registered<T>` - Check if type is in registry
- `MyApp::serialize<T>(message)` - Serialize message to bytes
- `MyApp::deserialize<T>(buffer)` - Deserialize message from bytes
- `MyApp::visit(buffer, visitor)` - Type dispatch with visitor pattern
- `MyApp::dispatch(buffer, overload_set)` - Type dispatch with overload set
- `MyApp::max_message_size` - Maximum message size constant
- `MyApp::payload_types` - Type alias to tuple of all payload types

**Header:** `<commrat/commrat.hpp>`

---

## Module Base Class

### Module<OutputSpec, InputSpec, ...CommandTypes>

Base class for all CommRaT modules. Handles mailbox management, threading, subscription protocol, and message dispatch.

```cpp
template<typename UserRegistry,
         typename OutputSpec,
         typename InputSpec,
         typename... CommandTypes>
class Module;
```

**Template Parameters:**
- `OutputSpec` - Output specification: `Output<T>`, `Outputs<T, U, ...>`, or `NoOutput`
- `InputSpec` - Input specification: `Input<T>`, `Inputs<T, U, ...>`, `PeriodicInput`, or `LoopInput`
- `CommandTypes...` - Optional command types handled by this module

**Pure Virtual Methods:**

User must override the appropriate `process()` method based on I/O specification:

```cpp
// PeriodicInput with single output
void process(T& output) override;

// PeriodicInput with multiple outputs
void process(T1& out1, T2& out2, ...) override;

// Input<T> with single output
void process(const T& input, U& output) override;

// Input<T> with multiple outputs
void process(const T& input, U1& out1, U2& out2, ...) override;

// Inputs<T, U, V> with single output
void process(const T& in1, const U& in2, const V& in3, W& output) override;

// Inputs<T, U, V> with multiple outputs
void process(const T& in1, const U& in2, const V& in3, W1& out1, W2& out2, ...) override;

// LoopInput with single output (no blocking, continuous)
void process(T& output) override;
```

**Lifecycle Methods:**
```cpp
void start();  // Start module threads
void stop();   // Stop module threads gracefully
```

**Command Handling:**
```cpp
// Override for each command type
virtual void on_command(const CommandType& cmd);
```

**Protected Members:**
```cpp
const ModuleConfig& config_;  // Module configuration
```

**Header:** `<commrat/registry_module.hpp>`

**Example:**
```cpp
class SensorModule : public MyApp::Module<
    Output<SensorData>,
    PeriodicInput
> {
protected:
    void process(SensorData& output) override {
        output = SensorData{.value = read_sensor()};
    }
};
```

---

## I/O Specifications

### Output Specifications

**Output<T>** - Single output type
```cpp
template<typename T>
struct Output;
```

**Outputs<Ts...>** - Multiple output types
```cpp
template<typename... Ts>
struct Outputs;
```

**NoOutput** - No output (sink module)
```cpp
struct NoOutput;
```

**Example:**
```cpp
// Single output
Module<Output<Data>, Input<Sensor>>

// Multiple outputs
Module<Outputs<Raw, Filtered, Stats>, PeriodicInput>

// No output (sink)
Module<NoOutput, Input<LogData>>
```

### Input Specifications

**PeriodicInput** - Timer-driven execution
```cpp
struct PeriodicInput;
```
- Executes `process()` at fixed intervals
- Period specified in `ModuleConfig::period`
- No input parameters to `process()`

**LoopInput** - Maximum throughput execution
```cpp
struct LoopInput;
```
- Executes `process()` as fast as possible
- No blocking, continuous loop
- Use for computation-heavy modules

**Input<T>** - Single continuous input
```cpp
template<typename T>
struct Input;
```
- Blocks on `receive()` for messages of type T
- `process(const T& input, OutputType& output)` called for each message
- Automatic subscription to source module

**Inputs<Ts...>** - Multiple synchronized inputs
```cpp
template<typename... Ts>
struct Inputs;
```
- First type is primary (blocking receive)
- Secondary inputs synchronized via `getData(primary_timestamp)`
- `process(const T1& in1, const T2& in2, ..., OutputType& output)` called when primary receives

**Example:**
```cpp
// Periodic generation
Module<Output<Data>, PeriodicInput>

// Event-driven processing
Module<Output<Filtered>, Input<Raw>>

// Multi-sensor fusion
Module<Output<Fused>, Inputs<IMU, GPS, Lidar>>

// Maximum throughput
Module<Output<Result>, LoopInput>
```

**Header:** `<commrat/io_spec.hpp>`

---

## Metadata Accessors

Modules with inputs can access metadata about received messages.

### Index-Based Access

Works for any number of inputs:

```cpp
// Get full metadata structure
auto meta = get_input_metadata<0>();  // Primary input
auto meta = get_input_metadata<1>();  // Secondary input

// Convenience accessors
uint64_t ts = get_input_timestamp<0>();
bool fresh = has_new_data<1>();
bool valid = is_input_valid<2>();
```

### Type-Based Access

Works when input types are unique (limited to first 2 inputs currently):

```cpp
// Get metadata by type
auto imu_meta = get_input_metadata<IMUData>();
uint64_t gps_ts = get_input_timestamp<GPSData>();
bool fresh = has_new_data<LidarData>();
```

**Note:** Type-based access limited to first 2 input types due to tuple unpacking implementation. Use index-based access for 3+ inputs.

### InputMetadata Structure

```cpp
template<typename T>
struct InputMetadata {
    uint64_t timestamp;          // From TimsHeader (nanoseconds since epoch)
    uint32_t sequence_number;    // Message sequence number
    uint32_t message_id;         // Message type ID
    bool is_new_data;            // True if fresh, false if reused from history
    bool is_valid;               // True if getData succeeded, false if failed
};
```

**Example:**
```cpp
class FusionModule : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>
> {
protected:
    void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
        auto imu_meta = get_input_metadata<0>();
        auto gps_meta = get_input_metadata<1>();
        
        if (!gps_meta.is_new_data) {
            std::cout << "GPS data is stale\n";
        }
        
        output = fuse_sensors(imu, gps);
    }
};
```

**Header:** `<commrat/module/metadata/input_metadata_accessors.hpp>`

---

## Configuration

### ModuleConfig

Configuration structure for module initialization.

```cpp
struct ModuleConfig {
    const char* name;                    // Module name (for logging)
    uint8_t system_id;                   // System ID (0-255)
    uint8_t instance_id;                 // Instance ID within system (0-255)
    
    // For periodic modules
    Duration period{Milliseconds(0)};    // Execution period
    
    // For input modules (single source)
    uint8_t source_system_id{0};
    uint8_t source_instance_id{0};
    uint32_t source_primary_output_type_id{0};  // For multi-output filtering
    
    // For multi-input modules
    struct InputSource {
        uint8_t system_id;
        uint8_t instance_id;
        Duration requested_period{Milliseconds(0)};
    };
    sertial::fixed_vector<InputSource, 8> input_sources;
    
    // Multi-input synchronization
    uint64_t sync_tolerance_ns{100'000'000};  // 100ms default
};
```

**Example:**
```cpp
// Periodic producer
ModuleConfig sensor_config{
    .name = "Sensor",
    .system_id = 10,
    .instance_id = 1,
    .period = Milliseconds(100)  // 10 Hz
};

// Single-input consumer
ModuleConfig filter_config{
    .name = "Filter",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1
};

// Multi-input fusion
ModuleConfig fusion_config{
    .name = "Fusion",
    .system_id = 30,
    .instance_id = 1,
    .period = Milliseconds(10),
    .input_sources = {
        {.system_id = 10, .instance_id = 1},  // IMU
        {.system_id = 11, .instance_id = 1}   // GPS
    },
    .sync_tolerance_ns = 50'000'000  // 50ms
};
```

**Header:** `<commrat/registry_module.hpp>`

---

## Threading Abstractions

CommRaT provides platform-agnostic threading primitives for portability and future real-time platform support (e.g., libevl).

### Thread

Wrapper around `std::thread` (or platform equivalent).

```cpp
class Thread {
public:
    template<typename Func>
    Thread(Func&& func);
    
    void join();
    void detach();
};
```

### Synchronization Primitives

```cpp
class Mutex;              // std::mutex wrapper
class SharedMutex;        // std::shared_mutex wrapper
class Lock;               // std::lock_guard wrapper
class SharedLock;         // std::shared_lock wrapper
```

**Example:**
```cpp
Mutex mutex_;
std::vector<int> data_;

void add_data(int value) {
    Lock lock(mutex_);
    data_.push_back(value);
}
```

**Header:** `<commrat/threading.hpp>`

**Note:** Always use CommRaT abstractions instead of `std::` types directly to enable future platform-specific implementations.

---

## Timestamp Abstractions

Platform-agnostic time and duration types.

### Timestamp

```cpp
using Timestamp = std::chrono::steady_clock::time_point;
```

### Duration

```cpp
using Duration = std::chrono::nanoseconds;
using Nanoseconds = std::chrono::nanoseconds;
using Microseconds = std::chrono::microseconds;
using Milliseconds = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;
```

### Time Utilities

```cpp
class Time {
public:
    static Timestamp now();
    static void sleep(Duration duration);
};
```

**Example:**
```cpp
auto start = Time::now();
// ... do work ...
auto end = Time::now();
auto elapsed = std::chrono::duration_cast<Milliseconds>(end - start);

Time::sleep(Milliseconds(100));
```

**Header:** `<commrat/timestamp.hpp>`

---

## Mailbox Interface

Low-level mailbox interface (typically not used directly - Module class handles this).

### Mailbox<Registry>

```cpp
template<typename Registry>
class Mailbox {
public:
    explicit Mailbox(uint32_t address);
    
    // Type-safe send/receive
    template<typename T>
    auto send(const T& message, uint32_t dest_address) -> MailboxResult<void>;
    
    template<typename T>
    auto receive() -> MailboxResult<ReceivedMessage<T>>;
    
    // Visitor-based polymorphic receive
    template<typename Visitor>
    auto receive_any(Visitor&& visitor) -> MailboxResult<void>;
};
```

### ReceivedMessage<T>

```cpp
template<typename T>
struct ReceivedMessage {
    TimsHeader header;
    T message;
};
```

### TimsHeader

```cpp
struct TimsHeader {
    uint64_t timestamp;
    uint32_t sequence_number;
    uint32_t message_id;
    uint8_t priority;
    uint8_t flags;
    uint16_t source_system_id;
    uint16_t source_instance_id;
    // ... additional fields
};
```

**Header:** `<commrat/mailbox.hpp>`

**Note:** Most users work with `Module` class and never directly use mailboxes.

---

## Message Definitions

### Message Namespace

Helper types for defining messages in the registry:

```cpp
namespace Message {
    // Data message (user data types)
    template<typename T>
    struct Data;
    
    // Command message (module commands)
    template<typename T>
    struct Command;
}
```

**Usage:**
```cpp
struct TemperatureData { float temp; };
struct ResetCmd { bool clear_state; };

using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Command<ResetCmd>
>;
```

### System Messages

Automatically included in every registry:

```cpp
enum class SystemMessages : uint32_t {
    SubscribeRequest   = 0x00000001,
    SubscribeReply     = 0x00000002,
    UnsubscribeRequest = 0x00000003,
    UnsubscribeReply   = 0x00000004
};
```

**Header:** `<commrat/messages.hpp>`

---

## Serialization Integration

CommRaT uses SeRTial for automatic serialization. Your message types must be SeRTial-compatible (POD structs work automatically).

### Requirements

Message types must:
- Be POD (Plain Old Data) or SeRTial-serializable
- Have default constructors
- Use bounded containers: `sertial::fixed_vector<T, N>`, `sertial::fixed_string<N>`
- Avoid `std::vector`, `std::string` in real-time paths

**Valid:**
```cpp
struct SensorData {
    float value;
    uint64_t timestamp;
    sertial::fixed_vector<float, 10> history;
};
```

**Invalid (dynamic allocation):**
```cpp
struct BadData {
    std::vector<float> values;  // Dynamic allocation!
    std::string name;           // Dynamic allocation!
};
```

---

## Address Calculation

Modules have hierarchical addressing:

```cpp
// Base address calculation
uint32_t base = calculate_base_address(system_id, instance_id);

// Mailbox offsets (per output type)
uint32_t cmd_mbx  = base + output_index * 16 + 0;   // Command
uint32_t work_mbx = base + output_index * 16 + 4;   // Subscription protocol
uint32_t pub_mbx  = base + output_index * 16 + 8;   // Publish control

// Shared DATA mailbox (all inputs)
uint32_t data_mbx = base + 12;  // Fixed offset
```

**Note:** Address calculation is handled internally by Module class.

---

## Error Handling

CommRaT uses `std::optional` and result types for error handling (no exceptions in real-time paths).

```cpp
template<typename T>
using MailboxResult = std::optional<T>;
```

**Example:**
```cpp
auto result = mailbox.receive<SensorData>();
if (result) {
    auto& msg = result->message;
    process(msg);
} else {
    // Receive failed
}
```

---

## Compile-Time Features

### Message ID Calculation

Message IDs computed at compile time:

```cpp
constexpr uint32_t id = MyApp::get_message_id<TemperatureData>();
static_assert(id == expected_id, "Message ID mismatch");
```

### Type Safety

All type mismatches caught at compile time:

```cpp
// ERROR: Wrong input type - won't compile
class BadModule : public MyApp::Module<
    Output<DataA>,
    Input<DataB>  // If DataB not in registry
> {};
```

### Zero Overhead

All registry lookups and type dispatch resolved at compile time - no runtime cost.

---

## Introspection System

### MyApp::Introspection

Helper class for exporting message schemas (CommRaT metadata + SeRTial layout).

**Header:** `<commrat/introspection.hpp>`

#### export_as<T, Writer>()

Export complete schema for a single message type.

```cpp
template<typename T, typename Writer = rfl::json::Writer>
static std::string export_as();
```

**Returns:** Formatted string containing:
- **CommRaT metadata**: message_id, payload_type, full_type, max_message_size, registry_name
- **SeRTial layout**: Full `TimsMessage<T>` structure (header + payload) with field names, types, sizes, offsets
- **JSON schema**: Embedded schema in `type_schema` field

**Example:**
```cpp
// Export to JSON (default)
auto json = MyApp::Introspection::export_as<TemperatureData>();

// Export to YAML
auto yaml = MyApp::Introspection::export_as<TemperatureData, rfl::yaml::Writer>();
```

**Output structure:**
```json
{
  "commrat": {
    "message_id": 16777219,
    "payload_type": "TemperatureData",
    "full_type": "commrat::TimsMessage<TemperatureData>",
    "max_message_size": 104,
    "registry_name": "MyApp"
  },
  "layout": {
    "name": "commrat::TimsMessage<TemperatureData>",
    "sizeof_bytes": 40,
    "base_packed_size": 40,
    "max_packed_size": 40,
    "has_variable_fields": false,
    "field_count": 2,
    "field_names": ["header", "payload"],
    "field_types": ["commrat::TimsHeader", "TemperatureData"],
    "field_sizes": [24, 16],
    "field_offsets": [0, 24],
    "type_schema": "{...embedded JSON schema...}"
  }
}
```

#### export_all<Writer>()

Export schemas for all registered message types.

```cpp
template<typename Writer = rfl::json::Writer>
static std::string export_all();
```

**Returns:** JSON array of MessageSchema objects (one per registered type)

**Example:**
```cpp
auto all_schemas = MyApp::Introspection::export_all();
std::cout << all_schemas;  // Prints JSON array
```

#### write_to_file<Writer>(filename)

Convenience method to write all schemas to a file.

```cpp
template<typename Writer = rfl::json::Writer>
static void write_to_file(const std::string& filename);
```

**Example:**
```cpp
// Write JSON schemas
MyApp::Introspection::write_to_file("schemas.json");

// Write YAML schemas
MyApp::Introspection::write_to_file<rfl::yaml::Writer>("schemas.yaml");
```

### MessageSchema<PayloadT, Registry>

Complete schema structure combining CommRaT and SeRTial metadata.

```cpp
template<typename PayloadT, typename Registry>
struct MessageSchema {
    struct CommRaTMetadata {
        uint32_t message_id;
        std::string payload_type;
        std::string full_type;
        size_t max_message_size;
        std::string registry_name;
    } commrat;
    
    sertial::StructLayout<TimsMessage<PayloadT>> layout;
};
```

**Direct usage:**
```cpp
using Schema = commrat::MessageSchema<TemperatureData, MyApp>;

// Access metadata at compile time
constexpr auto msg_id = Schema{}.commrat.message_id;

// Access layout information
constexpr auto num_fields = Schema{}.layout.field_count;
```

**Use Cases:**
- Generic logger/replay tools (type-agnostic logging)
- Web-based message viewers (display schemas)
- JSON configuration validation (check field names/types)
- Documentation generation (auto-generate API docs)
- ROS 2 adapter (map CommRaT ↔ ROS message types)

---

## See Also

- [User Guide](USER_GUIDE.md) - Comprehensive framework guide
- [Getting Started](GETTING_STARTED.md) - First application tutorial
- [Architecture](ARCHITECTURE.md) - Design decisions and internals
- [Introspection Example](../examples/introspection_example.cpp) - Complete working example
- [Examples](../examples/) - Working code examples
- [Doxygen Docs](api/html/index.html) - Generated API documentation (after `make docs`)
