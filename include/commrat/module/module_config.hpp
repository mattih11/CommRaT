#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <vector>
#include <rfl.hpp>
#include "commrat/mailbox/mailbox_type.hpp"

namespace commrat {

// ============================================================================
// Mailbox Configuration
// ============================================================================

// RACK-style mailbox index allocation
// CMD mailboxes for outputs start at index 0
// DATA mailboxes for inputs start after all CMD mailboxes
// See address_helpers.hpp for address encoding: [type:8][sys:8][inst:8][mbx:8]

// Default mailbox slot counts (configurable per module)
constexpr uint32_t DEFAULT_CMD_SLOTS = 10;   // Command/subscription buffering
constexpr uint32_t DEFAULT_DATA_SLOTS = 50;  // Historical buffering for getData

// ============================================================================
// Output Configuration (TaggedUnion)
// ============================================================================

/// No output - Module only consumes inputs (logger, monitor, controller)
/// Still needs addressing for CMD/WORK mailboxes
struct NoOutputConfig {
    uint8_t system_id{0};
    uint8_t instance_id{0};
};

/// Simple output - All outputs share same system_id/instance_id
/// Use when output types are distinct (no collisions)
struct SimpleOutputConfig {
    uint8_t system_id{0};
    uint8_t instance_id{0};
};

/// Multi-output - Each output gets its own addressing
/// Use when multiple outputs have same type (would collide with SimpleOutput)
struct MultiOutputConfig {
    struct OutputAddress {
        uint8_t system_id{0};
        uint8_t instance_id{0};
    };
    std::vector<OutputAddress> addresses;  // One per output in Outputs<>
};

using OutputConfig = rfl::TaggedUnion<"output_type", NoOutputConfig, SimpleOutputConfig, MultiOutputConfig>;

// ============================================================================
// Input Configuration (TaggedUnion)
// ============================================================================

/// No input - Periodic or free-running execution (based on config.period)
struct NoInputConfig {};

/// Single input - One source module
struct SingleInputConfig {
    uint8_t source_system_id{0};
    uint8_t source_instance_id{0};
};

/// Multi-input - Multiple synchronized sources
struct MultiInputConfig {
    struct InputSource {
        uint8_t system_id{0};
        uint8_t instance_id{0};
        bool is_primary{false};  // Exactly one must be primary (drives execution)
        mutable size_t input_index{0};  // Auto-populated during subscription
    };
    std::vector<InputSource> sources;  // Order matches Inputs<T1, T2, ...>
    size_t history_buffer_size{100};   // Buffer capacity for getData synchronization
    std::chrono::milliseconds sync_tolerance{50};  // Tolerance for getData calls
};

using InputConfig = rfl::TaggedUnion<"input_type", NoInputConfig, SingleInputConfig, MultiInputConfig>;

// ============================================================================
// Module Configuration
// ============================================================================

struct ModuleConfig {
    std::string name;
    
    // Output and input configuration (Variant)
    OutputConfig outputs = SimpleOutputConfig{.system_id = 0, .instance_id = 0};
    InputConfig inputs = NoInputConfig{};
    
    // Common configuration
    // Period is optional:
    // - With ContinuousInput: ignored (input-driven execution)
    // - Without ContinuousInput + period: periodic execution
    // - Without ContinuousInput + no period: free-running loop (max throughput)
    std::optional<std::chrono::milliseconds> period{std::chrono::milliseconds{100}};
    size_t message_slots{10};  // Legacy: kept for compatibility
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    
    // Mailbox-specific slot counts (RACK-style)
    // Optional fields with defaults for backward compatibility
    rfl::DefaultVal<uint32_t> cmd_message_slots = DEFAULT_CMD_SLOTS;
    rfl::DefaultVal<uint32_t> data_message_slots = DEFAULT_DATA_SLOTS;
    
    // ========================================================================
    // Output Configuration Accessors
    // ========================================================================
    
    /// Get system_id - NoOutput or SimpleOutput only
    [[nodiscard]] uint8_t system_id() const {
        if (auto* no = rfl::get_if<NoOutputConfig>(&outputs.variant())) {
            return no->system_id;
        } else if (auto* simple = rfl::get_if<SimpleOutputConfig>(&outputs.variant())) {
            return simple->system_id;
        } else {
            throw std::logic_error("system_id() without index not valid for MultiOutputConfig - use system_id(index)");
        }
    }
    
    /// Get instance_id - NoOutput or SimpleOutput only
    [[nodiscard]] uint8_t instance_id() const {
        if (auto* no = rfl::get_if<NoOutputConfig>(&outputs.variant())) {
            return no->instance_id;
        } else if (auto* simple = rfl::get_if<SimpleOutputConfig>(&outputs.variant())) {
            return simple->instance_id;
        } else {
            throw std::logic_error("instance_id() without index not valid for MultiOutputConfig - use instance_id(index)");
        }
    }
    
    /// Get system_id for specific output index (MultiOutput only)
    [[nodiscard]] uint8_t system_id(size_t index) const {
        auto* multi = rfl::get_if<MultiOutputConfig>(&outputs.variant());
        if (!multi) {
            throw std::logic_error("system_id(index) only valid for MultiOutputConfig");
        }
        if (index >= multi->addresses.size()) {
            throw std::out_of_range("Output index out of range");
        }
        return multi->addresses[index].system_id;
    }
    
    /// Get instance_id for specific output index (MultiOutput only)
    [[nodiscard]] uint8_t instance_id(size_t index) const {
        auto* multi = rfl::get_if<MultiOutputConfig>(&outputs.variant());
        if (!multi) {
            throw std::logic_error("instance_id(index) only valid for MultiOutputConfig");
        }
        if (index >= multi->addresses.size()) {
            throw std::out_of_range("Output index out of range");
        }
        return multi->addresses[index].instance_id;
    }
    
    // ========================================================================
    // Input Configuration Accessors
    // ========================================================================
    
    /// Get source_system_id (SingleInput only)
    [[nodiscard]] uint8_t source_system_id() const {
        auto* single = rfl::get_if<SingleInputConfig>(&inputs.variant());
        if (!single) {
            throw std::logic_error("source_system_id() only valid for SingleInputConfig");
        }
        return single->source_system_id;
    }
    
    /// Get source_instance_id (SingleInput only)
    [[nodiscard]] uint8_t source_instance_id() const {
        auto* single = rfl::get_if<SingleInputConfig>(&inputs.variant());
        if (!single) {
            throw std::logic_error("source_instance_id() only valid for SingleInputConfig");
        }
        return single->source_instance_id;
    }
    
    /// Get input sources (MultiInput only)
    [[nodiscard]] const std::vector<MultiInputConfig::InputSource>& input_sources() const {
        auto* multi = rfl::get_if<MultiInputConfig>(&inputs.variant());
        if (!multi) {
            throw std::logic_error("input_sources() only valid for MultiInputConfig");
        }
        return multi->sources;
    }
    
    /// Get mutable input sources (MultiInput only) - for populating input_index
    [[nodiscard]] std::vector<MultiInputConfig::InputSource>& input_sources() {
        auto* multi = rfl::get_if<MultiInputConfig>(&inputs.variant());
        if (!multi) {
            throw std::logic_error("input_sources() only valid for MultiInputConfig");
        }
        return multi->sources;
    }
    
    /// Get sync_tolerance (MultiInput only)
    [[nodiscard]] std::chrono::milliseconds sync_tolerance() const {
        auto* multi = rfl::get_if<MultiInputConfig>(&inputs.variant());
        if (!multi) {
            throw std::logic_error("sync_tolerance() only valid for MultiInputConfig");
        }
        return multi->sync_tolerance;
    }
    
    /// Get history_buffer_size (MultiInput only)
    [[nodiscard]] size_t history_buffer_size() const {
        auto* multi = rfl::get_if<MultiInputConfig>(&inputs.variant());
        if (!multi) {
            throw std::logic_error("history_buffer_size() only valid for MultiInputConfig");
        }
        return multi->history_buffer_size;
    }
    
    /// Get source system_id at index (MultiInput only)
    [[nodiscard]] uint8_t input_system_id(size_t index) const {
        auto* multi = rfl::get_if<MultiInputConfig>(&inputs.variant());
        if (!multi) {
            throw std::logic_error("input_system_id(index) only valid for MultiInputConfig");
        }
        if (index >= multi->sources.size()) {
            throw std::out_of_range("Input index out of range");
        }
        return multi->sources[index].system_id;
    }
    
    /// Get source instance_id at index (MultiInput only)
    [[nodiscard]] uint8_t input_instance_id(size_t index) const {
        auto* multi = rfl::get_if<MultiInputConfig>(&inputs.variant());
        if (!multi) {
            throw std::logic_error("input_instance_id(index) only valid for MultiInputConfig");
        }
        if (index >= multi->sources.size()) {
            throw std::out_of_range("Input index out of range");
        }
        return multi->sources[index].instance_id;
    }
    
    // ========================================================================
    // Type Checking Helpers
    // ========================================================================
    
    [[nodiscard]] bool has_no_output() const { return rfl::get_if<NoOutputConfig>(&outputs.variant()) != nullptr; }
    [[nodiscard]] bool has_simple_output() const { return rfl::get_if<SimpleOutputConfig>(&outputs.variant()) != nullptr; }
    [[nodiscard]] bool has_multi_output_config() const { return rfl::get_if<MultiOutputConfig>(&outputs.variant()) != nullptr; }
    
    [[nodiscard]] bool has_no_input() const { return rfl::get_if<NoInputConfig>(&inputs.variant()) != nullptr; }
    [[nodiscard]] bool has_single_input() const { return rfl::get_if<SingleInputConfig>(&inputs.variant()) != nullptr; }
    [[nodiscard]] bool has_multi_input_config() const { return rfl::get_if<MultiInputConfig>(&inputs.variant()) != nullptr; }
};

} // namespace commrat
