#pragma once

#include <commrat/messaging/system/subscription_messages.hpp>
#include <sertial/containers/fixed_string.hpp>
#include <cstdint>

namespace commrat {

// ============================================================================
// Param command payload structs
// ============================================================================

// Request the full params JSON for a module.
struct GetParamsPayload {};

// Reply carries the params serialized as a JSON object.
// json is empty when has_params == false (module has no Params struct).
struct GetParamsReplyPayload {
    sertial::fixed_string<512> json;
    bool has_params{false};
};

// Replace the module's full params from a JSON object.
struct SetParamsPayload {
    sertial::fixed_string<512> json;
};

struct SetParamsReplyPayload {
    bool success{false};
    sertial::fixed_string<128> error;
};

// ============================================================================
// MessageDefinition aliases — SystemSubPrefix::Control
// ============================================================================

using GetParamsCmd = MessageDefinition<
    GetParamsPayload,
    MessagePrefix::System,
    SystemSubPrefix::Control,
    0x0001,
    GetParamsReplyPayload
>;

using SetParamsCmd = MessageDefinition<
    SetParamsPayload,
    MessagePrefix::System,
    SystemSubPrefix::Control,
    0x0002,
    SetParamsReplyPayload
>;

using GetParamsReply = typename GetParamsCmd::ReplyMessageDef;
using SetParamsReply = typename SetParamsCmd::ReplyMessageDef;

} // namespace commrat
