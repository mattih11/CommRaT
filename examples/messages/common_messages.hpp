#pragma once

#include "commrat/commrat.hpp"

/**
 * @file common_messages.hpp
 * @brief Shared message definitions for examples
 */

namespace example_messages {

// ============================================================================
// Payload Definitions
// ============================================================================

struct StatusData {
    uint64_t timestamp{0};
    uint32_t counter{0};
    float cpu_load{0.0f};
    float memory_usage{0.0f};
};

struct CounterData {
    uint64_t value{0};
};

struct TemperatureData {
    uint64_t timestamp{0};
    uint32_t sensor_id{0};
    float temperature_c{0.0f};
    float confidence{0.0f};
};

struct PoseData {
    uint64_t timestamp{0};
    float x{0.0f};
    float y{0.0f};
    float theta{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float omega{0.0f};
};

// ============================================================================
// Message Registry with Automatic Module/Mailbox Aliases
// ============================================================================

using ExampleApp = commrat::Registry<
    commrat::Message::Data<StatusData>,
    commrat::Message::Data<CounterData>,
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<PoseData>
>;

// Module and Mailbox are automatically available!
// Just use: ExampleApp::Module<OutputData, InputMode>
//       or: ExampleApp::Mailbox

// Convenient re-exports in this namespace
template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
using Module = ExampleApp::Module<OutputDataT, InputModeT, CommandTypes...>;

using Mailbox = ExampleApp::Mailbox;

// Re-export for convenience
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ContinuousInput;
using commrat::ModuleConfig;
using commrat::MailboxConfig;

} // namespace example_messages
