# Unified Configuration Proposal

**Date**: 2024-02-12  
**Status**: PROPOSAL  
**Phase**: Post-Phase 7.0 Refactoring

## Problems with Current Design

### Problem 1: Single-Input Special Casing

**Current state**: Two separate code paths for inputs:
```cpp
// Single-input (legacy)
std::optional<uint8_t> source_system_id;
std::optional<uint8_t> source_instance_id;

// Multi-input (new)
std::vector<InputSource> input_sources;
```

**Why this is bad**:
- Duplication: `subscribe_to_all_sources()` has `if continuous else multi` branches
- Complexity: Many functions check `has_continuous_input` vs `has_multi_input`
- Maintenance: Changes require updating both paths
- **Truth**: Single-input is just multi-input with N=1!

### Problem 2: Multi-Output Collisions

**Current state**: One system_id/instance_id for entire module:
```cpp
struct ModuleConfig {
    uint8_t system_id{0};      // Shared by ALL outputs
    uint8_t instance_id{0};    // Shared by ALL outputs
};
```

**Collision scenario**:
```cpp
using MyApp = CommRaT<
    Message::Data<SensorData>  // ID=1
>;

class DualSensor : public MyApp::Module<
    Outputs<SensorData, SensorData>,  // Both use type_id=1!
    PeriodicInput
> {};

ModuleConfig config{
    .system_id = 10,
    .instance_id = 1
};
// Output 0: Base = (1 << 16) | (10 << 8) | 1 = 0x010A01
// Output 1: Base = (1 << 16) | (10 << 8) | 1 = 0x010A01  ❌ COLLISION!
```

**Why this happens**:
- Mailbox address = `(type_id << 16) | (system_id << 8) | instance_id`
- Same type → Same type_id → Same mailbox address!

**Real-world use case**:
```cpp
// Two temperature sensors on same robot
Outputs<TemperatureData, TemperatureData>  // Sensor A and Sensor B
```

## Proposed Solution

### Part 1: Unify Input Configuration

**Use InputConfig TaggedUnion**:

```cpp
struct ModuleConfig {
    // ... basic fields ...
    
    // ========================================================================
    // Input Configuration (TaggedUnion)
    // ========================================================================
    
    // Input configuration: none, single, or multi
    // Order matches Inputs<T1, T2, ...> tuple for MultiInput
    InputConfig inputs = NoInput{};
    
    // REMOVED (obsolete):
    // std::optional<uint8_t> source_system_id;
    // std::optional<uint8_t> source_instance_id;
    // std::vector<InputSource> input_sources;
};
```

**Migration**:
```cpp
// BEFORE (single-input)
ModuleConfig config{
    .source_system_id = 10,
    .source_instance_id = 1
};

// AFTER (unified with TaggedUnion)
ModuleConfig config{
    .inputs = SingleInput{.source_system_id = 10, .source_instance_id = 1}
};
```

**Benefits**:
- Single code path for subscription
- Remove `if has_continuous_input` branches
- Simpler maintenance
- Natural extension: single-input → multi-input just adds elements

### Part 2: Per-Output Addressing (rfl::TaggedUnion)

**Support two modes**:

```cpp
// Simple mode: All outputs share system_id/instance_id (current behavior)
struct SimpleOutput {
    uint8_t system_id{0};
    uint8_t instance_id{0};
};

// Per-output mode: Each output gets its own addressing
struct MultiOutput {
    struct OutputAddress {
        uint8_t system_id{0};
        uint8_t instance_id{0};
    };
    std::vector<OutputAddress> addresses;  // One per output in Outputs<>
};

// No output: Input-only modules (still need addressing for CMD/WORK mailboxes!)
struct NoOutput {
    uint8_t system_id{0};     // Required for command/subscription mailboxes
    uint8_t instance_id{0};   // Even without outputs, module needs mailbox addressing
};

using OutputConfig = rfl::TaggedUnion<"type", NoOutput, SimpleOutput, MultiOutput>;

struct ModuleConfig {
    std::string name;
    
    // ========================================================================
    // Output Configuration (TaggedUnion)
    // ========================================================================
    
    // Output addressing: none, simple (shared), or per-output (explicit)
    OutputConfig outputs = SimpleOutput{.system_id = 0, .instance_id = 0};
    
    // ... rest of config ...
};
```

**Usage**:

```cpp
// Case 1: Single output or non-colliding multi-output
class WeatherStation : public MyApp::Module<
    Outputs<TemperatureData, PressureData>,  // Different types → No collision
    PeriodicInput
> {};

ModuleConfig config{
    .name = "WeatherStation",
    .outputs = SimpleOutput{.system_id = 10, .instance_id = 1}
    // Temp: 0x010A01, Pressure: 0x020A01 (different type_ids)
};

// Case 2: Duplicate types → Must use per-output
class DualSensor : public MyApp::Module<
    Outputs<SensorData, SensorData>,  // Same type → Collision without per-output!
    PeriodicInput
> {};

ModuleConfig config{
    .name = "DualSensor",
    .outputs = MultiOutput{
        .addresses = {
            {.system_id = 10, .instance_id = 1},  // Sensor A
            {.system_id = 10, .instance_id = 2}   // Sensor B (different instance!)
        }
    }
};
// Output 0: Base = 0x010A01
// Output 1: Base = 0x010A02 ✓ No collision!
```

**JSON config with rfl**:

```json
// NoOutput module (logger, monitor, etc.)
{
  "name": "DataLogger",
  "outputs": {
    "type": "NoOutput",
    "system_id": 15,
    "instance_id": 1
  },
  "inputs": {
    "type": "SingleInput",
    "source_system_id": 10,
    "source_instance_id": 1
  }
}

// Simple output addressing
{
  "name": "WeatherStation",
  "outputs": {
    "type": "SimpleOutput",
    "system_id": 10,
    "instance_id": 1
  }
}

// Per-output addressing
{
  "name": "DualSensor",
  "outputs": {
    "type": "MultiOutput",
    "addresses": [
      {"system_id": 10, "instance_id": 1},
      {"system_id": 10, "instance_id": 2}
    ]
  }
}
```

**Compile-time validation**:

```cpp
template<typename OutputSpec>
void validate_output_config(const ModuleConfig& config) {
    constexpr size_t num_outputs = OutputSpec::size();
    
    if (config.outputs.holds<MultiOutput>()) {
        auto& multi = config.outputs.get<MultiOutput>();
        
        if (multi.addresses.size() != num_outputs) {
            throw std::runtime_error(
                "MultiOutput requires exactly " + 
                std::to_string(num_outputs) + " addresses"
            );
        }
    }
}
```

## Implementation Plan

### Phase 1: Unify Input Configuration

**Changes**:
1. Define `InputConfig` TaggedUnion with `NoInput`, `SingleInput`, `MultiInput`
2. Remove `source_system_id`, `source_instance_id` from ModuleConfig
3. Replace `input_sources` vector with `inputs` TaggedUnion field
4. Update subscription logic to use `config.inputs.visit()` pattern
5. Remove `has_continuous_input` branches - check variant type instead

**Backward compatibility**: Add migration helper:
```cpp
ModuleConfig config_from_legacy(uint8_t source_sys, uint8_t source_inst) {
    return ModuleConfig{
        .inputs = SingleInput{.source_system_id = source_sys, .source_instance_id = source_inst}
    };
}
```

### Phase 2: Per-Output Addressing

**Changes**:
1. Define `OutputConfig` TaggedUnion with `NoOutput`, `SimpleOutput`, `MultiOutput`
2. Replace `system_id`, `instance_id` with `outputs` field
3. Update `calculate_base_address()` to handle all variants:
```cpp
uint32_t calculate_base_address(const ModuleConfig& config, size_t output_index) {
    uint16_t type_id_low = /* get type_id for output_index */;
    
    return config.outputs.visit(
        [&](const NoOutput& no_out) -> uint32_t {
            // NoOutput modules still need addressing for CMD/WORK mailboxes
            // Use a reserved type_id (e.g., 0) for modules without data outputs
            constexpr uint16_t no_output_type_id = 0;
            return (no_output_type_id << 16) | (no_out.system_id << 8) | no_out.instance_id;
        },
        [&](const SimpleOutput& simple) {
            return (type_id_low << 16) | (simple.system_id << 8) | simple.instance_id;
        },
        [&](const MultiOutput& multi) {
            const auto& addr = multi.addresses[output_index];
            return (type_id_low << 16) | (addr.system_id << 8) | addr.instance_id;
        }
    );
}
```

4. Update mailbox creation in registry_module.hpp
5. Update all examples

**Backward compatibility**: Default to SimpleOutput

## Benefits Summary

### Unified Inputs
- ✅ Remove ~15 `if has_continuous_input` branches
- ✅ Single subscription code path
- ✅ Simpler mental model
- ✅ Easier testing (one path to test)
- ✅ Natural extension: add elements to vector

### Per-Output Addressing
- ✅ Solves duplicate type collision problem
- ✅ Enables advanced use cases (multiple same-type sensors)
- ✅ Backward compatible (SimpleOutput is default)
- ✅ Type-safe with rfl::TaggedUnion
- ✅ JSON-serializable out of the box

## Migration Complexity

**Low risk**:
- Input unification: Mechanical refactoring, existing tests validate
- Per-output addressing: Additive feature (doesn't break existing code)
- Both changes are compile-time detectable

**Estimated effort**:
- Phase 1 (input unification): 4-6 hours
- Phase 2 (per-output addressing): 6-8 hours
- Testing: 2-3 hours
- **Total**: ~1.5 days

## Alternative Considered: Per-Output Type IDs

**Instead of per-output addressing**, use **type aliases** to create distinct types:

```cpp
struct SensorDataA { /* same fields as SensorData */ };
struct SensorDataB { /* same fields as SensorData */ };

using MyApp = CommRaT<
    Message::Data<SensorDataA>,  // ID=1
    Message::Data<SensorDataB>   // ID=2
>;

class DualSensor : public MyApp::Module<
    Outputs<SensorDataA, SensorDataB>,  // Different types → No collision
    PeriodicInput
> {};
```

**Why rejected**:
- Requires duplicate struct definitions
- Breaks semantic equivalence (they're the SAME data!)
- Consumers must know which variant to subscribe to
- Not a general solution (what if you have 10 sensors?)

**Per-output addressing is superior**: Same type semantically, distinct addressing mechanically.

## Open Questions

1. **Input addressing for consumers**: Should we also allow per-input addressing?
   - Use case: Multiple consumers with same input type subscribing to same producer
   - Current: Uses consumer's system_id/instance_id (works fine)
   - **Decision**: Not needed - consumers already have unique addressing

2. **Validation timing**: Compile-time or runtime?
   - Per-output address count can be validated at config load (runtime)
   - Type collision detection could be compile-time (static_assert)
   - **Decision**: Runtime validation is sufficient (simpler)

3. **Default behavior**: What if user doesn't specify outputs config?
   - Option A: Default to SimpleOutput{0, 0} (current behavior)
   - Option B: Require explicit specification
   - **Decision**: Default to SimpleOutput (backward compatible)

## Recommendation

**Implement both phases**:

1. **Phase 1 first**: Unify input config (simpler, immediate benefit)
2. **Phase 2 later**: Add per-output addressing when needed

**Priority**: Phase 1 is higher priority - reduces complexity immediately. Phase 2 can wait for actual use case demanding duplicate types.

**User impact**: Minimal - both are backward compatible with migration helpers.
