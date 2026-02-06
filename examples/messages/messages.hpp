#pragma once

#include "user_messages.hpp"
#include <commrat/commrat.hpp>

/**
 * @file messages.hpp
 * @brief Application-specific type aliases - include this in your modules!
 * 
 * This file provides clean Module and Mailbox aliases configured with your
 * application's message registry. System messages are automatically included.
 * 
 * Usage in your module code:
 * @code
 * #include "messages/messages.hpp"
 * 
 * class SensorModule : public Module<TempData, PeriodicInput> {
 *     TempData process() override { return read_sensor(); }
 * };
 * @endcode
 */

namespace user_app {

// ============================================================================
// Module Template (Automatically Configured with AppMessageRegistry)
// ============================================================================

/**
 * @brief Application Module template with registry pre-configured
 * 
 * Users just specify OutputData and InputMode - registry is automatic!
 * System messages (subscription protocol) are automatically included.
 * Optionally specify CommandTypes for command handling.
 * 
 * @tparam OutputDataT The payload type this module produces
 * @tparam InputModeT Input mode: PeriodicInput, LoopInput, or ContinuousInput<T>
 * @tparam CommandTypes Optional variadic command payload types this module handles
 * 
 * Example:
 * @code
 * // Simple module without commands
 * class SensorModule : public Module<TemperatureData, PeriodicInput> {
 * protected:
 *     TemperatureData process() override {
 *         return TemperatureData{.temperature_celsius = 25.0f};
 *     }
 * };
 * 
 * // Module with command handling
 * class ActuatorModule : public Module<StatusData, LoopInput, ResetCmd, CalibrateCmd> {
 * protected:
 *     StatusData process() override { return get_status(); }
 *     
 *     void on_command(const ResetCmd& cmd) override {
 *         // Handle reset
 *     }
 *     
 *     void on_command(const CalibrateCmd& cmd) override {
 *         // Handle calibration
 *     }
 * };
 * @endcode
 */
template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
using Module = commrat::Module<AppMessageRegistry, OutputDataT, InputModeT, CommandTypes...>;

// ============================================================================
// Mailbox Template (Automatically Configured with AppMessageRegistry)
// ============================================================================

/**
 * @brief Application Mailbox with registry pre-configured
 * 
 * System messages automatically included.
 * 
 * Example:
 * @code
 * Mailbox mbx(config);
 * mbx.start();
 * 
 * TemperatureData temp{.temperature_celsius = 25.5f};
 * mbx.send(temp, dest_mailbox_id);
 * 
 * auto result = mbx.receive<TemperatureData>();
 * @endcode
 */
using Mailbox = commrat::RegistryMailbox<AppMessageRegistry>;

// Re-export input mode types for convenience
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ContinuousInput;

// Re-export module config
using commrat::ModuleConfig;
using commrat::MailboxConfig;

} // namespace user_app
