#pragma once
/**
 * @file module_description.hpp
 * @brief Serialisable structs describing CommRaT module configurations.
 *
 * All structs are rfl-reflectable aggregates and can be loaded from any
 * format supported by reflectcpp:
 *
 *   auto desc = rfl::json::read<AppDescription>(text).value();
 *   auto desc = rfl::yaml::read<AppDescription>(text).value();
 *   auto desc = rfl::toml::read<AppDescription>(text).value();
 *
 * The structs mirror ModuleConfig but use plain data types instead of
 * C++ template arguments so they can be expressed in human-editable config
 * files.  The Launcher<App> translates them to ModuleConfig at startup using
 * App::get_address().
 */

#include <rfl.hpp>
#include <rfl/Generic.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace commrat {

/**
 * Describes one output of a module.
 */
struct OutputDescription {
    uint8_t system_id{0};
    uint8_t instance_id{1};
};

/**
 * Describes one input of a module.
 * synced = true → SyncedInput (pull-based, secondary).
 * synced = false (default) → Input (primary, continuous).
 */
struct InputDescription {
    uint8_t source_system_id{0};
    uint8_t source_instance_id{1};
    rfl::DefaultVal<bool> synced = false;  // for backward compat with flat inputs[]; absent = false
};

/**
 * Describes one module instance.
 * module_class must be registered with Launcher::register_module<T>(name).
 */
struct ModuleDescription {
    std::string                    name;
    std::string                    module_class;
    std::vector<OutputDescription> outputs;
    std::vector<InputDescription>  inputs;                              // continuous inputs; synced: bool kept for backward compat
    std::optional<std::vector<InputDescription>>  synced_inputs;        // absent = no synced inputs
    std::optional<uint32_t>        period_ms;      // timer-driven modules only
    std::optional<rfl::Generic>    params;         // module-specific params, forwarded to ModuleConfig
};

/**
 * Describes a non-CommRaT companion process (e.g. a web dashboard) to be
 * started alongside the CommRaT modules.  The binary is looked up in the
 * same descriptor directories as module binaries.
 *
 * Example (AppDescription JSON):
 *   "companions": [{"name": "ratgui", "binary": "ratgui", "args": ["--port", "8080"]}]
 */
struct CompanionDescription {
    std::string              name;    ///< Human-readable label
    std::string              binary;  ///< Binary name (looked up in descriptor dirs) or absolute path
    std::vector<std::string> args;    ///< Extra command-line arguments
};

/**
 * Top-level application description.
 * Maps to one CommRaT application (one CommRaT<> instantiation).
 */
struct AppDescription {
    std::string                       app_name;
    std::vector<ModuleDescription>    modules;
    /// Optional: extra directories to scan for *.module.json and companion binaries.
    std::optional<std::vector<std::string>> descriptor_dirs;
    /// Optional: non-CommRaT companion processes started after all modules.
    std::optional<std::vector<CompanionDescription>> companions;
};

} // namespace commrat
