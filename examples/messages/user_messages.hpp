#pragma once

#include "commrat/commrat.hpp"

namespace user_app {

// ============================================================================
// Payload Definitions (Plain POD Structs)
// ============================================================================

struct TemperatureData {
    float temperature_celsius{0.0f};
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
// Application Definition
// ============================================================================

// Define your CommRaT application with all message types
using App = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>,
    commrat::Message::Data<CounterData>,
    commrat::Message::Data<PoseData>
>;

// App provides:
//   App::Module<OutputSpec, InputSpec, ...Commands>
//   App::Mailbox<T>
//   App::serialize(msg) / deserialize<T>(data)
//   App::get_message_id<T>()

} // namespace user_app
