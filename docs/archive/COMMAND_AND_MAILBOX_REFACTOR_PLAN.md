# Command System Refactoring Plan

**Date**: February 11, 2026  
**Status**: Mailbox Sizing COMPLETE | Command Refactoring IN PROGRESS  
**Priority**: Medium (improves API consistency and type safety)

**Note**: Mailbox buffer sizing (Phase 1) is complete and working. This document now focuses on remaining command system improvements (Phases 2-3).

---

## Problems Identified

### 1. Mailbox Buffer Oversizing

**Current Behavior:**
```cpp
// ALL mailboxes use Registry::max_message_size
constexpr size_t buffer_size = Registry::max_message_size;  // e.g., 4096 bytes
```

**Problem:**
- CMD mailbox allocates space for largest message in entire registry
- DATA mailbox allocates space for largest message in entire registry
- WORK mailbox needs only system messages (SubscribeRequest, etc.)
- Continuous input mailboxes only need their specific input type

**Example Waste:**
```cpp
// Registry has: TinyCmd (8 bytes), HugeData (2048 bytes)
// CMD mailbox only handles TinyCmd but allocates 2048 bytes per slot!
// With 10 slots = 20,480 bytes wasted (should be 80 bytes)
```

### 2. No Message Type Validation

**Current Behavior:**
- Mailboxes accept any message from registry
- No compile-time or runtime check that message belongs to mailbox

**Problem:**
- Type safety gap - wrong messages accepted
- Runtime errors instead of compile-time errors

### 3. Inconsistent Command Specification

**Current:**
```cpp
Module<Registry, Output<T>, Input<U>, ResetCmd, CalibrateCmd>  // Raw types
```

**Problem:**
- Commands are raw types, not wrapped like Input<>/Output<>
- Inconsistent with I/O specification pattern
- Harder to extract and validate command types

### 4. No Command-to-Output Type Checking

**Current:**
- Commands are independent of module outputs
- No validation that command makes sense for module

**Problem:**
- Can send ResetCmd to module that doesn't handle it
- No compile-time link between command and target module

---

## Proposed Solutions

### Solution 1: Specialized Mailbox Buffer Sizes

#### Phase 1A: Template-Based Maximum Size Calculation

Add compile-time size calculation for specific message subsets:

```cpp
// In message_registry.hpp
template<typename... SpecificTypes>
static constexpr size_t max_size_for_types() {
    return std::max({
        sertial::Message<TimsMessage<SpecificTypes>>::serialized_size()...
    });
}
```

#### Phase 1B: Mailbox Type Subsets

Create specialized mailbox templates that know their message types:

```cpp
// Current: Mailbox knows full Registry
template<typename Registry>
class Mailbox {
    static constexpr size_t buffer_size = Registry::max_message_size;  // TOO LARGE
};

// Proposed: Mailbox knows its specific message types
template<typename Registry, typename... AllowedTypes>
class TypedMailbox {
    static constexpr size_t buffer_size = Registry::template max_size_for_types<AllowedTypes...>();
    
    // Compile-time validation
    template<typename T>
    auto send(const T& msg, uint32_t dest) {
        static_assert((std::is_same_v<T, AllowedTypes> || ...), 
                      "Message type not allowed in this mailbox");
        // ...
    }
};
```

#### Phase 1C: Module Mailbox Creation

Update Module to create typed mailboxes with correct sizes:

```cpp
class Module<Registry, Output<DataA>, Input<DataB>, Command<CmdX>> {
    // CMD mailbox: only handles CmdX
    using CmdMailboxType = TypedMailbox<Registry, CmdX>;
    CmdMailboxType cmd_mailbox_;  // buffer_size = sizeof(TimsMessage<CmdX>)
    
    // DATA mailbox: only handles DataB (input type)
    using DataMailboxType = TypedMailbox<Registry, DataB>;
    DataMailboxType data_mailbox_;  // buffer_size = sizeof(TimsMessage<DataB>)
    
    // WORK mailbox: only handles system messages
    using WorkMailboxType = TypedMailbox<SystemRegistry, 
                                         SubscribeRequest, SubscribeReply, 
                                         UnsubscribeRequest, UnsubscribeReply>;
    WorkMailboxType work_mailbox_;  // buffer_size = max(system messages)
};
```

**Benefits:**
- 80-90% memory reduction for mailboxes with small messages
- Compile-time type safety - wrong messages rejected at compile time
- Self-documenting - mailbox type shows what it accepts

**Implementation Steps:**
1. Add `max_size_for_types<Ts...>()` to MessageRegistry
2. Create `TypedMailbox<Registry, AllowedTypes...>` template
3. Add static_assert in send/receive to validate message types
4. Update Module to use TypedMailbox for each mailbox
5. Extract message types from OutputSpec, InputSpec, CommandSpec

---

### Solution 2: Command Specification Types

#### Phase 2A: Add Command Wrappers to io_spec.hpp

```cpp
/**
 * @brief Single command specification
 * 
 * Specifies that a module handles exactly one command type.
 * 
 * @tparam T The command payload type
 * 
 * Example:
 * @code
 * class ControlModule : public Module<Registry, 
 *                                     Output<StatusData>, 
 *                                     PeriodicInput,
 *                                     Command<ResetCmd>> {
 *     void on_command(const ResetCmd& cmd) override {
 *         perform_reset();
 *     }
 * };
 * @endcode
 */
template<typename T>
struct Command {
    using PayloadType = T;
    static constexpr size_t count = 1;
};

/**
 * @brief Multiple commands specification
 * 
 * Specifies that a module handles multiple command types.
 * 
 * @tparam Ts... The command payload types
 * 
 * Example:
 * @code
 * class AdvancedModule : public Module<Registry,
 *                                      Output<StatusData>,
 *                                      PeriodicInput,
 *                                      Commands<ResetCmd, CalibrateCmd, SetModeCmd>> {
 *     void on_command(const ResetCmd& cmd) override { /* ... */ }
 *     void on_command(const CalibrateCmd& cmd) override { /* ... */ }
 *     void on_command(const SetModeCmd& cmd) override { /* ... */ }
 * };
 * @endcode
 */
template<typename... Ts>
struct Commands {
    using PayloadTypes = std::tuple<Ts...>;
    static constexpr size_t count = sizeof...(Ts);
    
    static_assert(count > 0, "Commands<> requires at least one type");
};

/**
 * @brief No commands specification (default)
 * 
 * Specifies that a module does not handle any commands.
 * This is the default if no command specification is provided.
 */
struct NoCommands {
    static constexpr size_t count = 0;
};
```

#### Phase 2B: Update Module Template Signature

```cpp
// OLD signature (raw command types)
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>  // Raw types
class Module;

// NEW signature (structured command spec)
template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename CommandSpec_ = NoCommands>  // Single structured spec
class Module;
```

#### Phase 2C: Normalization and Extraction

```cpp
// Normalize raw types and Command<T> to Commands<T>
template<typename T>
struct NormalizeCommandSpec {
    using Type = Commands<T>;  // Raw type → Commands<T>
};

template<typename T>
struct NormalizeCommandSpec<Command<T>> {
    using Type = Commands<T>;  // Command<T> → Commands<T>
};

template<typename... Ts>
struct NormalizeCommandSpec<Commands<Ts...>> {
    using Type = Commands<Ts...>;  // Already Commands<Ts...>
};

template<>
struct NormalizeCommandSpec<NoCommands> {
    using Type = NoCommands;  // No commands
};

// Extract command payload types
template<typename CommandSpec>
struct ExtractCommandPayloads;

template<typename... Ts>
struct ExtractCommandPayloads<Commands<Ts...>> {
    using Types = std::tuple<Ts...>;
};

template<>
struct ExtractCommandPayloads<NoCommands> {
    using Types = std::tuple<>;
};
```

**Migration Path:**
- OLD: `Module<Reg, Out, In, Cmd1, Cmd2>` still works (normalized internally)
- NEW: `Module<Reg, Out, In, Commands<Cmd1, Cmd2>>` preferred
- ALSO: `Module<Reg, Out, In, Command<Cmd1>>` for single command

---

### Solution 3: SendCommand Pattern

#### Phase 3A: SendCommand Type

```cpp
/**
 * @brief Specifies that this module can send commands to another module
 * 
 * @tparam TargetModuleType The type of the target module (for validation)
 * @tparam CommandType The command payload type to send
 * 
 * Example:
 * @code
 * class ControllerModule : public Module<Registry,
 *                                        Output<ControlData>,
 *                                        PeriodicInput,
 *                                        NoCommands,
 *                                        SendCommand<SensorModule, ResetCmd>> {
 * protected:
 *     ControlData process() override {
 *         if (need_reset) {
 *             send_command(ResetCmd{.hard_reset = true});
 *         }
 *         return ControlData{};
 *     }
 * };
 * @endcode
 */
template<typename TargetModuleType, typename CommandType>
struct SendCommand {
    using TargetModule = TargetModuleType;
    using Command = CommandType;
    
    // Compile-time validation: Target module must accept this command
    static_assert(/* TargetModule has CommandType in its CommandSpec */,
                  "Target module does not accept this command type");
};
```

#### Phase 3B: Command Sending API

```cpp
class Module {
protected:
    // Send command to another module (if SendCommand<Target, Cmd> specified)
    template<typename TargetModule, typename CmdType>
    void send_command(const CmdType& cmd) {
        static_assert(has_send_command_spec<TargetModule, CmdType>(),
                      "Module not configured to send this command to target");
        
        // Calculate target CMD mailbox address
        uint32_t target_cmd_mailbox = calculate_cmd_mailbox(/* target config */);
        
        // Send to target's CMD mailbox
        cmd_mailbox_.send(cmd, target_cmd_mailbox);
    }
};
```

**Alternative Design (Index-Based):**

```cpp
// If target module is specified by index in config
template<size_t TargetIndex, typename CmdType>
void send_command(const CmdType& cmd) {
    static_assert(TargetIndex < config_.command_targets.size(),
                  "Invalid command target index");
    
    uint32_t target_cmd_mailbox = config_.command_targets[TargetIndex].cmd_mailbox;
    cmd_mailbox_.send(cmd, target_cmd_mailbox);
}
```

---

### Solution 4: Command-Output Type Association (Optional)

#### Concept: Commands Linked to Output Types

```cpp
// Associate commands with output types they control
template<typename OutputType, typename... CommandTypes>
struct OutputWithCommands {
    using Output = OutputType;
    using Commands = std::tuple<CommandTypes...>;
};

// Example usage
class SensorModule : public Module<Registry,
                                   OutputWithCommands<SensorData, ResetCmd, CalibrateCmd>,
                                   PeriodicInput> {
    // Module outputs SensorData and accepts ResetCmd, CalibrateCmd
};
```

**Benefits:**
- Clear semantic link between output and its control commands
- Compile-time validation: Only modules outputting SensorData can be sent SensorData commands
- Self-documenting code

**Complexity:**
- Requires output type lookup when sending commands
- More template metaprogramming
- May be overkill for simple systems

**Decision:** Defer to Phase 4 (optional advanced feature)

---

## Implementation Status

### Phase 1: Mailbox Buffer Sizing - COMPLETE ✅

**Goal:** Reduce memory waste by sizing mailboxes based on actual message types

**Status**: FULLY IMPLEMENTED (February 15, 2026)

**What was completed:**

#### Phase 1A-1C: Basic TypedMailbox (February 10, 2026)
- ✅ Added `max_size_for_types<Ts...>()` to MessageRegistry
- ✅ Created `TypedMailbox<Registry, AllowedTypes...>` template with compile-time type validation
- ✅ Added static_assert in send/receive to validate message types
- ✅ Updated Module to create typed mailboxes (CMD, WORK, PUBLISH, DATA)
- ✅ Added type extraction helpers for OutputSpec/InputSpec/CommandSpec
- ✅ Verified 70-95% memory reduction in mailbox allocations

#### Phase 1D: ReceiveTypes/SendOnlyTypes Separation (February 15, 2026)
- ✅ Added `ReceiveTypes<Ts...>` and `SendOnlyTypes<Ts...>` tag types to TypedMailbox
- ✅ Specialization: `TypedMailbox<Registry, ReceiveTypes<Cmds...>, SendOnlyTypes<Outputs...>>`
- ✅ Buffer sized for ReceiveTypes only (SendOnly types don't affect buffer)
- ✅ CMD mailbox optimization: receive commands (small), send outputs (large)
  ```cpp
  // Buffer = max(Commands), but can send Commands + Outputs
  TypedMailbox<Registry, ReceiveTypes<Cmd1, Cmd2>, SendOnlyTypes<HugeOutput>>
  ```
- ✅ Send-only mailbox: `TypedMailbox<Registry, SendOnlyTypes<Outputs...>>` (minimal buffer)
- ✅ Updated MailboxSet to use ReceiveTypes/SendOnlyTypes pattern
- ✅ Updated module_types.hpp with `MakeTypedCmdMailboxWithSend` helper
- ✅ Conditional command thread spawning (`num_command_types > 0`)

**Result**: 
- Mailboxes use minimal memory based on their specific RECEIVE message types
- Send-only types don't inflate buffer size
- Type safety enforced at compile time (both send and receive)
- Modules without commands use send-only CMD mailboxes (no command buffer waste)
- **Typical memory savings: 70-95% per mailbox**

---

### Phase 2: Command Specification Types - DEFERRED

**Goal:** Unify command handling with Input/Output pattern

**Priority**: MEDIUM (deferred in favor of Phase 7.5 addressing work)

**Status**: Not started - current variadic `CommandTypes...` syntax works adequately

**Rationale for deferral:**
- Current command system works correctly with variadic template parameters
- More urgent: Fix per-output CMD mailbox addressing (Phase 7.5)
- TypedMailbox already provides compile-time type safety for commands
- Command<>/Commands<> wrapper would be nice-to-have, not critical

**Tasks (when resumed):**
1. Add `Command<T>`, `Commands<Ts...>`, `NoCommands` to io_spec.hpp
2. Add `NormalizeCommandSpec` trait
3. Add `ExtractCommandPayloads` trait
4. Update Module template signature: `CommandTypes...` → `CommandSpec_`
5. Update command dispatch to extract from CommandSpec
6. Update all examples to use new syntax
7. Add backward compatibility tests (raw types still work)
8. Update documentation (USER_GUIDE Section 9)

**Files to Modify:**
- `include/commrat/module/io_spec.hpp` (add Command types)
- `include/commrat/module/traits/command_traits.hpp` (NEW: normalization)
- `include/commrat/registry_module.hpp` (update template, dispatch)
- `examples/*.cpp` (migrate to Commands<> syntax)
- `docs/USER_GUIDE.md` (Section 9: Command Handling)

**Estimated Effort:** 6-8 hours (when prioritized)

---

### Phase 3: SendCommand Pattern - PLANNED

**Goal:** Enable compile-time validated command sending between modules

**Priority**: MEDIUM

**Tasks:**
1. Design SendCommand<Target, Cmd> specification
2. Add to Module template signature as optional parameter
3. Implement `send_command<Target, Cmd>()` helper
4. Add compile-time validation (target accepts command)
5. Add example: controller→sensor command sending
6. Document in USER_GUIDE Section 9

**Files to Modify:**
- `include/commrat/module/io_spec.hpp` (add SendCommand)
- `include/commrat/registry_module.hpp` (add send_command API)
- `examples/command_sender_example.cpp` (NEW: inter-module commands)
- `docs/USER_GUIDE.md` (Section 9: expand with SendCommand)

**Estimated Effort:** 4-6 hours

---

### Phase 4: Command-Output Association (Priority: LOW - OPTIONAL)

**Goal:** Link commands semantically to output types they control

**Tasks:**
1. Design OutputWithCommands<Output, Cmds...> pattern
2. Evaluate usefulness vs complexity
3. Implement if benefits are clear
4. Add examples showing the pattern
5. Document rationale and use cases

**Decision Point:** Evaluate after Phase 3 complete. May be unnecessary if SendCommand is sufficient.

**Estimated Effort:** TBD (if pursued)

---

## Memory Savings Estimation

### Current State (Example System)

```cpp
// Registry: 10 message types
// Sizes: Cmd1=16B, Cmd2=24B, Data1=128B, Data2=256B, ..., HugeData=2048B
// Registry::max_message_size = 2048 bytes

// Module with Output<Data1>, Input<Data2>, Commands<Cmd1, Cmd2>
// Current mailbox sizes:
//   CMD  mailbox: 2048 bytes × 10 slots = 20,480 bytes (handles Cmd1, Cmd2)
//   WORK mailbox: 2048 bytes × 10 slots = 20,480 bytes (handles system msgs)
//   DATA mailbox: 2048 bytes × 10 slots = 20,480 bytes (handles Data2)
// TOTAL: 61,440 bytes

// Optimized mailbox sizes:
//   CMD  mailbox: 24 bytes × 10 slots = 240 bytes (Cmd1=16, Cmd2=24, max=24)
//   WORK mailbox: 64 bytes × 10 slots = 640 bytes (system msgs ~50 bytes)
//   DATA mailbox: 256 bytes × 10 slots = 2,560 bytes (Data2=256)
// TOTAL: 3,440 bytes

// SAVINGS: 58,000 bytes per module (94% reduction!)
```

### System-Wide Impact

```
// 20-module system:
// Current: 20 × 61,440 = 1,228,800 bytes (~1.2 MB)
// Optimized: 20 × 3,440 = 68,800 bytes (~67 KB)
// TOTAL SAVINGS: 1.16 MB (95% reduction!)
```

---

## Testing Strategy

### Unit Tests

1. **test_typed_mailbox.cpp**
   - Verify correct buffer size calculation
   - Test send/receive with allowed types
   - Test compile-time rejection of wrong types (static_assert)

2. **test_command_spec.cpp**
   - Verify Command<T> normalization
   - Verify Commands<Ts...> extraction
   - Test backward compatibility (raw types)

3. **test_send_command.cpp**
   - Test send_command<Target, Cmd>() API
   - Verify compile-time validation of target
   - Test inter-module command sending

### Integration Tests

1. **test_mailbox_memory.cpp**
   - Measure actual memory usage before/after
   - Verify expected savings (70-95%)

2. **test_command_system.cpp**
   - Full command flow: sender → receiver
   - Multiple command types
   - Error handling

---

## Migration Guide for Users

### Before (Current)

```cpp
class MyModule : public App::Module<
    Output<StatusData>,
    Input<SensorData>,
    ResetCmd, CalibrateCmd  // Raw types
> {
    void on_command(const ResetCmd& cmd) override { /* ... */ }
    void on_command(const CalibrateCmd& cmd) override { /* ... */ }
};
```

### After (Recommended)

```cpp
class MyModule : public App::Module<
    Output<StatusData>,
    Input<SensorData>,
    Commands<ResetCmd, CalibrateCmd>  // Wrapped in Commands<>
> {
    void on_command(const ResetCmd& cmd) override { /* ... */ }
    void on_command(const CalibrateCmd& cmd) override { /* ... */ }
};
```

**Backward Compatibility:** Old syntax still works (automatically normalized)

---

## Open Questions

1. **SendCommand target specification:**
   - By module type? `SendCommand<SensorModule, ResetCmd>`
   - By index? `send_command<0>(cmd)` with config.command_targets[0]
   - By ID? `send_command(cmd, target_system_id, target_instance_id)`

2. **Command-output association:**
   - Worth the complexity?
   - Or is SendCommand validation sufficient?

3. **Dynamic command targets:**
   - Should modules discover command targets at runtime?
   - Or compile-time static configuration only?

4. **SeRTial buffer aliases:**
   - Use `sertial::FixedBuffer<N>` instead of `std::array<std::byte, N>`?
   - Performance benefits?

---

## Success Criteria

**Phase 1 Complete:** ✓
- Mailbox buffer sizes reduced by 70-95%
- Compile-time type validation for all mailbox operations
- No runtime performance regression
- Currently in production use

**Phase 2 Complete:**
- Command specification consistent with Input/Output pattern
- Full backward compatibility maintained
- Examples and documentation updated

**Phase 3 Complete:**
- Inter-module command sending with compile-time validation
- Example demonstrating controller→sensor commands
- Documentation complete

---

## Timeline

- **Phase 1:** Days 1-2 (Mailbox sizing)
- **Phase 2:** Days 3-4 (Command specs)
- **Phase 3:** Day 5 (SendCommand)
- **Testing & Documentation:** Day 6
- **Total:** ~1 week for Phases 1-3

---

## Next Steps

1. Review this plan
2. Approve or suggest modifications
3. Start Phase 1 implementation
4. Test and validate Phase 1
5. Proceed to Phase 2

