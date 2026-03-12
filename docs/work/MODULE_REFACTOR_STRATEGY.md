# Module Refactor Strategy

## Current State (Phase 1.5 COMPLETE)

✅ **Completed**:
1. I/O specification tags: Output<T>, Input<T>, Period<D>, SyncedInput<T>
2. Type traits for tags and instances
3. Execution mode validation and auto-inference (Input/Period/Loop)
4. CreateIOInstance mapper (tag → instance type)
5. BuildIOTuple (compile-time tuple construction)
6. Consolidated io_spec.hpp (removed duplicates)
7. Module2 with I/O tuple architecture
8. Zero-copy workspace API (ModuleOutput)
9. Structured I/O access (inputs()/outputs() named fields)
10. GetData protocol (SyncedInput consumer + ModuleOutput producer)
11. GetData handlers tested and validated

⏳ **In Progress (Phase 1.6)**:
1. Subscription protocol (Subscribe/Unsubscribe handlers)
2. TypedMailbox/WorkMailbox template fixes
3. Continuous data streaming

❌ **Remaining Issues**:
1. Module2 not used in production (Module still primary)
2. Command association not integrated into Module2
3. WORK mailbox compilation issues (TypedMailbox template)
4. Subscription handlers not implemented
5. No integration tests for complete Module2 flow

## Goal: Clean Module Architecture

### Desired Module Template

```cpp
template<typename Registry, typename... IOSpecs>
class Module {
    // IOSpecs are:
    // - Output<DataType> or Output<DataWithCommands<...>>
    // - Input<DataType> (continuous, input-driven)
    // - SyncedInput<DataType> (get_data, secondary)
    // - Period<Duration> (timer-driven, auto-inferred if no Input)
    // - (Loop mode auto-inferred if neither Input nor Period)
};
```

### Example Usage

```cpp
// Define data with commands
using SensorData = Message::DataWith<SensorPayload>::Commands<
    CalibrateCmd,
    SetRateCmd
>;

// Input-driven module
using SensorModule = Module<
    MyRegistry,
    Output<SensorData>,           // Output with commands
    Input<PressureData>,          // Continuous input (drives execution)
    SyncedInput<IMUData>          // Synchronized input (secondary)
>;

// Timer-driven module  
using PeriodicSensor = Module<
    MyRegistry,
    Output<SensorData>,
    Period<Milliseconds(100)>     // 100Hz periodic execution
>;

// Loop-driven (auto-inferred)
using LoopSensor = Module<
    MyRegistry,
    Output<SensorData>            // Maximum throughput
>;
```

## Refactor Plan

### Phase 1: I/O Tuple Infrastructure

#### 1.1 I/O Specification System ✅ COMPLETE

**Files created:**
- `include/commrat/module/io/io_spec.hpp` - Complete I/O system
- `include/commrat/module/io/synced.hpp` - Zero-copy Synced<T> wrapper
- `include/commrat/module/io/output/module_output.hpp` - Zero-copy output
- `include/commrat/module/io/input/synced_input.hpp` - Pull-based input

**Features:**
```cpp
// I/O Tags
Output<T>, Input<T>, Period<D>, SyncedInput<T>

// Type traits
is_output_v<T>, is_input_v<T>, is_period_v<T>, is_synced_input_v<T>

// Execution mode validation
ValidateExecutionMode<IOSpecs...>
- Auto-infers: Input-driven, Timer-driven, or Loop-driven
- Validates: At most one Input<T>, at most one Period<D>, mutually exclusive

// Instance creation
CreateIOInstance<Registry, IOSpec>::type
- Output<T> → ModuleOutput<Registry, T>
- Input<T> → ContinuousInput<Registry, T>
- SyncedInput<T> → SyncedInputImpl<Registry, T>
- Period<D> → void (config only)

// Tuple builder
BuildIOTuple<Registry, IOSpecs...>::type
- Returns: std::tuple<ModuleOutput<...>, ContinuousInput<...>, ...>
- Filters out void types
- Provides metadata: num_outputs, is_input_driven, etc.

// GetData protocol
- GetDataRequest/GetNextDataRequest auto-registration
- SyncedInput::get_data(timestamp) - consumer RPC
- ModuleOutput::handle_get_data_request() - producer handler
- Zero-copy buffer search with InterpolationMode
```
#### 1.2 Module Integration ✅ COMPLETE

**Goal**: Integrate BuildIOTuple into Module2<>

**Status**: Module2 created with full I/O tuple architecture

```cpp
// include/commrat/module2.hpp

template<typename Registry, typename... IOSpecs>
class Module2 {
private:
    // I/O tuple (compile-time constructed)
    using IOBuilder = BuildIOTuple<Registry, IOSpecs...>;
    using IOTuple = typename IOBuilder::type;
    IOTuple io_instances_;
    
    // Execution mode (compile-time constants)
    static constexpr bool is_input_driven = IOBuilder::is_input_driven;
    static constexpr bool is_timer_driven = IOBuilder::is_timer_driven;
    static constexpr bool is_loop_driven = IOBuilder::is_loop_driven;
    
public:
    // Structured I/O access (named fields via reflect-cpp)
    auto inputs() { /* returns named tuple */ }
    auto outputs() { /* returns named tuple */ }
    
    // Zero-copy workspace API
    template<size_t N>
    auto& get_output_workspace() { return std::get<N>(outputs_).get_workspace(); }
    
    // Metadata access
    template<size_t N>
    uint64_t get_input_timestamp() { return std::get<N>(inputs_).get_timestamp(); }
};
```

**Deliverables**:
- [x] Add BuildIOTuple_t member to Module2<>
- [x] Implement structured I/O access (inputs()/outputs())
- [x] Threading architecture (data_thread + command_threads)
- [x] Execution mode dispatch (input/timer/loop)
- [x] Zero-copy workspace integration
- [ ] Full integration tests
- [ ] Fix TypedMailbox/WorkMailbox issues

---

### Phase 2: Command Integration (Week 2)

**Goal**: Integrate DataWithCommands into Module and I/O classes

#### 2.1 Registry Auto-Expansion

```cpp
// include/commrat/messaging/message_registry.hpp

template<typename... MessageTypes>
class MessageRegistry {
private:
    // Expand DataWithCommands before processing
    using ExpandedMessages = ExpandAllDataWithCommands_t<MessageTypes...>;
    
    // Convert tuple to template parameters
    using ProcessedRegistry = /* convert tuple to MessageRegistry<...> */;
};
```

#### 2.2 Output Command Handlers

```cpp
// include/commrat/module/io/output/module_output.hpp

template<typename Registry, typename DataType>
class ModuleOutput {
private:
    // Extract commands from DataType
    using Commands = ExtractCommands_t<DataType>;
    
    // Generate command handler for each command
    template<typename CmdType>
    void handle_command(const TimsMessage<typename CmdType::Payload>& cmd,
                       TimsMessage<typename CmdType::ReplyType>& reply) {
        // Dispatch to user's on_command() handler
        static_cast<DerivedModule*>(this)->on_command(cmd, reply);
    }
    
    // Command dispatch loop
    void command_loop() {
        // Receive from cmd_mbx_
        // Dispatch to correct handler based on message type
    }
};
```

#### 2.3 Input Command Interface

```cpp
// CmdInput already extracts commands from OutputType
template<typename Registry, typename OutputType>
class CmdInput {
    using Commands = ExtractCommands_t<OutputType>;  // ✓ Already done
    
    // TODO: Add command validation in send_command
};
```

**Deliverables**:
- [ ] Registry auto-expansion of DataWithCommands
- [ ] Command handler generation in ModuleOutput
- [ ] Command dispatch in command_loop
- [ ] Input command validation
- [ ] Update examples to use DataWithCommands

---

### Phase 3: Mixin Simplification (Week 3)

**Goal**: Consolidate mixins using I/O tuple

#### 3.1 Current Mixins (Fragmented)

```
module/module_mixins.hpp
├── PeriodicMixin
├── ContinuousMixin
├── MultiInputMixin
├── OutputMixin
└── CommandMixin
```

#### 3.2 New Mixin Structure

```cpp
// include/commrat/module/mixins/io_mixin.hpp

template<typename Registry, typename IOTuple>
class IOMixin {
protected:
    IOTuple& io_instances_;
    
    // Generate methods for each I/O instance
    // - For each Output: publish(), handle_subscribe(), etc.
    // - For each Input: poll_data(), is_valid(), etc.
    // - For each SyncedInput: get_data(), is_fresh(), etc.
};
```

#### 3.3 Process Loop Mixin

```cpp
// include/commrat/module/mixins/process_mixin.hpp

template<typename Module, typename IOTuple>
class ProcessMixin {
    // Automatic process() signature generation based on I/O tuple
    // - Extract input types
    // - Extract output types
    // - Generate process(const Input1&, const Input2&, Output1&, Output2&)
};
```

**Deliverables**:
- [ ] Consolidate mixins into IOMixin
- [ ] Auto-generate process() signatures
- [ ] Remove old fragmented mixins
- [ ] Update Module to use new mixins

---

### Phase 4: Mailbox Management (Week 4)

**Goal**: Automatic mailbox creation and management

#### 4.1 Mailbox Calculator

```cpp
// include/commrat/module/mailbox/mailbox_calculator.hpp

template<typename Registry, typename... IOSpecs>
struct CalculateMailboxes {
    // How many CMD mailboxes? (one per output)
    static constexpr size_t num_cmd = /* count Output<T> */;
    
    // How many DATA mailboxes? (one per continuous input)
    static constexpr size_t num_data = /* count Input<T> */;
    
    // WORK mailbox? (if any inputs for subscription)
    static constexpr bool needs_work = /* has Input<T> or SyncedInput<T> */;
    
    // PUBLISH mailbox? (if any multi-output)
    static constexpr bool needs_publish = /* has Outputs<T, U, ...> */;
};
```

#### 4.2 Mailbox Tuple

```cpp
template<typename Registry, typename... IOSpecs>
class Module {
private:
    using MailboxInfo = CalculateMailboxes<Registry, IOSpecs...>;
    
    // Create mailboxes based on calculation
    std::array<Mailbox<Registry>, MailboxInfo::num_cmd> cmd_mailboxes_;
    std::array<Mailbox<Registry>, MailboxInfo::num_data> data_mailboxes_;
    std::optional<Mailbox<Registry>> work_mailbox_;
    std::optional<Mailbox<Registry>> publish_mailbox_;
};
```

**Deliverables**:
- [ ] Mailbox count calculator
- [ ] Automatic mailbox creation
- [ ] Mailbox address encoding
- [ ] Connect mailboxes to I/O instances

---

### Phase 5: Module Config Simplification (Week 5)

**Goal**: Simpler module configuration using I/O specs

#### 5.1 Current Config (Complex)

```cpp
struct ModuleConfig {
    std::string name;
    uint8_t system_id;
    uint8_t instance_id;
    std::optional<Duration> period;  // For periodic
    std::vector<InputSource> input_sources;  // For inputs
    Duration sync_tolerance;  // For synced inputs
    // Too many fields!
};
```

#### 5.2 New Config (Simple)

```cpp
// Base config (minimal)
struct ModuleConfig {
    std::string name;
    uint8_t system_id;
    uint8_t instance_id;
};

// I/O-specific configs
struct PeriodicConfig {
    Duration period;
};

struct InputSourceConfig {
    uint8_t source_system_id;
    uint8_t source_instance_id;
};

struct SyncedInputConfig : InputSourceConfig {
    Duration tolerance = Milliseconds(50);
    InterpolationMode mode = InterpolationMode::NEAREST;
};

// Module builder pattern
auto module = ModuleBuilder<MyRegistry>()
    .with_config({.name = "Sensor", .system_id = 10, .instance_id = 1})
    .with_output<SensorData>()
    .with_input<PressureData>({.source_system_id = 11, .source_instance_id = 1})
    .with_periodic({.period = Milliseconds(100)})
    .build();
```

**Deliverables**:
- [ ] Split config into base + I/O-specific
- [ ] ModuleBuilder for clean construction
- [ ] Update all examples

---

### Phase 6: Example Migration (Week 6)

**Goal**: Update all examples to new architecture

#### Examples to Update

1. `continuous_input_example.cpp`
   - Use `ContinuousInput` class
   - Use `DataWithCommands` for sensor data
   - Show command sending

2. `multi_input_example.cpp`
   - Use tuple of inputs
   - Show synced vs continuous
   - Demonstrate different input rates

3. `command_example.cpp`
   - Show command association
   - Demonstrate type-safe command sending
   - Show compile-time command extraction

**Deliverables**:
- [ ] Update all existing examples
- [ ] Create new examples for DataWithCommands
- [ ] Create new examples for input classes
- [ ] Update documentation

---

## Implementation Order

### Week 1: Foundation
1. Create I/O wrapper types
2. Build I/O tuple infrastructure
3. Update Module to store I/O tuple
4. Basic access methods (get_io<Index>, get_io<Type>)

### Week 2: Commands
1. Registry auto-expansion
2. Command handler generation
3. Command dispatch in ModuleOutput
4. Update CmdInput validation

### Week 3: Mixins
1. Create IOMixin with I/O tuple
2. Auto-generate process() signatures
3. Remove old mixins
4. Test compilation

### Week 4: Mailboxes
1. Mailbox calculator
2. Automatic mailbox creation
3. Address management
4. Connect to I/O instances

### Week 5: Configuration
1. Split config structures
2. Create ModuleBuilder
3. Update existing modules
4. Migration guide

### Week 6: Examples & Docs
1. Migrate examples
2. Update documentation
3. Create migration guide
4. Final testing

---

## Migration Guide (for users)

### Old Code (Current)

```cpp
using MyModule = Module<
    MyRegistry,
    Output<SensorData>,
    Input<PressureData>
>;

ModuleConfig config{
    .name = "Sensor",
    .system_id = 10,
    .instance_id = 1,
    .period = Milliseconds(100),
    .input_sources = {{11, 1}}
};

MyModule module(config);
```

### New Code (After Refactor)

```cpp
// Define data with commands
using SensorData = Message::DataWith<SensorPayload>::Commands<
    CalibrateCmd,
    SetRateCmd
>;

// Create module with builder
auto module = ModuleBuilder<MyRegistry>()
    .with_config({.name = "Sensor", .system_id = 10, .instance_id = 1})
    .with_output<SensorData>()
    .with_input<PressureData>({.source_system_id = 11, .source_instance_id = 1})
    .with_periodic({.period = Milliseconds(100)})
    .build();

// Or traditional template approach
using MyModule = Module<
    MyRegistry,
    Output<SensorData>,  // Now includes commands
    Input<PressureData>,
    PeriodicInput
>;

MyModule module({.name = "Sensor", .system_id = 10, .instance_id = 1},
                PeriodicConfig{.period = Milliseconds(100)},
                InputSourceConfig{.source_system_id = 11, .source_instance_id = 1});
```

---

## Testing Strategy

### Unit Tests

1. **I/O Tuple Tests**
   - Correct tuple construction
   - Type extraction
   - Access by index/type

2. **Command Tests**
   - Registry expansion
   - Command extraction
   - Handler generation

3. **Mailbox Tests**
   - Correct count calculation
   - Address encoding
   - Mailbox access

### Integration Tests

1. **Module Construction**
   - All I/O combinations
   - Config validation
   - Mailbox creation

2. **Runtime Tests**
   - Command sending
   - Data flow
   - Subscription protocol

---

## Success Criteria

- [ ] Module template has ≤3 template parameters
- [ ] I/O instances in compile-time tuple
- [ ] DataWithCommands fully integrated
- [ ] Command handlers auto-generated
- [ ] Mailboxes created automatically
- [ ] All examples compile and run
- [ ] No functionality regressions
- [ ] Code size reduction >30%
- [ ] Compile time increase <20%

---

## Open Questions

1. **Backward compatibility**: Support old Module<> syntax during transition?
2. **Builder vs Template**: Primary construction method?
3. **Config validation**: Compile-time or runtime?
4. **Process signature**: Auto-generate or require override?
5. **Error messages**: How to improve template error readability?

---

## Next Steps

1. Review this strategy with team
2. Prioritize phases
3. Create detailed task breakdown for Phase 1
4. Set up feature branch: `feature/module-refactor`
5. Begin Phase 1 implementation
