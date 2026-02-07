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
// Convenient Aliases - Direct Access to App Components
// ============================================================================

// Module and Mailbox aliases for cleaner syntax in this namespace
template<typename OutputSpec, typename InputSpec, typename... CommandTypes>
using Module = App::Module<OutputSpec, InputSpec, CommandTypes...>;

template<typename PayloadT>
using Mailbox = App::Mailbox<PayloadT>;

// I/O specifications
using commrat::Output;
using commrat::Input;
using commrat::PeriodicInput;
using commrat::LoopInput;

// Config types
using commrat::ModuleConfig;

} // namespace user_app
