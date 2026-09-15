#pragma once

#include <commrat/messaging/system/subscription_messages.hpp>
#include <cstdint>

namespace commrat {

enum class LifecycleState : uint8_t {
    Disabled,
    Enabling,
    Enabled,
    Disabling,
    Error
};

enum class LifecycleTarget : uint8_t {
    Off,
    On
};

enum class LifecycleResult : uint8_t {
    Success,
    AlreadyInState,
    Busy,
    Failed,
    ShuttingDown
};

struct LifecycleOnPayload {};
struct LifecycleOffPayload {};
struct GetLifecycleStatusPayload {};

struct LifecycleOnReplyPayload {
    uint8_t result{static_cast<uint8_t>(LifecycleResult::Failed)};
    uint8_t state{static_cast<uint8_t>(LifecycleState::Disabled)};
    uint8_t target{static_cast<uint8_t>(LifecycleTarget::Off)};
    uint32_t error_code{0};
};

struct LifecycleOffReplyPayload {
    uint8_t result{static_cast<uint8_t>(LifecycleResult::Failed)};
    uint8_t state{static_cast<uint8_t>(LifecycleState::Disabled)};
    uint8_t target{static_cast<uint8_t>(LifecycleTarget::Off)};
    uint32_t error_code{0};
};

struct LifecycleStatusReplyPayload {
    uint8_t state{static_cast<uint8_t>(LifecycleState::Disabled)};
    uint8_t target{static_cast<uint8_t>(LifecycleTarget::Off)};
    uint32_t error_code{0};
    uint32_t retry_count{0};
    uint64_t state_since_ns{0};
};

using LifecycleOnCmd = MessageDefinition<
    LifecycleOnPayload,
    MessagePrefix::System,
    SystemSubPrefix::Control,
    0x0010,
    LifecycleOnReplyPayload>;

using LifecycleOffCmd = MessageDefinition<
    LifecycleOffPayload,
    MessagePrefix::System,
    SystemSubPrefix::Control,
    0x0011,
    LifecycleOffReplyPayload>;

using GetLifecycleStatusCmd = MessageDefinition<
    GetLifecycleStatusPayload,
    MessagePrefix::System,
    SystemSubPrefix::Control,
    0x0012,
    LifecycleStatusReplyPayload>;

using LifecycleOnReply = typename LifecycleOnCmd::ReplyMessageDef;
using LifecycleOffReply = typename LifecycleOffCmd::ReplyMessageDef;
using GetLifecycleStatusReply = typename GetLifecycleStatusCmd::ReplyMessageDef;

} // namespace commrat