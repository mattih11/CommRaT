#pragma once

#include "../message_id.hpp"
#include <cstdint>

namespace commrat {

/**
 * @brief Request to subscribe to continuous data from a producer module
 * 
 * Consumer sends this to producer to request data stream.
 * Producer will add consumer to its subscriber list.
 * 
 * RACK-style addressing: subscriber_base_addr contains [type][sys][inst][0]
 * mailbox_index specifies which mailbox to send data to (DATA mailbox index)
 */
struct SubscribeRequestPayload {
    uint32_t subscriber_base_addr{0};    ///< Consumer's base address ([type][sys][inst][mbx=0])
    uint8_t mailbox_index{0};            ///< Which mailbox to send data to (DATA mailbox index)
    int64_t requested_period_ms{0};      ///< Desired update period in ms (0 = as fast as possible)
};

/**
 * @brief Reply to subscription request
 * 
 * Producer sends this back to confirm subscription and report actual period.
 */
struct SubscribeReplyPayload {
    int64_t actual_period_ms{0};       ///< Actual update period in ms
    bool success{false};               ///< True if subscription succeeded
    uint32_t error_code{0};            ///< 0=success, 1=max_subscribers, 2=other
};

/**
 * @brief Request to unsubscribe from continuous data
 * 
 * Consumer sends this to producer to stop receiving data.
 */
struct UnsubscribeRequestPayload {
    uint32_t subscriber_mailbox_id{0};   ///< Consumer's mailbox ID
};

/**
 * @brief Acknowledgment of unsubscribe request
 */
struct UnsubscribeReplyPayload {
    bool success{true};                  ///< Always true unless error
};

// ============================================================================
// Message Definitions with Compile-Time IDs
// ============================================================================

// Subscribe request/reply pair with explicit IDs
using SubscribeRequest = MessageDefinition<
    SubscribeRequestPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0001  // Explicit ID for request
>;

using SubscribeReply = MessageDefinition<
    SubscribeReplyPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0002  // Explicit ID for reply (could use Reply<SubscribeRequest> with negative ID)
>;

// Unsubscribe request/reply pair with explicit IDs
using UnsubscribeRequest = MessageDefinition<
    UnsubscribeRequestPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0003
>;

using UnsubscribeReply = MessageDefinition<
    UnsubscribeReplyPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0004
>;

// Type aliases for accessing payloads
using SubscribeRequestType = typename SubscribeRequest::Payload;
using SubscribeReplyType = typename SubscribeReply::Payload;
using UnsubscribeRequestType = typename UnsubscribeRequest::Payload;
using UnsubscribeReplyType = typename UnsubscribeReply::Payload;

} // namespace commrat
