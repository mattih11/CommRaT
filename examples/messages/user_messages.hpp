#pragma once

#include "commrat/commrat.hpp"

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

// Define your messages and get Module/Mailbox aliases automatically!
using App = commrat::Registry<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>,
    commrat::Message::Data<CounterData>,
    commrat::Message::Data<PoseData>
>;

// That's it! App::Module and App::Mailbox are now available automatically.

} // namespace user_app
