# Parameter System

**Status**: Phase 1 implemented — plain `struct Params` + `Params<T>` IOSpec + whole-struct Get/Set commands  
**Priority**: High  
**Created**: February 12, 2026

## Overview

Strongly-typed parameter definition and management system with automatic loading, validation, and command-based access. Parameters are defined at compile time with zero runtime overhead for access.

## Motivation

Current state: Users manually handle configuration
```cpp
struct MyConfig {
    float gain;
    int buffer_size;
    std::string device_path;
};

// Manual loading
MyConfig load_config(const std::string& path) {
    // Parse JSON/YAML/etc manually
    // No type validation
    // No default values
    // No bounds checking
}
```

**Problems**:
- Boilerplate for every module
- No standardized commands for parameter access
- No validation or bounds checking
- Runtime errors instead of compile-time safety

## Current Design

### Parameter Definition (implemented)

Params are a plain rfl-reflectable aggregate. Field names and types drive
serialization; no wrapper template is needed for Phase 1:

```cpp
// include sertial/containers/reflectors.hpp to make fixed_string/fixed_vector rfl-reflectable
#include <sertial/containers/reflectors.hpp>

struct MySensorParams {
    float gain{1.0f};
    int   filter_window{5};
    sertial::fixed_string<64> device{"/dev/i2c-1"};
    sertial::fixed_vector<float, 8> coefficients{};
};
```

Allowed types:
- Primitives (`float`, `int`, `bool`, `uint32_t`, …)
- `sertial::fixed_string<N>` — RT-safe; rfl-reflectable via `sertial/containers/reflectors.hpp`
- `sertial::fixed_vector<T, N>` — RT-safe; rfl-reflectable
- Plain POD structs with only rfl-reflectable fields

`std::vector` is **not** allowed (heap allocation on modification).
`commrat/module/params.hpp` already includes `reflectors.hpp`.

### Module usage (implemented)

Add `Params<T>` to the IOSpec. Module2 automatically:
- stores `params_` of type `T`
- loads it from `ModuleConfig::params` on construction (via `params_from_config<T>(config)` with `rfl::DefaultIfMissing` so partial JSON overrides work)
- overrides `commrat_get_params_json_()` / `commrat_set_params_json_()` as `virtual final`
- handles `GetParamsCmd` / `SetParamsCmd` on the CMD mailbox with no user code

```cpp
struct MySensorParams {
    float gain{1.0f};
    sertial::fixed_string<64> device{"/dev/i2c-1"};
};

class SensorModule : public MyApp::Module2<Output<SensorData>,
                                           Period<Milliseconds(10)>,
                                           Params<MySensorParams>> {
public:
    explicit SensorModule(const ModuleConfig& config) : Base(config) {}

protected:
    void process(SensorData& out) override {
        out.value = read_sensor() * params_.gain;   // zero overhead
    }

    void on_params_changed() override {             // optional
        RTLOG_INFO(logger_) << "gain=" << params_.gain;
    }
};
```

### Automatic features (implemented)

**1. Load from launch config**

```json
{
  "name": "sensor",
  "module_class": "SensorModule",
  "outputs": [{"system_id": 10, "instance_id": 1}],
  "params": {"gain": 2.5}    // partial — missing fields use struct defaults
}
```

**2. Get/Set whole params via command**

```cpp
// Get all params as a JSON object
GetParamsCmd get_cmd{};
auto reply = send_command<GetParamsReply>(module_address, get_cmd);
// reply.json == {"gain":2.5,"device":"/dev/i2c-1"}
// reply.has_params == true

// Replace all params from a JSON object
SetParamsCmd set_cmd{};
set_cmd.json = "{\"gain\":3.0}";
auto set_reply = send_command<SetParamsReply>(module_address, set_cmd);
// set_reply.success == true; on_params_changed() was called
```

**3. Build-time descriptor**

`*.module.json` descriptor contains `params_defaults` as a nested JSON object:
```json
{"params_defaults": {"gain": 1.0, "filter_window": 5, "device": "/dev/i2c-1"}}
```
Generated automatically by `--commrat-inspect` POST_BUILD step.

**4. Type safety**

`params_` is accessed directly in `process()` — no function call, no lock, no copy.
Modification only happens from the command thread via `commrat_set_params_json_()`,
which is `virtual final` — cannot be accidentally overridden by the user module.

### Not yet implemented

- Thread safety: no `SharedMutex` yet — the command thread writes `params_` while
  the data thread reads it. For Phase 1 this is acceptable for simple types; a
  `SharedMutex` wrapper is needed for correctness under load.
- Per-field Get/Set by name (current commands send the whole params JSON).
- `ListParamsCmd` (enumerate all fields with type/value/range info).
- `SaveParamsCmd` / `LoadParamsCmd` (persist to/from file).
- `Parameters<Param<"name", T, Default<v>, Range<lo,hi>>>` typed wrapper
  (Range validation, compile-time field indexing).

---

## Future Design

### Automatic Features (future — `Parameters<Param<...>>` system)

These require the `Parameters<>` typed wrapper (Phase 2+):
```cpp
// Module constructor automatically loads from config
MyModule module(config);  
// Reads: /etc/commrat/modules/MyModule_10_1.json
// Format:
// {
//   "gain": 2.5,
//   "buffer_size": 200,
//   "device_path": "/dev/ttyUSB0",
//   "enabled": true
// }
```

**2. Validation**
```cpp
// Range checking automatic
params_.set<"gain">(15.0f);  // ERROR: Out of range [0.0, 10.0]

// Type checking at compile time
params_.set<"gain">("string");  // Compile error: Type mismatch

// Missing parameters use defaults
// If "gain" not in config file → uses 1.0f
```

**3. System Commands**

Automatically generated commands (no user code needed):

```cpp
// Get parameter value
GetParamCmd cmd{.param_name = "gain"};
auto reply = send_command<GetParamReply>(module_address, cmd);
RTLOG_INFO(logger_) << "gain=" << reply.value;

// Set parameter value
SetParamCmd cmd{.param_name = "gain", .value = "3.0"};
send_command(module_address, cmd);  // Module validates and applies

// List all parameters
ListParamsCmd cmd{};
auto reply = send_command<ListParamsReply>(module_address, cmd);
// Returns: [{name: "gain", type: "float", value: "2.5", range: "[0.0, 10.0]"}, ...]

// Save current parameters
SaveParamsCmd cmd{.path = "/etc/commrat/modules/MyModule_10_1.json"};
send_command(module_address, cmd);  // Writes current values to file
```

### Command Definitions

All parameter command structs use SeRTial bounded containers so they are
safe to send over TiMS (no heap allocation, bounded size at compile time):

```cpp
struct GetParamCmd {
    sertial::fixed_string<64> param_name;
};

struct GetParamReply {
    sertial::fixed_string<64>  param_name;
    sertial::fixed_string<256> value;    // JSON-encoded string representation
    sertial::fixed_string<32>  type;     // "float", "int", "bool", "fixed_string", …
    bool valid{false};
};

struct SetParamCmd {
    sertial::fixed_string<64>  param_name;
    sertial::fixed_string<256> value;    // JSON-encoded
    bool temporary{false};               // if true, don't persist to file
};

struct SetParamReply {
    bool success{false};
    sertial::fixed_string<128> error_message;
};

struct ParamInfo {
    sertial::fixed_string<64>  name;
    sertial::fixed_string<32>  type;
    sertial::fixed_string<256> value;
    sertial::fixed_string<256> default_value;
    sertial::fixed_string<128> range;    // "[min, max]" or empty
};

struct ListParamsReply {
    uint32_t count;
    sertial::fixed_vector<ParamInfo, 64> parameters;
};

struct SaveParamsCmd {
    sertial::fixed_string<256> path;     // empty = default location
};

struct LoadParamsCmd {
    sertial::fixed_string<256> path;
    bool apply_immediately{true};
};
```

## Implementation Details

### Parameter Storage

```cpp
template<typename... ParamDefs>
class ParameterSet {
    // Storage: std::tuple of values
    std::tuple<typename ParamDefs::Type...> values_;
    
    // Metadata: constexpr lookup table
    static constexpr auto metadata = make_param_metadata<ParamDefs...>();
    
public:
    // Compile-time access (zero overhead)
    template<fixed_string Name>
    auto& get() {
        return std::get<param_index<Name>()>(values_);
    }
    
    template<fixed_string Name>
    void set(typename param_type<Name>::Type value) {
        validate_range<Name>(value);  // Compile-time if possible
        std::get<param_index<Name>()>(values_) = value;
        notify_changed();
    }
    
    // Runtime access (for commands — not called from hot path)
    bool get_as_json(const sertial::fixed_string<64>& name, sertial::fixed_string<256>& out) const;
    bool set_from_json(const sertial::fixed_string<64>& name, const sertial::fixed_string<256>& value);

    // Serialization (startup/shutdown only — not RT-safe)
    void load_from_json(const std::string& path);
    void save_to_json(const std::string& path);
};
```

### Module Integration (implemented)

All the boilerplate below is **auto-generated** by Module2 when `Params<T>` is in the IOSpec.
See the "Current Design" section above for the actual user API.

### Module2 Automatic Command Handling (implemented)

Module2's existing per-output command threads already dispatch system commands
(Subscribe, GetData) and user commands (DataWithCommands). Parameter commands
are handled the same way — **no boilerplate in user modules**.

`has_params` is a `static constexpr bool` on `Module2`, set when `Params<T>` is
in the IOSpec. `visit_param_commands` is wired into `command_loop_impl` for
`GetParamsCmd` (returns whole params JSON) and `SetParamsCmd` (replaces whole
params from JSON, calls `on_params_changed()`).

Current: whole-struct Get/Set only. Per-field Get/Set by name is a future addition.

Parameter commands are routed to **output 0's CMD mailbox** (the module's
primary command interface).

**Thread safety**: not yet guarded — `params_` is written by the command thread
and read by the data thread. Safe for simple atomic types in practice; a
`SharedMutex` is planned for Phase 2.

## Benefits

1. **Type Safety**: Compile-time parameter validation
2. **Zero Overhead**: Direct access via compile-time indexing
3. **Automatic Commands**: No boilerplate for get/set/list/save
4. **Default Values**: Parameters always have sensible defaults
5. **Range Validation**: Bounds checking prevents invalid configurations
6. **Persistence**: Save/load parameters without custom code
7. **Remote Access**: Control parameters via commands from any module
8. **Documentation**: Parameter metadata self-documents configuration

## Advanced Features

### Dynamic Reconfiguration

```cpp
class AdaptiveFilter : public MyApp::Module<
    Output<Filtered>,
    Input<Raw>,
    Params<FilterParams>
> {
protected:
    void on_params_changed() override {
        // Automatically called when parameters change
        float new_gain = params_.get<"gain">();
        filter_.set_gain(new_gain);
        RTLOG_INFO(logger_) << "Filter reconfigured with gain=" << new_gain;
    }
};
```

### Parameter Groups

```cpp
// Organize related parameters
struct NetworkParams : Parameters<
    Param<"ip_address", sertial::fixed_string<64>, Default<"127.0.0.1">>,
    Param<"port", int, Default<5000>, Range<1024, 65535>>
> {};

struct FilterParams : Parameters<
    Param<"cutoff_hz", float, Default<10.0f>, Range<0.1f, 1000.0f>>,
    Param<"order", int, Default<4>, Range<1, 10>>
> {};

// Combine groups
using MyModuleParams = CombineParams<NetworkParams, FilterParams>;
```

### Conditional Parameters

```cpp
struct ConditionalParams : Parameters<
    Param<"mode", sertial::fixed_string<16>, Default<"auto">, Options<"auto", "manual", "test">>,
    
    // Only used in manual mode
    Param<"manual_value", float, Default<0.0f>, 
          EnabledIf<"mode", "manual">>,
    
    // Only used in test mode
    Param<"test_duration", int, Default<60>,
          EnabledIf<"mode", "test">>
> {};
```

## Implementation Plan

### Done

- `Params<T>` IOSpec tag (`io_spec.hpp`; `CreateIOInstance` returns `void` so `BuildIOTuple` ignores it)
- `Module2`: `ExtractParams`, `ParamsType`, `has_params`, `params_` member, constructor loading
- `Module2`: `commrat_get_params_json_()` / `commrat_set_params_json_()` auto-overrides (`virtual final`)
- `Module2`: `visit_param_commands` dispatches `GetParamsCmd` / `SetParamsCmd` / `ListParamsCmd`
- `Module2`: `SharedMutex params_mutex_` guards `params_`; data thread holds shared lock during `process()`, command thread holds exclusive lock during `set_params_json_`
- `param_messages.hpp`: `GetParamsCmd/Reply`, `SetParamsCmd/Reply`, `ListParamsCmd/Reply` with `ParamInfo` per field
- System registry + CommRaT<> registry include all three param commands
- `CmdMailbox` in `module_output.hpp` includes all param payload types
- `params_from_config<T>(config)` helper with `rfl::DefaultIfMissing`
- `ModuleConfig::params` field (`std::optional<rfl::Generic>`)
- `meta/inspect.hpp`: `params_defaults` as nested JSON object in `*.module.json`
- `on_params_changed()` virtual callback
- Example: `module_with_params_example.cpp`
- Test: `test/test_params.cpp` (6 cases)

### Phase 2 — per-field commands + persistence (implemented)

- `GetParamCmd` / `SetParamCmd` with `param_name` field (per-field, not whole-struct)
- `SaveParamsCmd` / `LoadParamsCmd` — read/write a JSON file

**EVL note**: both the data thread and command thread run OOB in EVL mode. `SaveParamsCmd`/`LoadParamsCmd` use `std::ofstream`/`std::ifstream` which demote the command thread to in-band. Once CoreRaT's EVL IPC backend lands (Phase 3 of CoreRaT), these handlers must use `corerat::RtFile` (EVL proxy I/O). Marked with `TODO(EVL)` comments in `module2.hpp`.

### Phase 4 — typed `Parameters<Param<...>>` system (future)

- `Param<"name", T, Default<v>, Range<lo,hi>>` typed descriptors
- Compile-time range validation
- `params_.get<"name">()` / `params_.set<"name">(v)` named accessors
- Parameter groups and conditional parameters

## File locations

| File | Role |
|---|---|
| `include/commrat/module/io/io_spec.hpp` | `Params<T>` tag, `is_params_spec_v`, `CreateIOInstance` specialization |
| `include/commrat/module2.hpp` | `ExtractParams`, `params_`, auto-overrides, `visit_param_commands` |
| `include/commrat/module/params.hpp` | `params_from_config<T>(config)` helper |
| `include/commrat/messaging/system/param_messages.hpp` | `GetParamsCmd/Reply`, `SetParamsCmd/Reply` |
| `include/commrat/messaging/system/system_registry.hpp` | registers param commands as system messages |
| `include/commrat/module/io/output/module_output.hpp` | adds param types to `CmdMailbox` |
| `include/commrat/meta/descriptor.hpp` | `ModuleDescriptor::params_defaults` as `rfl::Generic` |
| `include/commrat/meta/inspect.hpp` | populates `params_defaults` from `ModuleType::ParamsType` |
| `examples/module_with_params_example.cpp` | working example |
| `test/test_params.cpp` | 6 unit tests |
