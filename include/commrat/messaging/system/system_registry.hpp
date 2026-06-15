#pragma once

#include <corerat/messaging/message_id.hpp>
#include "../message_registry.hpp"
#include "subscription_messages.hpp"
#include "data_request_messages.hpp"

namespace commrat {

// ============================================================================
// System Message Registry
// ============================================================================

/**
 * @brief Framework system messages that are always included
 * 
 * Used by the framework for subscription protocol and internal communication.
 * Users don't need to manually include these.
 * 
 * Note: GetDataRequest/Reply and GetNextDataRequest/Reply are type-specific
 * (GetDataRequest<T>) and are automatically added when Message::Data<T>
 * types are registered.
 */
using SystemRegistry = MessageRegistry<
    SubscribeRequest,
    UnsubscribeRequest
>;

} // namespace commrat
