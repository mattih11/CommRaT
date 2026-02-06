# Ultra-Clean User Interface

**Status**: ✅ RUNTIME VALIDATED (February 4, 2026)

**Examples Tested**:
- ✅ `continuous_input_example.cpp` - Producer→Consumer with auto-subscription
- ✅ `clean_interface_example.cpp` - Ultra-clean Module<> interface
- ✅ `command_example.cpp` - Variadic command handling

**All Features Validated**:
- ✅ Payload-only API (users never see MessageDefinition)
- ✅ System messages auto-included via CombinedRegistry
- ✅ Module<OutputData, InputMode> alias (registry hidden)
- ✅ Variadic CommandTypes with on_command() handlers
- ✅ Automatic subscription protocol
- ✅ Compile-time message ID generation (0xPSMM format)
- ✅ Type-safe serialization/deserialization

## The Complete User Experience

### File 1: Define Your Messages ONCE (`messages/user_messages.hpp`)

```cpp
#pragma once
#include <commrat/system_registry.hpp>

namespace user_app {

// 1. Define your payload structures
struct TemperatureData {
    float temperature_celsius{0.0f};
    uint64_t timestamp_ms{0};
};

struct PoseData {
    float x{0.0f}, y{0.0f}, theta{0.0f};
};

// 2. Create registry with ONLY your messages
//    System messages (subscription protocol) included automatically!
using AppMessageRegistry = commrat::CombinedRegistry<
    commrat::MessageDefinition<TemperatureData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<PoseData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>
>;

} // namespace user_app
```

### File 2: Create Type Aliases (`messages/messages.hpp`)

```cpp
#pragma once
#include "user_messages.hpp"
#include <commrat/commrat.hpp>

namespace user_app {

// Module template with registry pre-configured
template<typename OutputDataT, typename InputModeT>
using Module = commrat::Module<OutputDataT, InputModeT, AppMessageRegistry>;

// Mailbox with registry pre-configured
using Mailbox = commrat::RegistryMailbox<AppMessageRegistry>;

// Re-export for convenience
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ContinuousInput;
using commrat::ModuleConfig;

} // namespace user_app
```

### File 3: Write Your Module (`sensor_module.cpp`)

```cpp
#include "messages/messages.hpp"  // ONE INCLUDE!

using namespace user_app;

// Ultra-clean module definition
class SensorModule : public Module<TemperatureData, PeriodicInput> {
protected:
    TemperatureData process() override {
        return TemperatureData{
            .temperature_celsius = read_sensor()
        };
    }
};

// Module with command handling (variadic CommandTypes)
class ActuatorModule : public Module<StatusData, LoopInput, ResetCmd, CalibrateCmd> {
protected:
    StatusData process() override {
        return get_status();
    }
    
    void on_command(const ResetCmd& cmd) override {
        perform_reset();
    }
    
    void on_command(const CalibrateCmd& cmd) override {
        calibrate_sensors();
    }
};

int main() {
    ModuleConfig config{.mailbox_id = 100, .period = 100ms};
    SensorModule sensor(config);
    sensor.start();
    // Module publishes TemperatureData automatically to subscribers
}
```

## What You See vs What You Don't

### ✅ What You Write:
```cpp
#include "messages/messages.hpp"

class SensorModule : public Module<TemperatureData, PeriodicInput> {
    TemperatureData process() { return TemperatureData{...}; }
};
```

### ❌ What You DON'T Write:
```cpp
// NO registry parameter in Module<>
Module<TemperatureData, PeriodicInput, AppMessageRegistry>  // ❌

// NO MessageDefinition in module code  
MessageDefinition<TemperatureData, ...>  // ❌

// NO TimsMessage wrappers
TimsMessage<TemperatureData>  // ❌

// NO manual message IDs
template<> struct message_type_for<TemperatureData> { ... };  // ❌

// NO subscription protocol imports
#include "subscription_messages.hpp"  // ❌
```

## The Magic: How It Works

```
User Code:
  Module<TemperatureData, PeriodicInput>
    ↓
messages.hpp alias:
  commrat::Module<TemperatureData, PeriodicInput, AppMessageRegistry>
    ↓
AppMessageRegistry = CombinedRegistry<User Messages...>
    ↓
CombinedRegistry automatically includes:
  - SubscribeRequest
  - SubscribeReply  
  - UnsubscribeRequest
  - UnsubscribeReply
  + Your user messages
    ↓
Module automatically:
  - Handles subscription protocol
  - Extracts message IDs from registry
  - Wraps/unwraps TimsMessage
  - Manages subscribers
```

## Comparison: Before vs After

### Old Way (Verbose)
```cpp
// Had to manually include system messages
using Registry = MessageRegistry<
    SubscribeRequest,      // Manual
    SubscribeReply,        // Manual
    UnsubscribeRequest,    // Manual
    UnsubscribeReply,      // Manual
    MessageDefinition<TempData, ...>,
    MessageDefinition<PoseData, ...>
>;

// Had to pass registry to Module
class Sensor : public Module<TempData, PeriodicInput, Registry> {
    TempData process() { ... }
};
```

### New Way (Clean)
```cpp
// System messages included automatically by CombinedRegistry
using Registry = CombinedRegistry<
    MessageDefinition<TempData, ...>,
    MessageDefinition<PoseData, ...>
>;

// Registry hidden in messages.hpp alias
class Sensor : public Module<TempData, PeriodicInput> {
    TempData process() { ... }
};
```

## File Structure

```
your_app/
├── messages/
│   ├── user_messages.hpp       # Define payloads + registry ONCE
│   └── messages.hpp            # Type aliases (Module, Mailbox)
│
└── modules/
    ├── sensor_module.cpp       # Just include messages/messages.hpp
    ├── filter_module.cpp       # Use Module<PayloadT, InputMode>
    └── actuator_module.cpp     # That's it!
```

## Summary

**You define:**
- Payload structs (plain data)
- One registry with your messages (data + commands)
- One alias file (boilerplate)

**You use:**
- `Module<PayloadT, InputMode>` (no commands)
- `Module<PayloadT, InputMode, Cmd1, Cmd2, ...>` (with commands)
- `process()` returns payload
- `process_continuous(const PayloadT&)` receives payload
- `on_command(const CmdT&)` handles commands

**You never see:**
- Registry in module code (hidden in alias)
- System messages (automatically included)
- MessageDefinition (only in registry definition)
- TimsMessage wrappers (internal)
- Message IDs (automatic)
- Manual command dispatch (type-safe visitor pattern)

**Result: Ultra-clean payload-centric code with variadic command support!**
