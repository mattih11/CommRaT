# CommRaT Architecture Analysis & Improvement Roadmap

**Date**: February 6, 2026  
**Status**: Phase 4 Complete - Analyzing Next Evolution

---

## Current State Assessment

### What We Have ✅

#### 1. **Compile-Time Message System**
- ✅ `MessageRegistry<MessageDefs...>` - Variadic template, all types known at compile time
- ✅ `MessageDefinition<PayloadT, Prefix, SubPrefix, LocalID>` - Compile-time ID calculation
- ✅ Zero runtime allocation for message dispatch (visitor pattern with compile-time type expansion)
- ✅ Automatic buffer size calculation: `max_message_size` computed via fold expressions
- ✅ Type-safe serialization via SeRTial (compile-time size validation)

**Verdict**: ✅ **Excellent foundation** - No changes needed to message system

#### 2. **3-Mailbox Architecture**
- ✅ Hierarchical addressing: `(system_id << 8) | instance_id` + mailbox offset
- ✅ Separate concerns:
  - CMD (base+0): User commands - blocking receive
  - WORK (base+16): Subscription protocol - blocking receive  
  - DATA (base+32): Input data streams - blocking receive
- ✅ Zero CPU when idle (blocking receives, not polling)
- ✅ Thread-per-mailbox model

**Verdict**: ✅ **Solid architecture** - Scales well, clear separation of concerns

#### 3. **Module Pattern**
Current signature:
```cpp
template<typename UserRegistry,
         typename OutputDataT_,
         typename InputModeT,
         typename... CommandTypes>
class Module { ... };
```

**Single Input/Output Model:**
- ✅ `OutputDataT_` - One output type per module
- ✅ `InputModeT` - One input mode: `PeriodicInput`, `LoopInput`, or `ContinuousInput<T>`
- ✅ `CommandTypes...` - Variadic commands (multiple command types supported)
- ✅ Virtual dispatch: `process()`, `process_continuous(const InputData&)`

**Limitations Identified:**
- ⚠️ **Single input constraint**: Can only process one input type via `ContinuousInput<T>`
- ⚠️ **Single output constraint**: Only `OutputDataT_` published to subscribers
- ⚠️ **Manual overload**: `process_continuous()` requires manual override
- ⚠️ **Command dispatch**: `on_command(const CmdT&)` works but relies on template dispatch, not automatic

**Verdict**: 🟡 **Good but limiting** - Ready for evolution to multi-I/O

---

## Proposed Evolution: Multi-Input/Multi-Output Modules

### Vision Statement

**Goal**: Support modules with multiple typed inputs and outputs, all resolved at compile time, with automatic generation of:
- Appropriate `process()` signatures with all inputs/outputs as parameters
- Separate subscription handling per input type
- Multiple publish operations per output type
- Type-safe command dispatch with optional reply mechanisms

### Design Principles

1. **Zero Runtime Cost**: All type dispatch via templates/concepts, no virtual overhead
2. **Intuitive API**: User sees simple `process(in1, in2, out1, out2)` - complexity hidden
3. **Compile-Time Validation**: Invalid configurations rejected with clear error messages
4. **Backward Compatible**: Existing single-I/O modules still work (special case of multi-I/O)

---

## Proposed Template Design

### 1. Input/Output Bundles (Tag Types)

```cpp
// Single input (current behavior - backward compatible)
template<typename T>
struct Input {
    using PayloadType = T;
    static constexpr size_t count = 1;
};

// Multiple inputs
template<typename... Ts>
struct Inputs {
    using PayloadTypes = std::tuple<Ts...>;
    static constexpr size_t count = sizeof...(Ts);
};

// Single output (current behavior)
template<typename T>
struct Output {
    using PayloadType = T;
    static constexpr size_t count = 1;
};

// Multiple outputs
template<typename... Ts>
struct Outputs {
    using PayloadTypes = std::tuple<Ts...>;
    static constexpr size_t count = sizeof...(Ts);
};

// No output (sink module)
struct NoOutput {
    static constexpr size_t count = 0;
};
```

### 2. Updated Module Signature

```cpp
template<typename UserRegistry,
         typename OutputSpec,      // Output<T>, Outputs<Ts...>, or NoOutput
         typename InputSpec,       // PeriodicInput, LoopInput, Input<T>, Inputs<Ts...>
         typename... CommandTypes>
class Module {
    // ... automatic machinery
};
```

### 3. Compile-Time Signature Generation

**Key Insight**: Use SFINAE/concepts to generate correct `process()` signature based on InputSpec/OutputSpec.

```cpp
// For PeriodicInput or LoopInput with single output
template<typename Out = OutputSpec, typename In = InputSpec>
    requires (is_periodic_or_loop<In> && is_single_output<Out>)
virtual typename Out::PayloadType process() {
    return typename Out::PayloadType{};
}

// For PeriodicInput or LoopInput with multiple outputs
template<typename Out = OutputSpec, typename In = InputSpec>
    requires (is_periodic_or_loop<In> && is_multi_output<Out>)
virtual void process(output_references<Out>... outputs) {
    // outputs expanded as: T1& out1, T2& out2, ...
}

// For Input<T> with single output
template<typename Out = OutputSpec, typename In = InputSpec>
    requires (is_single_input<In> && is_single_output<Out>)
virtual typename Out::PayloadType process(const typename In::PayloadType& input) {
    return typename Out::PayloadType{};
}

// For Inputs<Ts...> with single output
template<typename Out = OutputSpec, typename In = InputSpec>
    requires (is_multi_input<In> && is_single_output<Out>)
virtual typename Out::PayloadType process(input_const_references<In>... inputs) {
    // inputs expanded as: const T1& in1, const T2& in2, ...
    return typename Out::PayloadType{};
}

// For Inputs<Ts...> with Outputs<Us...>
template<typename Out = OutputSpec, typename In = InputSpec>
    requires (is_multi_input<In> && is_multi_output<Out>)
virtual void process(input_const_references<In>... inputs,
                     output_references<Out>... outputs) {
    // Full parameter expansion for all inputs and outputs
}
```

**Implementation Note**: Use `std::apply` with tuple machinery to expand parameter packs:

```cpp
// Helper to generate parameter pack from tuple types
template<typename TupleT>
struct tuple_to_params;

template<typename... Ts>
struct tuple_to_params<std::tuple<Ts...>> {
    template<typename Func>
    static auto call(Func&& func) {
        return std::forward<Func>(func)(std::declval<const Ts&>()...);
    }
};
```

### 4. Command Dispatch Evolution

**Current**: `void on_command(const CmdT& cmd)` - one-way command

**Proposed**: Support three patterns automatically:

```cpp
// Pattern 1: One-way command (fire and forget)
virtual void on_command(const ResetCmd& cmd) {
    // Handle reset, no reply
}

// Pattern 2: Command with reply (fills output parameter)
virtual void on_command(const GetStatusCmd& cmd, StatusReply& reply) {
    reply.temperature = current_temp_;
    reply.status = "OK";
}

// Pattern 3: Command with return value (alternative reply mechanism)
virtual StatusReply on_command(const GetStatusCmd& cmd) {
    return StatusReply{.temperature = current_temp_, .status = "OK"};
}
```

**Detection**: Use SFINAE to check function signature:
```cpp
template<typename CmdT, typename ReplyT = void>
concept has_reply = requires(Module* m, const CmdT& cmd, ReplyT& reply) {
    { m->on_command(cmd, reply) } -> std::same_as<void>;
};

template<typename CmdT>
concept returns_reply = requires(Module* m, const CmdT& cmd) {
    { m->on_command(cmd) } -> std::convertible_to<typename CmdT::ReplyType>;
};
```

---

## Technical Challenges & Solutions

### Challenge 1: Multiple Input Subscriptions

**Problem**: With `Inputs<TempData, PressureData, HumidityData>`, module must subscribe to 3 different source modules.

**Solution**: Generate separate data mailboxes per input type:
```cpp
// For Inputs<T1, T2, T3>:
std::tuple<
    std::unique_ptr<RegistryMailbox<T1>>,
    std::unique_ptr<RegistryMailbox<T2>>,
    std::unique_ptr<RegistryMailbox<T3>>
> input_mailboxes_;

// Each gets unique mailbox ID:
// DATA_0 = base + 32 + 0*16
// DATA_1 = base + 32 + 1*16
// DATA_2 = base + 32 + 2*16
```

**Subscription**: Each input requires source configuration:
```cpp
struct MultiInputConfig {
    struct InputSource {
        uint8_t system_id;
        uint8_t instance_id;
        std::chrono::milliseconds requested_period{0};
    };
    
    std::array<InputSource, N> input_sources;  // N = number of inputs
};
```

**Thread Model**: Two options:

**Option A - Single Thread with receive_any()** (Recommended):
```cpp
void multi_input_loop() {
    while (running_) {
        // Receive from any input mailbox (non-deterministic order)
        std::apply([this](auto&... mailboxes) {
            // Wait on any mailbox using select/poll pattern
            // Process whichever has data
        }, input_mailboxes_);
    }
}
```

**Option B - Thread per Input**:
- More threads, more overhead
- Deterministic per-input latency
- Requires thread-safe accumulation before calling `process()`

### Challenge 2: Multiple Output Publishing

**Problem**: `Outputs<CounterData, StatisticsData, DiagnosticsData>` means 3 separate subscriber lists and publish operations.

**Solution**: Expand subscriber lists per output type:
```cpp
// For Outputs<T1, T2, T3>:
std::tuple<
    std::vector<uint32_t>,  // subscribers to T1
    std::vector<uint32_t>,  // subscribers to T2
    std::vector<uint32_t>   // subscribers to T3
> output_subscribers_;

std::tuple<
    std::mutex,  // protects T1 subscribers
    std::mutex,  // protects T2 subscribers
    std::mutex   // protects T3 subscribers
> output_mutexes_;
```

**Publishing**: After `process()` returns:
```cpp
void publish_all_outputs(const OutputTuple& outputs) {
    std::apply([this](const auto&... output_values) {
        size_t idx = 0;
        (publish_single_output<idx>(output_values), ...);  // Fold expression
    }, outputs);
}

template<size_t Idx, typename T>
void publish_single_output(const T& data) {
    auto& subscribers = std::get<Idx>(output_subscribers_);
    auto& mutex = std::get<Idx>(output_mutexes_);
    
    std::lock_guard lock(mutex);
    for (uint32_t subscriber_addr : subscribers) {
        cmd_mailbox_.send(data, subscriber_addr);
    }
}
```

### Challenge 3: Parameter Expansion for process()

**Problem**: Generate `process(const T1& in1, const T2& in2, T3& out1, T4& out2)` from template parameters.

**Solution**: Use index sequences and tuple machinery:

```cpp
template<typename InputTuple, typename OutputTuple>
class ProcessCaller {
    template<size_t... Is, size_t... Os>
    static auto call_impl(Module* m,
                         const InputTuple& inputs,
                         OutputTuple& outputs,
                         std::index_sequence<Is...>,
                         std::index_sequence<Os...>) {
        return m->process(std::get<Is>(inputs)..., std::get<Os>(outputs)...);
    }
    
public:
    static auto call(Module* m, const InputTuple& inputs, OutputTuple& outputs) {
        constexpr size_t input_count = std::tuple_size_v<InputTuple>;
        constexpr size_t output_count = std::tuple_size_v<OutputTuple>;
        
        return call_impl(m, inputs, outputs,
                        std::make_index_sequence<input_count>{},
                        std::make_index_sequence<output_count>{});
    }
};
```

**User View**: Simple override:
```cpp
class FusionModule : public Module<Registry, 
                                   Outputs<FusedData, Diagnostics>,
                                   Inputs<IMUData, GPSData, LidarData>> {
protected:
    void process(const IMUData& imu, const GPSData& gps, const LidarData& lidar,
                 FusedData& fused, Diagnostics& diag) override {
        // Fusion algorithm here
        fused = fusion_filter_.update(imu, gps, lidar);
        diag.latency_us = calculate_latency();
    }
};
```

---

## Backward Compatibility Strategy

**Guarantee**: All existing code continues to work without modification.

**How**:
1. Single input/output becomes special case of multi-I/O:
   ```cpp
   // Old API (still works):
   Module<Registry, OutputData, ContinuousInput<InputData>, Cmd1, Cmd2>
   
   // Internally mapped to:
   Module<Registry, Output<OutputData>, Input<InputData>, Cmd1, Cmd2>
   ```

2. Concepts detect old vs new API:
   ```cpp
   template<typename T>
   concept is_legacy_input = is_one_of<T, PeriodicInput, LoopInput> ||
                             is_specialization_of<T, ContinuousInput>;
   
   template<typename T>
   concept is_new_input = is_specialization_of<T, Input> ||
                          is_specialization_of<T, Inputs>;
   ```

3. SFINAE selects appropriate implementation based on detected pattern.

---

## Example: Multi-Sensor Fusion Module

```cpp
// Message definitions
struct IMUData { /* ... */ };
struct GPSData { /* ... */ };
struct LidarData { /* ... */ };
struct FusedPose { /* ... */ };
struct FusionDiagnostics { /* ... */ };

// Registry
using FusionRegistry = MessageRegistry<
    // System messages
    MessageDefinition<SubscribeRequestPayload, ...>,
    MessageDefinition<SubscribeReplyPayload, ...>,
    MessageDefinition<UnsubscribeRequestPayload, ...>,
    MessageDefinition<UnsubscribeReplyPayload, ...>,
    // User messages
    MessageDefinition<IMUData, MessagePrefix::UserDefined, UserSubPrefix::Data, 0>,
    MessageDefinition<GPSData, MessagePrefix::UserDefined, UserSubPrefix::Data, 1>,
    MessageDefinition<LidarData, MessagePrefix::UserDefined, UserSubPrefix::Data, 2>,
    MessageDefinition<FusedPose, MessagePrefix::UserDefined, UserSubPrefix::Data, 3>,
    MessageDefinition<FusionDiagnostics, MessagePrefix::UserDefined, UserSubPrefix::Data, 4>
>;

// Fusion module: 3 inputs, 2 outputs
class SensorFusionModule : public Module<
    FusionRegistry,
    Outputs<FusedPose, FusionDiagnostics>,  // Two outputs
    Inputs<IMUData, GPSData, LidarData>     // Three inputs
> {
public:
    explicit SensorFusionModule(const ModuleConfig& config)
        : Module(config) {}

protected:
    // Automatic signature: all inputs const&, all outputs &
    void process(const IMUData& imu,
                 const GPSData& gps,
                 const LidarData& lidar,
                 FusedPose& pose,
                 FusionDiagnostics& diag) override {
        
        // Fusion algorithm
        pose = ekf_.update(imu, gps, lidar);
        
        // Diagnostics
        diag.imu_latency_us = calculate_latency(imu.timestamp);
        diag.gps_latency_us = calculate_latency(gps.timestamp);
        diag.lidar_latency_us = calculate_latency(lidar.timestamp);
        diag.filter_innovation = ekf_.get_innovation();
    }

private:
    ExtendedKalmanFilter ekf_;
};

// Configuration
int main() {
    ModuleConfig fusion_config{
        .name = "SensorFusion",
        .system_id = 1,
        .instance_id = 0,
        // Multi-input source configuration
        .input_sources = {
            {.system_id = 0, .instance_id = 0},  // IMU source
            {.system_id = 0, .instance_id = 1},  // GPS source
            {.system_id = 0, .instance_id = 2}   // Lidar source
        }
    };
    
    SensorFusionModule fusion(fusion_config);
    fusion.start();
    
    // Module subscribes to 3 sources automatically
    // Publishes 2 output types to separate subscriber lists
    
    std::this_thread::sleep_for(std::chrono::seconds(60));
    fusion.stop();
}
```

---

## Implementation Phases

### Phase 5: Multi-Input/Multi-Output Foundation (4-6 weeks)

**Week 1-2: Core Template Machinery**
- [ ] Define `Input<T>`, `Inputs<Ts...>`, `Output<T>`, `Outputs<Ts...>`, `NoOutput`
- [ ] Implement concept detection: `is_single_input`, `is_multi_input`, etc.
- [ ] Create parameter expansion helpers (index sequences, tuple machinery)
- [ ] Add backward compatibility mapping (legacy → new API)

**Week 3-4: Module Class Refactoring**
- [ ] Update `Module<>` template signature
- [ ] Generate multiple data mailboxes for `Inputs<Ts...>`
- [ ] Generate multiple subscriber lists for `Outputs<Ts...>`
- [ ] Implement multi-input subscription logic
- [ ] Implement multi-output publishing logic

**Week 5: Process Signature Generation**
- [ ] SFINAE-based `process()` overload selection
- [ ] Parameter pack expansion for all input/output combinations
- [ ] Virtual dispatch hooks for user overrides
- [ ] Static assertions for invalid configurations

**Week 6: Testing & Examples**
- [ ] Update existing examples to ensure backward compatibility
- [ ] Create multi-input/output fusion example
- [ ] Create multi-output pipeline example (one input → multiple processed outputs)
- [ ] Performance benchmarks (compare single vs multi-I/O overhead)

### Phase 6: Command Reply Mechanisms (2-3 weeks)

**Week 1: Reply Type Detection**
- [ ] Implement `has_reply<CmdT, ReplyT>` concept
- [ ] Implement `returns_reply<CmdT>` concept
- [ ] Detect reply types from command message definitions

**Week 2: Reply Mailbox Infrastructure**
- [ ] Add reply routing to command handler
- [ ] Implement blocking wait for reply on sender side
- [ ] Timeout mechanism for failed replies

**Week 3: Examples & Documentation**
- [ ] Create command/reply example (GetStatus, SetParameter, etc.)
- [ ] Document all three command patterns
- [ ] Performance analysis (request/reply latency)

### Phase 7: Advanced Features (4 weeks)

**Optional Future Enhancements:**
- [ ] **Conditional Outputs**: Modules that sometimes publish one output, sometimes another
- [ ] **Dynamic Input Selection**: Switch between input sources at runtime
- [ ] **Priority-Based Input Processing**: Handle certain input types with higher priority
- [ ] **Zero-Copy Shared Memory**: For large data types, use shared memory instead of message copies
- [ ] **Partial Publishing**: Only publish outputs that changed (delta encoding)

---

## Benefits Analysis

### Compile-Time Benefits ✅

1. **Zero Overhead Abstraction**: All multi-I/O machinery resolves at compile time
2. **Type Safety**: Invalid input/output combinations rejected by compiler
3. **Buffer Pre-Allocation**: All mailbox sizes known at compile time
4. **No Virtual Dispatch**: Direct function calls via template expansion

### Developer Experience ✅

1. **Intuitive API**: `process(in1, in2, out1, out2)` reads like natural code
2. **Clear Errors**: Concepts provide readable compile errors
3. **Single Registry**: One place defines all communication types
4. **Flexible Patterns**: Supports simple single-I/O and complex multi-I/O equally well

### Performance ✅

1. **No Runtime Allocation**: All mailboxes and subscriber lists pre-sized
2. **Efficient Publishing**: Direct iteration over subscriber lists
3. **Minimal Locking**: One mutex per output type, not per module
4. **Cache-Friendly**: Tuple-based storage keeps related data together

### Scalability ✅

1. **Distributed Systems**: Registry can be shared across processes
2. **Modular Architecture**: Add/remove modules without changing others
3. **Type Evolution**: Change message types without touching module logic
4. **Code Reuse**: Generic modules work with any compatible message types

---

## Risks & Mitigations

### Risk 1: Compilation Time Explosion

**Risk**: Heavy template metaprogramming increases build times.

**Mitigation**:
- Use explicit template instantiation for common patterns
- Provide precompiled registry headers
- Employ `extern template` declarations
- Measure compile times in CI, reject PRs that degrade performance

### Risk 2: Complex Error Messages

**Risk**: Template errors are notoriously cryptic.

**Mitigation**:
- Use C++20 concepts with clear requirements
- Provide `static_assert` with human-readable messages
- Document common error patterns in troubleshooting guide
- Use `requires` clauses instead of SFINAE where possible

### Risk 3: Debugging Difficulty

**Risk**: Template-heavy code is hard to step through in debugger.

**Mitigation**:
- Provide debug logging macros (disabled in release builds)
- Create simple test modules that demonstrate each pattern
- Use `decltype()` and `typeid()` to inspect types at runtime
- Maintain examples with progressively complex multi-I/O

### Risk 4: Learning Curve

**Risk**: Multi-I/O API might confuse new users.

**Mitigation**:
- Keep simple single-I/O examples prominent in documentation
- Provide migration guide from single to multi-I/O
- Create tutorial series: basic → intermediate → advanced
- Maintain "recipes" for common patterns

---

## Open Questions

### Q1: Input Synchronization Policy

**Question**: When module has `Inputs<T1, T2, T3>`, should `process()` be called:
- **Option A**: When ANY input arrives (process immediately, use stale data for others)
- **Option B**: When ALL inputs arrive (wait for complete set, ensure temporal alignment)
- **Option C**: User-configurable (template parameter or runtime config)

**Recommendation**: Start with **Option A** (asynchronous, low latency), add **Option B** as `SynchronizedInputs<T1, T2, T3>` later.

### Q2: Output Publishing Order

**Question**: When module has `Outputs<T1, T2, T3>`, are outputs published:
- **Option A**: Simultaneously (all subscribers notified in parallel)
- **Option B**: Sequentially (deterministic order T1→T2→T3)
- **Option C**: Priority-based (critical outputs first)

**Recommendation**: **Option B** (sequential, predictable), allows Option C later via `PrioritizedOutputs<...>`.

### Q3: Command Reply Timeout

**Question**: If command expects reply but source doesn't respond, what happens?
- **Option A**: Block forever (current behavior)
- **Option B**: Configurable timeout with error return
- **Option C**: Async reply via callback

**Recommendation**: **Option B** with default 1-second timeout, extensible to async later.

### Q4: Registry Versioning

**Question**: How to handle registry changes across distributed systems?
- **Option A**: Manual version tracking (user responsibility)
- **Option B**: Compile-time hash in message header
- **Option C**: Dynamic capability negotiation

**Recommendation**: **Option B** (compile-time hash), detect version mismatch at runtime, fail fast with clear error.

---

## Conclusion

The proposed multi-input/multi-output architecture is:

✅ **Feasible**: All mechanisms are standard C++20 template metaprogramming  
✅ **Performant**: Zero runtime overhead, fully compile-time resolved  
✅ **Intuitive**: User sees simple `process(in1, in2, out1, out2)` interface  
✅ **Backward Compatible**: Existing single-I/O modules continue working  
✅ **Scalable**: Supports arbitrarily complex fusion/distribution topologies  

**Recommendation**: Proceed with **Phase 5 implementation** as next major milestone.

**Estimated Effort**: 10-12 weeks for Phases 5-6, plus 4 weeks for Phase 7 optional features.

**Dependencies**: None - current architecture is solid foundation for this evolution.

---

## Next Steps

1. **Community Feedback**: Review this document with maintainers/users
2. **Prototype**: Implement minimal multi-I/O example (2 inputs, 1 output)
3. **Benchmark**: Measure compile-time and runtime overhead
4. **Iterate**: Refine API based on prototype experience
5. **Full Implementation**: Roll out Phase 5 with comprehensive tests
6. **Documentation**: Update all guides, tutorials, and examples

**Target Date**: Phase 5 complete by **April 30, 2026**

---

## References

- **Current Codebase**: `/home/muddy/src/CommRaT/`
- **Registry Implementation**: `include/commrat/message_registry.hpp`
- **Module Base Class**: `include/commrat/registry_module.hpp`
- **Example Usage**: `examples/loop_mode_example.cpp`, `examples/continuous_input_example.cpp`
- **C++20 Concepts**: https://en.cppreference.com/w/cpp/language/constraints
- **Fold Expressions**: https://en.cppreference.com/w/cpp/language/fold
- **SFINAE & Templates**: https://en.cppreference.com/w/cpp/language/sfinae
