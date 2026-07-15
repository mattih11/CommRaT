#pragma once
/**
 * @file module_description.hpp
 * @brief Serialisable structs describing CommRaT module configurations.
 *
 * Load from YAML/JSON/TOML via reflectcpp:
 *   auto desc = rfl::yaml::read<AppDescription>(text).value();
 *
 * The structs mirror ModuleConfig but use string type names instead of
 * C++ types so they can be expressed in human-editable config files.
 * The Launcher<App> translates them to ModuleConfig at startup using
 * App::get_address().
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace commrat {

/**
 * Describes one output of a module.
 * type_name must match a registered payload type (App::get_type_name<T>()).
 */
struct OutputDescription {
    std::string type_name;
    uint8_t     system_id{0};
    uint8_t     instance_id{1};
};

/**
 * Describes one input of a module.
 * type_name must match a registered payload type.
 * synced = true → SyncedInput (pull-based, secondary).
 * synced = false (default) → Input (primary, continuous).
 */
struct InputDescription {
    std::string type_name;
    uint8_t     source_system_id{0};
    uint8_t     source_instance_id{1};
    bool        synced{false};
};

/**
 * Describes one module instance.
 * module_class must be registered with Launcher::register_module<T>(name).
 */
struct ModuleDescription {
    std::string                    name;
    std::string                    module_class;
    std::vector<OutputDescription> outputs;
    std::vector<InputDescription>  inputs;
    std::optional<uint32_t>        period_ms;  // timer-driven modules only
};

/**
 * Top-level application description.
 * Maps to one CommRaT application (one CommRaT<> instantiation).
 */
struct AppDescription {
    std::string                    app_name;
    std::vector<ModuleDescription> modules;
};

} // namespace commrat
