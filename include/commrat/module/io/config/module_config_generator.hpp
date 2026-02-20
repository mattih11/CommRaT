#pragma once

/**
 * @file module_config_generator.hpp
 * @brief Compile-time generation of module config structures based on I/O specs
 * 
 * Uses reflect-cpp with std::optional fields for clean, type-specific configs.
 * No complex template selection - just optional fields that appear when needed.
 * 
 * Example:
 *   Module<Output<SensorData>>
 *   → Config with: name, period, system_id, instance_id (output fields present)
 * 
 *   Module<Output<SensorData>, Input<CommandData>>
 *   → Adds: input{...} (optional field becomes present)
 * 
 *   Module<Outputs<IMU, GPS>, Inputs<Camera, Lidar>>
 *   → Has: outputs[2], primary_input, synced_inputs[1], sync_tolerance
 */

#include "commrat/module/io/config/input_config.hpp"
#include "commrat/module/io/config/output_config.hpp"
#include "commrat/module/io/io_spec.hpp"
#include "commrat/module/helpers/type_name.hpp"
#include <rfl.hpp>
#include <string>
#include <chrono>
#include <optional>
#include <array>

namespace commrat {

// ============================================================================
// Universal Module Config (all combinations)
// ============================================================================

/**
 * @brief Universal module configuration with optional I/O fields
 * 
 * Uses std::optional for I/O fields - they only appear when needed:
 * - Single output: system_id, instance_id present
 * - Multiple outputs: outputs array present
 * - Single input: input present
 * - Multiple inputs: primary_input, synced_inputs, sync_tolerance present
 * - Command input: command_input present
 * 
 * The Module template specialization sets which fields are populated.
 */
struct UniversalModuleConfig {
    // ========================================================================
    // Common fields (always present)
    // ========================================================================
    
    std::string name;
    std::optional<std::chrono::milliseconds> period{std::chrono::milliseconds{100}};
    int priority{10};
    bool realtime{false};
    size_t max_subscribers{8};
    uint32_t cmd_message_slots{10};
    uint32_t data_message_slots{50};
    
    // ========================================================================
    // Output fields (optional - present based on OutputSpec)
    // ========================================================================
    
    // Single output: Output<T>
    std::optional<uint8_t> system_id;
    std::optional<uint8_t> instance_id;
    
    // Multiple outputs: Outputs<Ts...>
    // Size determined at instantiation
    std::optional<std::vector<BufferedOutputConfig>> outputs;
    
    // ========================================================================
    // Input fields (optional - present based on InputSpec)
    // ========================================================================
    
    // Single continuous input: Input<T>
    std::optional<ContinuousInputConfig> input;
    
    // Multiple inputs: Inputs<Ts...>
    std::optional<ContinuousInputConfig> primary_input;
    std::optional<std::vector<SyncedInputConfig>> synced_inputs;
    std::optional<std::chrono::milliseconds> sync_tolerance;
    
    // Command-only input: CmdOnlyInput<T>
    std::optional<CmdInputConfig> command_input;
};

// ============================================================================
// Config Type Alias (use UniversalModuleConfig for all cases)
// ============================================================================

/**
 * @brief Module configuration type - same for all I/O combinations
 * 
 * The Module template will populate the appropriate optional fields.
 * Unused fields remain std::nullopt and won't appear in JSON.
 * 
 * @tparam OutputSpec Output specification (determines which output fields are set)
 * @tparam InputSpec Input specification (determines which input fields are set)
 */
template<typename OutputSpec, typename InputSpec = void>
using ModuleConfig = UniversalModuleConfig;

} // namespace commrat
