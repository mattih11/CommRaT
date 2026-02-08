#pragma once

#include <cstdint>
#include <type_traits>

namespace commrat {

// ============================================================================
// Message ID Structure: 0xPSMM
// P  = Prefix (1 byte)      - System(0x00) or UserDefined(0x01+)
// S  = SubPrefix (1 byte)   - Category within prefix
// MM = Message ID (2 bytes) - Specific message within category
// ============================================================================

/// Message ID prefixes
enum class MessagePrefix : uint8_t {
    System = 0x00,       ///< Framework control messages (subscription, etc.)
    UserDefined = 0x01   ///< User application messages (start from 0x01)
};

/// System message sub-categories (when Prefix == System)
enum class SystemSubPrefix : uint8_t {
    Subscription = 0x00,  ///< Subscription protocol messages
    Control = 0x01,       ///< Module control (on/off/reset/etc.)
    Reserved = 0xFF       ///< Reserved for future use
};

/// User-defined message sub-categories (when Prefix == UserDefined)
enum class UserSubPrefix : uint8_t {
    Data = 0x00,          ///< Data messages (sensor data, state, etc.)
    Commands = 0x01,      ///< Command messages
    Events = 0x02,        ///< Event notifications
    Custom = 0x03         ///< User can start custom categories from here
};

/// Compile-time message ID construction
constexpr uint32_t make_message_id(uint8_t prefix, uint8_t subprefix, uint16_t id) {
    return (static_cast<uint32_t>(prefix) << 24) |
           (static_cast<uint32_t>(subprefix) << 16) |
           static_cast<uint32_t>(id);
}

/// System message ID helper
constexpr uint32_t system_message_id(SystemSubPrefix subprefix, uint16_t id) {
    return make_message_id(
        static_cast<uint8_t>(MessagePrefix::System),
        static_cast<uint8_t>(subprefix),
        id
    );
}

/// User message ID helper
constexpr uint32_t user_message_id(UserSubPrefix subprefix, uint16_t id) {
    return make_message_id(
        static_cast<uint8_t>(MessagePrefix::UserDefined),
        static_cast<uint8_t>(subprefix),
        id
    );
}

// ============================================================================
// Message Definition - Compile-time message metadata
// ============================================================================

/// Default values for message definition
struct DefaultMessageDef {
    static constexpr MessagePrefix prefix = MessagePrefix::UserDefined;
    static constexpr UserSubPrefix user_subprefix = UserSubPrefix::Data;
    static constexpr SystemSubPrefix system_subprefix = SystemSubPrefix::Reserved;
    static constexpr uint16_t id = 0xFFFF;  // Auto-assign marker
};

/**
 * @brief Message definition with compile-time ID assignment
 * 
 * @tparam PayloadT The payload type
 * @tparam Prefix_ Message prefix (System or UserDefined)
 * @tparam SubPrefix_ Category within prefix
 * @tparam ID_ Specific ID (0xFFFF = auto-assign)
 * 
 * Examples:
 * - MessageDefinition<SubscribeRequest, MessagePrefix::System, SystemSubPrefix::Subscription>
 * - MessageDefinition<ScanData>  // Defaults to UserDefined::Data with auto ID
 * - MessageDefinition<SetPositionCmd, MessagePrefix::UserDefined, UserSubPrefix::Commands, 0x0004>
 */
template<
    typename PayloadT,
    MessagePrefix Prefix_ = DefaultMessageDef::prefix,
    auto SubPrefix_ = DefaultMessageDef::user_subprefix,  // auto to accept both enum types
    uint16_t ID_ = DefaultMessageDef::id
>
struct MessageDefinition {
    using Payload = PayloadT;
    static constexpr MessagePrefix prefix = Prefix_;
    static constexpr uint16_t local_id = ID_;
    
    // Extract subprefix value based on prefix type
    static constexpr uint8_t subprefix = []() constexpr {
        if constexpr (Prefix_ == MessagePrefix::System) {
            if constexpr (std::is_same_v<decltype(SubPrefix_), SystemSubPrefix>) {
                return static_cast<uint8_t>(SubPrefix_);
            } else {
                return static_cast<uint8_t>(DefaultMessageDef::system_subprefix);
            }
        } else {
            if constexpr (std::is_same_v<decltype(SubPrefix_), UserSubPrefix>) {
                return static_cast<uint8_t>(SubPrefix_);
            } else {
                return static_cast<uint8_t>(DefaultMessageDef::user_subprefix);
            }
        }
    }();
    
    // Full message ID (set during registry construction with auto-increment)
    static constexpr bool needs_auto_id = (ID_ == 0xFFFF);
};

// ============================================================================
// Request-Reply Pairing (RACK-style: reply = -request_id)
// ============================================================================

/**
 * @brief Mark a message as a request that expects a reply
 * 
 * The reply will automatically get ID = -request_id (in signed space)
 */
template<typename MessageDef>
struct Request : MessageDef {
    static constexpr bool is_request = true;
};

/**
 * @brief Define a reply message paired with a request
 * 
 * Automatically gets negative ID of the request
 */
template<typename RequestMessageDef>
struct Reply {
    using Payload = typename RequestMessageDef::Payload;  // Can be different, override if needed
    static constexpr MessagePrefix prefix = RequestMessageDef::prefix;
    static constexpr uint8_t subprefix = RequestMessageDef::subprefix;
    
    // Reply ID is negative of request ID (in int16_t space)
    static constexpr uint16_t local_id = []() constexpr {
        int16_t signed_id = static_cast<int16_t>(RequestMessageDef::local_id);
        return static_cast<uint16_t>(-signed_id);
    }();
    
    static constexpr bool is_reply = true;
    static constexpr bool needs_auto_id = false;  // Always explicit from request
};

// ============================================================================
// Empty message support (commands with no payload)
// ============================================================================

/// Empty payload for messages that only need their ID
struct EmptyPayload {};

} // namespace commrat
