# Mailbox Architecture Cleanup Design

**Branch**: `feature/mailbox-cleanup`  
**Date**: February 20, 2026  
**Status**: Implementation Phase - I/O Classes Complete

## Inspiration

This architecture is inspired by the [RACK framework](https://github.com/smolorz/RACK) by Sebastian Smolorz. RACK's clean separation of concerns between data storage (buffers) and communication (mailboxes) provides a solid foundation for real-time systems.

**Key RACK principles adopted:**
- Mailboxes for communication only (not storage)
- Buffers for data storage (separate concern)
- Clear ownership and responsibility
- Type-safe interfaces throughout

## Motivation

Current mailbox architecture has grown organically with multiple mailbox types:
- `BlankMailbox` - Untyped, raw TiMS wrapper
- `TypedMailbox<T...>` - Type-safe wrapper
- `RegistryMailbox` - Variant-based dispatch (duplicates TypedMailbox)
- `HistoricalMailbox` - Ring buffer wrapper (wrong abstraction)

**Problems:**
1. Confusion between RegistryMailbox and TypedMailbox (same functionality)
2. HistoricalMailbox mixes storage with communication
3. Too many mailbox abstractions
4. Unclear ownership and responsibility

## New Clean Architecture

### Single Mailbox Type: TypedMailbox

**Merge RegistryMailbox into TypedMailbox** - they're the same concept:
```cpp
template<typename... MessageTypes>
class TypedMailbox {
    // Type-safe send/receive
    // Variant-based receive_any()
    // All existing functionality
};
```

**Helper factories** for clarity:
```cpp
template<typename App, typename OutputType>
auto CreateCommandMailbox() -> TypedMailbox<SystemCommands..., UserCommands<OutputType>...>;

template<typename InputType>
auto CreateDataMailbox() -> TypedMailbox<InputType>;
```

### Per-Output Components

**IMPLEMENTED**: `BufferedOutput<CommratApp, T, SLOTS>` (single class, no hierarchy)

```cpp
template<typename CommratApp, typename T, std::size_t SLOTS = 100>
class BufferedOutput {
    TimestampedRingBuffer<TimsMessage<T>, SLOTS> buffer_;  // Timestamped storage
    TypedMailbox<Commands...> cmd_mbx_;                     // Communication channel
    
    auto publish(const T& data, Timestamp ts) -> MailboxResult<void>;
    auto getData(uint64_t timestamp, Milliseconds tolerance, InterpolationMode mode) 
        -> std::optional<TimsMessage<T>>;
};
```

**Combined functionality:**
- **TimestampedRingBuffer**: Stores recent outputs with timestamp indexing (from HistoricalMailbox)
- **Command Mailbox**: Receives Subscribe/Unsubscribe + user commands
- **Dual purpose**: Supports both publish() for ContinuousInput AND getData() for SyncedInput
- **No tuple complexity**: Strongly typed for single type T (no per-type tuple like HistoricalMailbox)

**Command Mailbox Details:**
- **Receives** system requests: `SubscribeRequest`, `UnsubscribeRequest`, etc.
- **Receives** user-defined commands specific to this output type
- **Sends** subscription acknowledgments to subscriber's WorkMailbox
- **Type**: `TypedMailbox<SystemCommands..., UserCommands<OutputType>...>`
- **Address**: Per-output (depends on type_id) - REQUIRED by TiMS addressing

**Subscription Flow:**
1. Consumer's WorkMailbox → `SubscribeRequest` → Producer's CommandMailbox
2. Producer's CommandMailbox → `SubscriptionAck` → Consumer's WorkMailbox
3. Producer's CommandMailbox → Data → Consumer's DataMailbox (ongoing)

**File**: `include/commrat/module/io/output/buffered_output.hpp`

### Per-Input Components

**IMPLEMENTED**: Three input classes with inheritance hierarchy

**1. CmdInput<CommratApp, T>** - Base class (command-only)
```cpp
class CmdInput {
    SystemId producer_system_id_;
    InstanceId producer_instance_id_;
    uint32_t producer_cmd_address_;  // Producer's command mailbox address
    
    template<typename CmdType>
    auto send_command(const CmdType& command) -> MailboxResult<void>;
};
```
- Send commands to remote module without receiving data
- Does NOT participate in process()
- Calculates producer's command mailbox address

**2. ContinuousInput<CommratApp, T> : public CmdInput** - Push model streaming
```cpp
class ContinuousInput : public CmdInput<CommratApp, T> {
    TypedMailbox<T> data_mbx_;           // Receive data stream
    WorkMailbox& work_mbx_;              // Reference to module's shared work mailbox
    Duration poll_timeout_;
    
    auto subscribe() -> MailboxResult<void>;
    auto poll_data() -> MailboxResult<TimsMessage<T>>;
};
```
- Receives continuous data stream via data_mbx_
- subscribe/unsubscribe via module's shared work_mbx_
- poll_data() blocks waiting for data → feeds process()
- NO local buffer (producer has BufferedOutput)

**3. SyncedInput<CommratApp, T> : public CmdInput** - Pull model timestamp sync
```cpp
class SyncedInput : public CmdInput<CommratApp, T> {
    std::optional<std::reference_wrapper<WorkMailbox>> work_mbx_;  // Optional
    Duration tolerance_;
    InterpolationMode interpolation_;
    
    auto get_data(const Timestamp& timestamp) -> std::optional<T>;
    bool is_valid() const;
    bool is_fresh() const;
};
```
- get_data(timestamp) queries producer's BufferedOutput (RPC)
- NO data_mbx_ (no continuous stream, only RPC)
- Optional work_mbx_ (may not need subscription for getData)
- Tolerance and interpolation configuration
- Tracks validity/freshness metadata

**Files**: 
- `include/commrat/module/io/input/cmd_input.hpp`
- `include/commrat/module/io/input/continuous_input.hpp`
- `include/commrat/module/io/input/synced_input.hpp`

## Migration Strategy

### Phase 1: TypedMailbox Unification (PENDING)
1. Audit all RegistryMailbox usage
2. Replace with TypedMailbox
3. Remove RegistryMailbox class
4. Update tests

### Phase 2: Remove HistoricalMailbox (IN PROGRESS)
1. ✅ Move timestamped buffer logic into BufferedOutput
2. ✅ Create input/output class hierarchy
3. ⏳ Remove HistoricalMailbox class
4. ⏳ Update existing modules to use new I/O classes
5. ⏳ Update tests to use new I/O classes

### Phase 3: Update Module Framework (PENDING)
1. Update Module base class to use BufferedOutput/Input classes
2. Integrate subscription protocol with new command mailboxes
3. Implement publish() and getData() RPC in framework
4. Wire up continuous vs synced input handling in process() dispatcher
3. Type-safe, single responsibility

### Phase 4: Cleanup (PENDING)
1. Remove RegistryMailbox (merge into TypedMailbox)
2. Remove BlankMailbox if unused
3. Update documentation
4. Clean up old test files

## Benefits (Achieved)

1. **Clear separation** - Storage (BufferedOutput) vs Communication (TypedMailbox)
2. **Type safety** - Compile-time guarantees, no tuple dispatch complexity
3. **RACK principles** - Buffers only on producer side, clean proxy pattern for inputs
4. **Three input patterns** - CmdInput (commands only), ContinuousInput (push), SyncedInput (pull)
5. **Single output type** - BufferedOutput supports both continuous and synced consumers
6. **No inheritance complexity** - Outputs are single class, inputs have clear hierarchy
7. **Timestamped access** - BufferedOutput uses TimestampedRingBuffer for efficient getData

## Implementation Summary

### Completed (Phase 2 - I/O Classes)

**Input Class Hierarchy** (✅ DONE):
```
CmdInput<CommratApp, T>              // Base: command-only interface
├── ContinuousInput<CommratApp, T>    // Push model: data_mbx + subscribe()
└── SyncedInput<CommratApp, T>        // Pull model: getData(timestamp)
```

**Output Class** (✅ DONE):
```
BufferedOutput<CommratApp, T, SLOTS>  // Single class: TimestampedRingBuffer + cmd_mbx
```

**Key Achievements**:
- ✅ Moved TimestampedRingBuffer from HistoricalMailbox to BufferedOutput
- ✅ Clean separation: storage (output) vs communication (mailboxes)
- ✅ No tuple complexity - each output strongly typed for single T
- ✅ Three input patterns for different communication needs
- ✅ Work mailbox shared at module level (inputs hold reference)
- ✅ Command mailbox per-output (required by TiMS addressing)

### Remaining Work

**Phase 2 Completion**:
- ⏳ Remove HistoricalMailbox class (functionality now in BufferedOutput)
- ⏳ Update existing modules to use new I/O classes
- ⏳ Update tests

**Phase 3 - Module Framework Integration**:
- ⏳ Wire BufferedOutput into Module base class
- ⏳ Implement subscription protocol (Subscribe/Ack flow)
- ⏳ Implement getData RPC for SyncedInput
- ⏳ Update process() dispatcher for input type handling

**Phase 4 - Cleanup**:
- ⏳ Merge RegistryMailbox into TypedMailbox
- ⏳ Remove unused mailbox types
- ⏳ Documentation updates

## Open Questions

1. ~~Should work_mailbox be per-input or shared per-module?~~
   - **RESOLVED**: Shared per-module, inputs hold reference
   - One work mailbox handles all subscriptions for the module
   - Cleaner, more memory efficient

2. Input data flow - continuous vs getData()?
   - **Continuous input**: Data goes directly to process(), no buffering needed
   - **getData()**: Needs producer's buffer (output-side storage)
   - Inputs never need local buffers - only outputs store data

3. ~~Command mailbox per-output vs shared?~~
   - **RESOLVED**: Per-output (required by TiMS addressing and type-specific commands)
   - TiMS addresses are type-dependent: calculate_address(sys_id, inst_id, **type_id**)
   - Each output type MUST have unique mailbox address for subscription protocol
   - Output-specific commands (SetSensorRate, SetFilter, etc.) are type-dependent
   - Cannot share command mailbox across different output types

## Implementation Notes

- Prototype classes in `include/commrat/module/io/`
- Keep backward compatibility during migration
- Update tests incrementally
- Document changes in USER_GUIDE.md

## Related Files

**Implemented**:
- `include/commrat/module/io/input/cmd_input.hpp`
- `include/commrat/module/io/input/continuous_input.hpp`
- `include/commrat/module/io/input/synced_input.hpp`
- `include/commrat/module/io/output/buffered_output.hpp`
- `include/commrat/module/io/module_input.hpp` (exports all input types)
- `include/commrat/module/io/module_output.hpp` (exports BufferedOutput)

**To Be Updated**:
- `include/commrat/mailbox/typed_mailbox.hpp` - Primary mailbox type
- `include/commrat/mailbox/registry_mailbox.hpp` - TO BE REMOVED (merge into TypedMailbox)
- `include/commrat/mailbox/historical_mailbox.hpp` - TO BE REMOVED (functionality in BufferedOutput)
- `include/commrat/registry_module.hpp` - Update to use new I/O classes
- `include/commrat/mailbox/historical_mailbox.hpp` - TO BE REMOVED
- `include/commrat/module/io/module_input.hpp` - New input abstraction
- `include/commrat/module/io/module_output.hpp` - New output abstraction
