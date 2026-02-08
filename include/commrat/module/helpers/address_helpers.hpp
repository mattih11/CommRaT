#pragma once

#include "commrat/module/module_config.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include <cstdint>

namespace commrat {

// ============================================================================
// Address Calculation Helpers
// ============================================================================

/**
 * @brief Extract message type ID from subscriber base address
 * Base address format: [data_type_id_low16:16][system_id:8][instance_id:8]
 */
inline constexpr uint16_t extract_message_type_from_address(uint32_t base_addr) {
    return static_cast<uint16_t>((base_addr >> 16) & 0xFFFF);
}

/**
 * @brief Calculate base mailbox address from output type, system_id, and instance_id
 * Format: [data_type_id_low16:16][system_id:8][instance_id:8]
 * Uses lower 16 bits of message ID to fit in uint32_t
 */
template<typename OutputData, typename OutputTypesTuple, typename UserRegistry>
static constexpr uint32_t calculate_base_address(uint8_t system_id, uint8_t instance_id) {
    // Get message ID for output data type from registry
    // For multi-output, use first output type from the tuple
    using BaseType = std::conditional_t<
        std::is_void_v<OutputData>,
        std::tuple_element_t<0, OutputTypesTuple>,
        OutputData
    >;
    constexpr uint32_t data_type_id = UserRegistry::template get_message_id<BaseType>();
    // Use lower 16 bits of message ID to fit addressing in 32 bits
    constexpr uint16_t data_type_id_low = static_cast<uint16_t>(data_type_id & 0xFFFF);
    return (static_cast<uint32_t>(data_type_id_low) << 16) | (system_id << 8) | instance_id;
}

/**
 * @brief Get specific mailbox address
 */
template<typename OutputData, typename OutputTypesTuple, typename UserRegistry>
static constexpr uint32_t get_mailbox_address(uint8_t system_id, uint8_t instance_id, MailboxType type) {
    return calculate_base_address<OutputData, OutputTypesTuple, UserRegistry>(system_id, instance_id) + 
           static_cast<uint8_t>(type);
}

// ============================================================================
// Mailbox Configuration Helpers
// ============================================================================

template<typename UserRegistry>
static inline MailboxConfig createMailboxConfig(const ModuleConfig& config, MailboxType type) {
    // Note: This version doesn't calculate address (needs template params)
    // Will be called from Module class with proper template context
    return MailboxConfig{
        .mailbox_id = 0,  // Set by caller
        .message_slots = config.message_slots,
        .max_message_size = UserRegistry::max_message_size,
        .send_priority = static_cast<uint8_t>(config.priority),
        .realtime = config.realtime,
        .mailbox_name = config.name + "_" + 
                       (type == MailboxType::CMD ? "cmd" : 
                        type == MailboxType::WORK ? "work" : "data")
    };
}

static inline MailboxConfig createWorkMailboxConfig(const ModuleConfig& config) {
    return MailboxConfig{
        .mailbox_id = 0,  // Set by caller
        .message_slots = config.message_slots,
        .max_message_size = SystemRegistry::max_message_size,
        .send_priority = static_cast<uint8_t>(config.priority),
        .realtime = config.realtime,
        .mailbox_name = config.name + "_work"
    };
}

} // namespace commrat
