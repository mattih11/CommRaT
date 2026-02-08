#pragma once

#include "mailbox.hpp"
#include "messaging/message_registry.hpp"
#include "platform/threading.hpp"

namespace commrat {

// ============================================================================
// Registry-Based Mailbox (User-Friendly Interface)
// ============================================================================

/**
 * @brief Mailbox that takes a MessageRegistry and exposes payload-only interface
 * 
 * Users define their MessageRegistry once with all MessageDefinitions, then
 * use this mailbox with payload types only. No need to repeat MessageDefinition
 * in every send/receive call.
 * 
 * Example:
 * @code
 * // Define registry once:
 * using MyRegistry = MessageRegistry<
 *     MessageDefinition<StatusData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
 *     MessageDefinition<CommandData, MessagePrefix::UserDefined, UserSubPrefix::Commands>
 * >;
 * 
 * // Use mailbox with payload types only:
 * RegistryMailbox<MyRegistry> mbx(config);
 * mbx.start();
 * 
 * StatusData status{.status_code = 100};
 * mbx.send(status, dest_id);  // Payload type only!
 * 
 * auto result = mbx.receive<StatusData>();  // Payload type only!
 * @endcode
 */
template<typename Registry>
class RegistryMailbox {
private:
    // Extract MessageDefinition types from registry to create underlying Mailbox type
    template<typename... MessageDefs>
    static Mailbox<MessageDefs...> extract_defs(MessageRegistry<MessageDefs...>*);
    
    using UnderlyingMailbox = decltype(extract_defs(static_cast<Registry*>(nullptr)));
    
    UnderlyingMailbox mailbox_;
    
public:
    // ========================================================================
    // Construction and Lifecycle (Same as Mailbox)
    // ========================================================================
    
    explicit RegistryMailbox(const MailboxConfig& config)
        : mailbox_(config) {}
    
    ~RegistryMailbox() = default;
    
    // Non-copyable, movable
    RegistryMailbox(const RegistryMailbox&) = delete;
    RegistryMailbox& operator=(const RegistryMailbox&) = delete;
    RegistryMailbox(RegistryMailbox&&) noexcept = default;
    RegistryMailbox& operator=(RegistryMailbox&&) noexcept = default;
    
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
    // Type Validation (Payload Types)
    // ========================================================================
    
    template<typename PayloadT>
    static constexpr bool is_registered = Registry::template is_registered<PayloadT>;
    
    static constexpr size_t num_message_types() {
        return Registry::size();
    }
    
    // ========================================================================
    // Send Operations (Payload Types Only)
    // ========================================================================
    
    /**
     * @brief Send a message payload to a destination mailbox
     * 
     * Message ID is automatically determined from the registry.
     * 
     * @tparam PayloadT Payload type (must be registered)
     * @param payload Payload to send
     * @param dest_mailbox Destination mailbox ID
     * @return Success or error
     */
    template<typename PayloadT>
        requires is_registered<PayloadT>
    auto send(PayloadT& payload, uint32_t dest_mailbox) -> MailboxResult<void> {
        // Create TimsMessage wrapper
        TimsMessage<PayloadT> msg{
            .header = {
                .msg_type = Registry::template get_message_id<PayloadT>(),
                .msg_size = 0,  // Will be set by serialization
                .timestamp = 0, // Will be set by TiMS
                .seq_number = 0, // Will be set by TiMS
                .flags = 0
            },
            .payload = payload
        };
        
        return mailbox_.send(msg, dest_mailbox);
    }
    
    /**
     * @brief Send a message payload with explicit timestamp (Phase 6.10)
     * 
     * Use this for timestamp propagation in message chains.
     * 
     * @tparam PayloadT Payload type (must be registered)
     * @param payload Payload to send
     * @param dest_mailbox Destination mailbox ID
     * @param timestamp Timestamp to set in header (nanoseconds since epoch)
     * @return Success or error
     */
    template<typename PayloadT>
        requires is_registered<PayloadT>
    auto send(PayloadT& payload, uint32_t dest_mailbox, uint64_t timestamp) -> MailboxResult<void> {
        // Create TimsMessage wrapper with explicit timestamp
        TimsMessage<PayloadT> msg{
            .header = {
                .msg_type = Registry::template get_message_id<PayloadT>(),
                .msg_size = 0,  // Will be set by serialization
                .timestamp = timestamp, // USER-PROVIDED timestamp
                .seq_number = 0, // Will be set by TiMS
                .flags = 0
            },
            .payload = payload
        };
        
        return mailbox_.send(msg, dest_mailbox);
    }
    
    // ========================================================================
    // Receive Operations (Payload Types Only)
    // ========================================================================
    
    /**
     * @brief Receive a message of specific payload type (blocking)
     */
    template<typename PayloadT>
        requires is_registered<PayloadT>
    auto receive() -> MailboxResult<TimsMessage<PayloadT>> {
        return mailbox_.template receive<PayloadT>();
    }
    
    /**
     * @brief Try to receive a message (non-blocking)
     */
    template<typename PayloadT>
        requires is_registered<PayloadT>
    auto try_receive() -> MailboxResult<TimsMessage<PayloadT>> {
        return mailbox_.template try_receive<PayloadT>();
    }
    
    /**
     * @brief Receive with timeout
     */
    template<typename PayloadT>
        requires is_registered<PayloadT>
    auto receive_for(std::chrono::milliseconds timeout) -> MailboxResult<TimsMessage<PayloadT>> {
        return mailbox_.template receive_for<PayloadT>(timeout);
    }
    
    /**
     * @brief Receive any registered message type using visitor pattern
     * 
     * Visitor receives payload types only.
     */
    template<typename Visitor>
    auto receive_any(Visitor&& visitor) -> MailboxResult<void> {
        return mailbox_.receive_any(std::forward<Visitor>(visitor));
    }
    
    template<typename Visitor>
    auto try_receive_any(Visitor&& visitor) -> MailboxResult<void> {
        return mailbox_.try_receive_any(std::forward<Visitor>(visitor));
    }
    
    template<typename Visitor>
    auto receive_any_for(std::chrono::milliseconds timeout, Visitor&& visitor) -> MailboxResult<void> {
        // Poll with non-blocking receives and short sleeps to avoid TiMS timeout quirk
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            // Non-blocking receive (timeout = -1ms)
            auto result = mailbox_.receive_any_raw(std::chrono::milliseconds{-1});
            if (result) {
                // Use Registry::visit to deserialize and dispatch
                bool handled = Registry::visit(
                    static_cast<uint32_t>(result->type),
                    std::span{result->buffer.data(), result->buffer.size()},
                    [&](auto& tims_msg) {
                        // Registry::visit deserializes to TimsMessage<PayloadT>
                        // Pass it directly to visitor
                        visitor(tims_msg);
                    }
                );
                
                if (handled) {
                    return MailboxResult<void>::ok();
                }
            }
            // Sleep longer to reduce busy-waiting CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return MailboxResult<void>::error(MailboxError::Timeout);
    }
    
    // ========================================================================
    // Direct Access to Underlying Mailbox (If Needed)
    // ========================================================================
    
    UnderlyingMailbox& underlying() { return mailbox_; }
    const UnderlyingMailbox& underlying() const { return mailbox_; }
};

// ============================================================================
// Convenience Factory Function
// ============================================================================

template<typename Registry>
auto make_mailbox(const MailboxConfig& config) {
    return RegistryMailbox<Registry>(config);
}

} // namespace commrat
