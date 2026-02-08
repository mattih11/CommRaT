#pragma once

#include "commrat/messages.hpp"
#include <array>
#include <cstddef>
#include <iostream>

namespace commrat {

// ============================================================================
// Phase 6.10: Input Metadata Storage and Accessors
// ============================================================================

/**
 * @brief Storage for input message metadata
 * 
 * Stores metadata about received input messages, populated before process() calls.
 */
struct InputMetadataStorage {
    uint64_t timestamp{0};           // Message timestamp (from TimsHeader)
    uint32_t sequence_number{0};     // Message sequence number (from TimsHeader)
    uint32_t message_id{0};          // Message type ID (from TimsHeader)
    bool is_new_data{false};         // True if fresh, false if stale/reused
    bool is_valid{false};            // True if getData succeeded, false if failed
};

/**
 * @brief Input metadata structure returned by accessor methods
 * 
 * Contains timestamp, sequence number, and freshness information about
 * a received input message. Available for modules with continuous inputs.
 * 
 * @tparam T The payload type of the input
 */
template<typename T>
struct InputMetadata {
    uint64_t timestamp;          ///< Message timestamp (from TimsHeader)
    uint32_t sequence_number;    ///< Message sequence number
    uint32_t message_id;         ///< Message type ID
    bool is_new_data;            ///< True if freshly received, false if stale/reused
    bool is_valid;               ///< True if getData succeeded, false if failed
    
    // Helper to get input type (for debugging/logging)
    static constexpr const char* type_name() { return typeid(T).name(); }
};

/**
 * @brief Find index of type T in input tuple
 * 
 * Compile-time helper to locate type in parameter pack.
 * Static asserts if type not found or appears multiple times.
 */
template<typename T, typename... Types>
static constexpr std::size_t find_type_index() {
    constexpr std::size_t count = ((std::is_same_v<T, Types> ? 1 : 0) + ...);
    static_assert(count > 0, "Type not found in inputs - check your input specification");
    static_assert(count == 1, "Type appears multiple times in inputs - use index-based access instead");
    
    // Find index where type matches
    std::size_t index = 0;
    bool found = false;
    ((std::is_same_v<T, Types> ? (found = true) : (found ? true : (++index, false))), ...);
    return index;
}

} // namespace commrat
