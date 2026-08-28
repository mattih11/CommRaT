#pragma once

#include <corerat/messaging/message_id.hpp>
#include "../message_registry.hpp"
#include "subscription_messages.hpp"
#include "data_request_messages.hpp"
#include "param_messages.hpp"

namespace commrat {

// ============================================================================
// System Message Registry
// ============================================================================

/**
 * @brief Framework system messages that are always included
 * 
 * Users don't need to manually include these.
 * GetDataRequest<T>/GetNextDataRequest<T> are type-specific and added per-output.
 */
using SystemRegistry = MessageRegistry<
    SubscribeRequest,
    UnsubscribeRequest,
    GetParamsCmd,
    SetParamsCmd,
    ListParamsCmd,
    GetParamCmd,
    SetParamCmd,
    SaveParamsCmd,
    LoadParamsCmd
>;

} // namespace commrat
