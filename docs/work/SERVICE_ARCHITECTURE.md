# Service-Oriented Module2 Architecture

## Overview

Module2 now uses clean service-oriented design with separation of concerns:

```
Module2 (thin orchestration layer)
├── IOHandler (I/O management)
│   ├── IOBuilder infrastructure
│   ├── I/O tuple storage
│   ├── Structured access (inputs(), outputs())
│   └── Publishing/fetching operations
├── CommandHandler (command dispatch)
│   ├── System command visitor
│   ├── User command visitor
│   └── Single source of truth (registry)
└── SelectProcessorBase (process() signature)
    └── Zero-copy parameter mapping
```

## Services

### 1. IOHandler (module/services/io_handler.hpp)

**Responsibility**: Manage all I/O infrastructure

**Interface**:
```cpp
template<typename Registry, typename... IOSpecs>
class IOHandler {
    // Structured access
    auto inputs()  -> rfl::NamedTuple<...>
    auto outputs() -> rfl::NamedTuple<...>
    
    // Index access
    template<size_t N> auto& get_input()
    template<size_t N> auto& get_output()
    
    // Operations
    template<size_t... Is> void fetch_inputs(...)
    template<size_t... Is> void publish_outputs(...)
};
```

**Benefits**:
- IOBuilder encapsulated (not exposed to Module2)
- Single responsibility (I/O only)
- Reusable across different module types

### 2. CommandHandler (module/services/command_handler.hpp)

**Responsibility**: Dispatch commands using registry data

**Interface**:
```cpp
template<typename Registry, typename IOBuilder, typename IOTuple>
class CommandHandler {
    // System commands from Registry::subscription_messages_t<>
    using SystemCommands = ...
    
    // User commands from DataWithCommands
    template<size_t OutputIndex>
    using UserCommandsFor = ExtractUserCommands_t<...>
    
    // Visitors
    template<size_t OutputIndex, typename Msg>
    bool visit_system_command(Output&, Msg&&)
    
    template<size_t OutputIndex, typename Msg, typename Module>
    bool visit_user_command(Msg&&, Module*)
};
```

**Benefits**:
- NO duplication of system command logic
- Single source of truth (registry)
- Clean visitor pattern

### 3. Module2 (module2.hpp)

**Responsibility**: Thin orchestration layer

**Size**: ~850 lines (down from 1000+)

**What Module2 Does**:
- Inherit from services
- Expose service APIs (inputs(), outputs())
- Lifecycle management (start/stop threads)
- Data loop orchestration
- process() signature (from SelectProcessorBase)

**What Module2 Does NOT Do**:
- I/O storage (IOHandler)
- I/O operations (IOHandler)
- Command dispatch logic (CommandHandler)
- IOBuilder management (IOHandler)

## Usage Example

```cpp
// Define application
using MyApp = CommRaT<
    Message::Data<SensorData>,
    Message::Data<FilteredData>
>;

// Create module - services are private base classes
class MySensor : public MyApp::Module2<
    Output<SensorData>,
    Period<Milliseconds(100)>
> {
protected:
    void process(SensorData& output) override {
        // IOHandler provides outputs()
        auto outs = outputs();  // rfl::NamedTuple
        output = read_sensor();
    }
    
    // CommandHandler delegates user commands here
    template<size_t OutputIndex>
    void on_command(const CalibrateCmd& cmd, CalibrateReply& reply) {
        apply_calibration(cmd.offset);
        reply.success = true;
    }
};
```

## Threading Architecture

**Per Module** (N outputs):
- 1 `data_thread` (runs process())
- N `command_threads` (CMD mailbox per output)
- WorkMailbox (no thread - used from main/data thread)

**Command Dispatch** (per-output CMD mailbox):
```cpp
cmd_mailbox.receive_any([&](auto&& msg) {
    using Cmd = decltype(msg)::payload_type;
    
    // System commands (from Registry)
    if constexpr (is_in_tuple_v<Cmd, SystemCommands>) {
        CmdService::visit_system_command(output, msg);
    }
    // User commands (from DataWithCommands)
    else if constexpr (is_in_tuple_v<Cmd, UserCommands>) {
        CmdService::visit_user_command(msg, this);
    }
    // Unknown
    else {
        send_unknown_command_reply();
    }
});
```

## Benefits

### Separation of Concerns
- **IOHandler**: I/O only
- **CommandHandler**: Commands only
- **Module2**: Orchestration only

### Single Source of Truth
- System commands → `Registry::subscription_messages_t<>`
- User commands → `ExtractUserCommands_t<DataWithCommands<...>>`
- No duplication anywhere

### Maintainability
- Each service is ~150 lines
- Module2 is ~850 lines (down from 1000+)
- Clear responsibilities
- Easy to test independently

### Extensibility
- Add new services without touching Module2
- IOHandler reusable for different module types
- CommandHandler reusable with different registries

## Next Steps

1. Implement CMD mailbox infrastructure
2. Implement subscription protocol handlers
3. Add CRTP for user command detection
4. Performance profiling
5. Consider moving more helpers to services

## Status

- ✅ IOHandler service created
- ✅ CommandHandler service created
- ✅ Module2 refactored to use services
- ✅ IOBuilder moved into IOHandler
- ⏳ CMD mailbox integration (TODO)
- ⏳ Subscription protocol implementation (TODO)
