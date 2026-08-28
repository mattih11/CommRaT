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

// Single-field get: returns name, type, and current value.
struct GetParamPayload {
    sertial::fixed_string<48> name;
};

struct GetParamReplyPayload {
    sertial::fixed_string<48>  name;
    sertial::fixed_string<48>  type_name;
    sertial::fixed_string<256> value_json;
    bool found{false};
};

// Single-field set: only the named field is updated; all others keep their current values.
struct SetParamPayload {
    sertial::fixed_string<48>  name;
    sertial::fixed_string<256> value_json;
};

struct SetParamReplyPayload {
    bool success{false};
    sertial::fixed_string<128> error;
};

// Persist current params to a JSON file (empty path = default location).
struct SaveParamsPayload {
    sertial::fixed_string<256> path;
};

struct SaveParamsReplyPayload {
    bool success{false};
    sertial::fixed_string<128> error;
};

// Load and apply params from a JSON file.
struct LoadParamsPayload {
    sertial::fixed_string<256> path;
};

struct LoadParamsReplyPayload {
    bool success{false};
    sertial::fixed_string<128> error;
};

// ============================================================================
// MessageDefinition aliases — SystemSubPrefix::Control
// ============================================================================

using GetParamsCmd = MessageDefinition<GetParamsPayload,    MessagePrefix::System, SystemSubPrefix::Control, 0x0001, GetParamsReplyPayload>;
using SetParamsCmd = MessageDefinition<SetParamsPayload,    MessagePrefix::System, SystemSubPrefix::Control, 0x0002, SetParamsReplyPayload>;
using ListParamsCmd= MessageDefinition<ListParamsPayload,   MessagePrefix::System, SystemSubPrefix::Control, 0x0003, ListParamsReplyPayload>;
using GetParamCmd  = MessageDefinition<GetParamPayload,     MessagePrefix::System, SystemSubPrefix::Control, 0x0004, GetParamReplyPayload>;
using SetParamCmd  = MessageDefinition<SetParamPayload,     MessagePrefix::System, SystemSubPrefix::Control, 0x0005, SetParamReplyPayload>;
using SaveParamsCmd= MessageDefinition<SaveParamsPayload,   MessagePrefix::System, SystemSubPrefix::Control, 0x0006, SaveParamsReplyPayload>;
using LoadParamsCmd= MessageDefinition<LoadParamsPayload,   MessagePrefix::System, SystemSubPrefix::Control, 0x0007, LoadParamsReplyPayload>;

using GetParamsReply  = typename GetParamsCmd::ReplyMessageDef;
using SetParamsReply  = typename SetParamsCmd::ReplyMessageDef;
using ListParamsReply = typename ListParamsCmd::ReplyMessageDef;
using GetParamReply   = typename GetParamCmd::ReplyMessageDef;
using SetParamReply   = typename SetParamCmd::ReplyMessageDef;
using SaveParamsReply = typename SaveParamsCmd::ReplyMessageDef;
using LoadParamsReply = typename LoadParamsCmd::ReplyMessageDef;

} // namespace commrat
