#pragma once

#include "message_id.hpp"
#include "message_registry.hpp"
#include "subscription_messages.hpp"

namespace commrat {

// ============================================================================
// System Message Registry
// ============================================================================

/**
 * @brief Framework system messages that are always included
 * 
 * These messages are used by the framework for subscription protocol
 * and other internal communication. Users don't need to manually include these.
 */
using SystemRegistry = MessageRegistry<
    SubscribeRequest,
    SubscribeReply,
    UnsubscribeRequest,
    UnsubscribeReply
>;

// ============================================================================
// Registry Combiner - Merge System + User Messages
// ============================================================================

/**
 * @brief Combine system messages with user messages into one registry
 * 
 * This automatically includes framework messages with user-defined messages.
 * Users don't need to know about subscription protocol messages.
 */
template<typename... UserMessageDefs>
using CombinedRegistry = MessageRegistry<
    // System messages (always included)
    SubscribeRequest,
    SubscribeReply,
    UnsubscribeRequest,
    UnsubscribeReply,
    // User messages
    UserMessageDefs...
>;

} // namespace commrat
