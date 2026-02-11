#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <vector>

namespace commrat {

// ============================================================================
// Input Mode Tags
// ============================================================================

/// Periodic execution - module runs on a timer, no input data
struct PeriodicInput {
    std::chrono::milliseconds period{100};
};

/// Free-running loop - module runs as fast as possible, no input data
struct LoopInput {};

// ============================================================================
// Mailbox Configuration
// ============================================================================

// Mailbox type identifiers (offsets from base address)
enum class MailboxType : uint8_t {
    CMD = 0,      // Command mailbox - receives user commands only
    WORK = 16,    // Work/System mailbox - subscription protocol + system messages
    PUBLISH = 32, // Publish mailbox - sends output data to subscribers
    DATA = 48     // Data mailbox - receives input data streams
};

// Phase 6.5: Multi-input source configuration
struct InputSource {
    uint8_t system_id{0};      // Source module's system identifier
    uint8_t instance_id{0};    // Source module's instance number
    bool is_primary{false};    // Is this the primary input? (drives execution)
    
    // For multi-output producers: primary output type ID for base address calculation
    std::optional<uint32_t> source_primary_output_type_id;
};

struct ModuleConfig {
    std::string name;
    uint8_t system_id{0};        // System/robot identifier
    uint8_t instance_id{0};      // Instance number for this module type
    std::chrono::milliseconds period{100};
    size_t message_slots{10};
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    
    // ========================================================================
    // Single-Input Mode (backward compatible)
    // ========================================================================
    
    // For Input<T> mode: source module's system_id and instance_id
    std::optional<uint8_t> source_system_id;
    std::optional<uint8_t> source_instance_id;
    
    // For multi-output producers: the primary output type ID used for base address calculation
    // If not set, will use InputData type (works for single-output producers)
    std::optional<uint32_t> source_primary_output_type_id;
    
    // ========================================================================
    // Multi-Input Mode (Phase 6.5)
    // ========================================================================
    
    // Multiple input sources (one per type in Inputs<Ts...>)
    // Order must match order of types in Inputs<T1, T2, ...>
    // Exactly one source must have is_primary=true
    std::vector<InputSource> input_sources;
    
    // Per-input history buffer capacity (for getData synchronization)
    size_t history_buffer_size{100};
    
    // Default tolerance for getData() calls (milliseconds)
    std::chrono::milliseconds sync_tolerance{50};
};

} // namespace commrat
