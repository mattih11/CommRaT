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
// Message Registry (System messages included automatically)
// ============================================================================

using ExampleRegistry = commrat::CombinedRegistry<
    commrat::MessageDefinition<StatusData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<CounterData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<TemperatureData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<PoseData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>
>;

// ============================================================================
// Type Aliases for Clean Interface
// ============================================================================

template<typename OutputDataT, typename InputModeT, typename... CommandTypes>
using Module = commrat::Module<ExampleRegistry, OutputDataT, InputModeT, CommandTypes...>;

using Mailbox = commrat::RegistryMailbox<ExampleRegistry>;

// Re-export for convenience
using commrat::PeriodicInput;
using commrat::LoopInput;
using commrat::ContinuousInput;
using commrat::ModuleConfig;
using commrat::MailboxConfig;

} // namespace example_messages
