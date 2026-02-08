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
 * class SensorModule : public App::Module<Output<TempData>, PeriodicInput> {
 *     explicit SensorModule(const ModuleConfig& config) : App::Module<Output<TempData>, PeriodicInput>(config) {}
 *     TempData process() override { return read_sensor(); }
 * };
 * @endcode
 */

namespace user_app {

// Re-export I/O specifications
using commrat::Output;
using commrat::Outputs;
using commrat::Input;
using commrat::Inputs;
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ModuleConfig;

} // namespace user_app
