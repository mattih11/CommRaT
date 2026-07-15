# CommRaT Launcher + Module Description System

## What already exists (do not duplicate)

| Existing | Location | What it provides |
|---|---|---|
| `TypeName<T>::value` | `module/helpers/type_name.hpp` | `constexpr fixed_string` name of T (via reflectcpp) |
| `Registry::get_message_id<T>()` | `messaging/message_registry.hpp` | compile-time `uint32_t` message ID for payload T |
| `Registry::PayloadTypes` | same | `std::tuple<T1, T2, ...>` of all user payload types |
| `Registry::visit(id, buf, visitor)` | same | runtime dispatch by message ID |
| `calculate_base_address<T, Tuple, Reg>(sys, inst)` | `module/helpers/address_helpers.hpp` | compile-time mailbox address for (T, system_id, instance_id) |
| `IntrospectionHelper<App>` | `introspection/introspection_helper.hpp` | export all message schemas to JSON/YAML via reflectcpp |
| `MessageSchema<T, Registry>` | `introspection/message_schema.hpp` | per-type schema struct (CommRaT metadata + SeRTial layout) |

## What is missing (things to build)

### 1. String-to-type address resolution (`App::GetAddress`)

Currently `calculate_base_address<T>()` requires T at compile time.
We need a runtime path: given `"TemperatureData"` + `system_id` + `instance_id` → `uint32_t` mailbox address.

**Design**: add to `CommRaT<>`:

```cpp
// Compile-time (existing, just surface it cleanly):
static constexpr uint32_t get_address<T>(uint8_t sys, uint8_t inst);

// Runtime (new):
static std::optional<uint32_t> get_address(std::string_view type_name,
                                            uint8_t sys, uint8_t inst);
```

Implementation: iterate `PayloadTypes` at runtime, compare `TypeName<T>::value` to `type_name`,
call `calculate_base_address` for the matching T. One linear scan through registered types —
no map needed (compile-time types → no heap allocation required at startup).

### 2. Module description struct (`ModuleDescription`)

A plain reflectcpp-serializable struct. Loaded from YAML/JSON.
Maps 1:1 to `ModuleConfig` but uses string type names instead of C++ types.

```cpp
// include/commrat/launcher/module_description.hpp
namespace commrat {

struct OutputDescription {
    std::string  type_name;     // "TemperatureData"
    uint8_t      system_id;
    uint8_t      instance_id;
};

struct InputDescription {
    std::string  type_name;     // "IMUData"
    uint8_t      source_system_id;
    uint8_t      source_instance_id;
    bool         synced = false; // SyncedInput if true
};

struct ModuleDescription {
    std::string                   name;
    std::string                   module_class;  // "IMUModule", "SensorFusion", etc.
    std::vector<OutputDescription> outputs;
    std::vector<InputDescription>  inputs;
    std::optional<uint32_t>        period_ms;   // for timer-driven modules
};

struct AppDescription {
    std::string                     app_name;
    std::vector<ModuleDescription>  modules;
};

} // namespace commrat
```

### 3. Module factory registration

User registers their module classes by name at startup (before `Launcher::start()`):

```cpp
// User code (requires #include <commrat/launcher/launcher.hpp>):
MyApp::Launcher launcher;
launcher.register_module<IMUModule>("IMUModule");
launcher.register_module<SensorFusionModule>("SensorFusion");

launcher.start("config.yml");  // reads AppDescription, creates + starts modules
```

Factory stores `std::function<std::unique_ptr<ModuleBase>(ModuleConfig)>` per name.

### 4. `Launcher<App>`

```cpp
// include/commrat/launcher/launcher.hpp
template<typename App>
class Launcher {
public:
    // Register a user module class by name.
    // Factory: (ModuleConfig) -> unique_ptr<ModuleBase>
    template<typename ModuleT>
    void register_module(std::string name);

    // Load AppDescription from file (any rfl-supported format: .json, .yml, .toml)
    // Detect format from extension, or pass explicitly.
    void start(const std::string& description_file);
    void stop();

    // Convenience: main() that reads description_file from argv[1]
    static int run(int argc, char** argv,
                   std::function<void(Launcher&)> register_fn);
private:
    // Translate ModuleDescription → ModuleConfig using App's address resolution
    ModuleConfig to_config(const ModuleDescription& desc);

    std::unordered_map<std::string,
        std::function<std::unique_ptr<ModuleBase>(ModuleConfig)>> factories_;
    std::vector<std::unique_ptr<ModuleBase>> instances_;
};
```

Usage:

```cpp
// myapp_launcher.cpp
// commrat.hpp forward-declares Launcher; include this to use it:
#include <commrat/launcher/launcher.hpp>

int main(int argc, char** argv) {
    return MyApp::Launcher::run(argc, argv, [](auto& launcher) {
        launcher.register_module<IMUModule>("IMUModule");
        launcher.register_module<SensorFusionModule>("SensorFusion");
    });
}
```

```yaml
# myapp.yml
app_name: MySensorSystem
modules:
  - name: IMU_1
    module_class: IMUModule
    outputs:
      - type_name: IMUData
        system_id: 10
        instance_id: 1
    period_ms: 10

  - name: Fusion_1
    module_class: SensorFusion
    outputs:
      - type_name: FusedData
        system_id: 100
        instance_id: 1
    inputs:
      - type_name: IMUData
        source_system_id: 10
        source_instance_id: 1
      - type_name: GPSData
        source_system_id: 20
        source_instance_id: 1
        synced: true
```

### 5. RaTGUI integration

RaTGUI reads the same `AppDescription` YAML. It calls `App::get_address(type_name, sys, inst)` to
build `ModuleConfig` for each bridge module — no need to hardcode types in `main.cpp`.

```cpp
// ratgui/src/main.cpp (simplified)
AppDescription desc = rfl::yaml::read<AppDescription>(read_file(argv[1])).value();
WebServer server(web_port);

// For each output in the description, create a bridge module
App::for_each_output(desc, [&](const OutputDescription& out) {
    // Runtime dispatch: create BridgeModule for the correct type
    App::Registry::visit_type(out.type_name, [&]<typename T>() {
        auto store = std::make_shared<MessageStore<T>>();
        auto mod = std::make_unique<BridgeModule<App, T>>(
            make_input_config(out), server, *store);
        mod->start();
        // ...
    });
});
```

## Implementation Order

1. `App::get_address(string_view, sys, inst)` — unblocks RaTGUI runtime wiring
2. `ModuleDescription` + reflectcpp YAML/JSON load — enables config-driven launch
3. `Launcher<App>` — ties together factory + config loading
4. RaTGUI uses `AppDescription` instead of hardcoded types in `main.cpp`

## Files to create

```
include/commrat/
  launcher/
    module_description.hpp   # AppDescription, ModuleDescription (rfl-serializable)
    launcher.hpp              # Launcher<App> template
src/
  (launcher is header-only — no .cpp needed)
```

`module_description.hpp` and `launcher.hpp` are pure headers (templates + rfl).
No new compiled sources required.

## What NOT to add

- Do NOT add a runtime type registry or type-erasure layer beyond what's needed for the factory.
  The compile-time registry already provides all type safety; the launcher only needs string→factory lookup.
- Do NOT store `type_name` in the wire messages or `ModuleConfig`. String names are only for the
  config/launcher layer; the runtime uses numeric IDs.
- Do NOT duplicate `TypeName<T>` — reuse `commrat::TypeName<T>::value` from `type_name.hpp`.
