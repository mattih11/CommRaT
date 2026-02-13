# Phase 7.0: Per-Binary Module Main Wrapper

**Date**: February 12, 2026  
**Status**: Planning  
**Priority**: High (enables deployment pattern)

**Key Innovation**: Use rfl (reflect-cpp) for automatic CLI parsing and config loading from ModuleConfig struct - zero manual parsing code!

---

## Overview

Provide a simple main() wrapper pattern that turns any CommRaT module into a standalone executable. This enables one-process-per-module deployment (better isolation, easier debugging, standard UNIX process model).

**Current State**: Modules work fine but require manual main() boilerplate  
**Goal**: Single macro/template to create standalone module binaries  
**Magic**: rfl automatically generates CLI parsers and file loaders from ModuleConfig reflection

---

## Problem Statement

Currently, to deploy a module you need:

```cpp
// Lots of boilerplate for every module binary
int main() {
    // Parse command line arguments
    // Load configuration 
    // Instantiate module
    // Handle signals (SIGINT, SIGTERM)
    // Run module
    // Cleanup
    return 0;
}
```

**Issues:**
- Repetitive code across all module binaries
- Inconsistent signal handling
- Configuration loading duplicated
- Error handling patterns vary
- No standard shutdown behavior

**What we want:**

```cpp
// sensor_module_main.cpp
#include <commrat/module_main.hpp>
#include "sensor_module.hpp"

COMMRAT_MODULE_MAIN(SensorModule, MyApp)
```

That's it! Compiles to standalone binary: `./sensor_module`

---

## Design Requirements

### Must Have

1. **Simple API** - One macro/function call to create main()
2. **Configuration** - Support both code and JSON config
3. **Signal Handling** - Graceful shutdown on SIGINT/SIGTERM
4. **Error Reporting** - Clear error messages to stderr
5. **Zero Overhead** - No runtime cost vs manual main()

### Should Have

6. **Command Line Args** - Standard flags (--config, --help, --version)
7. **Logging Setup** - Optional stdout/stderr configuration
8. **Return Codes** - Standard exit codes (0=success, 1=error, 130=SIGINT)

### Could Have

9. **Daemon Mode** - Optional background execution
10. **PID File** - For process management
11. **Hot Reload** - Config reload on SIGHUP

---

## Proposed API

### Option A: Macro (Simple, No Template Instantiation Cost)

```cpp
// sensor_module_main.cpp
#include <commrat/module_main.hpp>
#include "sensor_module.hpp"

// Macro expands to main() function
COMMRAT_MODULE_MAIN(SensorModule, MyApp)
```

**Pros:**
- Simplest possible user code
- No template instantiation overhead
- Can customize easily (edit macro expansion)

**Cons:**
- Macros are harder to debug
- Limited customization without editing

### Option B: Template Function (Type-Safe, Customizable)

```cpp
// sensor_module_main.cpp
#include <commrat/module_main.hpp>
#include "sensor_module.hpp"

int main(int argc, char** argv) {
    return commrat::module_main<SensorModule, MyApp>(argc, argv);
}
```

**Pros:**
- Type-safe, better IDE support
- Easy to customize (add code before/after)
- Clear control flow

**Cons:**
- One extra line of code
- Template instantiation cost (compile time)

### Option C: Hybrid (Best of Both)

```cpp
// Simple case - macro
COMMRAT_MODULE_MAIN(SensorModule, MyApp)

// Custom case - template function
int main(int argc, char** argv) {
    setup_custom_logging();
    return commrat::module_main<SensorModule, MyApp>(argc, argv);
}
```

**Recommendation**: **Option C** - Provide both, macro calls template internally

---

## Implementation Plan

### Phase 7.0.1: Basic Template Function (~2 hours)

**File**: `include/commrat/module_main.hpp`

```cpp
#pragma once

#include <commrat/commrat.hpp>
#include <commrat/threading.hpp>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <atomic>

namespace commrat {

// Global flag for signal handling
inline std::atomic<bool> g_shutdown_requested{false};

// Signal handler
inline void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_shutdown_requested.store(true);
        std::cerr << "\nShutdown requested (signal " << signum << ")\n";
    }
}

/**
 * @brief Run a CommRaT module as a standalone process
 * 
 * Handles:
 * - Module instantiation
 * - Signal handling (SIGINT, SIGTERM)
 * - Graceful shutdown
 * - Error reporting
 * 
 * @tparam ModuleType Module class (must inherit from Registry::Module<...>)
 * @tparam Registry CommRaT application registry
 * @param config Module configuration
 * @return Exit code (0=success, 1=error, 130=SIGINT)
 */
template<typename ModuleType, typename Registry>
int module_main(const ModuleConfig& config) {
    try {
        // Install signal handlers
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create module instance
        std::cout << "Starting " << config.name << " (system_id=" 
                  << config.system_id << ", instance_id=" 
                  << config.instance_id << ")\n";
        
        ModuleType module(config);
        
        // Start module
        module.start();
        std::cout << config.name << " running...\n";
        
        // Wait for shutdown signal
        while (!g_shutdown_requested.load()) {
            Time::sleep(Milliseconds(100));
        }
        
        // Stop module
        std::cout << "Stopping " << config.name << "...\n";
        module.stop();
        
        std::cout << config.name << " stopped successfully\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "ERROR: Unknown exception\n";
        return 1;
    }
}

} // namespace commrat

// Macro for simple case (uses default config from CLI/file in main)
#define COMMRAT_MODULE_MAIN(ModuleType, Registry) \
    int main(int argc, char** argv) { \
        ModuleConfig config; \
        if (argc == 1) { \
            /* No args - use defaults (will fail if required fields missing) */ \
            config.name = #ModuleType; \
            config.system_id = 1; \
            config.instance_id = 1; \
        } else if (argc == 2 && !std::string(argv[1]).starts_with("--")) { \
            /* Single arg without -- is config file */ \
            std::string filename = argv[1]; \
            if (filename.ends_with(".json")) { \
                config = rfl::json::load<ModuleConfig>(filename).value(); \
            } else if (filename.ends_with(".yaml")) { \
                config = rfl::yaml::load<ModuleConfig>(filename).value(); \
            } else if (filename.ends_with(".bson")) { \
                config = rfl::bson::load<ModuleConfig>(filename).value(); \
            } else if (filename.ends_with(".toml")) { \
                config = rfl::toml::load<ModuleConfig>(filename).value(); \
            } else { \
                std::cerr << "Unknown config format: " << filename << "\n"; \
                return 1; \
            } \
        } else { \
            /* Multiple args or --flags - parse as CLI */ \
            try { \
                config = rfl::parse_args<ModuleConfig>(argc, argv).value(); \
            } catch (const std::exception& e) { \
                std::cerr << "Configuration error: " << e.what() << "\n"; \
                std::cerr << "Usage: " << argv[0] << " <config.json|yaml|bson|toml>\n"; \
                std::cerr << "   OR: " << argv[0] << " --name <name> --system-id <id> ...\n"; \
                std::cerr << "\nAvailable options:\n"; \
                std::cerr << rfl::describe<ModuleConfig>() << "\n"; \
                return 1; \
            } \
        } \
        return commrat::module_main<ModuleType, Registry>(config); \
    }
```

**Note**: The macro handles:
1. No args → use compile-time defaults
2. Single file arg → detect format by extension, load via rfl
3. CLI flags → parse via rfl::parse_args

**Testing**: Create `examples/module_main_basic.cpp`

### Phase 7.0.2: Command Line Arguments (~2 hours)

**Use rfl for automatic CLI parsing from ModuleConfig fields!**

rfl can automatically generate CLI parsers from struct reflection:

```cpp
#include <rfl.hpp>
#include <rfl/json.hpp>

// ModuleConfig is already reflectable!
ModuleConfig config = rfl::parse_args<ModuleConfig>(argc, argv).value();
```

**Automatic flags from ModuleConfig fields:**
- `--name <string>` - Module name
- `--system-id <uint32_t>` - System ID
- `--instance-id <uint32_t>` - Instance ID  
- `--period <duration>` - Period (if PeriodicInput)
- `--input-source <json>` - Input source (repeatable for multi-input)

**Special flags:**
- `--help` - Show all available flags (auto-generated from ModuleConfig)
- `--config <file>` - Load from JSON/YAML/BSON file instead

**Implementation**: rfl handles everything - no manual parsing!

```cpp
template<typename ModuleType, typename Registry>
int module_main(int argc, char** argv) {
    try {
        ModuleConfig config;
        
        // Check for config file first
        if (argc == 2 && std::string(argv[1]).ends_with(".json")) {
            config = rfl::json::load<ModuleConfig>(argv[1]).value();
        } else if (argc == 2 && std::string(argv[1]).ends_with(".yaml")) {
            config = rfl::yaml::load<ModuleConfig>(argv[1]).value();
        } else if (argc == 2 && std::string(argv[1]).ends_with(".bson")) {
            config = rfl::bson::load<ModuleConfig>(argv[1]).value();
        } else {
            // Parse command line arguments
            config = rfl::parse_args<ModuleConfig>(argc, argv).value();
        }
        
        return module_main<ModuleType, Registry>(config);
        
    } catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        std::cerr << "Usage: " << argv[0] << " <config.json|config.yaml|config.bson>\n";
        std::cerr << "   OR: " << argv[0] << " --name <name> --system-id <id> ...\n";
        std::cerr << "\nAvailable options:\n";
        std::cerr << rfl::describe<ModuleConfig>() << "\n";
        return 1;
    }
}
```

**Benefits:**
- Zero manual CLI parsing code
- Automatic `--help` generation from struct fields
- Type-safe (rfl validates types automatically)
- Consistent with file loading (same struct, different format)

### Phase 7.0.3: Configuration Loading (~1 hour)

**rfl handles all formats automatically!**

```cpp
// JSON
auto config = rfl::json::load<ModuleConfig>("config.json").value();

// YAML  
auto config = rfl::yaml::load<ModuleConfig>("config.yaml").value();

// BSON (binary)
auto config = rfl::bson::load<ModuleConfig>("config.bson").value();

// TOML
auto config = rfl::toml::load<ModuleConfig>("config.toml").value();
```

**Example JSON config:**
```json
{
  "name": "SensorModule",
  "system_id": 10,
  "instance_id": 1,
  "period": 100000000,
  "input_sources": [
    {"system_id": 11, "instance_id": 1}
  ]
}
```

**Example YAML config:**
```yaml
name: SensorModule
system_id: 10
instance_id: 1
period: 100000000  # nanoseconds
input_sources:
  - system_id: 11
    instance_id: 1
```

**No extra code needed** - rfl deserializes directly to ModuleConfig!

### Phase 7.0.4: Examples and Documentation (~1 hour)

Create three examples:

**1. `examples/module_main_basic.cpp`** - Minimal usage:
```cpp
class SimpleModule : public MyApp::Module<Output<Data>, PeriodicInput> {
    Data process() override { return Data{...}; }
};

COMMRAT_MODULE_MAIN(SimpleModule, MyApp);
```

Run: `./simple_module --system-id 10 --instance-id 1 --period 100000000`

**2. `examples/module_main_with_config.cpp`** - File-based config:
```cpp
class ConsumerModule : public MyApp::Module<Output<Filtered>, Input<Raw>> {
    Filtered process(const Raw& input) override { return filter(input); }
};

COMMRAT_MODULE_MAIN(ConsumerModule, MyApp);
```

Run: `./consumer_module config.json`

**3. `examples/module_main_multiformat.cpp`** - Multiple formats:
```cpp
class FusionModule : public MyApp::Module<Output<Fused>, Inputs<IMU, GPS>> {
    Fused process(const IMU& imu, const GPS& gps) override { return fuse(imu, gps); }
};

COMMRAT_MODULE_MAIN(FusionModule, MyApp);
```

Run any of:
- `./fusion_module config.json`
- `./fusion_module config.yaml`
- `./fusion_module config.bson`
- `./fusion_module --system-id 20 --instance-id 1 --period 10000000 --input-source '{"system_id":10,"instance_id":1}' --input-source '{"system_id":11,"instance_id":1}'`

**Documentation Updates:**
- `docs/USER_GUIDE.md` - Section 9: Deployment Patterns
- `docs/API_REFERENCE.md` - Section 11: Module Main Wrapper
- `docs/README.md` - Update with per-binary deployment pattern

## rfl Integration Details

**Why rfl is perfect for this:**

1. **Zero boilerplate** - ModuleConfig is already reflectable (plain struct)
2. **Type-safe** - Validates all field types automatically
3. **Auto-generated help** - `rfl::describe<ModuleConfig>()` shows all fields
4. **Multiple formats** - JSON, YAML, BSON, TOML with same code
5. **CLI + file unified** - Same struct, different loading mechanism

**Example: Automatic CLI help from struct:**
```cpp
$ ./my_module --help

Available options:
  --name <string>              Module name (required)
  --system-id <uint32_t>       System ID (required)
  --instance-id <uint32_t>     Instance ID (required)
  --period <uint64_t>          Period in nanoseconds (optional)
  --input-sources <json>       Input sources (optional, repeatable)
  --sync-tolerance <uint64_t>  Sync tolerance in ns (optional)
```

All generated from ModuleConfig struct - no manual help strings!

**ModuleConfig already reflectable:**
```cpp
struct ModuleConfig {
    std::string name = "";
    uint32_t system_id = 0;
    uint32_t instance_id = 0;
    uint64_t period = 0;
    std::vector<InputSource> input_sources{};
    uint64_t sync_tolerance = 50'000'000;
};
```

rfl automatically handles:
- Field names → CLI flags (`system_id` → `--system-id`)
- Type validation (uint32_t range checks)
- Optional vs required fields
- Nested structs (InputSource)
- Containers (std::vector<InputSource>)

---

## File Structure

```
include/commrat/
└── module_main.hpp              # NEW - Template functions + COMMRAT_MODULE_MAIN macro
                                 #       Includes rfl CLI parsing + file loading

examples/
├── module_main_basic.cpp        # NEW - Minimal usage (CLI args)
├── module_main_with_config.cpp  # NEW - File-based config
└── module_main_multiformat.cpp  # NEW - Multiple config formats

docs/
└── work/
    └── PHASE_7.0_MODULE_MAIN_WRAPPER.md  # This file
```

**Note**: No separate `module_config_json.hpp` needed - rfl handles everything!

---

## Implementation Notes

### What Already Exists (Don't Duplicate!)

✅ **TiMS Initialization**: Handled by Module constructor  
✅ **Threading**: Module base class manages threads  
✅ **Mailbox Creation**: Automatic in Module constructor  
✅ **Subscription Protocol**: Handled by Module::start()  
✅ **Message Dispatch**: Handled by Module loops

### What We're Adding (Just the Wrapper!)

🆕 **Process Lifecycle**: main() → instantiate → start → wait → stop  
🆕 **Signal Handling**: SIGINT/SIGTERM → graceful shutdown  
🆕 **Error Handling**: try/catch around module lifecycle  
🆕 **Config Loading**: Optional JSON file → ModuleConfig  
🆕 **Command Line**: Standard flags (--help, --config, etc.)

---

## Usage Examples

### Example 1: Basic Standalone Module

```cpp
// sensor_module.hpp
class SensorModule : public MyApp::Module<Output<SensorData>, PeriodicInput> {
protected:
    void process(SensorData& output) override {
        output.value = read_sensor();
    }
};

// sensor_module_main.cpp
#include <commrat/module_main.hpp>
#include "sensor_module.hpp"

COMMRAT_MODULE_MAIN(SensorModule, MyApp)
```

**Compile**: `g++ -o sensor_module sensor_module_main.cpp -lcommrat -ltims`  
**Run**: `./sensor_module`

### Example 2: With JSON Config

```cpp
// filter_module_main.cpp
#include <commrat/module_main.hpp>
#include <commrat/module_config_json.hpp>
#include "filter_module.hpp"

int main(int argc, char** argv) {
    auto config = commrat::load_config_from_json("filter_config.json");
    return commrat::module_main<FilterModule, MyApp>(config);
}
```

**Run**: `./filter_module --config filter_config.json`

### Example 3: Custom Setup

```cpp
int main(int argc, char** argv) {
    // Custom initialization
    setup_logging();
    calibrate_sensors();
    
    ModuleConfig config{
        .name = "SensorModule",
        .system_id = 10,
        .instance_id = 1,
        .period = Milliseconds(100)
    };
    
    // Use template function
    return commrat::module_main<SensorModule, MyApp>(config);
}
```

---

## Benefits

1. **Reduced Boilerplate**: ~50 lines → 2 lines per module binary
2. **Consistent Behavior**: All modules handle signals the same way
3. **Better Deployment**: One process per module (standard UNIX model)
4. **Easier Debugging**: Modules run independently, easier to isolate issues
5. **Process Isolation**: Module crashes don't affect others
6. **Resource Control**: Per-process CPU affinity, nice levels, cgroups
7. **Standard Tools**: Works with systemd, supervisord, Docker, etc.

---

## Testing Strategy

1. **Unit Tests**: Test signal handling, error paths
2. **Integration Tests**: Run standalone modules, verify communication
3. **Deployment Test**: Multi-module system with separate processes
4. **Signal Tests**: Verify graceful shutdown (SIGINT, SIGTERM)
5. **Error Tests**: Verify proper error reporting and exit codes

---

## Estimated Effort

- **Phase 7.0.1** (Basic Template): 2 hours
- **Phase 7.0.2** (CLI via rfl::parse_args): 1 hour
- **Phase 7.0.3** (File loading via rfl): 0 hours (already done in 7.0.2)
- **Phase 7.0.4** (Examples): 1 hour
- **Total**: ~4 hours (half day)

**Time savings**: rfl eliminates manual CLI parsing and format-specific loading!

---

## Success Criteria

- ✅ Single macro creates standalone module binary
- ✅ Graceful shutdown on SIGINT/SIGTERM
- ✅ Clear error messages
- ✅ Config loading from files (JSON, YAML, BSON, TOML)
- ✅ Command line argument parsing with auto-generated help
- ✅ Type-safe configuration validation
- ✅ Examples compile and run
- ✅ Both CLI and file-based config work seamlessly
- ✅ Documentation updated

---

## Next Steps After 7.0

Once per-binary deployment works:

1. **Phase 7.1**: Optional secondary inputs (getData can fail)
2. **Phase 7.2**: Input buffering strategies
3. **Phase 7.3**: ROS 2 adapter
4. **Phase 7.4**: Performance profiling

---

## Open Questions

1. **Config Override Priority**: CLI args > JSON file > code defaults? (rfl handles this naturally)
2. **Daemon Mode**: Should we support backgrounding? (NO - use systemd)
3. **PID Files**: Needed? (NO - systemd handles this)
4. **Multiple Modules**: One binary with multiple modules? (NO - defeats isolation purpose)
5. **Help Format**: Use rfl::describe<ModuleConfig>() or custom formatter? (Use rfl - automatic)

## Related Files

**To Create**:
- `include/commrat/module_main.hpp`
- `examples/module_main_basic.cpp`
- `examples/module_main_with_config.cpp`
- `examples/module_main_multiformat.cpp`

**To Reference**:
- `include/commrat/registry_module.hpp` - Module base class
- `include/commrat/module/configuration/module_config.hpp` - Config structure
- `include/commrat/threading.hpp` - Time::sleep, signal abstractions
- `SeRTial/include/rfl.hpp` - Reflection, CLI parsing, file loading

**To Update**:
- `docs/USER_GUIDE.md` - Add deployment section
- `docs/API_REFERENCE.md` - Add module_main documentation
- `CMakeLists.txt` - Add new examples

---

## Summary

**Problem**: Users write repetitive main() boilerplate for each module binary

**Solution**: COMMRAT_MODULE_MAIN macro + module_main<> template using rfl for config

**Key Benefits:**
- **2-line deployment**: Module class definition + COMMRAT_MODULE_MAIN
- **Automatic CLI parsing**: rfl::parse_args from ModuleConfig struct
- **Multiple config formats**: JSON, YAML, BSON, TOML with zero extra code
- **Auto-generated help**: `--help` from struct reflection (rfl::describe)
- **Type-safe config**: Validation automatic via rfl
- **Standard signals**: SIGINT/SIGTERM graceful shutdown
- **Process isolation**: One process per module (UNIX model)

**Estimated Effort**: ~4 hours total (down from 7h with rfl!)
- Phase 7.0.1: Basic template with signal handling (2h)
- Phase 7.0.2: CLI parsing via rfl::parse_args (1h)
- Phase 7.0.3: File loading (0h - already done via rfl)
- Phase 7.0.4: Examples and documentation (1h)

**rfl Magic Eliminates:**
- Manual CLI argument parsing
- Manual help text generation
- Format-specific deserialization code
- Type validation logic
- Config struct tracking

**Usage Example:**
```cpp
// Define your module
class MyModule : public MyApp::Module<Output<Data>, PeriodicInput> {
    Data process() override { return Data{...}; }
};

// Deploy in 2 lines!
COMMRAT_MODULE_MAIN(MyModule, MyApp);
```

**Run with either:**
- `./my_module --system-id 10 --instance-id 1 --period 100000000`
- `./my_module config.json`
- `./my_module config.yaml`

No manual config parsing needed - rfl handles everything!
