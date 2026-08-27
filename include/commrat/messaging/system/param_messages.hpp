#pragma once

#include <commrat/messaging/system/subscription_messages.hpp>
#include <sertial/containers/fixed_string.hpp>
#include <sertial/containers/fixed_vector.hpp>
#include <cstdint>

namespace commrat {

// ============================================================================
// Param command payload structs
// ============================================================================

struct GetParamsPayload {};

struct GetParamsReplyPayload {
    sertial::fixed_string<512> json;
    bool has_params{false};
};

struct SetParamsPayload {
    sertial::fixed_string<512> json;
};

struct SetParamsReplyPayload {
    bool success{false};
    sertial::fixed_string<128> error;
};

// One entry in a ListParamsReply — name, C++ type, and current value as JSON.
struct ParamInfo {
    sertial::fixed_string<48>  name;
    sertial::fixed_string<48>  type_name;  // C++ type (e.g. "float", "int")
    sertial::fixed_string<128> value_json; // current value serialised as JSON
};

struct ListParamsPayload {};

struct ListParamsReplyPayload {
    sertial::fixed_vector<ParamInfo, 16> params;
    bool has_params{false};
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

using ListParamsCmd = MessageDefinition<
    ListParamsPayload,
    MessagePrefix::System,
    SystemSubPrefix::Control,
    0x0003,
    ListParamsReplyPayload
>;

using GetParamsReply  = typename GetParamsCmd::ReplyMessageDef;
using SetParamsReply  = typename SetParamsCmd::ReplyMessageDef;
using ListParamsReply = typename ListParamsCmd::ReplyMessageDef;

} // namespace commrat
