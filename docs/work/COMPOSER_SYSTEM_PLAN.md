# JSON Composer System Implementation Plan

## Overview

Create a JSON-based configuration system that allows defining entire CommRaT applications (modules, connections, configurations) declaratively. The composer validates type compatibility and establishes subscriptions automatically.

**Key Architectural Decision**: Each module is a separate binary/process, not a class instance. Modules are identified by their output types, system_id, and instance_id. The "module type" is essentially its output type signature.

**Key Insights**: 
- CommRaT knows all message types and IDs at compile time
- Modules communicate via TiMS (already supports multi-process)
- Module binaries load their config from JSON at startup
- Composer orchestrates launching and connecting separate processes

**Why Process-Per-Module?**
1. **Scalability**: Distribute modules across machines/cores naturally
2. **Isolation**: Module crashes don't bring down entire application
3. **Flexibility**: Mix languages, update modules independently
4. **Simplicity**: No factory pattern needed - modules are just executables
5. **Module Identity**: Defined by output types + IDs, not class name

**Module Type = Output Types**:
- Two modules outputting `TemperatureData` are the same "type"
- Different `system_id`/`instance_id` makes them distinct instances
- No need for "SensorModule" vs "FilterModule" classes
- Module behavior is in the binary, not the type system

## Core Concept

**Application Description JSON**:
```json
{
  "application": {
    "name": "SensorProcessingPipeline",
    "version": "1.0.0"
  },
  "modules": [
    {
      "binary": "./bin/temperature_sensor",
      "system_id": 10,
      "instance_id": 1,
      "config": {
        "sensor_address": "/dev/i2c-1"
      }
    },
    {
      "binary": "./bin/kalman_filter",
      "system_id": 20,
      "instance_id": 1,
      "config": {
        "filter_window": 5
      }
    }
  ]
}
```

**Note**: 
- `name`, `inputs`, `outputs` are compile-time known (from Module<> template)
- `period` is compile-time constant in module class
- `input_sources` determined by orchestrator from binary introspection or separate mapping
- Only `system_id`, `instance_id`, and custom `config` are runtime parameters

**Metadata Discovery** (orchestrator auto-detects I/O types):
1. **Build-time extraction** (PREFERRED): CMake generates `.metadata.json` during compilation (zero runtime overhead)
2. **Runtime introspection**: `./module_binary --metadata` returns JSON (fallback)
3. **Separate mapping file**: Manual registry (least preferred)

**Example metadata file** (`temperature_sensor.metadata.json`):
```json
{
  "name": "TemperatureSensor",
  "inputs": [],
  "outputs": ["TemperatureData"],
  "period_ns": 100000000
}
```

**Generated at build time** → Zero runtime overhead, no process spawning needed!
}
```

**Key Changes**:
- `binary` field: Path to executable (not class name)
- Module type IS the output type(s)
- Two modules with same output type = same "type", different IDs
- `input_sources` embedded in module definition
- Connections implicit from `input_sources`
- **Message types auto-generated**: Not in JSON - extracted from CommRaT<> registry at compile time
```

**Key Features**:
- **Full type information**: Message types with IDs and schemas
- **Module metadata**: System/instance IDs, input/output types
- **Connection validation**: Ensure type compatibility
- **Configuration per module**: Type-safe config structs

## Architecture

### 1. Module Factory Pattern

**Base factory interface**:
**Helper for loading module config** (in CommRaT):
```cpp
namespace commrat {

struct RuntimeModuleConfig {
    uint32_t system_id;
    uint32_t instance_id;
};

struct LoadedModuleConfig {
    RuntimeModuleConfig module;
    rfl::json::Object custom;  // Module-specific config
};

inline LoadedModuleConfig load_module_config(const std::string& filename) {
    std::ifstream file(filename);
    std::string json_str((std::istreambuf_iterator<char>(file)),
                          std::istreambuf_iterator<char>());
    
    auto result = rfl::json::read<LoadedModuleConfig>(json_str);
    if (!result.has_value()) {
        throw std::runtime_error("Failed to parse module config: " + filename);
    }
    
    return result.value();
}

// Helper to build full ModuleConfig from compile-time info + runtime IDs
template<typename ModuleType>
inline ModuleConfig make_module_config(const RuntimeModuleConfig& runtime) {
    return ModuleConfig{
        .name = ModuleType::module_name,  // Compile-time constant
        .system_id = runtime.system_id,
        .instance_id = runtime.instance_id,
        .period = ModuleType::default_period,  // Compile-time constant
        .input_sources = ModuleType::default_input_sources  // Compile-time constant or empty
    };
}

// Signal handling for graceful shutdown
inline void wait_for_shutdown() {
    std::atomic<bool> shutdown_requested{false};
    
    std::signal(SIGINT, [](int) { 
        /* set flag */ 
    });
    std::signal(SIGTERM, [](int) { 
        /* set flag */ 
    });
    
    while (!shutdown_requested.load()) {
        Time::sleep(Milliseconds(100));
    }
}

} // namespace commrat
```

### 3. Build-Time Metadata Extraction (CMake)

**CMake function** (`cmake/CommRaTModule.cmake`):
```cmake
# Extract module metadata at build time (zero runtime overhead)
function(commrat_add_module)
    set(oneValueArgs TARGET MODULE_TYPE)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "" ${ARGN})
    
    # Create a small executable that only exports metadata
    set(METADATA_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}_metadata.cpp")
    
    file(WRITE ${METADATA_SOURCE} "
#include <iostream>
#include <commrat/commrat.hpp>
using MyApp = commrat::CommRaT</* same types as main */>;
int main() {
    std::cout << MyApp::export_module_metadata<${ARG_MODULE_TYPE}>() << std::endl;
    return 0;
}
    ")
    
    # Build metadata extractor
    add_executable(${ARG_TARGET}_metadata_extractor ${METADATA_SOURCE})
    target_link_libraries(${ARG_TARGET}_metadata_extractor commrat)
    
    # Run at build time to generate .metadata.json
    add_custom_command(
        OUTPUT ${ARG_TARGET}.metadata.json
        COMMAND $<TARGET_FILE:${ARG_TARGET}_metadata_extractor> > ${ARG_TARGET}.metadata.json
        DEPENDS ${ARG_TARGET}_metadata_extractor
        COMMENT "Extracting metadata for ${ARG_TARGET}"
    )
    
    # Ensure metadata is generated before main target
    add_custom_target(${ARG_TARGET}_metadata ALL
        DEPENDS ${ARG_TARGET}.metadata.json
    )
    add_dependencies(${ARG_TARGET} ${ARG_TARGET}_metadata)
endfunction()
```

**Usage in CMakeLists.txt**:
```cmake
add_executable(temperature_sensor temperature_sensor.cpp)
target_link_libraries(temperature_sensor commrat)

# Generate temperature_sensor.metadata.json at build time
commrat_add_module(
    TARGET temperature_sensor
    MODULE_TYPE TemperatureSensor
)
```

**Result**: `temperature_sensor.metadata.json` created during compilation, no runtime overhead!

### 4. Alternative: Compile-Time Registry (Header Generation)

**Even simpler**: Generate a C++ header with all module metadata at build time:

```cmake
# Generate module_registry.hpp with all modules
function(commrat_generate_registry)
    set(REGISTRY_FILE "${CMAKE_CURRENT_BINARY_DIR}/module_registry.hpp")
    
    file(WRITE ${REGISTRY_FILE} "#pragma once\n")
    file(APPEND ${REGISTRY_FILE} "#include <string_view>\n")
    file(APPEND ${REGISTRY_FILE} "namespace commrat::registry {\n")
    
    # For each module, extract metadata and add to registry
    foreach(MODULE ${COMMRAT_MODULES})
        # ... extract metadata ...
        file(APPEND ${REGISTRY_FILE} "// ${MODULE} metadata here\n")
    endforeach()
    
    file(APPEND ${REGISTRY_FILE} "}\n")
endfunction()
```

**Orchestrator includes generated header**:
```cpp
#include "module_registry.hpp"

// All metadata available at compile time in orchestrator!
auto metadata = commrat::registry::get_module_metadata("temperature_sensor");
```

### 5. Process Orchestrator (Composer)::runtime_error("Invalid module configuration");
        }
        
        return std::make_unique<ModuleType>(config, typed_config.value());
    }
    
    std::string_view type_name() const override {
        return rfl::type_name_t<ModuleType>().str();
    }
    
    std::vector<std::string_view> input_types() const override {
        // Extract from ModuleType's InputSpec
        return extract_input_types<ModuleType>();
    }
    
    std::vector<std::string_view> output_types() const override {
        // Extract from ModuleType's OutputSpec
        return extract_output_types<ModuleType>();
    }

private:
    template<typename T>
    static std::vector<std::string_view> extract_input_types() {
        using InputSpec = typename T::InputSpecType;
        
        std::vector<std::string_view> types;
        
        if constexpr (is_input_spec<InputSpec>) {
            // Single Input<T>
            types.push_back(rfl::type_name_t<typename InputSpec::type>().str());
        } else if constexpr (is_inputs_spec<InputSpec>) {
            // Multi-input Inputs<T, U, V>
            append_tuple_types<typename InputSpec::types>(types);
        }
        // PeriodicInput and LoopInput have no input types
        
        return types;
    }
    
    template<typename T>
    static std::vector<std::string_view> extract_output_types() {
        using OutputSpec = typename T::OutputSpecType;
        
        std::vector<std::string_view> types;
        
        if constexpr (is_output_spec<OutputSpec>) {
            // Single Output<T>
            types.push_back(rfl::type_name_t<typename OutputSpec::type>().str());
        } else if constexpr (is_outputs_spec<OutputSpec>) {
            // Multi-output Outputs<T, U>
            append_tuple_types<typename OutputSpec::types>(types);
        }
        // NoOutput has no output types
        
        return types;
    }
};
```

### 2. Composer Class

```cpp
template<typename AppType>
class ApplicationComposer {
public:
    /// Register a module type that can be instantiated from JSON
    template<typename ModuleType>
    void register_module_type(std::string_view type_name) {
        factories_[std::string(type_name)] = 
            std::make_unique<ConcreteModuleFactory<AppType, ModuleType>>();
    }
    
    /// Load application definition from JSON file
    void load_from_file(const std::string& filename) {
        auto json_str = read_file(filename);
        auto app_def = rfl::json::read<ApplicationDefinition>(json_str);
        
        if (!app_def.has_value()) {
            throw std::runtime_error("Invalid application definition JSON");
        }
        
        load_from_definition(app_def.value());
    }
    
    /// Load from parsed definition
    void load_from_definition(const ApplicationDefinition& app_def) {
        app_def_ = app_def;
        
        // Validate before instantiation
        validate_message_types();
        validate_modules();
        validate_connections();
        
        // Instantiate all modules
        instantiate_modules();
        
        // Establish subscriptions
        setup_connections();
    }
    
    /// Start all modules
    void start_all() {
        for (auto& module : modules_) {
            module->start();
        }
    }
    
```cpp
template<typename AppType>
class ProcessOrchestrator {
public:
    /// Load application definition from JSON file
    void load_from_file(const std::string& filename) {

private:
    void validate_message_types() {
        // Check that all message types in JSON match compiled registry
        for (const auto& msg_type : app_def_.message_types) {
            bool found = false;
            
            AppType::for_each_message_type([&]<typename T>() {
private:
    void validate_modules() {
        for (const auto& mod_def : app_def_.modules) {
            // Check binary exists
            if (!std::filesystem::exists(mod_def.binary)) {
                throw std::runtime_error(
                    "Module binary not found: " + mod_def.binary
                );
            }
            
            // Load metadata (priority: .metadata.json > --metadata flag)
            auto metadata = load_module_metadata(mod_def.binary);
            
            // Validate input/output types are registered
            for (const auto& input_type : metadata.inputs) {
                if (!is_type_registered(input_type)) {
                    throw std::runtime_error(
                        "Input type not registered: " + input_type
                    );
                }
            }
            
            for (const auto& output_type : metadata.outputs) {
                if (!is_type_registered(output_type)) {
                    throw std::runtime_error(
                        "Output type not registered: " + output_type
                    );
                }
            }
            
            // Validate input_sources refer to valid modules
            for (const auto& source : mod_def.input_sources) {
                auto source_mod = find_module_def(source.system_id, source.instance_id);
                if (!source_mod) {
                    throw std::runtime_error(
                        "Module " + mod_def.name + " references unknown source: " +
                        std::to_string(source.system_id) + "," + 
                        std::to_string(source.instance_id)
                    );
                }
            }
        }
    }
    
    struct ModuleMetadata {
        std::string name;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
        uint64_t period_ns;
    };
    
    ModuleMetadata load_module_metadata(const std::string& binary_path) {
        // Priority 1: Load from .metadata.json (build-time generated)
        std::string metadata_file = binary_path + ".metadata.json";
        if (std::filesystem::exists(metadata_file)) {
            std::ifstream file(metadata_file);
            std::string json_str((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            
            auto metadata = rfl::json::read<ModuleMetadata>(json_str);
            if (metadata.has_value()) {
                return metadata.value();
            }
        }
        
        // Priority 2: Query via --metadata flag (runtime fallback)
        return query_module_metadata(binary_path);
    }
    
    ModuleMetadata query_module_metadata(const std::string& binary_path) {
        // Run binary with --metadata flag to get compile-time info
        std::string command = binary_path + " --metadata";
        
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw std::runtime_error("Failed to query module metadata: " + binary_path);
        }
        
        std::string result;
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe)) {
            result += buffer;
        }
        pclose(pipe);
        
        // Parse JSON metadata
        auto metadata = rfl::json::read<ModuleMetadata>(result);
        if (!metadata.has_value()) {
            throw std::runtime_error("Invalid metadata from: " + binary_path);
        }
        
        return metadata.value();
    }
    
    void launch_module(const ModuleDefinition& mod_def) {
        // Create per-module config file
        std::string config_filename = write_module_config(mod_def);
        
        // Launch process
        ProcessHandle process;
        process.start(mod_def.binary, {config_filename});
        
        // Store process handle
        auto key = std::make_pair(mod_def.system_id, mod_def.instance_id);
        processes_[key] = std::move(process);
        
        std::cout << "Launched module: " << mod_def.name 
                  << " (" << mod_def.system_id << "," << mod_def.instance_id << ")\n";
    }
    
    std::string write_module_config(const ModuleDefinition& mod_def) {
        // Create temporary config file for this module
        std::string filename = "/tmp/commrat_module_" + 
                               std::to_string(mod_def.system_id) + "_" +
                               std::to_string(mod_def.instance_id) + ".json";
        
        // Build config structure (ONLY runtime parameters)
        LoadedModuleConfig config{
            .module = {
                .system_id = mod_def.system_id,
                .instance_id = mod_def.instance_id
            },
            .custom = mod_def.config
        };
        
        // Write to file
        std::ofstream file(filename);
        file << rfl::json::write(config);
        
        return filename;
    }
    
    bool is_type_registered(const std::string& type_name) const {
        bool found = false;
        AppType::for_each_message_type([&]<typename T>() {
            if (rfl::type_name_t<T>().str() == type_name) {
                found = true;
            }
        });
        return found;
    }       }
        }
        return nullptr;
    }

    ApplicationDefinition app_def_;
    std::unordered_map<std::string, std::unique_ptr<ModuleFactory<AppType>>> factories_;
    std::vector<std::unique_ptr<ModuleBase>> modules_;
    std::unordered_map<std::pair<uint32_t, uint32_t>, ModuleBase*> module_map_;
};
```

### 3. JSON Schema Definitions

```cpp
struct MessageTypeDefinition {
    std::string name;
    uint32_t message_id;
    rfl::json::Object schema;  // Optional schema for validation
};

struct IOTypeDefinition {
    std::string type;
    uint32_t message_id;
};

struct ModuleDefinition {
    std::string type;             // Module class name
    std::string name;             // Instance name
    uint32_t system_id;
    uint32_t instance_id;
    std::vector<IOTypeDefinition> inputs;
    std::vector<IOTypeDefinition> outputs;
    rfl::json::Object config;     // Module-specific configuration
};

struct ConnectionEndpoint {
    uint32_t system_id;
    uint32_t instance_id;
    std::string output_type;      // For 'from'
    std::string input_type;       // For 'to'
};

struct ConnectionDefinition {
    ConnectionEndpoint from;
    ConnectionEndpoint to;
};

struct ApplicationInfo {
    ApplicationDefinition app_def_;
    std::unordered_map<std::pair<uint32_t, uint32_t>, ProcessHandle> processes_;
};

// Simple process handle (platform-specific implementation)
class ProcessHandle {
public:
    void start(const std::string& binary, const std::vector<std::string>& args);
    void terminate();
```cpp
struct MessageTypeDefinition {
    std::string name;
    uint32_t message_id;
    rfl::json::Object schema;  // Optional schema for validation
};

struct SourceDefinition {
    uint32_t system_id;
    uint32_t instance_id;
};

struct ModuleDefinition {
    std::string binary;                  // Path to executable
    std::string name;                    // Instance name
struct ModuleDefinition {
    std::string binary;                  // Path to executable
    std::string name;                    // Instance name
    uint32_t system_id;
    uint32_t instance_id;
    std::vector<SourceDefinition> input_sources;  // Where to subscribe
    rfl::json::Object config;            // Module-specific configuration
    // NOTE: inputs/outputs queried via binary --metadata flag
};

struct ApplicationInfo {
    std::string name;
    std::string version;
};

struct ApplicationDefinition {
    ApplicationInfo application;
    std::vector<ModuleDefinition> modules;
    // NOTE: message_types are auto-generated from CommRaT<> registry
    // NOTE: No separate connections array - connections are embedded in input_sources
};
```

### 5. Integration with CommRaT<>

```cpp
template<typename... MessageDefs>
struct CommRaT {
    // ... existing code ...
    
    // ========================================================================
    // ORCHESTRATOR INTEGRATION (NEW)
    // ========================================================================
    
    /// Create a process orchestrator for this application
    static auto create_orchestrator() {
        return ProcessOrchestrator<CommRaT>{};
    }
    
    /// Export message type registry (separate from application config)
    static std::string export_message_registry() {
        // Generate message types automatically from registry
        std::vector<MessageTypeDefinition> message_types;
        
        for_each_message_type([&]<typename T>() {
            MessageTypeDefinition msg_def{
                .name = std::string(rfl::type_name_t<T>().str()),
                .message_id = get_message_id<T>(),
                .schema = Introspection::template export_as<T, rfl::json>()
            };
            message_types.push_back(msg_def);
        });
        
        return rfl::json::write(message_types);
    }
    
    /// Write application template to JSON file
    static void write_application_template(const std::string& app_filename,
                                          const std::string& registry_filename = "") {
        // Application config (modules only)
        ApplicationDefinition app_def{
            .application = {.name = "MyApplication", .version = "1.0.0"},
            .modules = {}  // User fills in
        };
        
        std::ofstream file(app_filename);
        file << rfl::json::write(app_def);
        
        // Optionally write message registry separately (for reference)
        if (!registry_filename.empty()) {
            std::ofstream registry_file(registry_filename);
            registry_file << export_message_registry();
        }
    }
    
    /// Export module I/O metadata (for use in module binaries)
    template<typename ModuleType>
    static std::string export_module_metadata() {
        struct ModuleMetadata {
            std::string name;
            std::vector<std::string> inputs;
            std::vector<std::string> outputs;
            uint64_t period_ns;
        };
        
        ModuleMetadata metadata{
            .name = ModuleType::module_name,
            .inputs = extract_input_type_names<ModuleType>(),
            .outputs = extract_output_type_names<ModuleType>(),
            .period_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                ModuleType::default_period
            ).count()
        };
        
        return rfl::json::write(metadata);
    }
    
private:
    template<typename ModuleType>
    static std::vector<std::string> extract_input_type_names() {
        using InputSpec = typename ModuleType::InputSpecType;
        std::vector<std::string> types;
        
        if constexpr (has_input_types<InputSpec>) {
            extract_type_names_from_tuple<typename InputSpec::types>(types);
        }
        return types;
    }
    
    template<typename ModuleType>
    static std::vector<std::string> extract_output_type_names() {
        using OutputSpec = typename ModuleType::OutputSpecType;
        std::vector<std::string> types;
        
        if constexpr (has_output_types<OutputSpec>) {
            extract_type_names_from_tuple<typename OutputSpec::types>(types);
        }
        return types;
    }
    
    template<typename... Types>
    static void extract_type_names_from_tuple(std::vector<std::string>& types,
                                             std::tuple<Types...>* = nullptr) {
        (types.push_back(std::string(rfl::type_name_t<Types>().str())), ...);
    }
};
```
```

## Usage Example

**Module Binary 1** (`temperature_sensor.cpp`):
```cpp
using MyApp = commrat::CommRaT<
    commrat::Message::Data<TempData>,
    commrat::Message::Data<FilteredData>
>;

struct SensorConfig {
    std::string sensor_address;
    float calibration_offset;
};

class TemperatureSensor : public MyApp::Module<Output<TempData>, PeriodicInput> {
public:
    // Compile-time configuration (known at build time)
    static constexpr const char* module_name = "TemperatureSensor";
    static constexpr Duration default_period = Milliseconds(100);
    static constexpr std::array<SourceId, 0> default_input_sources = {};
    
    TemperatureSensor(const ModuleConfig& config, const SensorConfig& custom)
        : Module(config), custom_(custom) {}

protected:
    void process(TempData& output) override {
        output = read_i2c_sensor(custom_.sensor_address);
        output.value += custom_.calibration_offset;
    }

private:
    SensorConfig custom_;
};

int main(int argc, char** argv) {
    // Support --metadata flag for introspection
    if (argc > 1 && std::string(argv[1]) == "--metadata") {
        std::cout << MyApp::export_module_metadata<TemperatureSensor>() << std::endl;
        return 0;
    }
    
    commrat::initialize_tims();
    
    // Load runtime config (only system_id and instance_id)
    auto config = commrat::load_module_config(argv[1]);
    auto custom = rfl::json::read<SensorConfig>(config.custom).value();
    
    // Build full ModuleConfig from compile-time info + runtime IDs
    auto module_config = commrat::make_module_config<TemperatureSensor>(config.module);
    
    TemperatureSensor sensor(module_config, custom);
    sensor.start();
    
    commrat::wait_for_shutdown();
    
    sensor.stop();
    return 0;
}
```

**Module Binary 2** (`kalman_filter.cpp`):
```cpp
struct FilterConfig {
    int filter_window;
    float process_noise;
};

class KalmanFilter : public MyApp::Module<Output<FilteredData>, Input<TempData>> {
public:
    KalmanFilter(const ModuleConfig& config, const FilterConfig& custom)
        : Module(config), custom_(custom) {}

protected:
    void process(const TempData& input, FilteredData& output) override {
        output = apply_kalman_filter(input, custom_);
    }

private:
    FilterConfig custom_;
};

int main(int argc, char** argv) {
    commrat::initialize_tims();
    
    auto config = commrat::load_module_config(argv[1]);
    auto custom = rfl::json::read<FilterConfig>(config.custom).value();
    
    KalmanFilter filter(config.module, custom);
    filter.start();
    
    commrat::wait_for_shutdown();
    
    filter.stop();
    return 0;
}
```

**Orchestrator** (`main.cpp`):
```cpp
int main() {
    // Create orchestrator
    auto orchestrator = MyApp::create_orchestrator();
    
    // Load application definition (modules + connections)
    orchestrator.load_from_file("my_pipeline.json");
    
    // Launch all module processes
    orchestrator.launch_all();
    
    // Wait for user interrupt
    std::cout << "Press Ctrl+C to stop...\n";
    commrat::wait_for_shutdown();
    
    // Stop all processes
    orchestrator.stop_all();
    orchestrator.wait_all();
    
    return 0;
}
```

**Generate JSON template**:
```cpp
int main() {
    // Export application config template
    MyApp::write_application_template("app_config.json", "message_registry.json");
    
    // Generates:
    // - app_config.json: User fills in module binaries, IDs, custom config
    // - message_registry.json: Auto-generated reference (name, ID, schema for each type)
    
    // Message types are automatically known from CommRaT<> registry
    // No need to manually specify them in JSON!
}
```

## Implementation Checklist

### Phase 1: Module Config Loader (High Priority - 2-3 hours)
- [ ] Create `include/commrat/config_loader.hpp`
- [ ] Implement `RuntimeModuleConfig` structure (only system_id, instance_id)
- [ ] Implement `load_module_config()` function
- [ ] Add `make_module_config<ModuleType>()` helper
- [ ] Add `wait_for_shutdown()` with signal handling
- [ ] Test loading various config files

### Phase 2: Build-Time Metadata Extraction (High Priority - 3-4 hours)
- [ ] Create `cmake/CommRaTModule.cmake`
- [ ] Implement `commrat_add_module()` CMake function
- [ ] Add metadata extractor generation
- [ ] Test .metadata.json generation during build
- [ ] Optional: Add compile-time registry header generation
- [ ] Document CMake usage

### Phase 3: Process Handle (Medium Priority - 3-4 hours)
### Phase 3: Process Handle (Medium Priority - 3-4 hours)
- [ ] Create `include/commrat/process_handle.hpp`
- [ ] Implement platform-specific `ProcessHandle` (fork/exec on Linux)
- [ ] Add process monitoring (is_running, exit_code)
- [ ] Add graceful termination (SIGTERM then SIGKILL)
- [ ] Test process lifecycle

### Phase 4: Process Orchestrator (High Priority - 5-6 hours)
- [ ] Create `include/commrat/orchestrator.hpp`
- [ ] Implement `ProcessOrchestrator<AppType>` class
- [ ] Add validation (message types, modules, binaries exist)
- [ ] Add `load_module_metadata()` with .metadata.json priority
- [ ] Add module launching with config file generation
- [ ] Add process monitoring and restart capability
- [ ] Test multi-process coordination

### Phase 5: JSON Schema (Medium Priority - 2-3 hours)
- [ ] Define `ApplicationDefinition` with rfl structures
- [ ] Simplify schema (no message_types, no I/O types in modules)
- [ ] Add `SourceDefinition` for input_sources
- [ ] Test JSON serialization/deserialization

### Phase 6: CommRaT Integration (Medium Priority - 2-3 hours)
### Phase 6: CommRaT Integration (Medium Priority - 2-3 hours)
- [ ] Add `create_orchestrator()` to CommRaT<>
- [ ] Add `export_message_registry()` for auto-generating message types
- [ ] Add `export_module_metadata<ModuleType>()` for binary introspection
- [ ] Add `write_application_template()` for config file generation
- [ ] Add type extraction helpers for Input/Output specs
- [ ] Ensure application metadata APIs work

### Phase 7: Examples and Testing (High Priority - 4-5 hours)
- [ ] Create separate module binaries (sensor, filter, logger)
- [ ] Create CMakeLists.txt with `commrat_add_module()`
- [ ] Verify .metadata.json generation at build time
- [ ] Create example JSON configuration
- [ ] Test multi-process launch and communication
- [ ] Test validation (missing binaries, type mismatches)
- [ ] Test graceful shutdown of all processes
- [ ] Document JSON schema format

### Phase 8: Documentation (Medium Priority - 2-3 hours)
- [ ] Add section to USER_GUIDE.md on module binaries
- [ ] Document standard module main() pattern
- [ ] Document JSON configuration format
- [ ] Document orchestrator usage
- [ ] Add examples for common patterns
- [ ] Update ROADMAP.md

## Benefits

1. **Distributed by Default**: Modules run as separate processes
2. **No Factory Pattern**: Modules are just executables
3. **Type Identity**: Module type IS its output types
4. **Crash Isolation**: One module crash doesn't affect others
5. **Language Agnostic**: Could wrap non-C++ modules
6. **Easy Deployment**: Copy binaries + JSON config
7. **Development Flexibility**: Test modules individually
8. **Minimal Config**: Only runtime IDs in JSON, everything else compile-time
9. **Single Source of Truth**: Timestamps from TiMS, metadata from binary, message types from registry
10. **Zero Duplication**: Message types auto-generated via introspection, no manual JSON
11. **Build-Time Metadata**: `.metadata.json` generated during compilation (zero runtime overhead)
12. **No Process Spawning**: Orchestrator reads .metadata.json files directly, no `--metadata` flag needed
10. **Zero Duplication**: Message types auto-generated via introspection, no manual JSON

## Dependencies

- ✅ Introspection (INTROSPECTION_INTEGRATION_PLAN.md)
- ✅ Application metadata (for_each_message_type, get_message_id)
- ✅ rfl JSON support
- ⏳ Process management utilities (fork/exec, signal handling)
- ⏳ Config file loading helpers

## Open Questions

1. **How to handle Commands in JSON?**
   - Could add commands section to module definition
   - Commands need routing information

2. **Should we support hot-reloading?**
   - Stop modules, reload JSON, restart
   - Requires careful state management

3. **Should we generate C++ code from JSON?**
   - Could emit .cpp file for static compilation
   - Better performance, loses runtime flexibility

## Timeline

- **Phase 1** (Config Loader): 2-3 hours
- **Phase 2** (Build-Time Extraction): 3-4 hours
- **Phase 3** (Process Handle): 3-4 hours
- **Phase 4** (Orchestrator): 5-6 hours
- **Phase 5** (Schema): 2-3 hours
- **Phase 6** (Integration): 2-3 hours
- **Phase 7** (Examples): 4-5 hours
- **Phase 8** (Docs): 2-3 hours
- **Total**: ~23-31 hours (3-4 days of focused work)
