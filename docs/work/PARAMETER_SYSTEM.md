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
struct MyModuleParams : Parameters<
    Param<"gain", float, Default<1.0f>, Range<0.0f, 10.0f>>,
    Param<"buffer_size", int, Default<100>, Range<1, 1000>>,
    Param<"device_path", std::string, Default<"/dev/sensor0">>,
    Param<"enabled", bool, Default<true>>
> {};

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
        std::cout << "Parameters updated\n";
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
std::cout << reply.value;  // "2.5"

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

```cpp
// System parameter commands
enum class SystemMessages : uint32_t {
    // ... existing ...
    
    // Parameter management
    GetParam = 0x00000020,
    GetParamReply = 0x00000021,
    SetParam = 0x00000022,
    SetParamReply = 0x00000023,
    ListParams = 0x00000024,
    ListParamsReply = 0x00000025,
    SaveParams = 0x00000026,
    LoadParams = 0x00000027
};

struct GetParamCmd {
    char param_name[64];
};

struct GetParamReply {
    char param_name[64];
    char value[256];  // String representation
    char type[32];    // "float", "int", "bool", "string"
    bool valid{false};
};

struct SetParamCmd {
    char param_name[64];
    char value[256];
    bool temporary{false};  // If true, don't save to file
};

struct SetParamReply {
    bool success{false};
    char error_message[256];
};

struct ParamInfo {
    char name[64];
    char type[32];
    char value[256];
    char default_value[256];
    char range[128];  // "[min, max]" or empty
    char description[256];
};

struct ListParamsReply {
    uint32_t count;
    sertial::fixed_vector<ParamInfo, 64> parameters;
};

struct SaveParamsCmd {
    char path[256];  // If empty, use default location
};

struct LoadParamsCmd {
    char path[256];
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
    
    // Runtime access (for commands)
    std::optional<std::string> get_string(const std::string& name);
    bool set_from_string(const std::string& name, const std::string& value);
    
    // Serialization
    void load_from_json(const std::string& path);
    void save_to_json(const std::string& path);
};
```

### Module Integration

```cpp
template<typename OutputSpec, typename InputSpec, typename ParamSpec = NoParams>
class Module {
protected:
    // Parameter access
    ParamSpec params_;
    
    // Automatically called after parameter changes
    virtual void on_params_changed() {}
    
private:
    // Automatic command handler
    void handle_param_command(const ParamCommand& cmd) {
        switch (cmd.type) {
            case GetParam:
                return handle_get_param(cmd);
            case SetParam:
                return handle_set_param(cmd);
            // ... etc
        }
    }
};
```

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
        
        std::cout << "Filter reconfigured with gain=" << new_gain << "\n";
    }
};
```

### Parameter Groups

```cpp
// Organize related parameters
struct NetworkParams : Parameters<
    Param<"ip_address", std::string, Default<"127.0.0.1">>,
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
    Param<"mode", std::string, Default<"auto">, Options<"auto", "manual", "test">>,
    
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
