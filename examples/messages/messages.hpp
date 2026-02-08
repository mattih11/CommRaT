#pragma once

#include "user_messages.hpp"

/**
 * @file messages.hpp
 * @brief Convenient namespace aliases for clean module code
 * 
 * Usage:
 * @code
 * #include "messages/messages.hpp"
 * using namespace user_app;
 * 
 * class SensorModule : public Module<Output<TempData>, PeriodicInput> {
 *     TempData process() override { return read_sensor(); }
 * };
 * @endcode
 */

namespace user_app {

// Module template alias (cleaner than writing App::Module everywhere)
template<typename OutputSpec, typename InputSpec, typename... CommandTypes>
using Module = App::Module<OutputSpec, InputSpec, CommandTypes...>;

// Mailbox template alias
template<typename PayloadT>
using Mailbox = App::Mailbox<PayloadT>;

// Re-export I/O specifications
using commrat::Output;
using commrat::Outputs;
using commrat::Input;
using commrat::Inputs;
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ModuleConfig;

} // namespace user_app
