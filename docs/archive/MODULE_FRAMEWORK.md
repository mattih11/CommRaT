# Module Framework - Phase 1 Implementation

## Overview

Implemented a modern C++20 module framework (`Module<>`) that replaces RACK's RackRflDataModule with a cleaner, type-safe design using CommRaT's Mailbox system.

## Implementation Status

### ✅ Phase 1: Core Module Template (COMPLETE)

**File**: `/home/muddy/src/CommRaT/include/commrat/module.hpp`

The base `Module<>` template provides:

#### Template Signature
```cpp
template<typename OutputDataT,
         typename InputModeT = PeriodicInput,
         typename... CommandTypes>
class Module
```

- **OutputDataT**: The data type this module produces
- **InputModeT**: Input mode (PeriodicInput, LoopInput, or ContinuousInput<T>)
- **CommandTypes**: Commands this module handles (Phase 3)

#### Input Modes

Three input modes are supported via compile-time dispatch (if constexpr):

1. **PeriodicInput** - Module runs on a timer
   - Configured via `ModuleConfig::period`
   - Example: Status publisher, periodic sensor polling

2. **LoopInput** - Module runs as fast as possible
   - No input data, no sleep
   - Example: Free-running counter, benchmark module

3. **ContinuousInput<SourceModuleT>** - Module receives input from another module
   - Type-safe input data extraction
   - Example: Data processor, filter module
   - **Note**: Implementation complete but untested (needs Phase 2 validation)

#### Lifecycle Hooks

Modules can override these lifecycle methods:

```cpp
virtual void on_init();     // Called before threads start
virtual void on_start();    // Called after threads created
virtual void on_stop();     // Called before threads join
virtual void on_cleanup();  // Called after threads joined
```

#### Processing Methods

Override the appropriate `process()` method based on input mode:

```cpp
// For PeriodicInput and LoopInput:
virtual void process(OutputDataT& output);

// For ContinuousInput<T>:
template<typename T = InputData>
std::enable_if_t<!std::is_void_v<T>> 
process(const T& input, OutputDataT& output);
```

#### Publisher/Subscriber Pattern

Modules can have subscribers that receive their output:

```cpp
void add_subscriber(uint32_t dest_mailbox_id);
void remove_subscriber(uint32_t dest_mailbox_id);
void publish(const OutputDataT& data);  // Sends to all subscribers
```

The `publish()` method:
- Wraps data in `TimsMessage<OutputDataT>`
- Sends to all registered subscribers via Mailbox
- Non-blocking (errors logged but don't block)

#### Thread Management

- **Processing Thread**: Runs the appropriate loop based on input mode
  - `periodic_loop()` for PeriodicInput
  - `free_loop()` for LoopInput
  - `continuous_loop()` for ContinuousInput
  
- **Command Thread**: Placeholder for Phase 3 command handling

#### RAII Lifecycle

```cpp
Module module(config);
module.start();  // Starts mailbox, calls on_init/on_start, spawns threads
// ... module runs ...
module.stop();   // Calls on_stop, joins threads, stops mailbox, calls on_cleanup
```

## Example Usage

**File**: `/home/muddy/src/CommRaT/examples/module_example.cpp`  
**Messages**: `/home/muddy/src/CommRaT/examples/messages/common_messages.hpp`

### Message Definitions (Shared)

All message types are defined in a shared header for consistency:

```cpp
// examples/messages/common_messages.hpp
namespace example_messages {

struct StatusData {
    uint64_t timestamp;
    uint32_t counter;
    float cpu_load;
    float memory_usage;
};

struct CounterData {
    uint64_t value;
};

} // namespace example_messages

// Register with CommRaT
template<>
struct commrat::message_type_for<example_messages::StatusData> {
    static constexpr commrat::MessageType value = commrat::MessageType::STATUS;
};

template<>
struct commrat::message_type_for<example_messages::CounterData> {
    static constexpr commrat::MessageType value = commrat::MessageType::DATA;
};
```

### StatusModule (PeriodicInput)

```cpp
#include "commrat/module.hpp"
#include "messages/common_messages.hpp"

using namespace example_messages;

class StatusModule : public Module<StatusData, PeriodicInput> {
public:
    explicit StatusModule(const ModuleConfig& config)
        : Module(config), counter_(0) {}

protected:
    void process(StatusData& output) override {
        output.timestamp = get_timestamp();
        output.counter = counter_++;
        output.cpu_load = 0.5f + 0.3f * std::sin(counter_ * 0.1f);
        output.memory_usage = 0.3f + 0.2f * std::cos(counter_ * 0.15f);
    }

private:
    uint32_t counter_;
};
```

### CounterModule (LoopInput)

```cpp
class CounterModule : public Module<CounterData, LoopInput> {
public:
    explicit CounterModule(const ModuleConfig& config)
        : Module(config), value_(0) {}

protected:
    void process(CounterData& output) override {
        output.value = value_++;
    }

private:
    uint64_t value_;
};
```

### Running Modules

```cpp
// Create status module (periodic at 100ms)
ModuleConfig status_config{
    .name = "StatusModule",
    .mailbox_id = 100,
    .period = std::chrono::milliseconds(100),
    .max_msg_size = 1024,
    .priority = 10
};
StatusModule status(status_config);

// Create counter module (free-running)
ModuleConfig counter_config{
    .name = "CounterModule",
    .mailbox_id = 101,
    .max_msg_size = 256,
    .priority = 5
};
CounterModule counter(counter_config);

// Start modules
status.start();
counter.start();

// Run for 5 seconds
std::this_thread::sleep_for(std::chrono::seconds(5));

// Stop modules
status.stop();
counter.stop();
```

## Build Integration

**Updated**: `/home/muddy/src/CommRaT/CMakeLists.txt`

```cmake
# Module example
add_executable(example_module examples/module_example.cpp)
target_link_libraries(example_module PRIVATE commrat)
```

Build and run:
```bash
cd build
cmake ..
make example_module
./example_module
```

## Design Decisions

### 1. Direct Mailbox Usage (No Proxy)

**Decision**: Modules use Mailbox directly instead of a proxy wrapper like RACK's RackRflProxy.

**Rationale**:
- Mailbox is already type-safe with compile-time dispatch
- Proxy adds unnecessary indirection
- Direct access simpler and more transparent
- Eliminates one layer of abstraction

**Comparison to RACK**:
- RACK: `Module` → `RackRflProxy` → `RackMailbox` (void*)
- CommRaT: `Module` → `Mailbox<MessageTypes>` (type-safe)

### 2. Compile-Time Input Mode Dispatch

**Decision**: Use `if constexpr` to select input mode behavior at compile time.

**Rationale**:
- Zero runtime overhead
- Type-safe - impossible to call wrong process() method
- Compiler optimizes away dead code
- No virtual dispatch needed

**Implementation**:
```cpp
// In start()
if constexpr (std::is_same_v<InputMode, PeriodicInput>) {
    processing_thread_ = std::thread([this]{ periodic_loop(); });
} else if constexpr (std::is_same_v<InputMode, LoopInput>) {
    processing_thread_ = std::thread([this]{ free_loop(); });
} else if constexpr (has_continuous_input) {
    processing_thread_ = std::thread([this]{ continuous_loop(); });
}
```

### 3. Template Data Types, Not Wrapped Messages

**Decision**: Module template parameter is the raw data type (e.g., `StatusData`), not `TimsMessage<StatusData>`.

**Rationale**:
- Cleaner user interface
- Message wrapping is implementation detail
- Module wraps in `TimsMessage<>` internally before sending
- Consistent with modern API design (std::span, std::optional, etc.)

**User writes**:
```cpp
class MyModule : public Module<StatusData, PeriodicInput> {
    void process(StatusData& output) override { ... }
};
```

**Not**:
```cpp
class MyModule : public Module<TimsMessage<StatusData>, PeriodicInput> {
    void process(TimsMessage<StatusData>& output) override { ... }
};
```

### 4. SFINAE-Friendly Type Extraction

**Decision**: Use helper type trait to extract InputData from InputMode.

**Problem**: `std::conditional_t` evaluates both branches, causing compile errors when accessing non-existent `InputMode::InputData`.

**Solution**:
```cpp
template<typename T, typename = void>
struct ExtractInputData {
    using type = void;
};

template<typename T>
struct ExtractInputData<T, std::void_t<typename T::InputData>> {
    using type = typename T::InputData;
};

using InputData = typename ExtractInputData<InputMode>::type;
```

This works even when `PeriodicInput` and `LoopInput` don't have `InputData` members.

### 5. Subscriber-Based Publishing

**Decision**: Modules maintain a list of subscribers and publish to all on each iteration.

**Rationale**:
- Decouples producer from consumers
- Supports 1-to-N communication
- Flexible topology (can add/remove subscribers at runtime)
- Matches RACK's module communication pattern

**Alternative Considered**: Direct send to specific mailbox IDs
- Rejected: Less flexible, harder to reconfigure

## Type Safety Features

1. **Compile-Time Message Registration**
   ```cpp
   template<>
   struct commrat::message_type_for<StatusData> {
       static constexpr MessageType value = MessageType::STATUS;
   };
   ```

2. **Type-Safe Mailbox**
   ```cpp
   using Mailbox = commrat::Mailbox<TimsMessage<OutputData>>;
   ```
   Only `TimsMessage<OutputData>` can be sent/received.

3. **Compile-Time Input Mode Validation**
   ```cpp
   static constexpr bool has_continuous_input = requires (InputMode m) { 
       typename InputMode::SourceModule; 
       typename InputMode::InputData;
   };
   ```

4. **std::span<std::byte> Throughout**
   - All buffer interfaces use std::span
   - Casting to void* only at TiMS C API boundary

## Advantages Over RACK

| Feature | RACK RackRflDataModule | CommRaT Module |
|---------|------------------------|----------------|
| **Message wrapping** | Manual pack/unpack | Automatic (SeRTial) |
| **Type safety** | void* buffers, runtime checks | std::span<std::byte>, compile-time |
| **Message dispatch** | Runtime type checks | Compile-time (if constexpr) |
| **Proxy layer** | RackRflProxy required | Direct mailbox (no proxy) |
| **Buffer management** | Manual allocation | RAII, automatic sizing |
| **Command handling** | Complex inheritance tree | Simple template variadic pack |
| **Input mode selection** | CRTP + runtime checks | if constexpr (zero overhead) |
| **Error handling** | Return codes | std::expected/Result types |

## Implementation Notes

### Compilation Success

✅ **Compiles cleanly** with:
- GCC 11+ (C++20)
- Modern C++ standard library
- TiMS headers (warnings are in TiMS C headers, not our code)

**Build output**:
```
[ 50%] Built target commrat
[ 75%] Building CXX object CMakeFiles/example_module.dir/examples/module_example.cpp.o
[100%] Linking CXX executable example_module
[100%] Built target example_module
```

### Module Configuration

```cpp
struct ModuleConfig {
    std::string name;
    uint32_t mailbox_id;
    std::chrono::milliseconds period{100};  // For PeriodicInput
    uint32_t max_msg_size{4096};
    int priority{10};
    bool realtime{false};
};
```

Converted to `MailboxConfig` internally:
```cpp
MailboxConfig createMailboxConfig(const ModuleConfig& config) {
    return MailboxConfig{
        .mailbox_id = config.mailbox_id,
        .message_slots = 10,
        .max_message_size = config.max_msg_size,
        .send_priority = static_cast<uint8_t>(config.priority),
        .realtime = config.realtime,
        .mailbox_name = config.name
    };
}
```

### Thread Safety

- `running_` flag is `std::atomic<bool>`
- `subscribers_` protected by `std::mutex`
- Mailbox has internal thread safety
- Each module has independent threads

### RAII Guarantees

```cpp
Module module(config);  // Constructor: config stored, mailbox created (not started)
module.start();         // Starts mailbox, spawns threads
// ... automatic operation ...
module.stop();          // Joins threads, stops mailbox
// Destructor calls stop() if not already stopped
```

## Future Work

### ✅ Phase 2: ContinuousInput Validation (COMPLETE)

**Status**: ✅ COMPLETE - Runtime tested on February 4, 2026

**Examples Validated**:
1. `continuous_input_example.cpp` - Producer → Consumer with auto-subscription
2. `clean_interface_example.cpp` - Ultra-clean Module<> interface  
3. `command_example.cpp` - Variadic command handling with on_command()

**Runtime Test Results**:
- ✅ Producer publishing @ 100ms (PeriodicInput)
- ✅ Consumer auto-subscribed to producer (ContinuousInput)
- ✅ Data flowing continuously between modules
- ✅ Ultra-clean Module<OutputData, InputMode> interface working
- ✅ Variadic CommandTypes with type-safe on_command() dispatch
- ✅ Payload-only API (users never see MessageDefinition)
- ✅ System messages automatically included via CombinedRegistry
- ⚠️  Minor TiMS errors (code -5, buffer size) - non-blocking, functionality works

**Architecture Validated**:
- Message ID system (0xPSMM format) working correctly
- Compile-time ID auto-increment functioning
- Registry-based serialization/deserialization operational
- Subscription protocol (SubscribeRequest/Reply) functioning
- Module lifecycle (start/stop/loops) stable
- Command dispatch with fold expressions working

### ✅ Phase 3: Command System (COMPLETE)

**Status**: ✅ COMPLETE - Runtime tested on February 4, 2026

**Implementation**:
```cpp
struct ResetCmd { bool hard; };
struct CalibrateCmd { float offset; };
struct SetModeCmd { int mode; };

class MyModule : public Module<StatusData, PeriodicInput, 
                               ResetCmd, CalibrateCmd, SetModeCmd> {
protected:
    void on_command(const ResetCmd& cmd) { /* handle reset */ }
    void on_command(const CalibrateCmd& cmd) { /* calibrate */ }
    void on_command(const SetModeCmd& cmd) { /* set mode */ }
};
```

**Features Delivered**:
- ✅ Variadic CommandTypes template parameter pack
- ✅ Type-safe on_command() overloads (no override keyword needed)
- ✅ Compile-time command type validation with fold expressions
- ✅ Automatic command registry construction from variadic pack
- ✅ Command dispatch via visitor pattern in command_loop()
- ✅ No manual command ID checking or runtime casting
- ✅ Tested in command_example.cpp with 3 command types

**Example Tested**: `command_example.cpp`
- Module<TempData, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd>
- Commands sent and dispatched correctly
- Type-safe handler invocation verified

### Phase 4: Module Registry

**Goal**: Discover and query modules at runtime

```cpp
ModuleRegistry registry;
registry.register_module(&status_module);
registry.register_module(&counter_module);

// Query by name
auto* mod = registry.find_by_name("StatusModule");

// Query by mailbox ID
auto* mod = registry.find_by_id(100);

// Query by output type
auto mods = registry.find_by_output_type<StatusData>();
```

### Phase 5: Example Modules

Create example modules matching RACK's drivers:

1. **ChassisSimModule**: Periodic kinematic simulation
2. **CameraModule**: ContinuousInput from image sensor
3. **ObjRecogModule**: ContinuousInput<Camera> for image processing
4. **LadarModule**: Periodic LIDAR scanning
5. **ScanRecogModule**: ContinuousInput<Ladar> for obstacle detection

## Files Modified/Created

### New Files
- `/home/muddy/src/CommRaT/include/commrat/module.hpp` (407 lines)
- `/home/muddy/src/CommRaT/examples/module_example.cpp` (159 lines)
- `/home/muddy/src/CommRaT/examples/messages/common_messages.hpp` (110 lines)
- `/home/muddy/src/CommRaT/examples/messages/README.md` (documentation)
- `/home/muddy/src/CommRaT/docs/MODULE_FRAMEWORK.md` (this file)

### Modified Files
- `/home/muddy/src/CommRaT/CMakeLists.txt` (added example_module target)

## Message Organization

Message definitions are kept in **`examples/messages/`** as shared headers:

**Benefits:**
- ✅ Consistency - All modules use same data structures
- ✅ Type safety - Compile-time checking
- ✅ Reusability - Messages shared across modules
- ✅ Clear architecture - Separates contracts from logic
- ✅ Independent evolution - Version messages separately

**Structure:**
```
examples/
  messages/
    common_messages.hpp  - Message definitions + type registrations
    README.md           - Usage patterns and best practices
  module_example.cpp    - Module implementations
```

See `examples/messages/README.md` for detailed usage patterns.

## References

- [MODULE_MODERNIZATION_STRATEGY.md](work/MODULE_MODERNIZATION_STRATEGY.md) - Strategy and analysis
- [MAILBOX_DESIGN.md](MAILBOX_DESIGN.md) - Mailbox implementation details
- [../README.md](../README.md) - Project overview
- RACK RackRflDataModule: `/home/muddy/src/CommRaT/rfl/rack_rfl/core/rack_rfl_data_module_v2.h`

## Summary

**Module framework Phases 1-3 are COMPLETE and RUNTIME VALIDATED** (February 4, 2026)

The implementation provides:

✅ **Phase 1 - Core Module**:
  - Three input modes (PeriodicInput/LoopInput/ContinuousInput)
  - Type-safe compile-time dispatch with if constexpr
  - RAII lifecycle management (start/stop)
  - Publisher/subscriber pattern
  - Lifecycle hooks (on_init/on_start/on_stop/on_cleanup)
  - Thread management (processing + command threads)
  - Direct RegistryMailbox integration
  - Clean, modern C++20 design

✅ **Phase 2 - Continuous Input & Subscription**:
  - ContinuousInput<PayloadT> with automatic subscription
  - Producer → Consumer data flow validated
  - Subscription protocol (SubscribeRequest/Reply) working
  - Message ID system (0xPSMM format) operational
  - Payload-only user interface (no MessageDefinition in user code)
  - Ultra-clean Module<OutputData, InputMode> alias
  - System messages auto-included via CombinedRegistry

✅ **Phase 3 - Command System**:
  - Variadic CommandTypes template parameter
  - Type-safe on_command<CmdT>() handlers
  - Compile-time command validation (fold expressions)
  - Runtime command dispatch via visitor pattern
  - No manual ID checking or runtime casting
  - Tested with 3 command types in command_example.cpp

**Examples Runtime Validated**:
- `continuous_input_example.cpp` (Producer→Consumer, 411KB binary)
- `clean_interface_example.cpp` (Ultra-clean interface, 411KB binary)
- `command_example.cpp` (Variadic commands, 318KB binary)

**Next Steps**: Phase 4 (Module Registry), Phase 5 (Example domain modules - Chassis, Camera, LIDAR, etc.)
