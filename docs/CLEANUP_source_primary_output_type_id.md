# Removal of source_primary_output_type_id Field

**Date**: 2024-01-XX  
**Status**: COMPLETE  
**Phase**: Post-Phase 7.0 Cleanup

## Summary

Successfully removed the `source_primary_output_type_id` field from the entire CommRaT framework. This field is now **completely obsolete** thanks to auto-inference based on `InputData` type for single-input modules and index-based type lookup for multi-input modules.

## Changes Made

### 1. Configuration Structures (`module_config.hpp`)

**Removed**:
- `ModuleConfig::source_primary_output_type_id` (optional<uint32_t>)
- `InputSource::source_primary_output_type_id` (optional<uint32_t>)

**Added**:
- `InputSource::input_index` (mutable size_t) - Auto-populated during subscription for unsubscribe support

### 2. Subscription Logic (`services/subscription.hpp`)

**Before (3 priorities)**:
```cpp
if constexpr (has_multi_input) {
    source_data_type_id = get_input_type_id_at_index(source_index);
} else if constexpr (has_continuous_input) {
    source_data_type_id = Registry::get_message_id<InputData>();
} else if (source_primary_output_type_id) {
    source_data_type_id = *source_primary_output_type_id;  // ❌ FALLBACK (removed)
}
```

**After (2 priorities)**:
```cpp
if constexpr (has_multi_input) {
    source_data_type_id = get_input_type_id_at_index(source_index);
} else if constexpr (has_continuous_input) {
    source_data_type_id = Registry::get_message_id<InputData>();
} else {
    static_assert(false, "Invalid input configuration");
}
```

**Why the fallback is unreachable**:
- `subscribe_to_source_impl()` has `static_assert(has_continuous_input || has_multi_input)`
- If we reach subscription, one of the first two branches ALWAYS executes
- The fallback was dead code!

### 3. Unsubscribe Logic

**Before**:
```cpp
// Required source_primary_output_type_id to compute source WORK mailbox
if (!source.source_primary_output_type_id.has_value()) {
    std::cerr << "ERROR: Cannot unsubscribe!\n";
    return;
}
```

**After**:
```cpp
// Use input_index (populated during subscribe_to_all_sources)
uint32_t source_data_type_id = get_input_type_id_at_index(source.input_index);
```

**Key insight**: We track the index (`size_t i`) during subscription and save it to `InputSource::input_index`. Unsubscribe uses this index to look up the correct type.

### 4. Examples Updated

Removed all references from:
- `docs/examples/02_multi_output/multi_output_example.cpp`
- `docs/examples/03_multi_input_fusion/multi_input_fusion.cpp`
- `examples/multi_output_runtime.cpp`
- `examples/multi_output_sensor_fusion.cpp`
- `test/test_multi_input_module.cpp`
- `test/test_3input_fusion.cpp`

**Example transformation**:
```cpp
// BEFORE (manual type ID specification)
commrat::ModuleConfig pressure_monitor_config{
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = WeatherApp::get_message_id<TemperatureData>()  // ❌
};

// AFTER (auto-inference)
commrat::ModuleConfig pressure_monitor_config{
    .source_system_id = 10,
    .source_instance_id = 1
    // Type automatically inferred from Input<PressureData>! ✓
};
```

## How Auto-Inference Works

### Single-Input Modules

```cpp
class FilterModule : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {
    // ...
};
```

**Subscription calculates**:
1. `InputData = SensorData` (from `Input<SensorData>`)
2. `input_type_id = Registry::get_message_id<SensorData>()` (compile-time)
3. `subscriber_base_addr = (input_type_id_low16 << 16) | ...`
4. Subscribe to producer's **SensorData** output (correct mailbox!)

**Key**: Consumer uses its **own input type** to find the producer's matching output.

### Multi-Input Modules

```cpp
class FusionModule : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>  // Order matters!
> {};

// Configuration
ModuleConfig config{
    .input_sources = {
        {.system_id = 10, .instance_id = 1, .is_primary = true},   // IMU (index 0)
        {.system_id = 11, .instance_id = 1, .is_primary = false}   // GPS (index 1)
    }
};
```

**Subscription calculates**:
1. Loop `i` from 0 to N-1
2. `input_type_id = get_input_type_id_at_index(i)` (uses `std::tuple_element`)
3. `source.input_index = i` (save for unsubscribe!)
4. Subscribe to each producer using **their respective types**

**Key**: Each input source gets the type at its index in `Inputs<>` tuple.

### Multi-Output Producers

```cpp
class WeatherStation : public MyApp::Module<
    Outputs<TemperatureData, PressureData>,
    PeriodicInput
> {};
```

**Producer creates**:
- Mailbox set for `TemperatureData` (type_id=1)
- Mailbox set for `PressureData` (type_id=2)

**Consumers subscribe**:
- `Input<TemperatureData>` → Uses type_id=1 → Finds TemperatureData mailbox
- `Input<PressureData>` → Uses type_id=2 → Finds PressureData mailbox

**Result**: Each consumer finds the **correct output mailbox** automatically!

## Why This Works

### Registry Unification
All communicating modules **must use the same registry**:

```cpp
using MyApp = CommRaT<
    Message::Data<SensorData>,    // ID=1
    Message::Data<FilteredData>   // ID=2
>;

// Producer
class SensorModule : public MyApp::Module<Output<SensorData>, PeriodicInput> {};

// Consumer
class FilterModule : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {};
```

Both modules agree: `SensorData = ID 1`, so mailbox calculation is consistent!

### Type-Based Addressing

TiMS mailbox address format:
```
address = (type_id_low16 << 16) | (system_id << 8) | instance_id + offset
```

- Producer publishes to mailbox using its **output type**
- Consumer calculates mailbox using its **input type**
- If types match → same mailbox address!

### Multi-Output Type Filtering

Each output type gets its own mailbox set:
- `TemperatureData`: Base 0x010A01 (type=1, sys=10, inst=1)
- `PressureData`: Base 0x020A01 (type=2, sys=10, inst=1)

Subscribers filter by type:
- `Input<TemperatureData>` → Subscribes to 0x010A11 (WORK mailbox)
- `Input<PressureData>` → Subscribes to 0x020A11 (WORK mailbox)

## Testing

**All 22 CTests pass**:
- ✅ Single-input examples (continuous_input, clean_interface)
- ✅ Multi-output examples (multi_output, sensor_fusion)
- ✅ Multi-input examples (multi_input_fusion, 3input_fusion)
- ✅ Phase 7.0 module_main examples (sensor→filter pipeline)

**Manual verification**:
- Multi-output example: Both TempMonitor and PressureMonitor receive correct data
- Phase 7.0 sensor→filter: Communication works without type_id specification

## Benefits

1. **Simpler API**: Users no longer need to specify `source_primary_output_type_id`
2. **Compile-time safety**: Type inference uses compile-time type information
3. **Less error-prone**: No manual type ID calculation or specification
4. **Cleaner configs**: Fewer fields in `ModuleConfig` and `InputSource`
5. **Self-documenting**: Input/output types in module declaration tell the full story

## Related Work

- **Registry Bug Fix**: Messages now assigned IDs in forward order (SensorData=1, FilteredData=2)
- **Phase 7.0**: Per-binary module deployment with `module_main.hpp` wrapper
- **Auto-inference**: Single-input uses `InputData`, multi-input uses index-based lookup

## Migration Guide

**If you have existing code using `source_primary_output_type_id`:**

1. **Remove the field** from all `ModuleConfig` initializers
2. **Remove the field** from all `InputSource` initializers
3. **Verify registry unification**: All communicating modules use the same `CommRaT<...>` template
4. **Recompile** - type inference is automatic!

**No code changes needed beyond removing the obsolete field.**

## Technical Details

### InputSource.input_index

**Why mutable?**
```cpp
struct InputSource {
    // ...
    mutable size_t input_index{0};  // Allows modification in const objects
};
```

`config_->input_sources` is const during subscription, but we need to populate `input_index` for later use by unsubscribe. Using `mutable` is semantically correct: `input_index` is a cache/optimization, not part of the object's logical state.

**Alternative considered**: Store indices separately in `SubscriptionProtocol`, but this duplicates state and complicates unsubscribe logic.

### Static Assert Protection

```cpp
else {
    static_assert(has_continuous_input || has_multi_input, "Invalid input configuration");
    return;
}
```

The `static_assert` ensures at **compile time** that if we reach subscription logic, we have a valid input configuration. The fallback branch is provably unreachable, making the old `source_primary_output_type_id` check unnecessary.

## Conclusion

The removal of `source_primary_output_type_id` represents a significant simplification of the CommRaT API. **Auto-inference based on type information is more robust, less error-prone, and requires zero user configuration.**

**Status**: ✅ COMPLETE - All tests pass, all examples updated
