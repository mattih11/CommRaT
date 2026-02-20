# Mailbox Architecture Cleanup Design

**Branch**: `feature/mailbox-cleanup`  
**Date**: February 20, 2026  
**Status**: Design Phase

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

Each output gets TWO components:

**1. Data Buffer (Storage)**
```cpp
template<typename T, size_t SLOTS>
class ModuleOutput {
    sertial::RingBuffer<T, SLOTS> buffer_;      // Stores recent outputs
    TypedMailbox<Commands...> cmd_mailbox_;     // Communication channel
};
```
- Ring buffer stores recent outputs for publishing to subscribers
- Only outputs have buffers (inputs receive directly into process())

**2. Command Mailbox (Communication)**
- **Receives** system requests: `SubscribeRequest`, `UnsubscribeRequest`, etc.
- **Receives** user-defined commands specific to this output type
- **Sends** subscription acknowledgments to subscriber's WorkMailbox
- Publishes outputs to subscribers using the buffer
- Type: `TypedMailbox<SystemCommands..., UserCommands<OutputType>...>`

**Subscription Flow:**
1. Consumer's WorkMailbox → `SubscribeRequest` → Producer's CommandMailbox
2. Producer's CommandMailbox → `SubscriptionAck` → Consumer's WorkMailbox
3. Producer's CommandMailbox → Data → Consumer's DataMailbox (ongoing)

### Per-Input Components

Each input gets ONE mailbox + reference to module's work mailbox:

**1. Data Mailbox (Receive Channel)**
```cpp
template<typename T>
class ModuleInput {
    TypedMailbox<T> data_mbx_;                      // Receive data from producer
    TypedMailbox<SubscriptionAck>& work_mbx_;       // Reference to module's work mailbox
};
```

**2. Work Mailbox Reference (Subscription Protocol)**
- References the **module's shared work mailbox**
- Input uses it to **send** `SubscribeRequest` to producer's CommandMailbox
- Module **receives** `SubscriptionAck` from producers
- Shared across all inputs in the module
- Type: `TypedMailbox<SubscriptionAck, UnsubscribeAck, ...>`

## Migration Strategy

### Phase 1: TypedMailbox Unification
1. Audit all RegistryMailbox usage
2. Replace with TypedMailbox
3. Remove RegistryMailbox class
4. Update tests

### Phase 2: Remove HistoricalMailbox
1. Move ring buffer logic into ModuleInput/ModuleOutput
2. Mailboxes are ONLY for communication
3. Storage is separate concern

### Phase 3: Implement ModuleInput/ModuleOutput
1. Complete prototype implementations
2. Clear separation: storage vs communication
3. Type-safe, single responsibility

### Phase 4: Update Module Framework
1. Registry module uses new input/output classes
2. Remove mailbox_set complexity
3. Simpler, clearer architecture

## Benefits

1. **Single mailbox concept** - TypedMailbox for all communication
2. **Clear separation** - Storage (RingBuffer) vs Communication (Mailbox)
3. **Type safety** - Compile-time guarantees throughout
4. **Simpler mental model** - Fewer abstractions to learn
5. **Future optimization** - Easy to share work mailboxes, optimize storage

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

- `include/commrat/mailbox/typed_mailbox.hpp` - Primary mailbox type
- `include/commrat/mailbox/registry_mailbox.hpp` - TO BE REMOVED
- `include/commrat/mailbox/historical_mailbox.hpp` - TO BE REMOVED
- `include/commrat/module/io/module_input.hpp` - New input abstraction
- `include/commrat/module/io/module_output.hpp` - New output abstraction
