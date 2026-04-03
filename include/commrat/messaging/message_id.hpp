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
    GetData = 0x03,       ///< GetData request/reply (timestamp-synchronized fetch)
    GetNextData = 0x04,   ///< GetNextData request/reply (one-shot fetch)
    Custom = 0x05         ///< User can start custom categories from here
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
    static constexpr uint16_t id = 0;  // Auto-assign marker (0 = -0)
};

/// Maximum allowed message ID for non-reply messages (sign bit reserved for replies)
static constexpr uint16_t MAX_MESSAGE_ID = 0x7FFF;  // 32767

/**
 * @brief Message definition with compile-time ID assignment
 * 
 * @tparam PayloadT The payload type
 * @tparam Prefix_ Message prefix (System or UserDefined)
 * @tparam SubPrefix_ Category within prefix
 * @tparam ID_ Specific ID (0 = auto-assign, max 0x7FFF for non-replies)
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
    uint16_t ID_ = DefaultMessageDef::id,
    typename ReplyT = void  // Optional reply type for request-reply pairing
>
struct MessageDefinition {
    // Marker for type trait detection
    using is_message_definition_tag = void;
    
    using Payload = PayloadT;
    static constexpr MessagePrefix prefix = Prefix_;
    static constexpr uint16_t local_id = ID_;
    
    // Extract subprefix value based on prefix type
    static constexpr uint8_t subprefix = []() constexpr {
        // uint8_t passthrough: used internally by AutoAssignIDsProcess when re-creating
        // MessageDefinition with First::subprefix (which is already uint8_t)
        if constexpr (std::is_same_v<decltype(SubPrefix_), uint8_t>) {
            return SubPrefix_;
        } else if constexpr (Prefix_ == MessagePrefix::System) {
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
    
    // Type validation: SubPrefix must be the correct enum type, OR uint8_t (for auto-ID assignment)
    static_assert((Prefix_ == MessagePrefix::System && std::is_same_v<decltype(SubPrefix_), SystemSubPrefix>) ||
                  (Prefix_ == MessagePrefix::UserDefined && std::is_same_v<decltype(SubPrefix_), UserSubPrefix>) ||
                  (std::is_same_v<decltype(SubPrefix_), std::nullptr_t>) ||
                  (std::is_same_v<decltype(SubPrefix_), uint8_t>),  // Allow uint8_t for auto-ID assignment
                  "SubPrefix type must match Prefix type (or be uint8_t for internal use)");

    static_assert(std::is_same_v<ReplyT, void> || 
                  (Prefix_ == MessagePrefix::System && subprefix == static_cast<uint8_t>(SystemSubPrefix::Control)) ||
                  (Prefix_ == MessagePrefix::System && subprefix == static_cast<uint8_t>(SystemSubPrefix::Subscription)) ||
                  (Prefix_ == MessagePrefix::UserDefined && subprefix == static_cast<uint8_t>(UserSubPrefix::Commands)) ||
                  (Prefix_ == MessagePrefix::UserDefined && subprefix == static_cast<uint8_t>(UserSubPrefix::GetData)) ||
                  (Prefix_ == MessagePrefix::UserDefined && subprefix == static_cast<uint8_t>(UserSubPrefix::GetNextData)),
                  "non-void Reply type only allowed for System::Control, System::Subscription, UserDefined::Commands, UserDefined::GetData, or UserDefined::GetNextData messages");

    // Validate ID range for non-reply messages (need sign bit for replies)
    static_assert(std::is_same_v<ReplyT, void> || ID_ == 0 || ID_ <= MAX_MESSAGE_ID,
                  "Message ID for request messages must be <= 0x7FFF (sign bit reserved for reply)");
    static_assert(std::is_same_v<ReplyT, void> || static_cast<int16_t>(ID_) >= 0,
                  "Manually specified reply IDs not allowed (derived automatically)");

    using ReplyPayload = ReplyT;
    using ReplyMessageDef = std::conditional_t<!std::is_same_v<ReplyT, void>,
                                              MessageDefinition<ReplyT, Prefix_,
                                                                SubPrefix_,
                                                                static_cast<uint16_t>(-static_cast<int16_t>(ID_)),
                                                                void>,
                                              void>;
    static constexpr bool is_request = !std::is_same_v<ReplyT, void>;
    static constexpr bool is_reply = std::is_same_v<ReplyT, void>
                                        && (ID_ != 0)
                                        && (static_cast<int16_t>(ID_) < 0);
    static constexpr bool has_reply = !std::is_same_v<ReplyT, void>;
    
    // For reply messages, derive the request ID
    static constexpr uint16_t request_id = is_reply ? static_cast<uint16_t>(-static_cast<int16_t>(ID_)) : 0;

    // Full message ID (set during registry construction with auto-increment)
    static constexpr bool needs_auto_id = (ID_ == 0);
};

} // namespace commrat
