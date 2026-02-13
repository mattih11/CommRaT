#pragma once

#include "commrat/module/module_config.hpp"
#include "commrat/mailbox/mailbox.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include <cstdint>
#include <algorithm>

namespace commrat {

// ============================================================================
// Address Encoding Constants (RACK-style, byte-aligned)
// ============================================================================

// Address field shifts (high to low hierarchy)
constexpr uint8_t TYPE_ID_SHIFT = 24;
constexpr uint8_t SYSTEM_ID_SHIFT = 16;
constexpr uint8_t INSTANCE_ID_SHIFT = 8;
constexpr uint8_t MAILBOX_INDEX_SHIFT = 0;

// Address field masks
constexpr uint32_t TYPE_ID_MASK = 0xFF000000;
constexpr uint32_t SYSTEM_ID_MASK = 0x00FF0000;
constexpr uint32_t INSTANCE_ID_MASK = 0x0000FF00;
constexpr uint32_t MAILBOX_INDEX_MASK = 0x000000FF;

// Mailbox index constants (RACK-style)
constexpr uint8_t CMD_MBX_BASE = 0;  // CMD mailboxes start at index 0

// ============================================================================
// Address Encoding/Decoding Functions
// ============================================================================

/**
 * @brief Encode full mailbox address
 * Format: [type_id:8][system_id:8][instance_id:8][mailbox_index:8]
 * Example: 0xTTSSIIMM (easy hex debugging)
 */
constexpr uint32_t encode_address(uint8_t type_id, uint8_t system_id, 
                                   uint8_t instance_id, uint8_t mailbox_index) {
    return (static_cast<uint32_t>(type_id) << TYPE_ID_SHIFT) |
           (static_cast<uint32_t>(system_id) << SYSTEM_ID_SHIFT) |
           (static_cast<uint32_t>(instance_id) << INSTANCE_ID_SHIFT) |
           (static_cast<uint32_t>(mailbox_index) << MAILBOX_INDEX_SHIFT);
}

/**
 * @brief Get base address (type/system/instance only, mailbox_index=0)
 */
constexpr uint32_t get_base_address(uint8_t type_id, uint8_t system_id, uint8_t instance_id) {
    return encode_address(type_id, system_id, instance_id, 0);
}

/**
 * @brief Extract mailbox index from full address
 */
constexpr uint8_t extract_mailbox_index(uint32_t addr) {
    return static_cast<uint8_t>((addr & MAILBOX_INDEX_MASK) >> MAILBOX_INDEX_SHIFT);
}

/**
 * @brief Extract type ID from address
 */
constexpr uint8_t extract_type_id(uint32_t addr) {
    return static_cast<uint8_t>((addr & TYPE_ID_MASK) >> TYPE_ID_SHIFT);
}

/**
 * @brief Extract system ID from address
 */
constexpr uint8_t extract_system_id(uint32_t addr) {
    return static_cast<uint8_t>((addr & SYSTEM_ID_MASK) >> SYSTEM_ID_SHIFT);
}

/**
 * @brief Extract instance ID from address
 */
constexpr uint8_t extract_instance_id(uint32_t addr) {
    return static_cast<uint8_t>((addr & INSTANCE_ID_MASK) >> INSTANCE_ID_SHIFT);
}

/**
 * @brief Get DATA mailbox base index (after all CMD mailboxes)
 */
constexpr uint8_t get_data_mbx_base(uint8_t num_outputs) {
    return CMD_MBX_BASE + num_outputs;
}

/**
 * @brief Extract message type ID from subscriber base address (for backward compatibility)
 * Now just extracts type_id field from new encoding
 */
inline constexpr uint8_t extract_message_type_from_address(uint32_t base_addr) {
    return extract_type_id(base_addr);
}

// ============================================================================
// Template-based Address Calculation
// ============================================================================

/**
 * @brief Calculate base mailbox address from output type, system_id, and instance_id
 * New format: [type_id:8][system_id:8][instance_id:8][mailbox_index:8]
 * Returns base address with mailbox_index=0
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
    // Use lower 8 bits of message ID for type_id field
    constexpr uint8_t type_id = static_cast<uint8_t>(data_type_id & 0xFF);
    return get_base_address(type_id, system_id, instance_id);
}

/**
 * @brief Get specific mailbox address with index
 */
template<typename OutputData, typename OutputTypesTuple, typename UserRegistry>
static constexpr uint32_t get_mailbox_address(uint8_t system_id, uint8_t instance_id, uint8_t mailbox_index) {
    uint32_t base = calculate_base_address<OutputData, OutputTypesTuple, UserRegistry>(system_id, instance_id);
    return base | mailbox_index;
}

// ============================================================================
// Compile-Time Mailbox Sizing
// ============================================================================

/**
 * @brief Calculate CMD mailbox size at compile time
 * Size = max(SystemRegistry messages, Module's Commands<...>)
 */
template<typename... CommandTypes>
static constexpr size_t calculate_cmd_mailbox_size() {
    return std::max({
        sizeof(TimsMessage<SystemRegistry::SubscribeRequest>),
        sizeof(TimsMessage<SystemRegistry::UnsubscribeRequest>),
        sizeof(TimsMessage<SystemRegistry::SubscribeReply>),
        sizeof(TimsMessage<SystemRegistry::UnsubscribeReply>),
        sizeof(TimsMessage<CommandTypes>)...
    });
}

/**
 * @brief Calculate DATA mailbox size for specific input type
 */
template<typename InputType>
static constexpr size_t get_data_mailbox_size() {
    return sizeof(TimsMessage<InputType>);
}

// ============================================================================
// Mailbox Configuration Helpers
// ============================================================================

/**
 * @brief Create work mailbox config (for SystemRegistry messages)
 * @deprecated Work mailbox concept will be merged into CMD mailbox
 */
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
