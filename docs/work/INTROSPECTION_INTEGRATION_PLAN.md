# CommRaT Introspection Integration Plan

**Date**: February 12, 2026  
**Status**: Planning  
**Priority**: Medium

## Overview

Provide a thin wrapper in CommRaT that exposes SeRTial's introspection for all registered messages. The key insight: **SeRTial already does the heavy lifting** - CommRaT just needs to apply it to the message registry and leverage rfl's format flexibility.

**What CommRaT provides**:
- Access introspection for all registered messages via `CommRaT<>` template
- Export to any format supported by rfl (JSON, YAML, TOML, XML, etc.)
- Convenience for "export all messages" patterns

**What stays in SeRTial**:
- `StructLayout<T>` analysis (already done)
- `Reflector<StructLayout<T>>` (already done)
- Individual message schema export (already works)
- Schema viewer tools (already in SeRTial repo)

## SeRTial Introspection Capabilities

### Available Mechanisms

**1. StructLayout<T> Compile-Time Analysis**
```cpp
// Available via SeRTial
template<typename T>
struct StructLayout {
    static constexpr std::size_t num_fields;
    static constexpr std::size_t sizeof_bytes;
    static constexpr std::size_t base_packed_size;
    static constexpr std::size_t max_packed_size;
    static constexpr bool has_variable_fields;
    
    static constexpr std::array<std::size_t, N> field_sizes;
    static constexpr std::array<std::size_t, N> field_offsets;
    static constexpr std::array<bool, N> field_is_variable;
    static constexpr std::array<std::size_t, N> element_sizes;
    static constexpr std::array<std::size_t, N> capacities;
    // + execution blocks for serialization
};
```

**2. Reflector-Based Schema Export**
```cpp
// Via rfl::Reflector<StructLayout<T>>
template<typename T>
std::string export_schema() {
    return rfl::json::to_schema<StructLayout<T>>();
}
```

**3. Runtime Field Iteration**
```cpp
// Via rfl::named_tuple_t<T>
using NT = rfl::named_tuple_t<MyMessage>;
rfl::apply(message, [](auto& field) {
    // Visit each field at runtime
});
```

## Proposed CommRaT Integration

### Goal: Minimal CommRaT Wrapper for Registry-Wide Introspection

Provide simple access to SeRTial introspection for the entire message registry, leveraging rfl's built-in format support:

```cpp
using MyApp = commrat::CommRaT<
    commrat::Message::Data<TempData>,
    commrat::Message::Data<StatusData>
>;

// NEW FUNCTIONALITY (minimal wrapper):

// 1. Export single message to any rfl-supported format
auto json_schema = MyApp::Introspection::export_as<TempData, rfl::json>();
auto yaml_schema = MyApp::Introspection::export_as<TempData, rfl::yaml>();
auto toml_schema = MyApp::Introspection::export_as<TempData, rfl::toml>();

// 2. Export ALL registered messages (array format for viewer)
auto all_json = MyApp::Introspection::export_all<rfl::json>();
MyApp::Introspection::write_to_file<rfl::json>("schemas.json");

// 3. Direct access to SeRTial's StructLayout (zero abstraction cost)
using TempLayout = sertial::StructLayout<TempData>;
constexpr auto size = TempLayout::base_packed_size;

// 4. Runtime field iteration - use rfl directly (CommRaT doesn't need to wrap)
auto nt = rfl::to_named_tuple(temp_msg);
rfl::apply(nt, [](auto& field) { /* ... */ });
```

**Key Insight**: CommRaT provides registry-wide operations. For single-message operations, users can use SeRTial/rfl directly.

## Implementation Design

### Phase 1: Minimal Introspection Wrapper

**File**: `include/commrat/introspection.hpp` (NEW - single header)

```cpp
#pragma once

#include <sertial/core/layout/struct_layout.hpp>
#include <sertial/core/layout/struct_layout_reflector.hpp>
#include <rfl.hpp>
#include <string>
#include <vector>
#include <fstream>

namespace commrat {

// Introspection helper - format-agnostic using rfl's writers
template<typename UserRegistry>
struct IntrospectionHelper {
    // Export single message using any rfl-supported format
    template<typename T, typename Format = rfl::json>
    static auto export_as() {
        using Layout = sertial::StructLayout<T>;
        return Format::write(Layout{});  // Leverage rfl's format abstraction
    }
    
    // Export all messages in registry to format-specific container
    template<typename Format = rfl::json>
    static auto export_all() {
        using PayloadTuple = typename UserRegistry::payload_types;
        return export_all_impl<Format>(PayloadTuple{});
    }
    
    // Convenience: write all to file
    template<typename Format = rfl::json>
    static void write_to_file(const std::string& filename) {
        auto data = export_all<Format>();
        std::ofstream file(filename);
        file << data;
    }
    
private:
    // Helper to export tuple of types
    template<typename Format, typename... PayloadTypes>
    static auto export_all_impl(std::tuple<PayloadTypes...>) {
        // Create vector of StructLayout instances
        std::vector<rfl::Result<rfl::json::Reader::InputVarType>> layouts;
        
        (layouts.push_back(
            Format::write(sertial::StructLayout<PayloadTypes>{})
        ), ...);
        
        // rfl handles the format conversion automatically
        return Format::write(layouts);
    }
};

} // namespace commrat
```

**Notes**: 
- Only ~50 lines of code!
- Works with JSON, YAML, TOML, XML, etc. via rfl
- No custom formatting logic needed
- SeRTial's Reflector does all the heavy lifting

### Phase 2: Add to CommRaT<> Template

**File**: `include/commrat/commrat.hpp` (MODIFY)

```cpp
template<typename... MessageDefs>
struct CommRaT {
    // ... existing Module, Mailbox, serialize, etc. ...
    
    // ========================================================================
    // INTROSPECTION (NEW - minimal wrapper)
    // ========================================================================
    
    /// Introspection helper provides registry-wide export
    using Introspection = IntrospectionHelper<UserRegistry>;
    
    // That's it! Users access via:
    // MyApp::Introspection::export_as<Type, Format>()
    // MyApp::Introspection::export_all<Format>()
    // MyApp::Introspection::write_to_file<Format>("file.json")
};
```

**Benefits of this approach**:
- Only adds 3 lines to CommRaT<>
- Format-agnostic from day 1 (JSON, YAML, TOML, etc.)
- No wrapper code for single-message operations (use SeRTial/rfl directly)
- Clear separation: CommRaT = registry operations, SeRTial = single-message operations

### Phase 3: Example Utilities (Optional)

**Note**: These are application-specific and could go in examples/ rather than the library itself.

**File**: `examples/utils/message_logger.hpp` (OPTIONAL)

```cpp
#pragma once

#include <rfl.hpp>
#include <sertial/core/layout/struct_layout.hpp>
#include <iostream>

namespace examples {

/// Simple field printer using rfl directly (no CommRaT wrapper needed)
template<typename T>
void print_message(const T& message) {
    std::cout << "Message: " << rfl::type_name_t<T>().str() << "\n";
    
    // Use rfl directly - CommRaT doesn't need to wrap this
    auto nt = rfl::to_named_tuple(message);
    rfl::apply(nt, [](const auto& field) {
        std::cout << "  " << field.name() << ": " << field.value() << "\n";
    });
}

/// Print compile-time layout info using SeRTial directly
template<typename T>
void print_layout_info() {
    using Layout = sertial::StructLayout<T>;
    
    std::cout << "Layout: " << rfl::type_name_t<T>().str() << "\n";
    std::cout << "  Struct size: " << Layout::sizeof_bytes << " bytes\n";
    std::cout << "  Packed: " << Layout::base_packed_size 
              << "-" << Layout::max_packed_size << " bytes\n";
    std::cout << "  Fields: " << Layout::num_fields << "\n";
}

} // namespace examples
```

**Key Point**: These utilities use SeRTial and rfl directly - no need for CommRaT to wrap them!

## Usage Examples

### Example 1: Export Schemas for Debugging

```cpp
#include <commrat/commrat.hpp>
#include <commrat/introspection/schema_export.hpp>

struct TemperatureData {
    float temperature_celsius;
    uint64_t timestamp_ns;
};

struct StatusData {
    int status_code;
    float average_temp;
};

### Example 1: Export Schemas (Registry-Wide)

```cpp
#include <commrat/commrat.hpp>
#include <rfl/json.hpp>
#include <rfl/yaml.hpp>

struct TemperatureData {
    float temperature_celsius;
    uint64_t timestamp_ns;
};

struct StatusData {
    int status_code;
    float average_temp;
};

using MyApp = commrat::CommRaT<
### Example 2: Single Message Inspection (Use rfl Directly)

```cpp
#include <rfl.hpp>

int main() {
    TemperatureData temp{
        .temperature_celsius = 25.3f,
        .timestamp_ns = 1234567890
    };
    
    // Use rfl directly for runtime field iteration
    std::cout << "Message: " << rfl::type_name_t<TemperatureData>().str() << "\n";
    
    auto nt = rfl::to_named_tuple(temp);
    rfl::apply(nt, [](const auto& field) {
        std::cout << "  " << field.name() << ": " << field.value() << "\n";
    });
    
    // Output:
    // Message: TemperatureData
    //   temperature_celsius: 25.3
    //   timestamp_ns: 1234567890
}
```

### Example 3: Compile-Time Layout Queries (Use SeRTial Directly)

```cpp
#include <sertial/core/layout/struct_layout.hpp>

int main() {
    // Use SeRTial's StructLayout directly
    using TempLayout = sertial::StructLayout<TemperatureData>;
    
    std::cout << "TemperatureData analysis:\n";
    std::cout << "  Struct size: " << TempLayout::sizeof_bytes << " bytes\n";
    std::cout << "  Packed size: " << TempLayout::base_packed_size << " bytes\n";
    std::cout << "  Field count: " << TempLayout::num_fields << "\n";
    std::cout << "  Has variable fields: " 
              << (TempLayout::has_variable_fields ? "yes" : "no") << "\n";
    
    // All compile-time constants - zero runtime cost!
}
### Example 4: Multi-Format Export

```cpp
#include <commrat/commrat.hpp>
#include <rfl/json.hpp>
#include <rfl/yaml.hpp>
#include <rfl/toml.hpp>

int main() {
    // Export to different formats for different tools
    MyApp::Introspection::write_to_file<rfl::json>("schemas.json");   // For SeRTial viewer
    MyApp::Introspection::write_to_file<rfl::yaml>("schemas.yaml");   // Human-readable docs
    MyApp::Introspection::write_to_file<rfl::toml>("schemas.toml");   // Config generation
    
    // Single message to specific format
    auto temp_json = MyApp::Introspection::export_as<TemperatureData, rfl::json>();
    auto temp_yaml = MyApp::Introspection::export_as<TemperatureData, rfl::yaml>();
    
    std::cout << "Schemas exported in multiple formats!\n";
}
```

**Benefit**: rfl's format abstraction gives us JSON/YAML/TOML/XML support for free! 
    std::cout << "Registered messages:\n";
    for (size_t i = 0; i < schemas.message_names.size(); ++i) {
        std::cout << "  " << schemas.message_names[i] << "\n";
        
        // Could parse JSON and display structure here
        // Or use rfl::json::read to deserialize back to Layout
    }
}
```

## Documentation Updates

### 1. USER_GUIDE.md

Add new Section 13: Message Introspection

```markdown
## 13. Message Introspection

CommRaT provides runtime introspection of message types using SeRTial's reflection mechanisms.

### Exporting Schemas

Generate JSON schemas for all registered messages:

```cpp
// Export to file for visualization
MyApp::write_schemas_to_file("schemas.json");

// Or get as string
std::string json = MyApp::export_all_schemas();
```

### Using the Schema Viewer

1. Export schemas: `MyApp::write_schemas_to_file("my_schemas.json")`
2. Open SeRTial viewer: `file:///path/to/SeRTial/tools/sertial-inspect/viewer.html`
3. Load your JSON file
4. Inspect message layouts, sizes, padding, execution blocks

### Compile-Time Queries

```cpp
constexpr auto layout = MyApp::get_layout<MyMessage>();
std::cout << "Size: " << layout.sizeof_bytes << " bytes\n";
std::cout << "Fields: " << layout.num_fields << "\n";
```

### Runtime Inspection

```cpp
MyApp::inspect_message(my_msg, [](std::string_view name, const auto& value) {
    std::cout << name << " = " << value << "\n";
});
```
```

### 2. ROADMAP.md

Update Near-Term Features:

```markdown
### Developer Tools

**Message Introspection** (Complete)
- Export JSON schemas for all message types
- Integration with SeRTial schema viewer
- Runtime field iteration and logging
- Compile-time size and layout queries
- Status: Implemented
- Priority: Medium
```

### 3. docs/README.md

Add to "Additional Capabilities" section:

```markdown
**Message Introspection:**
- Export JSON schemas via `MyApp::write_schemas_to_file()`
- Compile-time layout queries: `MyApp::get_layout<T>()`
- Runtime field iteration: `MyApp::inspect_message(msg, visitor)`
- Integration with SeRTial schema viewer for visualization
```

### 4. New Example

Create `examples/introspection_example.cpp`:

```cpp
// Demonstrates message introspection and schema export

#include <commrat/commrat.hpp>
#include <commrat/introspection/schema_export.hpp>
#include <commrat/introspection/message_logger.hpp>

struct SensorData {
    uint32_t sensor_id;
    float temperature;
    float pressure;
    uint64_t timestamp_ns;
};

using MyApp = commrat::CommRaT<
    commrat::Message::Data<SensorData>
>;

int main() {
    // 1. Compile-time introspection
    constexpr auto layout = MyApp::get_layout<SensorData>();
    std::cout << "SensorData layout:\n";
    std::cout << "  Struct size: " << layout.sizeof_bytes << " bytes\n";
    std::cout << "  Packed size: " << layout.base_packed_size << " bytes\n";
    std::cout << "  Fields: " << layout.num_fields << "\n\n";
    
    // 2. Export schemas to file
    MyApp::write_schemas_to_file("sensor_schemas.json");
    std::cout << "Schemas exported to sensor_schemas.json\n\n";
    
    // 3. Runtime inspection
    SensorData data{
        .sensor_id = 42,
        .temperature = 25.3f,
        .pressure = 1013.25f,
        .timestamp_ns = 1234567890
    };
    
    std::cout << "Runtime field inspection:\n";
## Implementation Checklist

### Phase 1: Minimal Wrapper (High Priority - 2-3 hours)
- [ ] Create `include/commrat/introspection.hpp` (single header, ~50 lines)
- [ ] Add `using Introspection = IntrospectionHelper<UserRegistry>` to CommRaT<>
- [ ] Test JSON export with `write_to_file<rfl::json>()`
- [ ] Test YAML export with `write_to_file<rfl::yaml>()`
- [ ] Verify output works with SeRTial viewer

### Phase 2: Example (Medium Priority - 1-2 hours)
- [ ] Create `examples/introspection_example.cpp`
- [ ] Demonstrate multi-format export (JSON, YAML, TOML)
- [ ] Show direct SeRTial usage for single-message queries
- [ ] Show direct rfl usage for runtime field iteration
- [ ] Add CMakeLists.txt entry

### Phase 3: Documentation (High Priority - 2 hours)
- [ ] Add brief section to USER_GUIDE.md explaining:
  - Registry-wide export via `MyApp::Introspection`
  - Single-message via SeRTial directly
  - Runtime iteration via rfl directly
- [ ] Update docs/README.md feature list
- [ ] Update ROADMAP.md (mark as complete)
- [ ] Clarify what's CommRaT vs SeRTial responsibility

### Phase 4: Testing (High Priority - 1 hour)
- [ ] Test export for various message types
- [ ] Verify multi-format support works
- [ ] Load generated JSON in SeRTial viewer
- [ ] Validate no performance impactature
- [ ] Update docs/README.md feature list
- [ ] Add introspection example to docs/examples/

### Phase 4: Testing (High Priority)
- [ ] Test schema export for all message types
- [ ] Verify JSON format compatibility with sertial-inspect
- [ ] Test compile-time layout queries
- [ ] Test runtime field iteration
- [ ] Validate with SeRTial schema viewer

## Dependencies

**Required**:
## Benefits

1. **Minimal Code**: Only ~50 lines in CommRaT, rest is in SeRTial/rfl
2. **Format Agnostic**: JSON, YAML, TOML, XML support via rfl
3. **Zero Abstraction Cost**: Direct access to SeRTial/rfl when needed
4. **Clear Boundaries**: CommRaT = registry operations, SeRTial = single-message operations
5. **Leverage Existing Tools**: SeRTial viewer already exists and works
6. **Type Safety**: All introspection based on compile-time analysis
## Benefits
## Design Decisions

1. **Why not wrap single-message operations?**
   - SeRTial and rfl already provide clean APIs
   - Wrapping adds no value, just indirection
   - Users can use SeRTial/rfl directly when needed

2. **Why focus on registry-wide export?**
   - This is CommRaT-specific (the registry)
   - Common use case: "export all my app's messages"
   - Minimal code to implement (~50 lines)

3. **Why leverage rfl's format abstraction?**
   - Get JSON/YAML/TOML/XML support for free
   - No custom formatting logic needed
   - Future formats automatically supported

4. **Should we add CommRaT metadata to schemas?**
   - Could extend with message IDs, mailbox addresses
   - Would require custom Reflector for StructLayout
   - Better as separate feature if needed

## Future Extensions (Based on Introspection)

Once introspection is in place, several powerful features become possible:

### 1. Generic Logger/Replay System

**Generic Logger Module**: Record any message type to disk (selectable format)
```cpp
// Configure logger to record specific messages
LoggerConfig logger_config{
    .name = "DataLogger",
    .system_id = 100,
    .instance_id = 1,
    .input_sources = {
        {10, 1},  // TemperatureData
        {11, 1}   // StatusData
    },
    .log_format = LogFormat::BINARY,  // or JSON, YAML
    .output_file = "session.log"
};

// Logger uses introspection to handle any registered type
auto logger = MyApp::create_logger(logger_config);
```

**Replay Module**: Publish logged data back into the system
```cpp
// Replay from log file
ReplayConfig replay_config{
    .name = "DataReplay",
    .system_id = 101,
    .instance_id = 1,
    .input_file = "session.log",
    .playback_speed = 1.0  // Real-time
};

auto replay = MyApp::create_replay(replay_config);
// Automatically publishes to correct mailboxes based on message types
```

**Key Features**:
- **Binary logging**: Use SeRTial's packed format (minimal overhead)
- **JSON logging**: Human-readable debugging
- **Type-agnostic**: Logger works with any message in registry
- **Timestamp preservation**: Maintain exact timing for replay
- **Real-time safe**: Binary format has bounded size

**Implementation**: Separate plan needed (`LOGGER_REPLAY_PLAN.md`)

### 2. JSON Configuration System (Composer)

**Load entire dataflow from JSON config**:
```json
{
  "modules": [
    {
      "type": "SensorModule",
      "name": "TempSensor",
      "system_id": 10,
      "instance_id": 1,
      "config": {
        "period_ms": 100,
        "sensor_address": "/dev/i2c-1"
      }
    },
    {
      "type": "FilterModule",
      "name": "KalmanFilter",
      "system_id": 20,
      "instance_id": 1,
      "config": {
        "input_source": {"system_id": 10, "instance_id": 1}
      }
    }
  ],
  "connections": [
    {"from": [10, 1], "to": [20, 1], "message_type": "TemperatureData"}
  ]
}
```

**Composer API**:
```cpp
// Register available module types
MyApp::Composer composer;
composer.register_module_type<SensorModule>("SensorModule");
composer.register_module_type<FilterModule>("FilterModule");

// Load and instantiate from JSON
composer.load_from_file("dataflow.json");
composer.start_all();
```

**Key Features**:
- **Dynamic module instantiation**: Create modules from JSON
- **Type safety**: Use introspection to validate message types
- **Configuration validation**: Check connections before starting
- **Runtime composition**: No recompilation for dataflow changes

**Implementation**: Separate plan needed (`COMPOSER_SYSTEM_PLAN.md`)

### 3. Integration Points

**Introspection enables both features**:
- Logger uses `MyApp::Introspection::export_all()` to know what to record
- Replay uses message IDs to route correctly
- Composer uses schemas to validate connections
- All leverage rfl's JSON support for configuration

**Dependencies**:
1. ✅ Introspection (this plan)
2. ⏳ Logger/Replay system (needs separate plan)
3. ⏳ Composer system (needs separate plan)
4. ⏳ Module factory/registry pattern (required for composer)ation

2. **Do we need custom formatting** for CommRaT-specific metadata (message IDs, mailbox addresses)?
   - Could extend schema with CommRaT-specific fields
   - Would require custom Reflector

3. **Should we provide Python bindings** for schema parsing?
   - Could be useful for external tooling
## Timeline

- **Phase 1** (Minimal Wrapper): 2-3 hours
- **Phase 2** (Example): 1-2 hours
- **Phase 3** (Docs): 2 hours
- **Phase 4** (Testing): 1 hour
- **Total**: ~6-8 hours (less than 1 day)ge types
- [ ] Runtime field iteration works with visitor pattern
## Related Files

**To Create** (minimal):
- `include/commrat/introspection.hpp` (~50 lines)
- `examples/introspection_example.cpp`

**To Modify**:
- `include/commrat/commrat.hpp` (add 3 lines: `using Introspection = ...`)
- `docs/USER_GUIDE.md` (add brief introspection section)
- `docs/README.md` (update feature list)
- `docs/ROADMAP.md` (mark as complete)
- `CMakeLists.txt` (add example)

**Use Directly** (no wrapper needed):
- SeRTial: `sertial::StructLayout<T>` for compile-time queries
- rfl: `rfl::to_named_tuple()` + `rfl::apply()` for runtime iteration
- rfl: Format writers (`rfl::json`, `rfl::yaml`, `rfl::toml`, etc.)

**Tools** (already exist in SeRTial):
- SeRTial: `tools/sertial-inspect/viewer.html` (web viewer)
- SeRTial: `tools/sertial-inspect/sertial-inspect` (CLI tool)

## Next Steps After Introspection

Once introspection is complete, these features become feasible:

1. **Generic Logger/Replay** (`docs/work/LOGGER_REPLAY_PLAN.md` - to be created)
   - Binary logging using SeRTial packed format
   - JSON logging for debugging
   - Replay module for data playback
   - Type-agnostic via introspection

2. **JSON Composer System** (`docs/work/COMPOSER_SYSTEM_PLAN.md` - to be created)
   - Load module configurations from JSON
   - Validate connections using introspection
   - Dynamic module instantiation
   - Runtime dataflow composition

Both features depend on introspection being available first.

**To Modify**:
- `include/commrat/commrat.hpp` (add introspection methods)
- `docs/USER_GUIDE.md` (add Section 13)
- `docs/README.md` (update feature list)
- `docs/ROADMAP.md` (mark as complete)
- `CMakeLists.txt` (add new example)

**External Reference**:
- SeRTial: `tools/sertial-inspect/viewer.html`
- SeRTial: `include/sertial/core/layout/struct_layout.hpp`
- SeRTial: `include/sertial/core/layout/struct_layout_reflector.hpp`
