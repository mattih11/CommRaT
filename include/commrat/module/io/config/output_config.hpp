#pragma once

#include <corerat/platform/timestamp.hpp>
#include <corerat/platform/duration.hpp>
#include <cstdint>
#include <cstddef>

namespace commrat {

using corerat::Duration;
using corerat::Milliseconds;

/**
 * @brief Configuration for BufferedOutput
 * 
 * All outputs are buffered - support both publish() and get_data()
 */
struct OutputConfig {
    uint8_t system_id{0};
    uint8_t instance_id{0};
    std::size_t buffer_slots{100};              // OutputBuffer capacity
    Duration default_tolerance{Milliseconds(50)}; // Default for get_data queries
};

} // namespace commrat
