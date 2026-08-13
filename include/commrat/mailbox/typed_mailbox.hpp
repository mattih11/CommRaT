/**
 * @file typed_mailbox.hpp
 * @brief Type-restricted mailbox with optimized buffer sizing
 */

#pragma once

#include <corerat/ipc/mailbox.hpp>
#include "../messaging/message_registry.hpp"
#include "../messaging/registry_utils.hpp"
#include "../messaging/system/system_registry.hpp"
#include <type_traits>

namespace commrat {

/**
 * @brief Type-restricted mailbox with optimized buffer sizing
 * 
 * Restricts receive types to AllowedPayloadTypes (buffer sized accordingly).
 * Can additionally send SendOnlyTypes without buffer impact.
 * 
 * @tparam Registry Full message registry
 * @tparam AllowedPayloadTypes Payload types this mailbox can receive (determines buffer size)
 */
template<typename Registry, typename... AllowedPayloadTypes>
class TypedMailbox {
private:
    // SendOnlyTypes extracted from template magic if needed
    // For now, this is the base implementation
    // Extract MessageDefinition types for allowed payloads
    template<typename PayloadT>
    struct FindMessageDef {
        // TODO: Extract MessageDefinition from Registry for this PayloadT
        // For now, use MessageDefinition with UserDefined prefix
        using type = MessageDefinition<PayloadT, MessagePrefix::UserDefined, UserSubPrefix::Data>;
    };
    
    // Use MailboxFor<Registry> from message_registry.hpp (handles both
    // MessageRegistry<...> and CommRaT<...> transparently)
    using UnderlyingMailbox = MailboxFor<Registry>;
    
    UnderlyingMailbox mailbox_;
    
    // Compile-time validation helpers
    template<typename PayloadT>
    static constexpr bool is_allowed_type = (std::is_same_v<PayloadT, AllowedPayloadTypes> || ...);
    
    // For base TypedMailbox: sendable = allowed (no send-only types)
    template<typename PayloadT>
    static constexpr bool is_sendable_type = is_allowed_type<PayloadT>;
    
    template<typename PayloadT>
    static constexpr bool is_registered_type = Registry::template is_registered<PayloadT>;
    
public:
    // ========================================================================
    // Type Information
    // ========================================================================
    
    /**
     * @brief Maximum message size for allowed types only
     * 
     * This is the key optimization: buffer sized for max(AllowedPayloadTypes)
     * instead of max(all registry types).
     */
    static constexpr size_t max_message_size = 
        Registry::template max_size_for_types<AllowedPayloadTypes...>();
    
    static constexpr size_t num_allowed_types = sizeof...(AllowedPayloadTypes);
    
    /**
     * @brief Check if a payload type is allowed in this mailbox
     */
    template<typename PayloadT>
    static constexpr bool is_allowed() {
        return is_allowed_type<PayloadT>;
    }
    
    // ========================================================================
    // Construction and Lifecycle
    // ========================================================================
    
    /**
     * @brief Construct a typed mailbox
     * @param config Mailbox configuration (buffer sizes optimized automatically)
     */
    explicit TypedMailbox(const MailboxConfig& config)
        : mailbox_(config) {
        
        static_assert(sizeof...(AllowedPayloadTypes) > 0,
                      "TypedMailbox requires at least one allowed payload type");
        
        // Validate all allowed types are registered in the registry
        static_assert((is_registered_type<AllowedPayloadTypes> && ...),
                      "All allowed types must be registered in the message registry");
    }
    
    ~TypedMailbox() = default;
    
    // Non-copyable, movable
    TypedMailbox(const TypedMailbox&) = delete;
    TypedMailbox& operator=(const TypedMailbox&) = delete;
    TypedMailbox(TypedMailbox&&) noexcept = default;
    TypedMailbox& operator=(TypedMailbox&&) noexcept = default;
    
    // ========================================================================
    // Lifecycle Management
    // ========================================================================
    
    auto start() -> MailboxResult<void> {
        return mailbox_.start();
    }
    
    void stop() {
        mailbox_.stop();
    }
    
    bool is_running() const {
        return mailbox_.is_running();
    }
    
    uint32_t mailbox_id() const {
        return mailbox_.mailbox_id();
    }
    
    // ========================================================================
    // Type-Safe Send Operations
    // ========================================================================
    
    /**
     * @brief Send a message (type-restricted)
     * 
     * @tparam PayloadT Payload type (must be in AllowedPayloadTypes)
     * @param message Message to send
     * @param dest_mailbox Destination mailbox ID
     * @return Success or error
     * 
     * Compile-time validation:
     * - PayloadT must be in AllowedPayloadTypes (static_assert)
     * - Helpful error message if wrong type used
     */
    template<typename PayloadT>
    auto send(PayloadT& message, uint32_t dest_mailbox) 
        -> MailboxResult<void> {
        
        static_assert(is_sendable_type<PayloadT>,
                      "Message type not sendable from this mailbox. "
                      "Check that PayloadT is in AllowedPayloadTypes or SendOnlyTypes.");
        
        static_assert(is_registered_type<PayloadT>,
                      "Message type not registered in the message registry.");
        
        // Create TimsMessage wrapper
        TimsMessage<PayloadT> tims_msg{
            .header = {
                .msg_type = Registry::template get_message_id<PayloadT>(),
                .msg_size = 0,  // Set by serialization in Mailbox::send
                .timestamp = 0, // Caller should use the explicit-timestamp overload
                                // if a non-zero timestamp is required
                .seq_number = 0,
                .dest = dest_mailbox,
                .src = 0,       // Set by TiMS from mailbox_id at send time
                .flags = 0
            },
            .payload = message
        };
        
        return mailbox_.send(tims_msg, dest_mailbox);
    }
    
    /**
     * @brief Send a message with explicit timestamp (type-restricted)
     * 
     * @tparam PayloadT Payload type (must be in AllowedPayloadTypes)
     * @param message Message to send
     * @param dest_mailbox Destination mailbox ID
     * @param timestamp Explicit timestamp to set in TimsHeader
     * @return Success or error
     */
    template<typename PayloadT>
    auto send(PayloadT& message, uint32_t dest_mailbox, uint64_t timestamp) 
        -> MailboxResult<void> {
        
        static_assert(is_sendable_type<PayloadT>,
                      "Message type not sendable from this mailbox. "
                      "Check that PayloadT is in AllowedPayloadTypes or SendOnlyTypes.");
        
        static_assert(is_registered_type<PayloadT>,
                      "Message type not registered in the message registry.");
        
        // Create TimsMessage wrapper with explicit timestamp
        TimsMessage<PayloadT> tims_msg{
            .header = {
                .msg_type = Registry::template get_message_id<PayloadT>(),
                .msg_size = 0,  // Set by serialization in Mailbox::send
                .timestamp = timestamp,
                .seq_number = 0,
                .dest = dest_mailbox,
                .src = 0,       // Set by TiMS from mailbox_id at send time
                .flags = 0
            },
            .payload = message
        };
        
        return mailbox_.send(tims_msg, dest_mailbox);
    }
    
    /**
     * @brief Send a TimsMessage (type-restricted)
     */
    template<typename PayloadT>
    auto send(TimsMessage<PayloadT>& tims_message, uint32_t dest_mailbox)
        -> MailboxResult<void> {
        
        static_assert(is_sendable_type<PayloadT>,
                      "Message type not sendable from this mailbox.");
        
        static_assert(is_registered_type<PayloadT>,
                      "Message type not registered in the message registry.");
        
        return mailbox_.send(tims_message, dest_mailbox);
    }
    
    /**
     * @brief Send a reply to a request message (like RACK's sendMsgReply)
     * 
     * Uses MessageDefinition's built-in request-reply mechanism.
     * Automatically extracts destination from request.header.src.
     * 
     * @tparam RequestPayload Request payload type (must be a request with reply)
     * @tparam ReplyPayload Reply payload type (validated against MessageDef::ReplyMessageDef)
     * @param request The received request message
     * @param reply The reply payload to send (must be sendable from this mailbox)
     * @return Success or error
     * 
     * Example:
     * @code
     * auto reply = handle_subscribe_request(received_msg.payload);
     * cmd_mailbox.send_reply(received_msg, reply);  // Type-safe + sendable check
     * @endcode
     */
    template<typename RequestPayload, typename ReplyPayload>
        requires registry::is_request_payload_v<RequestPayload, Registry> && is_sendable_type<ReplyPayload>
    auto send_reply(const TimsMessage<RequestPayload>& request, 
                    ReplyPayload& reply) 
        -> MailboxResult<void> {
        
        // Use MessageDefinition's ReplyMessageDef to validate reply type
        using RequestDef = registry::find_message_def_t<RequestPayload, Registry>;
        using ExpectedReply = typename RequestDef::ReplyMessageDef::Payload;
        
        static_assert(std::is_same_v<ReplyPayload, ExpectedReply>,
                      "ReplyPayload must match RequestDef::ReplyMessageDef::Payload");
        
        // Delegate to the underlying mailbox so the reply is stamped with
        // Time::now() and src is filled from config_.mailbox_id, matching
        // the behaviour of corerat::Mailbox::send_reply.
        return mailbox_.send_reply(request, reply);
    }
    
    // ========================================================================
    // Type-Safe Receive Operations (Zero-Copy API)
    // ========================================================================
    
    /**
     * @brief Zero-copy receive with timeout (PRIMARY API)
     * 
     * Receives a message directly into the provided storage, avoiding intermediate moves.
     * This is the preferred API for performance-critical code.
     * 
     * @tparam PayloadT Payload type (must be in AllowedPayloadTypes)
     * @param message Storage for received message (filled on success)
     * @param timeout Maximum time to wait
     * @return true if message received successfully, false otherwise
     * 
     * @note Zero-copy: Message deserialized directly into provided reference
     */
    template<typename PayloadT>
    bool receive(TimsMessage<PayloadT>& message, Duration timeout) {
        static_assert(is_allowed_type<PayloadT>,
                      "Message type not allowed in this typed mailbox. "
                      "Check that PayloadT is in the AllowedPayloadTypes list.");
        static_assert(is_registered_type<PayloadT>,
                      "Message type not registered in the message registry.");
        return mailbox_.template receive<PayloadT>(message, timeout);
    }
    
    /**
     * @brief Zero-copy non-blocking receive
     * 
     * Attempts to receive a message without blocking. Returns immediately if no message available.
     * 
     * @tparam PayloadT Payload type (must be in AllowedPayloadTypes)
     * @param message Storage for received message (filled on success)
     * @return true if message received, false if no message available
     * 
     * @note Zero-copy: Message deserialized directly into provided reference
     */
    template<typename PayloadT>
    bool try_receive(TimsMessage<PayloadT>& message) {
        static_assert(is_allowed_type<PayloadT>,
                      "Message type not allowed in this typed mailbox.");
        static_assert(is_registered_type<PayloadT>,
                      "Message type not registered in the message registry.");
        return mailbox_.template try_receive<PayloadT>(message);
    }
    
    // ========================================================================
    // Visitor-Based Receive (Single API for any allowed type)
    // ========================================================================
    
    /**
     * @brief Blocking receive any allowed message type using visitor pattern
     * 
     * Uses optimized buffer size (max of AllowedPayloadTypes only).
     * Visitor is called with TimsMessage<T> for any received allowed type.
     * 
     * @tparam BufferSize Stack buffer size (default: optimized for allowed types)
     * @tparam Visitor Callable accepting TimsMessage<T> for any allowed T
     * @return Success or error
     * 
     * Example:
     * @code
     * typed_mailbox.receive_any([](auto&& msg) {
     *     using PayloadT = std::decay_t<decltype(msg)>::payload_type;
     *     if constexpr (std::is_same_v<PayloadT, ResetCmd>) {
     *         handle_reset(msg.payload);
     *     } else if constexpr (std::is_same_v<PayloadT, CalibrateCmd>) {
     *         handle_calibrate(msg.payload);
     *     }
     * });
     * @endcode
     */
    template<size_t BufferSize = max_message_size, typename Visitor>
    auto receive_any(Visitor&& visitor) -> MailboxResult<void> {
        return mailbox_.template receive_any<BufferSize>(std::forward<Visitor>(visitor));
    }
    
    /**
     * @brief Receive any allowed message type with timeout
     * 
     * @tparam BufferSize Stack buffer size (default: optimized for allowed types)
     * @tparam Visitor Callable accepting TimsMessage<T> for any allowed T
     * @param timeout Maximum time to wait
     * @return Success or error
     */
    template<size_t BufferSize = max_message_size, typename Visitor>
    auto receive_any_for(Duration timeout, Visitor&& visitor) -> MailboxResult<void> {
        return mailbox_.template receive_any_for<BufferSize>(timeout, std::forward<Visitor>(visitor));
    }
    
    // ========================================================================
    // Access to Underlying Mailbox
    // ========================================================================
    
    /**
     * @brief Get reference to underlying Mailbox
     * 
     * Provides access to the underlying registry-based mailbox.
     */
    auto& get_underlying_mailbox() { return mailbox_; }
    const auto& get_underlying_mailbox() const { return mailbox_; }
};

// ============================================================================
// Convenience Type Aliases
// ============================================================================

/**
 * @brief Create a typed mailbox for a single payload type
 * 
 * Example:
 * @code
 * SingleTypeMailbox<MyRegistry, SensorData> data_mailbox(config);
 * @endcode
 */
template<typename Registry, typename PayloadT>
using SingleTypeMailbox = TypedMailbox<Registry, PayloadT>;

/**
 * @brief Create a typed mailbox for command types
 * 
 * Useful naming convention for CMD mailboxes in modules.
 * 
 * Example:
 * @code
 * CommandMailbox<MyRegistry, ResetCmd, CalibrateCmd> cmd_mailbox(config);
 * @endcode
 */
template<typename Registry, typename... CommandTypes>
using CommandMailbox = TypedMailbox<Registry, CommandTypes...>;

/**
 * @brief Create a typed mailbox for data types
 * 
 * Useful naming convention for DATA mailboxes in modules.
 * 
 * Example:
 * @code
 * DataMailbox<MyRegistry, SensorData, StatusData> data_mailbox(config);
 * @endcode
 */
template<typename Registry, typename... DataTypes>
using DataMailbox = TypedMailbox<Registry, DataTypes...>;

} // namespace commrat

