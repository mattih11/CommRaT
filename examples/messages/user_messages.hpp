#pragma once

#include "commrat/message_id.hpp"
#include "commrat/system_registry.hpp"

namespace user_app {

// ============================================================================
// User Payload Definitions (Plain Data Structures)
// ============================================================================

struct TemperatureData {
    float temperature_celsius{0.0f};
    uint64_t timestamp_ms{0};
};

struct StatusData {
    uint32_t status_code{0};
    float cpu_load{0.0f};
};

struct CounterData {
    uint32_t count{0};
};

struct PoseData {
    float x{0.0f};
    float y{0.0f};
    float theta{0.0f};
};

// ============================================================================
// Application Message Registry (System messages included automatically)
// ============================================================================

// Define ONLY your user messages - system messages included automatically!
using AppMessageRegistry = commrat::CombinedRegistry<
    // User data messages (UserDefined prefix, auto-increment IDs)
    commrat::MessageDefinition<TemperatureData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<StatusData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<CounterData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>,
    commrat::MessageDefinition<PoseData, commrat::MessagePrefix::UserDefined, commrat::UserSubPrefix::Data>
>;

} // namespace user_app
