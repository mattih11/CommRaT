#pragma once

#include <corerat/messaging/message_id.hpp>
#include <cstdint>

namespace commrat {
// Bring corerat message-ID types into commrat:: namespace
using corerat::MessageDefinition;
using corerat::MessagePrefix;
using corerat::SystemSubPrefix;
using corerat::UserSubPrefix;
using corerat::DefaultMessageDef;
using corerat::make_message_id;

/**
 * @brief Request to subscribe to continuous data from a producer module
 * 
 * Consumer sends this to producer to request data stream.
 * Producer will add consumer to its subscriber list.
 */
struct SubscribeRequestPayload {
    uint32_t subscriber_addr{0};    ///< Consumer's DATA mailbox address to receive data on
    int64_t requested_period_ms{0}; ///< Desired update period in ms (0 = as fast as possible)
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
    uint32_t subscriber_addr{0};    ///< Consumer's mailbox address (same as used in subscribe)
};

/**
 * @brief Acknowledgment of unsubscribe request
 */
struct UnsubscribeReplyPayload {
    bool success{true};                  ///< Always true unless error
};

// ============================================================================
// Message Definitions with Request-Reply Pairing
// ============================================================================

// Subscribe request with automatic reply registration
using SubscribeRequest = MessageDefinition<
    SubscribeRequestPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0001,                        // Request ID
    SubscribeReplyPayload          // Reply type (automatically gets ID = -1)
>;

// Unsubscribe request with automatic reply registration
using UnsubscribeRequest = MessageDefinition<
    UnsubscribeRequestPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0003,                        // Request ID
    UnsubscribeReplyPayload        // Reply type (automatically gets ID = -3)
>;

// Reply message definitions (automatically created via ReplyMessageDef)
using SubscribeReply = typename SubscribeRequest::ReplyMessageDef;
using UnsubscribeReply = typename UnsubscribeRequest::ReplyMessageDef;

// Type aliases for accessing payloads
using SubscribeRequestType = typename SubscribeRequest::Payload;
using SubscribeReplyType = typename SubscribeReply::Payload;
using UnsubscribeRequestType = typename UnsubscribeRequest::Payload;
using UnsubscribeReplyType = typename UnsubscribeReply::Payload;

} // namespace commrat
