#pragma once

#include <corerat/messaging/wire_message.hpp>
#include <corerat/platform/timestamp.hpp>
#include <cstdint>
#include <utility>

namespace commrat {

template<typename T>
using TimsMessage = corerat::WireMessage<T>;
using TimsHeader = corerat::WireHeader;

// ============================================================================
// TimsMessage Creation Helpers
// ============================================================================

/**
 * @brief Create TimsMessage with header.timestamp set
 * 
 * Phase 6.10: Timestamps stored ONLY in TimsHeader, not in payload.
 * This helper wraps a payload in TimsMessage with the given timestamp.
 */
template<typename T>
TimsMessage<T> create_tims_message(T&& payload, uint64_t timestamp_ns) {
    TimsMessage<T> msg{
        .header = {
            .msg_type = 0,     // serialize() will set this
            .msg_size = 0,     // serialize() will set this
            .timestamp = timestamp_ns,  // ONE SOURCE OF TRUTH
            .seq_number = 0,   // TiMS will set this
            .dest = 0,
            .src = 0,
            .flags = 0
        },
        .payload = std::forward<T>(payload)
    };
    return msg;
}

} // namespace commrat
