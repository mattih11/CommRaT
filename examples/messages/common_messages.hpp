#pragma once

#include "commrat/commrat.hpp"

/**
 * @file common_messages.hpp
 * @brief Shared message definitions for examples
 */

namespace example_messages {

// ============================================================================
// Payload Definitions (Plain POD Structs)
// ============================================================================

struct StatusData {
    uint32_t counter{0};
    float cpu_load{0.0f};
    float memory_usage{0.0f};
};

struct CounterData {
    uint64_t value{0};
};

struct TemperatureData {
    uint32_t sensor_id{0};
    float temperature_c{0.0f};
    float confidence{0.0f};
};

struct PoseData {
    float x{0.0f};
    float y{0.0f};
    float theta{0.0f};
    float vx{0.0f};
    float vy{0.0f};
    float omega{0.0f};
};

// ============================================================================
// Application Definition
// ============================================================================

// Define your CommRaT application with all message types
using ExampleApp = commrat::CommRaT<
    commrat::Message::Data<StatusData>,
    commrat::Message::Data<CounterData>,
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<PoseData>
>;

// ExampleApp provides:
//   ExampleApp::Module<OutputSpec, InputSpec, ...Commands>
//   ExampleApp::Mailbox<T>
//   ExampleApp::serialize(msg) / deserialize<T>(data)
//   ExampleApp::get_message_id<T>()

// Convenient template aliases for this namespace
template<typename OutputSpec, typename InputSpec, typename... CommandTypes>
using Module = ExampleApp::Module<OutputSpec, InputSpec, CommandTypes...>;

template<typename PayloadT>
using Mailbox = ExampleApp::Mailbox<PayloadT>;

// Re-export I/O specifications
using commrat::Output;
using commrat::Outputs;
using commrat::Input;
using commrat::Inputs;
using commrat::NoOutput;
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ModuleConfig;
using commrat::MailboxConfig;

} // namespace example_messages

