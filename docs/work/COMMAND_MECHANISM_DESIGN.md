# Production-Ready Command Mechanism Design

**Status**: Design document for Phase 7+ enhancements  
**Date**: 2026-09-07  
**Scope**: End-to-end command send/receive patterns, API improvements, real-time safety

## Table of Contents
1. [Current State](#current-state)
2. [Problem Analysis](#problem-analysis)
3. [Desired API Design](#desired-api-design)
4. [Implementation Strategy](#implementation-strategy)
5. [Examples](#examples)
6. [Migration Path](#migration-path)

---

## Current State

### What Works Today

#### Receiving Commands (Producer Side)
- **Mechanism**: `on_command<OutputIndex>(cmd, reply)` CRTP override
- **Per-Output**: Each output has a dedicated command thread
- **Automatic Dispatch**: Framework routes commands to correct handler via visitor pattern
- **Location**: `module2.hpp` line 580-660 (`command_loop_impl()`)
- **System Commands**: Subscribe/Unsubscribe/GetData handled automatically
- **User Commands**: Custom commands dispatched to user's `on_command<>` override
- **Replies**: Sent via `cmd_mailbox.send_reply(received_msg, reply)`

**Example**:
```cpp
template<size_t OutputIndex>
void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
    reply.previous_offset = calibration_offset_;
    calibration_offset_ = cmd.offset;
    reply.success = true;
}
```

#### Sending Commands (Consumer Side)
- **Mechanism**: Two send functions in `Module2`
  1. `send_command<TargetData>(sys, inst, cmd)` - type-based address computation
  2. `send_command(addr, cmd)` - raw address form
- **Delivery**: Fire-and-forget via WORK mailbox
- **Location**: `module2.hpp` line 330-345
- **No Reply Wait**: Caller must implement separate receive logic

**Example**:
```cpp
ResetCmd reset_cmd{.hard_reset = true};
send_command<TemperatureData>(1, 0, reset_cmd);
// Fire-and-forget - no reply expected
```

#### RPC with Reply (Input Side)
- **Mechanism**: `CmdInput<T>` class in `include/commrat/module/io/input/cmd_input.hpp`
- **Pattern**: Send command + block waiting for reply (100ms default timeout)
- **Filtering**: Matches reply by source address (sys, inst, type)
- **Loop Pattern**: RACK while-loop style - retries until deadline
- **Usage**: Primary use case is SyncedInput's GetDataRequest RPC

**Example**:
```cpp
CmdInput<TemperatureData> cmd_input(work_mbx, 1, 0);
TimsMessage<CalibrateCmd> cmd = {.payload = {.offset = 0.5f}};
TimsMessage<CalibrateCmd::Reply> reply;
if (cmd_input.send_command(cmd, reply, Milliseconds(100))) {
    // Got reply
    float prev = reply.payload.previous_offset;
}
```

---

## Problem Analysis

### What's Missing

#### 1. **No RPC from Process Function**
**Problem**: `send_command()` in Module2 is fire-and-forget only. No built-in way to:
- Send a command from `process()`
- Wait for reply synchronously
- Timeout on no response
- Continue execution when reply arrives

**Current Workaround**: Create separate `CmdInput<T>` fields in module (clunky):
```cpp
class MyModule : public App::Module2<...> {
    CmdInput<SensorData> calibrator_;  // Manual member field
    
    MyModule(...) : calibrator_(work_mbx_, 1, 0) {}
    
    void process(Data& out) {
        // Inside hot path - calling RPC-style send_command() is awkward
        ResetCmd cmd;
        TimsMessage<ResetCmd::Reply> reply;
        if (calibrator_.send_command(cmd, reply)) { ... }
    }
};
```

#### 2. **No Type-Safe Command Addressing from Consumer**
**Problem**: Producer specifies command association with `DataWithCommands<>`. Consumer must know:
1. Target system/instance ID
2. Target output's data type (to compute CMD address)
3. Exact message type

**Result**: Fragile, requires manual coordination:
```cpp
// Consumer must know TemperatureData is the output type
send_command<TemperatureData>(1, 0, reset_cmd);

// If producer changes data type, consumer code breaks at runtime
```

#### 3. **No Implicit Type Matching for Multi-Output**
**Problem**: Same command type can be associated with different outputs. No way to specify "send to output N" without knowing its data type.

**Example**: Sensor module with outputs A and B both accept ResetCmd:
```cpp
using SensorOutputs = commrat::CommRaT<
    commrat::DataWithCommands<OutputDataA, ResetCmd, CalibrateCmd>,
    commrat::DataWithCommands<OutputDataB, ResetCmd, CalibrateCmd>
>;

// Consumer tries to send ResetCmd to output 1:
// send_command<??>(1, 0, reset_cmd);  // Which data type?
```

#### 4. **Fire-and-Forget Only in Process Path**
**Problem**: No synchronous reply waiting in process function without awkward workarounds.

**Constraints**:
- Real-time safe (bounded time, no dynamic allocation)
- Simple, clean API (single function call)
- Type-safe (compile-time verification)
- No separate thread overhead

#### 5. **No Command Metadata Access**
**Problem**: User can't query:
- What commands does this module accept on a given output?
- What is the reply type for command X?
- Does this module support command Y?

**Result**: Framework integration difficult (reflection, introspection tools)

#### 6. **No Built-In Command Serialization/Deserialization**
**Problem**: Commands are POD struct payloads only. No automatic versioning, evolution, or compatibility checking.

---

## Desired API Design

### Principle: Simple for Users, Complex Internals

**Users should only call one or two functions** to send commands and get replies synchronously. **Framework handles all addressing, routing, and type safety**.

### Consumer API (Sending Commands)

#### Option A: Implicit RPC from Process Function (Recommended)

```cpp
class SensorConsumer : public App::Module2<Output<FusedData>, Input<SensorData>> {
protected:
    void process(const SensorData& input, FusedData& output) override {
        // 1. Simple: Send command and wait for reply
        auto reply = send_command_to_input<0, CalibrateCmd>(
            CalibrateCmd{.offset = 0.1f}, 
            Milliseconds(100)  // timeout
        );
        
        // 2. Check if reply arrived
        if (reply) {
            RTLOG_INFO() << "Calibrated, prev offset: " << reply->previous_offset;
        } else {
            RTLOG_WARN() << "Calibration timeout";
        }
        
        // 3. Continue with fused output
        output.value = input.temperature_c + reply->correction;
    }
};
```

**How it works**:
- `send_command_to_input<InputIndex, CmdType>(cmd, timeout)`
- Framework automatically:
  1. Extracts producer module description from input metadata
  2. Computes CMD address from producer's system/instance + primary data type
  3. Sends command via WORK mailbox
  4. Blocks on same WORK mailbox for reply (with timeout)
  5. Filters by source address (system, instance, type_id)
  6. Returns `std::optional<TimsMessage<CmdType::Reply>>`
- **Type Safety**: `CmdType` must be in producer's registry
- **Real-Time Safe**: Bounded timeout, no allocations, deterministic
- **Limitation**: Only works if input metadata includes producer's system/instance

#### Option B: Explicit Module Addressing

```cpp
class SensorController : public App::Module2<Output<ControlData>, Period<1000>> {
    constexpr static uint8_t SENSOR_SYSTEM = 1;
    constexpr static uint8_t SENSOR_INSTANCE = 0;
    
protected:
    void process(ControlData& output) override {
        // Send command to known module location
        auto reply = send_command<SensorData, CalibrateCmd>(
            SENSOR_SYSTEM,
            SENSOR_INSTANCE,
            CalibrateCmd{.offset = temp_offset_}
        );
        
        if (!reply) {
            RTLOG_ERROR() << "Sensor not responding";
            return;
        }
        
        output.calibrated = true;
    }
};
```

**How it works**:
- `send_command<OutputDataType, CmdType>(sys, inst, cmd, timeout?)`
- Framework:
  1. Verifies `CmdType` registered in this module's registry
  2. Computes target CMD address from `OutputDataType` + sys/inst
  3. Sends and waits for reply (same WORK mailbox pattern)
  4. Returns `std::optional<TimsMessage<CmdType::Reply>>`
- **Type Safety**: Both `OutputDataType` and `CmdType` compile-time verified
- **API Clarity**: Explicit about target module location
- **Pre-requisite**: Caller must know target's output data type

#### Option C: Raw Address (Low-Level)

```cpp
auto reply = send_command(0x0A000000, cmd);  // target CMD address
if (reply) { ... }
```

**Reserved for**: Advanced use cases, command routers, system tools

---

### Producer API (Receiving Commands)

#### Current (Still Good)
```cpp
template<size_t OutputIndex>
void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
    reply.previous_offset = calibration_offset_;
    calibration_offset_ = cmd.offset;
    reply.success = true;
}
```

#### Enhancement: Multi-Command Convenience

```cpp
template<size_t OutputIndex>
void on_command(const ResetCmd& cmd, typename ResetCmd::Reply& reply) { ... }

template<size_t OutputIndex>
void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) { ... }

template<size_t OutputIndex>
void on_command(const SetModeCmd& cmd, typename SetModeCmd::Reply& reply) { ... }
```

**Status**: Already works! Multiple overloads dispatch automatically via visitor pattern.

#### Enhancement: Unknown Command Handler (Graceful Degradation)

```cpp
template<size_t OutputIndex>
void on_unknown_command(uint16_t msg_id, uint8_t* payload, size_t payload_size,
                       auto send_error_reply) {
    RTLOG_WARN() << "Unknown command " << msg_id;
    send_error_reply(UnknownCommandError{.msg_id = msg_id});
}
```

**Purpose**: Handle commands not explicitly handled (forward compatibility)

---

### Command Definition API

#### Current Pattern (Works)
```cpp
struct CalibrateCmd {
    float offset;
    struct Reply {
        bool success;
        float previous_offset;
    };
};

using SensorData = commrat::DataWithCommands<
    SensorPayload,
    CalibrateCmd,
    ResetCmd,
    SetModeCmd
>;
```

#### Desired Enhancement: Named Command Associations

```cpp
// Optional: Explicit command registration for clarity/tooling
using SensorCommands = commrat::Commands<
    Command<CalibrateCmd, "Calibrate", "Adjust sensor offset">,
    Command<ResetCmd, "Reset", "Reset sensor state">,
    Command<SetModeCmd, "SetMode", "Change operating mode">
>;

using SensorData = commrat::DataWithCommands<
    SensorPayload,
    SensorCommands
>;
```

**Benefits**:
- Self-documenting (names, descriptions in metadata)
- Enables reflection tools, CLIs, documentation generation
- Still type-safe and zero-overhead (names stripped at compile-time)

---

## Implementation Strategy

### Phase 1: RPC from Process (Core)

**Files to modify**:
1. `include/commrat/module2.hpp`
   - Add `send_command_to_input<InputIndex, CmdType>(cmd, timeout)` template
   - Add `send_command<OutputDataType, CmdType>(sys, inst, cmd, timeout)` overload
   - Both return `std::optional<TimsMessage<CmdType::Reply>>`

2. `include/commrat/module/io/input/cmd_input.hpp`
   - Already has `send_command()` with timeout logic
   - Expose as public method (currently works, may need minor tweaks)

3. `include/commrat/module/services/io_handler.hpp`
   - Add helpers to access input producer metadata
   - Add helpers to extract producer system/instance from input config

**Constraints**:
- Real-time safe: Bounded timeout, no allocations
- Deterministic: No dynamic memory, minimal branching in hot path
- Type-safe: Compile-time verification of command type

**Pseudo-code**:
```cpp
template<typename OutputDataType, typename CmdType>
std::optional<TimsMessage<typename CmdType::Reply>> 
Module2::send_command(uint8_t sys, uint8_t inst, const CmdType& cmd, 
                      Duration timeout = Milliseconds(100)) {
    static_assert(Registry::is_registered<CmdType>, 
                  "Command type not in registry");
    static_assert(std::is_same_v<typename CmdType::Reply, ...>,
                  "Command type must have Reply member");
    
    // 1. Compute target CMD address
    uint32_t cmd_addr = calculate_base_address<OutputDataType, ...>(sys, inst);
    
    // 2. Create message with header
    TimsMessage<CmdType> msg_to_send{.payload = cmd};
    msg_to_send.header.src = this->work_address();
    
    // 3. Send via WORK mailbox
    if (!work_mailbox_->send(msg_to_send, cmd_addr)) {
        return std::nullopt;
    }
    
    // 4. Wait for reply (deadline-based loop, like CmdInput)
    Timestamp deadline = Time::now() + Time::to_nanoseconds(timeout);
    while (Time::now() < deadline) {
        TimsMessage<typename CmdType::Reply> reply;
        Duration remaining = deadline - Time::now();
        
        if (work_mailbox_->receive_any(reply, remaining)) {
            // 5. Validate source address matches
            if (extract_type_id(reply.header.src) == OutputDataType::type_id &&
                extract_system_id(reply.header.src) == sys &&
                extract_instance_id(reply.header.src) == inst) {
                return reply;
            }
            // Else: Message from different source, continue waiting
        }
    }
    
    return std::nullopt;  // Timeout
}
```

### Phase 2: Command Metadata & Reflection

**Goal**: Enable introspection tools, CLI generators, documentation

**Files**:
1. `include/commrat/messaging/command_metadata.hpp` (new)
   - `CommandInfo<CmdType>` struct with name, description, version
   - `CommandsMetadata<DataType>` tuple of all commands for a data type

2. `include/commrat/module/helpers/command_introspection.hpp` (new)
   - `get_commands_for<DataType>()` → returns tuple of `CommandInfo`
   - `has_command<DataType, CmdType>()` → bool
   - `get_command_reply_type<CmdType>()` → type

**Mechanism**:
```cpp
// Inside app registry setup (compile-time):
template<typename DataType>
auto extract_command_metadata() {
    if constexpr (has_commands_v<DataType>) {
        return create_tuple_of<CommandInfo>(DataType::commands...);
    }
    return std::tuple<>();
}
```

### Phase 3: Unknown Command Handler

**Goal**: Graceful degradation for command versioning

**Implementation**:
1. Add fallback visitor in `CommandHandler::visit_user_commands()`
2. If no matching command found, call `on_unknown_command()` if defined
3. Provide error reply helper

---

## Examples

### Example 1: Simple RPC from Process (Recommended Pattern)

```cpp
#include <commrat/commrat.hpp>
#include <commrat/examples/common_messages.hpp>

using namespace example_messages;
using commrat::Milliseconds;
using commrat::ModuleConfig;

// ============================================================================
// Define Commands
// ============================================================================

struct CalibrateCmd {
    float offset;
    struct Reply {
        bool success;
        float previous_offset;
    };
};

// ============================================================================
// App Definition
// ============================================================================

namespace example_messages {
    using SensorWithCommands = commrat::DataWithCommands<
        TemperatureData,
        CalibrateCmd
    >;
    
    using RPCExampleApp = commrat::CommRaT<
        SensorWithCommands,
        commrat::Message::Data<FilteredData>
    >;
}

// ============================================================================
// Producer Module (receives commands)
// ============================================================================

class CommandableSensor : public RPCExampleApp::Module2<
    Output<TemperatureData>,
    Period<commrat::Milliseconds(200)>
> {
public:
    explicit CommandableSensor(const ModuleConfig& config) 
        : RPCExampleApp::Module2<Output<TemperatureData>, Period<commrat::Milliseconds(200)>>(config) {}

protected:
    void process(TemperatureData& output) override {
        output.sensor_id = 1;
        output.temperature_c = 20.0f + calibration_offset_;
        output.confidence = 1.0f;
    }
    
    template<size_t OutputIndex>
    void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
        reply.previous_offset = calibration_offset_;
        calibration_offset_ = cmd.offset;
        reply.success = true;
    }

private:
    float calibration_offset_ = 0.0f;
};

// ============================================================================
// Consumer Module (sends commands and waits for reply)
// ============================================================================

class SensorController : public RPCExampleApp::Module2<
    Output<FilteredData>,
    Input<TemperatureData>,
    Period<commrat::Milliseconds(1000)>
> {
public:
    explicit SensorController(const ModuleConfig& config) 
        : RPCExampleApp::Module2<Output<FilteredData>, Input<TemperatureData>, Period<commrat::Milliseconds(1000)>>(config) {}

protected:
    void process(const TemperatureData& input, FilteredData& output) override {
        // SIMPLE: Send command and wait for reply (100ms timeout)
        auto reply = send_command_to_input<0, CalibrateCmd>(
            CalibrateCmd{.offset = 0.1f},
            Milliseconds(100)
        );
        
        if (reply) {
            RTLOG_INFO() << "Calibrated! Previous offset: " << reply->payload.previous_offset;
            output.value = input.temperature_c;
            output.is_valid = true;
        } else {
            RTLOG_WARN() << "Calibration failed (timeout or error)";
            output.value = 0.0f;
            output.is_valid = false;
        }
    }
};

// ============================================================================
// Launcher
// ============================================================================

int main() {
    using App = RPCExampleApp;
    
    ModuleConfig sensor_config{
        .name = "Sensor",
        .system_id = 1,
        .instance_id = 0
    };
    
    ModuleConfig controller_config{
        .name = "Controller",
        .system_id = 2,
        .instance_id = 0,
        .subscriptions = {
            {1, 0}  // Subscribe to sensor (system 1, instance 0)
        }
    };
    
    auto sensor = std::make_shared<CommandableSensor>(sensor_config);
    auto controller = std::make_shared<SensorController>(controller_config);
    
    // Start modules (same mailbox system as before)
    sensor->on_start();
    controller->on_start();
    
    // Run for 5 seconds
    for (int i = 0; i < 50; ++i) {
        sensor->execute();
        controller->execute();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

**Output**:
```
[Controller] Calibrated! Previous offset: 0
[Controller] Calibrated! Previous offset: 0.1
[Controller] Calibrated! Previous offset: 0.1
```

### Example 2: Explicit Target Module Addressing

```cpp
class GlobalCalibrator : public RPCExampleApp::Module2<
    Output<StatusData>,
    Period<Milliseconds(2000)>
> {
    constexpr static uint8_t SENSOR_SYS = 1;
    constexpr static uint8_t SENSOR_INST = 0;
    
protected:
    void process(StatusData& output) override {
        // Explicit: Send to known module location
        // API: send_command<OutputDataType, CmdType>(sys, inst, cmd, timeout)
        auto reply = send_command<TemperatureData, CalibrateCmd>(
            SENSOR_SYS,
            SENSOR_INST,
            CalibrateCmd{.offset = rotation_++ * 0.1f},
            Milliseconds(200)
        );
        
        if (reply) {
            output.success = true;
            output.last_error = "";
        } else {
            output.success = false;
            output.last_error = "Sensor timeout";
        }
    }

private:
    int rotation_ = 0;
};
```

### Example 3: Multi-Output Command Routing

```cpp
// Producer: Multiple outputs, each with different commands
using MultiOutputSensor = commrat::CommRaT<
    commrat::DataWithCommands<SensorDataA, ResetCmd>,
    commrat::DataWithCommands<SensorDataB, ResetCmd, CalibrateCmd>
>;

class MultiSensor : public MultiOutputSensor::Module2<
    Output<SensorDataA>,
    Output<SensorDataB>,
    Period<Milliseconds(100)>
> {
protected:
    void process(SensorDataA& out_a, SensorDataB& out_b) override {
        out_a.value = read_sensor_a();
        out_b.value = read_sensor_b();
    }
    
    // Command handlers: OutputIndex disambiguates which output
    template<size_t OutputIndex>
    void on_command(const ResetCmd& cmd, typename ResetCmd::Reply& reply) {
        if constexpr (OutputIndex == 0) {
            reset_sensor_a();
        } else if constexpr (OutputIndex == 1) {
            reset_sensor_b();
        }
        reply.success = true;
    }
    
    template<size_t OutputIndex>
    void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
        if constexpr (OutputIndex == 1) {  // Only output 1 handles this
            calibrate_sensor_b(cmd.offset);
            reply.success = true;
        }
        // Output 0 doesn't have this command - won't compile
    }
};
```

---

## Migration Path

### Current Status (Today)
- ✅ `on_command<OutputIndex>(cmd, reply)` receiving works
- ✅ Fire-and-forget `send_command<TargetData>(sys, inst, cmd)` works
- ✅ RPC pattern via `CmdInput` works (but clunky in process function)
- ❌ No integrated RPC from process function
- ❌ No command metadata/reflection

### Phase 1 (Recommended Next): RPC from Process

1. Add to `module2.hpp`:
   - `send_command_to_input<InputIndex, CmdType>(cmd, timeout)` → `std::optional<TimsMessage<Reply>>`
   - `send_command<OutputDataType, CmdType>(sys, inst, cmd, timeout)` → `std::optional<TimsMessage<Reply>>`

2. Add to `io_handler.hpp`:
   - Helper `get_input_producer_id<InputIndex>()` → `{sys_id, inst_id}`
   - Used by `send_command_to_input` to look up producer

3. **Backward Compatibility**: Keep old fire-and-forget functions, mark as "for async use"

4. **Documentation**: Update API_REFERENCE.md with new patterns

**Effort**: Medium (1-2 days) — mainly template code, reuses existing CmdInput pattern

### Phase 2: Command Metadata

1. Add `command_metadata.hpp` with introspection helpers
2. Add reflection support (optional names/descriptions in Commands wrapper)
3. Update registry to include command metadata
4. **Backward Compatibility**: Fully backward compatible (optional feature)

**Effort**: Small-Medium (1 day) — mostly compile-time introspection

### Phase 3: Unknown Command Handler

1. Add optional CRTP override for unknown commands
2. Add dispatch fallback in CommandHandler
3. Provide error reply helpers

**Effort**: Small (few hours)

### No Breaking Changes

All proposed enhancements are **additive**:
- Old `send_command<TargetData>(sys, inst, cmd)` stays fire-and-forget
- New `send_command<OutputDataType, CmdType>(...)` returns optional
- New `send_command_to_input<InputIndex, CmdType>(...)` is additional overload
- Old `on_command` CRTP stays as-is
- Command definitions unchanged

---

## Design Decisions

### Q: Why `std::optional` instead of exceptions for timeout?

**A**: Real-time safety. Exceptions have unpredictable overhead in RT code. Optional is:
- Zero-cost abstraction (compile-time known size)
- Deterministic (no unwinding)
- Explicit error handling in hot path
- Follows Rusty error handling patterns

### Q: Why block on WORK mailbox instead of dedicated reply mailbox?

**A**: Simplicity and re-use. Benefits:
- No extra mailbox allocation per module
- Subscription replies already go to WORK mailbox
- Matches RACK pattern (request.header.src used as reply target)
- One mailbox per module to monitor, simpler debugging

**Cost**: Must filter replies by source address (cheap, already in CmdInput)

### Q: Why type-based addressing `<OutputDataType>` instead of output index?

**A**: Better API clarity and type safety. Benefits:
- Type name self-documents what's being called
- Compiler verifies type is in registry
- Matches existing `Output<T>` naming
- Extensible to service-oriented patterns

**Limitation**: Requires caller to know target's output data type

### Q: What about command versioning?

**A**: Out of scope for Phase 1, addressed in Phase 2:
- Named commands with version metadata
- Unknown command handler for forward compatibility
- Payload serialization versioning via SeRTial (future)

---

## Open Questions

1. **Reply Timeout Strategy**: Should it be module-configurable (constructor parameter)? Or per-call override only?
   - **Recommendation**: Both. Constructor sets default (e.g., 100ms), call site can override.

2. **Command Namespacing**: Should commands be grouped by domain (e.g., `Calibration::CalibrateCmd` vs `CalibrateCmd`)?
   - **Recommendation**: Not required for Phase 1, but document as best practice.

3. **Async Command Callbacks**: Should we support fire-and-forget with on-reply callback?
   - **Recommendation**: Out of scope for Phase 1 (adds complexity). Keep RPC pattern simple.

4. **Multi-Module Broadcast Commands**: Should single command go to multiple modules?
   - **Recommendation**: Out of scope. Users can loop and send individually (rare need).

5. **Command Reflection UI/CLI**: Should framework auto-generate command schemas?
   - **Recommendation**: Phase 2+. Metadata layer enables this.

---

## References

- [RACK Proxy Pattern](https://github.com/rclark-ratos/rack-docs): `proxySendRecvDataCmd` inspiration
- `include/commrat/module2.hpp` lines 330-345: Current send_command
- `include/commrat/module/io/input/cmd_input.hpp`: RPC template pattern
- `include/commrat/module/services/command_handler.hpp`: Dispatch logic
- `examples/command_example.cpp`: Current pattern demonstrations

