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
// Convenient Re-exports - No Manual Aliasing Needed!
// ============================================================================

// Module and Mailbox come directly from App (defined in user_messages.hpp)
// They're already configured with your registry!
template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
using Module = App::Module<OutputDataT, InputModeT, CommandTypes...>;

using Mailbox = App::Mailbox;

// Re-export input mode types for convenience
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ContinuousInput;

// Re-export module config
using commrat::ModuleConfig;
using commrat::MailboxConfig;

} // namespace user_app
