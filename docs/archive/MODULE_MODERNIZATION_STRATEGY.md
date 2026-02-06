# CommRaT Module Modernization Strategy

## Executive Summary

Replace RACK's C-style RackDataModule with a modern C++20 templated module framework that:
- Uses CommRaT's Mailbox API instead of RACK's void* mailboxes
- Leverages SeRTial for compile-time serialization
- Eliminates runtime overhead through template metaprogramming
- Provides cleaner, type-safe interfaces
- Maintains real-time guarantees (no dynamic allocation in hot paths)

## Current RACK Architecture Analysis

### RackRflDataModule (Existing)

**Strengths:**
- Typed command dispatch via CRTP
- Compile-time parameter tables
- rfl::NamedTuple for proxy management
- Lifecycle hooks (onInit, onTurnOn, etc.)
- Three input modes: PeriodicInput, LoopInput, ContinuousInput<T>
- **Automatic subscription management** for ContinuousInput
- **Listener system** for push-based data distribution
- **Command handlers** (MSG_GET_CONT_DATA, MSG_STOP_CONT_DATA)
- **Proxy system** for remote module control

**Pain Points:**
- Still uses void* buffers and manual pack/unpack
- RACK mailbox API (proxySendCmd, proxyRecvDataCmd)
- Manual buffer size calculations
- Mixing C-style RACK code with modern C++
- Command dispatcher uses type-erased callbacks
- Complex inheritance chain (RackDataModule → RackMailbox → C APIs)

### Module Pattern Analysis

```cpp
// Current pattern (chassis_sim.h):
class ChassisSim : public RackRflDataModule<
    Chassis,           // ModuleT (defines CLASS_ID, DataMessage, Commands)
    PeriodicInput,     // InputModeT
    ChassisSim         // DerivedT (CRTP)
> {
    int onInit() override { /* load params */ }
    int dataLoop(ChassisData_& out) override { /* generate output */ }
};

// Current pattern (obj_recog with input):
class ObjRecogImpl : public RackRflDataModule<
    ObjRecog,                    // ModuleT
    ContinuousInput<Camera>,     // InputModeT - receives Camera::DataMessage
    ObjRecogImpl                 // DerivedT
> {
    int dataLoop(const CameraData_& in, ObjRecogData_& out) override {
        // Process camera → objects
    }
};
```

### Command Pattern Analysis

```cpp
// Current: Simple command definitions
struct SetEstimateCmd {
    static constexpr int8_t msgType = MSG_SET_ESTIMATE;
    using PayloadType = ObjRecogData_;  // SEND command
};

struct GetStatusCmd {
    static constexpr int8_t msgType = MSG_GET_STATUS;
    using ReplyType = StatusData_;      // GET command
};

// Commands registered in module
using Commands = CommandList<SetEstimateCmd, StopRecogCmd>;

// Runtime dispatch in RackRflDataModule::moduleCommand()
// - Matches msgType
// - Calls derived handler via CRTP
```

## Proposed Modern Architecture

### Design Goals

1. **Pure C++20**: No C-style void* APIs in module code
2. **Mailbox-Based**: Use CommRaT::Mailbox<Messages...> instead of RackMailbox
3. **Compile-Time Everything**: All type dispatch, buffer sizes, validation at compile time
4. **Zero Overhead**: No vtables, no type erasure, no runtime allocation
5. **SeRTial Native**: Use buffer_type, max_buffer_size directly
6. **Clean Separation**: Module logic separate from communication layer

### Core Template: Module<OutputT, InputT, Commands...>

```cpp
namespace CommRaT {

// Input mode tags
struct PeriodicInput {};
struct LoopInput {};
template<typename T> struct ContinuousInput { using Source = T; };

// Module configuration (compile-time + runtime)
template<typename OutputDataT, 
         typename InputModeT = PeriodicInput,
         typename... CommandTypes>
class Module {
public:
    // Exposed types for introspection
    using OutputData = OutputDataT;
    using InputMode = InputModeT;
    
    // Compile-time checks
    static constexpr bool has_input = !std::is_same_v<InputMode, PeriodicInput> &&
                                      !std::is_same_v<InputMode, LoopInput>;
    
    // Extract input type (if continuous)
    using InputData = std::conditional_t<
        has_input,
        typename InputMode::Source::OutputData,
        void
    >;
    
    // Message registry for this module (all messages it handles)
    using InputMessages = std::conditional_t<
        has_input,
        MessageRegistry<InputData>,
        MessageRegistry<>
    >;
    
    using CommandMessages = MessageRegistry<typename CommandTypes::PayloadType...>;
    
    using AllMessages = /* concat InputMessages + CommandMessages + OutputData */;
    
    // Mailbox typed for this module's messages
    using Mailbox = CommRaT::Mailbox<AllMessages>;
    
protected:
    // RAII lifecycle
    Module(const Config& config) 
        : mailbox_(createMailboxConfig(config))
        , output_period_(config.period)
    {}
    
    virtual ~Module() { stop(); }
    
    // Lifecycle hooks (pure virtual or default implementations)
    virtual void on_init() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_cleanup() {}
    
    // Processing loop - implementations override based on input mode
    // Periodic/Loop: process(OutputData& out)
    // Continuous: process(const InputData& in, OutputData& out)
    virtual void process(OutputData& out) = 0;
    virtual void process(const InputData& in, OutputData& out) {
        (void)in; process(out);  // Fallback
    }
    
    // Command handlers - derived class implements via CRTP or lambdas
    // Type-safe dispatch handled by base
    
public:
    // Start module operation
    void start() {
        if (!mailbox_.start()) throw std::runtime_error("Mailbox start failed");
        on_start();
        
        // Start appropriate thread based on input mode
        if constexpr (std::is_same_v<InputMode, PeriodicInput>) {
            run_thread_ = std::thread([this]{ periodic_loop(); });
        } else if constexpr (has_input) {
            run_thread_ = std::thread([this]{ continuous_loop(); });
        } else {
            run_thread_ = std::thread([this]{ free_loop(); });
        }
        
        command_thread_ = std::thread([this]{ command_loop(); });
    }
    
    // Stop module operation
    void stop() {
        running_ = false;
        if (run_thread_.joinable()) run_thread_.join();
        if (command_thread_.joinable()) command_thread_.join();
        on_stop();
        mailbox_.stop();
    }
    
    // Send output data
    void publish(const OutputData& data, uint32_t dest_id) {
        mailbox_.send(data, dest_id);
    }
    
private:
    // Periodic execution loop
    void periodic_loop() {
        while (running_) {
            OutputData output{};
            process(output);
            // Publish to subscribers
            for (auto dest : subscribers_) {
                publish(output, dest);
            }
            std::this_thread::sleep_for(output_period_);
        }
    }
    
    // Continuous input loop
    void continuous_loop() {
        while (running_) {
            // Block waiting for input
            auto input_msg = mailbox_.template receive<InputData>();
            if (!input_msg) continue;
            
            OutputData output{};
            process(input_msg->message, output);
            
            // Publish to subscribers
            for (auto dest : subscribers_) {
                publish(output, dest);
            }
        }
    }
    
    // Free-running loop (LoopInput)
    void free_loop() {
        while (running_) {
            OutputData output{};
            process(output);
            for (auto dest : subscribers_) {
                publish(output, dest);
            }
            // No sleep - runs as fast as possible
        }
    }
    
    // Command processing loop
    void command_loop() {
        while (running_) {
            // Receive any command (visitor pattern)
            mailbox_.receive_any([this](auto&& cmd_msg) {
                handle_command(cmd_msg.message);
            });
        }
    }
    
    // Type-safe command dispatch using if constexpr
    template<typename CmdT>
    void handle_command(const CmdT& cmd) {
        if constexpr ((std::is_same_v<CmdT, typename CommandTypes::PayloadType> || ...)) {
            // Call derived handler
            static_cast<Derived*>(this)->on_command(cmd);
        }
    }
    
    Mailbox mailbox_;
    std::thread run_thread_;
    std::thread command_thread_;
    std::atomic<bool> running_{false};
    std::chrono::milliseconds output_period_;
    std::vector<uint32_t> subscribers_;
};

} // namespace CommRaT
```

### Key Improvements Over RACK

| Feature | RACK RackRflDataModule | CommRaT Module |
|---------|------------------------|----------------|
| Mailbox | void* RackMailbox | Mailbox<Messages...> |
| Serialization | Manual pack/unpack | SeRTial automatic |
| Buffer Management | Manual size calc | buffer_type from SeRTial |
| Message Passing | pointer+size | std::span<std::byte> |
| Command Dispatch | Type-erased callbacks | if constexpr compile-time |
| Input Modes | ContinuousInput<T> | ContinuousInput<T> (same) |
| Lifecycle | Virtual hooks | Virtual hooks (same) |
| Threading | dataTask/cmdTask | std::thread with RAII |
| Configuration | argTable_t[] | Structured Config |
| **Client Access** | **RackProxy wrapper** | **Direct mailbox use** |

### Command System Modernization

```cpp
// Define command with payload type
struct SetParameterCommand {
    using Payload = ParameterUpdate;
    static constexpr uint32_t id = 0x101;
};

// Module declares commands it handles
template<typename Derived>
class MyModule : public Module<
    MyOutputData,
    PeriodicInput,
    SetParameterCommand,
    GetStatusCommand
> {
    // Type-safe handler
    void on_command(const SetParameterCommand::Payload& cmd) {
        // Update parameters
    }
    
    void on_command(const GetStatusCommand::Payload& cmd) {
        // Send status reply
    }
};

// Compile-time validation: all commands in registry
// Automatic dispatch based on MessageType enum
```

### Proxy Modernization - **ELIMINATED**

**Analysis**: In RACK, `RackRflProxy` serves as a client-side wrapper because:
1. RACK uses void* mailboxes - proxy provides type safety
2. Manual pack/unpack needed - proxy handles it
3. Command execution requires complex API calls

**In CommRaT**: These problems are already solved!
1. `Mailbox<Messages...>` already type-safe
2. SeRTial handles serialization automatically
3. Mailbox API is clean: `send()`, `receive()`, `try_receive()`

**New Approach**: Modules simply use each other's mailboxes directly

```cpp
// Old RACK style:
CameraProxy camera_proxy(&mailbox, 0, CAMERA, 0);
CameraData_ data;
camera_proxy.getData(data, timestamp);

// New CommRaT style - no proxy needed!
// Option 1: Direct mailbox access
Mailbox<CameraData> camera_mailbox(camera_config);
auto data = camera_mailbox.receive<CameraData>();

// Option 2: Module provides accessor
class ObjRecogModule : public Module<ObjectList, ContinuousInput<CameraModule>> {
    void process(const CameraData& in, ObjectList& out) override {
        // 'in' comes directly from CameraModule via mailbox
        // No proxy wrapper needed!
    }
};

// Option 3: For external access, use mailbox reference
class UserCode {
    void read_camera(Mailbox<CameraData>& camera_mbx) {
        auto data = camera_mbx.try_receive<CameraData>(
            std::chrono::milliseconds(100)
        );
    }
};
```

**Benefits of Eliminating Proxy**:
- One less abstraction layer
- Direct use of Mailbox API (already learned)
- No wrapper overhead
- Simpler mental model
- Fewer templates to instantiate (faster compile)

**What About Command Sending?**

Commands are just messages! No special proxy needed:

```cpp
// Send command to module
SetVelocityCommand cmd{.vx = 1.0f, .vy = 0.0f};
module_mailbox.send(cmd, target_module_id);

// Wait for response (if needed)
auto response = module_mailbox.receive<AckMessage>();
```

**Module Discovery**: Instead of proxy creation, use a registry:

```cpp
class ModuleRegistry {
    template<typename ModuleT>
    Mailbox<typename ModuleT::OutputData>& get_mailbox() {
        return mailboxes_[ModuleT::id];
    }
};

// Usage
auto& camera_mbx = registry.get_mailbox<CameraModule>();
auto data = camera_mbx.receive<CameraData>();
```

## Implementation Phases

### Phase 1: Core Module Template ✅ COMPLETE
- ✅ Create `Module<OutputT, InputT, Commands...>` base template
- ✅ Implement PeriodicInput mode
- ✅ Implement LoopInput mode  
- ✅ Basic lifecycle (start/stop/process)
- ✅ Integration with CommRaT::Mailbox
- ✅ Simple examples: StatusModule, CounterModule
- ✅ Subscriber management (add/remove/publish)
- ✅ Compile-time message size calculation (SeRTial)
- ✅ Zero-overhead input mode dispatch (if constexpr)

### Phase 2: Continuous Input & Subscription System ✅ COMPLETE

**Status**: ✅ RUNTIME VALIDATED (February 4, 2026)

**Delivered Features**:

**Status**: ✅ RUNTIME VALIDATED (February 4, 2026)

**Delivered Features**:

#### 2.1 Subscription Protocol ✅
- ✅ SubscribeRequest/SubscribeReply message types
- ✅ UnsubscribeRequest/UnsubscribeReply message types
- ✅ Automatic subscription on ContinuousInput module start
- ✅ Automatic unsubscription on module stop
- ✅ Producer adds/removes subscribers via command loop
- ✅ Tested in continuous_input_example.cpp (Producer→Consumer)

#### 2.2 Message ID System ✅
- ✅ 0xPSMM format (Prefix 1 byte, SubPrefix 1 byte, Message ID 2 bytes)
- ✅ MessagePrefix::System (0x00) for framework messages
- ✅ MessagePrefix::UserDefined (0x01+) for application messages
- ✅ SubPrefixes for categories (Data, Commands, Events, Custom)
- ✅ Compile-time auto-increment within categories
- ✅ Collision detection at compile-time
- ✅ MessageDefinition<PayloadT, Prefix, SubPrefix, ID=0xFFFF>

#### 2.3 Ultra-Clean User Interface ✅
- ✅ Payload-only API (users never see MessageDefinition)
- ✅ CombinedRegistry auto-includes system subscription messages
- ✅ Module<OutputData, InputMode> alias (hides registry)
- ✅ ContinuousInput<PayloadT> (payload type only)
- ✅ process() returns payload type
- ✅ process_continuous(const PayloadT&) receives payload type
- ✅ Tested in clean_interface_example.cpp

#### 2.4 Command Handler System ✅
- ✅ Command loop processes subscription requests
- ✅ Visitor pattern dispatches to correct handler
- ✅ Type-safe command registration from CommandTypes pack
- ✅ Non-blocking command receive with timeout

**Examples Validated**:
- ✅ continuous_input_example.cpp (Producer→Consumer with auto-subscription)
- ✅ clean_interface_example.cpp (Ultra-clean Module<> interface)

**Critical Missing Features** (from RACK analysis) - NOW IMPLEMENTED:

#### 2.1 Subscription Protocol
RACK's continuous data flow:
```
Consumer → MSG_GET_CONT_DATA(periodTime, dataMbx) → Producer
Producer → addListener(periodTime, dataMbx) → internal registry
Producer → MSG_CONT_DATA(actualPeriod) → Consumer (reply)
Producer → automatically sends data to dataMbx on each publish
Consumer → MSG_STOP_CONT_DATA(dataMbx) → Producer
Producer → removeListener(dataMbx)
```

What we need in CommRaT:
```cpp
// Standard subscription messages
struct SubscribeRequest {
    uint32_t subscriber_mailbox_id;
    std::chrono::milliseconds requested_period;
};

struct SubscribeReply {
    std::chrono::milliseconds actual_period;
    bool success;
};

struct UnsubscribeRequest {
    uint32_t subscriber_mailbox_id;
};
```

#### 2.2 Command Handler System
```cpp
template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
class Module {
    // Command loop processes subscription requests
    void command_loop() {
        while (running_.load()) {
            // Use visitor pattern to handle any registered command
            auto result = mailbox_.receive_any(
                std::chrono::milliseconds(100),
                [this](auto&& cmd, const auto& msg_info) {
                    this->handle_command(std::forward<decltype(cmd)>(cmd), msg_info);
                }
            );
        }
    }
    
    // Handle standard subscription commands
    template<typename CmdT>
    void handle_command(const CmdT& cmd, const MessageInfo& info) {
        if constexpr (std::is_same_v<CmdT, SubscribeRequest>) {
            handle_subscribe(cmd, info);
        } else if constexpr (std::is_same_v<CmdT, UnsubscribeRequest>) {
            handle_unsubscribe(cmd, info);
        } else {
            // Forward to user-defined handler
            static_cast<DerivedT*>(this)->on_command(cmd, info);
        }
    }
    
    void handle_subscribe(const SubscribeRequest& req, const MessageInfo& info) {
        bool added = add_subscriber(req.subscriber_mailbox_id);
        
        SubscribeReply reply{
            .actual_period = config_.period,
            .success = added
        };
        
        mailbox_.send_reply(reply, info);
    }
    
    void handle_unsubscribe(const UnsubscribeRequest& req, const MessageInfo& info) {
        remove_subscriber(req.subscriber_mailbox_id);
        // Optionally send ack
    }
};
```

#### 2.3 Automatic Subscription for ContinuousInput
```cpp
template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
class Module {
    void start() {
        // ... existing start logic ...
        
        // For ContinuousInput, automatically subscribe to source
        if constexpr (has_continuous_input) {
            auto result = subscribe_to_source();
            if (!result) {
                throw std::runtime_error("Failed to subscribe to input source");
            }
        }
    }
    
    // Subscribe to source module for continuous input
    auto subscribe_to_source() -> MailboxResult<void> 
        requires(has_continuous_input)
    {
        using SourceModule = typename InputMode::SourceModule;
        
        SubscribeRequest req{
            .subscriber_mailbox_id = config_.mailbox_id,
            .requested_period = config_.period
        };
        
        // Send subscription request to source
        auto send_result = mailbox_.send(req, SourceModule::mailbox_id);
        if (!send_result) return send_result.error();
        
        // Wait for reply
        auto reply_result = mailbox_.receive<SubscribeReply>(
            std::chrono::seconds(5)
        );
        
        if (!reply_result) return reply_result.error();
        
        if (!reply_result->message.success) {
            return MailboxError::SubscriptionFailed;
        }
        
        // Store actual period negotiated with source
        actual_input_period_ = reply_result->message.actual_period;
        
        return {};
    }
    
    void stop() override {
        // Unsubscribe before stopping
        if constexpr (has_continuous_input) {
            unsubscribe_from_source();
        }
        
        // ... existing stop logic ...
    }
    
    void unsubscribe_from_source() 
        requires(has_continuous_input)
    {
        using SourceModule = typename InputMode::SourceModule;
        
        UnsubscribeRequest req{
            .subscriber_mailbox_id = config_.mailbox_id
        };
        
        mailbox_.send(req, SourceModule::mailbox_id);
        // Don't wait for reply - fire and forget
    }
};
```

#### 2.4 Updated continuous_loop
```cpp
void continuous_loop() {
    while (running_.load()) {
        // Wait for input data from subscribed source
        auto input_result = mailbox_.receive<InputData>(
            std::chrono::seconds(1)  // Reasonable timeout
        );
        
        if (!input_result) {
            if (input_result.error() == MailboxError::Timeout) {
                // Normal - no data yet, continue
                continue;
            } else {
                // Real error - log and continue
                // Could trigger error recovery here
                continue;
            }
        }
        
        // Process input -> output
        OutputDataT output{};
        process(input_result->message, output);
        
        // Publish to our subscribers
        publish(output);
    }
}
```

**Implementation Tasks**:
- ✅ Add SubscribeRequest/Reply/Unsubscribe message types
- ✅ Implement command_loop() with visitor pattern
- ✅ Add handle_subscribe()/handle_unsubscribe()
- ✅ Implement subscribe_to_source() for ContinuousInput
- ✅ Update continuous_loop() to receive from source
- ✅ Add MessageInfo to track sender/receiver
- ✅ Implement send_reply() helper
- ✅ Example: Producer → Consumer with automatic subscription (continuous_input_example.cpp)
- ✅ Ultra-clean interface with payload-only API (clean_interface_example.cpp)
- ✅ Runtime validation complete (February 4, 2026)

### Phase 3: Command System ✅ COMPLETE

**Status**: ✅ RUNTIME VALIDATED (February 4, 2026)

**Delivered Features**:
- ✅ Variadic CommandTypes template parameter pack
- ✅ Type-safe on_command<CmdT>() handler overloads
- ✅ Compile-time command validation with fold expressions
- ✅ Command registry construction from CommandTypes...
- ✅ Runtime command dispatch via visitor pattern
- ✅ No manual command ID checking or casting
- ✅ Example: Module with ResetCmd, CalibrateCmd, SetModeCmd (command_example.cpp)

**Implementation**:
```cpp
template<typename OutputT, typename InputT, typename... CommandTypes>
class Module {
    // User overrides on_command() for each command type
    template<typename CmdT>
    void on_command(const CmdT& cmd) { /* default: no-op */ }
    
    // Framework validates command types at compile-time
    static constexpr bool all_commands_registered = (
        Registry::template is_registered<CommandTypes>() && ...
    );
};
```

**Example Validated**:
```cpp
class ExtendedModule : public Module<TempData, PeriodicInput, 
                                     ResetCmd, CalibrateCmd, SetModeCmd> {
    void on_command(const ResetCmd& cmd) { /* handle reset */ }
    void on_command(const CalibrateCmd& cmd) { /* calibrate */ }
    void on_command(const SetModeCmd& cmd) { /* set mode */ }
};
```

### Phase 4: Module Discovery & Integration (PLANNED)
- [ ] ModuleRegistry for mailbox lookup
- [ ] Module ID management
- [ ] Subscriber/publisher patterns
- [ ] Inter-module communication examples
- [ ] Example: Multi-module system

### Phase 5: Advanced Features (Week 5+)
- [ ] Parameter tables using SeRTial reflection
- [ ] Statistics and monitoring
- [ ] Error handling and recovery
- [ ] Performance profiling hooks
- [ ] Data buffering with history (ring buffer)
- [ ] Timestamp-based data queries
- [ ] Migration guide from RACK

## Key Differences: RACK vs CommRaT Continuous Input

| Feature | RACK RackDataModule | CommRaT Module (Phase 2) |
|---------|---------------------|--------------------------|
| **Subscription** | MSG_GET_CONT_DATA command | SubscribeRequest message |
| **Publisher** | addListener() in producer | add_subscriber() in producer |
| **Data Push** | putDataBufferWorkSpace() | publish() to all subscribers |
| **Unsubscribe** | MSG_STOP_CONT_DATA | UnsubscribeRequest message |
| **Auto-subscribe** | Proxy->getContData() in moduleOn() | subscribe_to_source() in start() |
| **Period Negotiation** | Requested vs actual period | Same - actual_period in reply |
| **Type Safety** | void* buffers, manual pack | std::span<std::byte>, SeRTial auto |
| **Command Dispatch** | msgInfo->getType() switch | Visitor pattern + if constexpr |
| **Buffering** | Ring buffer with timestamps | Current data only (Phase 1) |
| **Historical Queries** | getDataBufferIndex(time) | Not yet (Phase 5) |
| **Proxy Layer** | RackDataProxy wrapper | Direct mailbox (no proxy) |
| **Message Types** | int8_t MSG_* defines | std::span<std::byte> typed messages |

## Phase 2 Detailed Implementation Plan

### 2.1 Add Subscription Message Types

Create `include/commrat/subscription_messages.hpp`:
```cpp
#pragma once
#include "commrat/messages.hpp"
#include <chrono>

namespace commrat {

// Subscription request from consumer to producer
struct SubscribeRequest {
    uint32_t subscriber_mailbox_id;
    std::chrono::milliseconds requested_period;
};

// Subscription reply from producer to consumer
struct SubscribeReply {
    std::chrono::milliseconds actual_period;
    bool success;
    uint32_t max_subscribers_reached;  // For diagnostics
};

// Unsubscribe request
struct UnsubscribeRequest {
    uint32_t subscriber_mailbox_id;
};

// Register message types
template<>
struct message_type_for<SubscribeRequest> {
    static constexpr MessageType value = MessageType::COMMAND;
};

template<>
struct message_type_for<SubscribeReply> {
    static constexpr MessageType value = MessageType::ACK;
};

template<>
struct message_type_for<UnsubscribeRequest> {
    static constexpr MessageType value = MessageType::COMMAND;
};

} // namespace commrat
```

### 2.2 Extend Mailbox with MessageInfo and send_reply

Update `mailbox.hpp`:
```cpp
// Message metadata for replies
struct MessageInfo {
    uint32_t sender_id;
    uint32_t receiver_id;
    MessageType type;
    rack_time_t timestamp;
};

template<typename... MessageTypes>
class Mailbox {
    // Existing receive with metadata
    template<typename T>
    auto receive() -> MailboxResult<ReceivedMessage<T>> {
        // ... includes MessageInfo in result ...
    }
    
    // Send reply to specific sender
    template<typename T>
    auto send_reply(const T& message, const MessageInfo& original_msg) 
        -> MailboxResult<void> 
    {
        return send(message, original_msg.sender_id);
    }
};
```

### 2.3 Update Module Template

Key changes to `module.hpp`:
1. Add subscription message handling to command_loop
2. Implement subscribe_to_source() for ContinuousInput
3. Add handle_subscribe/unsubscribe
4. Update start() to auto-subscribe
5. Update stop() to auto-unsubscribe

### 2.4 Create Example

`examples/continuous_input_example.cpp`:
```cpp
// Producer: Publishes sensor data periodically
class SensorModule : public Module<SensorData, PeriodicInput> {
    void process(SensorData& output) override {
        output.value = read_sensor();
        output.timestamp = get_time();
    }
};

// Consumer: Processes sensor data continuously
class FilterModule : public Module<FilteredData, ContinuousInput<SensorModule>> {
    void process(const SensorData& input, FilteredData& output) override {
        output.filtered_value = apply_filter(input.value);
        output.timestamp = input.timestamp;
    }
};

int main() {
    SensorModule sensor({.mailbox_id = 100, .period = 100ms});
    FilterModule filter({.mailbox_id = 101});
    
    sensor.start();
    filter.start();  // Automatically subscribes to sensor!
    
    // Data flows: sensor → filter automatically
    
    sleep(5);
    
    filter.stop();  // Automatically unsubscribes
    sensor.stop();
}
```

## Summary

**Phases 1-3 COMPLETE and RUNTIME VALIDATED** (February 4, 2026)

Phase 1 delivered a working foundation with:
- ✅ Type-safe module framework
- ✅ Three input modes (Periodic/Loop/Continuous)
- ✅ Compile-time optimizations
- ✅ Manual subscriber management
- ✅ Direct RegistryMailbox integration
- ✅ RAII lifecycle (start/stop)

Phase 2 delivered RACK-style automatic subscription:
- ✅ Subscription protocol (SubscribeRequest/Reply, UnsubscribeRequest/Reply)
- ✅ Command handling system with visitor pattern
- ✅ Automatic subscription for ContinuousInput
- ✅ Producer-consumer data flow validated
- ✅ Message ID system (0xPSMM format) with auto-increment
- ✅ Compile-time collision detection
- ✅ Ultra-clean payload-only API
- ✅ CombinedRegistry auto-includes system messages
- ✅ Module<OutputData, InputMode> alias (registry hidden)

Phase 3 delivered type-safe command system:
- ✅ Variadic CommandTypes template parameter
- ✅ on_command<CmdT>() handler overloads
- ✅ Compile-time command validation
- ✅ Runtime dispatch via visitor pattern
- ✅ No manual ID checking or casting

**Examples Runtime Validated**:
- ✅ continuous_input_example.cpp (Producer→Consumer with auto-subscription, 411KB)
- ✅ clean_interface_example.cpp (Ultra-clean Module<> interface, 411KB)
- ✅ command_example.cpp (Variadic commands with 3 command types, 318KB)

**Next Steps**:
- Phase 4: Module Registry (runtime discovery, mailbox lookup)
- Phase 5: Advanced Features (parameter tables, statistics, error recovery, data buffering)
- Domain Examples: Chassis simulator, Camera module, Object recognition, LIDAR processing

CommRaT modules now have feature parity with RACK's continuous data system while delivering modern C++20 type safety, zero-overhead abstractions, and an ultra-clean user interface where users define messages ONCE and use payload types everywhere.
- [ ] Parameter tables using SeRTial reflection
- [ ] Statistics and monitoring
- [ ] Error handling and recovery
- [ ] Performance profiling hooks
- [ ] Migration guide from RACK

## Example: Simple Chassis Simulator

```cpp
// Define output data
struct ChassisData {
    float x, y, theta;
    float vx, vy, omega;
};

// Define commands
struct SetVelocityCmd {
    using Payload = VelocityCommand;
    static constexpr uint32_t id = 0x201;
};

// Implementation
class ChassisSim : public CommRaT::Module<
    ChassisData,
    CommRaT::PeriodicInput,
    SetVelocityCmd
> {
public:
    ChassisSim(const Config& cfg) 
        : Module(cfg)
        , state_{}
    {}
    
protected:
    void on_init() override {
        // Load parameters from config
        max_velocity_ = config_.get<"max_velocity">();
    }
    
    void process(ChassisData& out) override {
        // Apply command
        state_.vx = current_cmd_.vx;
        state_.vy = current_cmd_.vy;
        state_.omega = current_cmd_.omega;
        
        // Integrate
        float dt = output_period_.count() / 1000.0f;
        state_.x += state_.vx * dt;
        state_.y += state_.vy * dt;
        state_.theta += state_.omega * dt;
        
        // Output
        out = state_;
    }
    
    void on_command(const SetVelocityCmd::Payload& cmd) override {
        current_cmd_ = cmd;
    }
    
private:
    ChassisData state_;
    VelocityCommand current_cmd_;
    float max_velocity_;
};

// Usage
int main() {
    CommRaT::ModuleConfig cfg{
        .mailbox_id = 100,
        .period = std::chrono::milliseconds(100)
    };
    
    ChassisSim sim(cfg);
    sim.start();
    
    // Module runs in background, publishes at 10Hz
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
    sim.stop();
}
```

## Example: Processing Pipeline

```cpp
// Camera module (periodic, publishes images)
class CameraModule : public CommRaT::Module<
    CameraImage,
    CommRaT::PeriodicInput
> {
    void process(CameraImage& out) override {
        // Capture from hardware
        capture_frame(out);
    }
};

// Object recognition (continuous, processes images)
class ObjRecogModule : public CommRaT::Module<
    ObjectList,
    CommRaT::ContinuousInput<CameraModule>
> {
    void process(const CameraImage& in, ObjectList& out) override {
        // Detect objects in image - input comes directly via mailbox
        detect_objects(in, out);
    }
};

// Usage - no proxies needed!
CameraModule camera(cam_config);
ObjRecogModule obj_recog(recog_config);

// Connect modules via mailbox IDs
camera.add_subscriber(obj_recog.mailbox_id());

camera.start();
obj_recog.start();  // Receives from camera's mailbox

// Pipeline runs: Camera→ObjRecog automatically
```

## Performance Guarantees

### Compile-Time Optimizations
1. All type dispatch via `if constexpr` (zero runtime cost)
2. Buffer sizes from `sertial::Message<T>::max_buffer_size`
3. Command registry using template parameter packs
4. No virtual function calls in hot path (CRTP)
5. Message routing via compile-time MessageRegistry

### Runtime Guarantees
1. No malloc in process() loop
2. No malloc in send/receive paths (SeRTial stack buffers)
3. Lock-free mailbox operations where possible
4. Thread affinity support for RT scheduling
5. Bounded execution time (no unbounded loops)

## Migration Strategy

### For Module Developers

```cpp
// Old RACK style:
class MyModule : public RackRflDataModule<MyModuleTraits, PeriodicInput, MyModule> {
    int dataLoop(MyData_& out) override {
        // Process
    }
};

// New CommRaT style:
class MyModule : public CommRaT::Module<MyData, CommRaT::PeriodicInput> {
    void process(MyData& out) override {
        // Same processing logic
    }
};
```

### Breaking Changes
1. No more void* buffers
2. No more manual pack/unpack
3. No more RACK mailbox API
4. Configuration uses modern C++ structs instead of argTable_t[]

### Compatible Features
1. Input modes (Periodic/Loop/Continuous) - same concept
2. Lifecycle hooks (onInit → on_init)
3. Command patterns - simplified but compatible
4. CRTP for static polymorphism - same approach

## Open Questions

1. **Thread Management**: Use std::thread or custom RT thread wrapper?
2. **Configuration**: Keep argTable compatibility or pure modern config?
3. **Backwards Compatibility**: Support RACK modules in same system?
4. **Proxy Discovery**: How do modules find each other's mailboxes?
5. **Message Routing**: Point-to-point vs publish/subscribe?

## Next Steps

1. Create `include/commrat/module.hpp` with base template
2. Implement Phase 1 (PeriodicInput)
3. Write comprehensive examples in `examples/module_*.cpp`
4. Benchmark against RACK equivalent
5. Document migration guide

## References

- RACK RackRflDataModule v2: `/rfl/rack_rfl/core/rack_rfl_data_module_v2.h`
- CommRaT Mailbox: `/include/commrat/mailbox.hpp`
- SeRTial Integration: `/SeRTial/include/sertial/sertial.hpp`
