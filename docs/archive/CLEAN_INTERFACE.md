# Clean User Interface Architecture

**Status**: ✅ RUNTIME VALIDATED (February 4, 2026)

**Implementation**: All features tested and working in production examples:
- `continuous_input_example.cpp` (Producer→Consumer)
- `clean_interface_example.cpp` (Minimal interface)
- `command_example.cpp` (Command handling)

## Design Philosophy

**Users define their message registry ONCE, then use payload types everywhere.**

No need to repeat `MessageDefinition<...>` in module declarations, send/receive calls, or processing functions. The message ID system is completely hidden from user code.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  User Code (Application Layer)                               │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 1. Define Registry ONCE (user_messages.hpp)         │    │
│  │    using AppMessageRegistry = MessageRegistry<       │    │
│  │      SubscribeRequest,  // Framework messages        │    │
│  │      MessageDefinition<TempData, ...>,  // User msgs│    │
│  │      MessageDefinition<PoseData, ...>               │    │
│  │    >;                                                │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ 2. Use Payload Types ONLY                           │    │
│  │    class SensorModule                               │    │
│  │      : public Module<TempData,  // Payload only!    │    │
│  │                      PeriodicInput,                  │    │
│  │                      AppMessageRegistry> {          │    │
│  │      TempData process() { return TempData{...}; }   │    │
│  │    };                                                │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  Framework Layer (Hidden from User)                          │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ RegistryMailbox<AppMessageRegistry>                 │    │
│  │   - Extracts MessageDefinitions from registry       │    │
│  │   - Exposes only payload types in API               │    │
│  │   - send(TempData&, dest_id)  // Payload type!      │    │
│  │   - receive<TempData>()       // Payload type!      │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ MessageRegistry<MessageDefs...>                     │    │
│  │   - Extracts message IDs at compile-time            │    │
│  │   - Auto-increments IDs within categories           │    │
│  │   - Detects ID collisions at compile-time           │    │
│  │   - get_message_id<TempData>() → 0x01000001         │    │
│  └─────────────────────────────────────────────────────┘    │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ MessageDefinition<PayloadT, Prefix, SubPrefix, ID>  │    │
│  │   - Compile-time metadata (never instantiated)      │    │
│  │   - ID format: 0xPSMM (4 bytes)                     │    │
│  │   - P: Prefix (System=0x00, UserDefined=0x01+)      │    │
│  │   - S: SubPrefix (Data, Commands, Events, etc.)     │    │
│  │   - MM: Message ID (auto-increment or explicit)     │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  Runtime Layer                                               │
│  ┌─────────────────────────────────────────────────────┐    │
│  │ TimsMessage<PayloadT>                                │    │
│  │   struct { TimsHeader header; PayloadT payload; }   │    │
│  │   - Serialized by SeRTial                           │    │
│  │   - Sent via TiMS                                   │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

## Step-by-Step User Workflow

### Step 1: Define Your Payload Types

Just plain data structures - no message metadata!

```cpp
// examples/messages/user_messages.hpp

struct TemperatureData {
    float temperature_celsius{0.0f};
    uint64_t timestamp_ms{0};
};

struct PoseData {
    float x{0.0f};
    float y{0.0f};
    float theta{0.0f};
};
```

### Step 2: Create Your Application Registry (ONCE!)

Define ALL messages in one place. Framework messages + your messages.

```cpp
// examples/messages/user_messages.hpp

using AppMessageRegistry = commrat::MessageRegistry<
    // Framework subscription protocol (must include these)
    commrat::SubscribeRequest,
    commrat::SubscribeReply,
    commrat::UnsubscribeRequest,
    commrat::UnsubscribeReply,
    
    // Your application messages (auto-increment IDs within category)
    commrat::MessageDefinition<TemperatureData, 
                               commrat::MessagePrefix::UserDefined, 
                               commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<PoseData,
                               commrat::MessagePrefix::UserDefined,
                               commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<StatusData,
                               commrat::MessagePrefix::UserDefined,
                               commrat::UserSubPrefix::Data>
>;
```

**That's it! You never need to reference `MessageDefinition` again.**

### Step 3: Create Modules Using Payload Types Only

```cpp
// Notice: Only TemperatureData (payload type), no MessageDefinition!
class SensorModule : public Module<TemperatureData,      // Output payload type
                                    PeriodicInput,         // Input mode
                                    AppMessageRegistry> {  // Your registry
protected:
    // Return payload type directly
    TemperatureData process() override {
        return TemperatureData{
            .temperature_celsius = read_sensor(),
            .timestamp_ms = get_timestamp()
        };
    }
};
```

### Step 4: Consumer Modules Also Use Payload Types

```cpp
// ContinuousInput<PayloadType> - uses payload type directly!
class FilterModule : public Module<TemperatureData,
                                    ContinuousInput<TemperatureData>,  // Payload type!
                                    AppMessageRegistry> {
protected:
    // Receives payload type, returns payload type
    TemperatureData process_continuous(const TemperatureData& input) override {
        float filtered = moving_average(input.temperature_celsius);
        return TemperatureData{
            .temperature_celsius = filtered,
            .timestamp_ms = input.timestamp_ms
        };
    }
};
```

### Step 5: Manual Send/Receive Also Uses Payload Types

```cpp
// Create mailbox from registry
RegistryMailbox<AppMessageRegistry> mbx(config);
mbx.start();

// Send payload type directly - message ID automatic!
TemperatureData data{.temperature_celsius = 25.5f};
mbx.send(data, dest_mailbox_id);  // Payload type only!

// Receive payload type directly
auto result = mbx.receive<TemperatureData>();  // Payload type only!
if (result) {
    std::cout << "Temp: " << result->message.temperature_celsius << "\n";
}

// Visitor also receives payload types
mbx.receive_any([](auto&& msg) {
    using PayloadT = std::decay_t<decltype(msg)>;
    
    if constexpr (std::is_same_v<PayloadT, TemperatureData>) {
        std::cout << "Got temperature: " << msg.temperature_celsius << "\n";
    } else if constexpr (std::is_same_v<PayloadT, PoseData>) {
        std::cout << "Got pose: (" << msg.x << ", " << msg.y << ")\n";
    }
});
```

## What Users DON'T See

Users never interact with:
- ❌ `MessageDefinition<...>` (after registry definition)
- ❌ `TimsMessage<...>` (wrapped internally)
- ❌ Message IDs (automatic from registry)
- ❌ `message_type_for<...>` (obsolete)
- ❌ Manual ID assignment (auto-increment)
- ❌ ID collision detection (compile-time automatic)

## What Users DO See

Users only work with:
- ✅ Plain payload structs (TemperatureData, PoseData, etc.)
- ✅ One registry definition (AppMessageRegistry)
- ✅ Module<PayloadType, InputMode, Registry>
- ✅ send(payload, dest_id)
- ✅ receive<PayloadType>()
- ✅ process() → PayloadType
- ✅ process_continuous(const PayloadType&) → PayloadType

## Benefits of This Architecture

1. **Define Once, Use Everywhere**
   - Registry defined once in user_messages.hpp
   - All modules reference AppMessageRegistry
   - No repetition of message metadata

2. **Payload-Centric Interface**
   - Users think in terms of data (TemperatureData)
   - Not in terms of messaging primitives (MessageDefinition)
   - Natural and intuitive API

3. **Zero Boilerplate**
   - No manual ID assignment
   - No message_type_for<> specializations
   - No TimsMessage<> wrappers in user code

4. **Compile-Time Safety**
   - ID collisions detected at compile-time
   - Auto-increment within categories
   - Type-safe send/receive

5. **Framework Separation**
   - System messages (subscription protocol) in framework
   - User messages automatically get UserDefined prefix
   - Clear namespace separation

## Message ID Structure (Hidden from User)

```
┌─────────┬────────────┬──────────────────┐
│ Prefix  │ SubPrefix  │  Message ID      │
│ 1 byte  │ 1 byte     │  2 bytes         │
├─────────┼────────────┼──────────────────┤
│ 0x00    │ 0x00       │  0x0001          │  SubscribeRequest (System)
│ 0x00    │ 0x00       │  0x0002          │  SubscribeReply (System)
│ 0x01    │ 0x00       │  0x0001          │  TemperatureData (User, auto)
│ 0x01    │ 0x00       │  0x0002          │  PoseData (User, auto)
│ 0x01    │ 0x01       │  0x0001          │  ResetCmd (User Commands, auto)
└─────────┴────────────┴──────────────────┘

Full ID: 0xPSMM (32-bit)
- 0x00000001 = SubscribeRequest
- 0x00000002 = SubscribeReply
- 0x01000001 = TemperatureData
- 0x01000002 = PoseData
- 0x01010001 = ResetCmd
```

## File Organization

```
CommRaT/
├── include/commrat/
│   ├── message_id.hpp             # MessageDefinition (internal)
│   ├── message_registry.hpp       # Registry with auto-ID (internal)
│   ├── registry_mailbox.hpp       # Payload-only interface
│   ├── registry_module.hpp        # Module using registry
│   └── subscription_messages.hpp  # Framework messages
│
└── examples/messages/
    └── user_messages.hpp          # USER DEFINES THIS ONCE
        - Payload structs (TemperatureData, etc.)
        - AppMessageRegistry with all messages
```

## Migration from Old System

### Old Way (Verbose, Repetitive)
```cpp
// Had to define message_type_for for every type
template<> struct message_type_for<TempData> { 
    static constexpr MessageType value = MessageType::TEMPERATURE; 
};

// Had to use TimsMessage everywhere
class Module : public Module<TimsMessage<TempData>, ...> {
    TimsMessage<TempData> process() {
        TimsMessage<TempData> msg;
        msg.payload = read_sensor();
        return msg;
    }
};

// Had to specify TimsMessage in mailbox
Mailbox<TimsMessage<TempData>, TimsMessage<PoseData>> mbx;
```

### New Way (Clean, Simple)
```cpp
// Define registry once
using AppRegistry = MessageRegistry<
    MessageDefinition<TempData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
    MessageDefinition<PoseData, MessagePrefix::UserDefined, UserSubPrefix::Data>
>;

// Use payload types only
class Module : public Module<TempData, PeriodicInput, AppRegistry> {
    TempData process() {
        return read_sensor();
    }
};

// Mailbox automatically configured from registry
RegistryMailbox<AppRegistry> mbx;
```

## Summary

**Before**: Users had to understand MessageDefinition, TimsMessage, message_type_for, manual IDs
**After**: Users define payloads + registry once, then use payloads everywhere

The message ID system is completely transparent - it just works!
