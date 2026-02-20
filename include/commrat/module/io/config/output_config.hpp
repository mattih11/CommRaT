#pragma once

#include "commrat/timestamp.hpp"
#include <cstdint>
#include <cstddef>

namespace commrat {

/**
 * @brief Configuration for BufferedOutput
 * 
 * All outputs are buffered - support both publish() and getData()
 */
struct BufferedOutputConfig {
    uint8_t system_id{0};
    uint8_t instance_id{0};
    std::size_t buffer_slots{100};              // TimestampedRingBuffer capacity
    Duration default_tolerance{Milliseconds(50)}; // Default for getData queries
};

} // namespace commrat
