# Parameter System

**Status**: Design Phase  
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

## Proposed Design

### Parameter Definition

```cpp
// Define parameters with metadata
// Allowed types: primitives, sertial::fixed_string<N>, sertial::fixed_vector<T,N>
struct MyModuleParams : Parameters<
    Param<"gain",        float,                        Default<1.0f>,          Range<0.0f, 10.0f>>,
    Param<"buffer_size", int,                          Default<100>,           Range<1, 1000>>,
    Param<"device_path", sertial::fixed_string<128>,   Default<"/dev/sensor0">>,
    Param<"enabled",     bool,                         Default<true>>,
    Param<"coefficients",sertial::fixed_vector<float,16>, Default<{}>>   // array param
> {};
```

All parameter types must be **RT-safe** — no heap allocation in the hot path:
- Primitives (`float`, `int`, `bool`, `uint32_t`, …) — always fine
- `sertial::fixed_string<N>` — RT-safe and rfl-reflectable via `sertial/containers/reflectors.hpp` (reflected as `std::string`)
- `sertial::fixed_vector<T, N>` — RT-safe and rfl-reflectable (reflected as `std::vector<T>`)
- Plain POD structs with only rfl-reflectable fields

Include `<sertial/containers/reflectors.hpp>` in any translation unit that serializes a `Params` struct. This is already done by `commrat/module/params.hpp`.

`std::vector` is **not** allowed as a parameter type (heap allocation on modification).

// Module uses parameters
class MyModule : public MyApp::Module<
    Output<Data>,
    Input<Sensor>,
    Params<MyModuleParams>  // NEW: Specify parameters
> {
protected:
    Data process(const Sensor& input) override {
        // Access parameters (compile-time, zero overhead)
        float gain = params_.get<"gain">();
        int size = params_.get<"buffer_size">();
        
        return Data{input.value * gain};
    }
    
    // Optional: Custom validation
    void on_params_changed() override {
        RTLOG_INFO(logger_) << "Parameters updated";
        reinitialize_filter();
    }
};
```

### Automatic Features

**1. Loading from Config File**
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

### Module Integration

```cpp
template<typename OutputSpec, typename InputSpec, typename ParamSpec = NoParams>
class Module {
protected:
    ParamSpec params_;

    // Called automatically after any parameter change (set via command or code).
    virtual void on_params_changed() {}

private:
    // Automatic command handler — wired into the CMD mailbox visitor.
    // No user code needed: Module2 detects 'typename ModuleType::Params'
    // at compile time and registers these handlers automatically.
    void handle_param_command(const ParamCommand& cmd);
};
```

### Module2 Automatic Command Handling

Module2's existing per-output command threads already dispatch system commands
(Subscribe, GetData) and user commands (DataWithCommands). Parameter commands
are handled the same way — **no boilerplate in user modules**.

At compile time, `command_loop_impl` detects the `Params<T>` IOSpec via `Module2::has_params`
(a `static constexpr bool` derived from the IOSpecs) and inserts the parameter
command handlers into the visitor. No user code is needed — declaring `Params<MyParams>`
in the IOSpec is sufficient:

```cpp
// Auto-generated inside command_loop_impl (conceptual — user sees none of this):
if constexpr (requires { typename ModuleType::Params; }) {
    if constexpr (std::is_same_v<CmdType, SetParamCmd>) {
        // 1. Locate the field by name using rfl field iteration
        // 2. Parse the JSON-encoded value into the correct type
        // 3. Apply range validation
        // 4. Update params_ atomically
        // 5. Call on_params_changed()
        // 6. Send SetParamReply via CMD mailbox
        handle_set_param(params_, received_msg.payload, reply);
        return;
    }
    if constexpr (std::is_same_v<CmdType, GetParamCmd>) { ... }
    if constexpr (std::is_same_v<CmdType, ListParamsCmd>) { ... }
}
```

rfl field iteration (`rfl::for_each`) is used at runtime to find the named
field and convert its value to/from the `fixed_string<256>` wire format.

Parameter commands are routed to **output 0's CMD mailbox** (the module's
primary command interface). A module with no outputs would use a dedicated
module-level CMD mailbox (not yet implemented).

**Thread safety**: `params_` is read from the data thread and written from the
command thread. A `SharedMutex` guards the struct — the data thread holds a
shared (read) lock, the command thread holds an exclusive (write) lock during
`handle_set_param`.

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

**Phase 1**: Core parameter system (2 weeks)
- `Param<>` template definition
- `ParameterSet` storage and access
- Compile-time indexing and validation

**Phase 2**: Serialization (1 week)
- JSON/YAML loading and saving
- Type conversion (string ↔ native type)
- Default value handling

**Phase 3**: System commands (2 weeks)
- Implement Get/Set/List/Save/Load commands
- Automatic command handler in Module base
- Reply mechanisms

**Phase 4**: Advanced features (2 weeks)
- Parameter groups and composition
- Dynamic reconfiguration hooks
- Conditional parameters

**Total Estimated Effort**: 7 weeks

## Related Work

- Module base: `include/commrat/registry_module.hpp`
- System commands: `include/commrat/messages.hpp`
- Configuration: `ModuleConfig` structure
- Introspection: `docs/work/INTROSPECTION_INTEGRATION_PLAN.md`
