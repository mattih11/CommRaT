#pragma once

#include "message_registry.hpp"
#include "data_with_commands.hpp"
#include <corerat/messaging/message_id.hpp>
#include "commrat/messaging/system/data_request_messages.hpp"
#include <cstdint>

namespace commrat {

using corerat::MessagePrefix;
using corerat::UserSubPrefix;
using corerat::SystemSubPrefix;
using corerat::DefaultMessageDef;
using corerat::MessageDefinition;
inline constexpr uint16_t AUTO_ID = 0;

/**
 * @file message_helpers.hpp
 * @brief Simplified message definition helpers
 * 
 * Provides a clean namespace-based API for message definitions:
 * 
 * @code
 * // Simple usage - just specify your type
 * using TempMsg = Message::Data<TemperatureData>;
 * using ResetCmd = Message::Command<ResetCommand>;
 * using ErrorEvt = Message::Event<ErrorData>;
 * 
 * // Advanced usage - custom prefix/ID if needed
 * using CustomMsg = Message::Data<MyData, MessagePrefix::Custom, 42>;
 * @endcode
 */

namespace Message {

// ============================================================================
// Data Messages (UserDefined prefix, Data subprefix, AUTO_ID by default)
// ============================================================================

/**
 * @brief Data message definition with automatic GetData protocol support
 * 
 * Usage:
 *   using TempMsg = Message::Data<TemperatureData>;
 *   // Automatically provides:
 *   //   TempMsg::GetDataRequest, TempMsg::GetDataReply
 *   //   TempMsg::GetNextDataRequest, TempMsg::GetNextDataReply
 * 
 * @tparam T Payload type
 * @tparam Prefix Message prefix (default: UserDefined)
 * @tparam LocalID Local message ID (default: AUTO_ID)
 */
template<typename T, 
         MessagePrefix Prefix = MessagePrefix::UserDefined,
         uint16_t LocalID = DefaultMessageDef::id>
struct DataMessageDef : MessageDefinition<T, Prefix, UserSubPrefix::Data, LocalID> {
    // Inherit all base functionality
    using Base = MessageDefinition<T, Prefix, UserSubPrefix::Data, LocalID>;
    
    // Re-export base members
    using Payload = typename Base::Payload;
    static constexpr uint32_t message_id = Base::message_id;
    static constexpr MessagePrefix prefix = Base::prefix;
    static constexpr uint8_t subprefix = Base::subprefix;
    static constexpr uint16_t local_id = Base::local_id;
    static constexpr bool is_request = Base::is_request;
    static constexpr bool is_reply = Base::is_reply;
    static constexpr bool has_reply = Base::has_reply;
    
    // Automatic GetData protocol support (type-specific)
    // Use same local_id as data message (different SubPrefix = no collision)
    using GetDataRequestDef = ::commrat::GetDataRequest<T, LocalID>;
    using GetDataReplyDef = ::commrat::GetDataReply<T, LocalID>;
    using GetNextDataRequestDef = ::commrat::GetNextDataRequest<T, LocalID>;
    using GetNextDataReplyDef = ::commrat::GetNextDataReply<T, LocalID>;
};

// Convenience alias for Message::Data<T>
template<typename T, 
         MessagePrefix Prefix = MessagePrefix::UserDefined,
         uint16_t LocalID = DefaultMessageDef::id>
using Data = DataMessageDef<T, Prefix, LocalID>;

// ============================================================================
// Command Messages (UserDefined prefix, Commands subprefix, AUTO_ID by default)
// ============================================================================

/**
 * @brief Command message definition
 * 
 * Usage:
 *   using ResetCmd = Message::Command<ResetCommand>;
 *   using CalibrateCmd = Message::Command<CalibrateCommand>;
 *   using GetStatusCmd = Message::Command<GetStatusRequest, StatusData>;  // With reply
 * 
 * @tparam T Payload type
 * @tparam ReplyT Reply message type (default: void for no reply)
 * @tparam Prefix Message prefix (default: UserDefined)
 * @tparam LocalID Local message ID (default: AUTO_ID)
 */
template<typename T, typename ReplyT = void,
         MessagePrefix Prefix = MessagePrefix::UserDefined,
         uint16_t LocalID = DefaultMessageDef::id>
using Command = MessageDefinition<T, Prefix, UserSubPrefix::Commands, LocalID, ReplyT>;

// ============================================================================
// Event Messages (UserDefined prefix, Events subprefix, AUTO_ID by default)
// ============================================================================

/**
 * @brief Event message definition
 * 
 * Usage:
 *   using ErrorEvent = Message::Event<ErrorData>;
 *   using WarningEvent = Message::Event<WarningData>;
 * 
 * @tparam T Payload type
 * @tparam Prefix Message prefix (default: UserDefined)
 * @tparam LocalID Local message ID (default: AUTO_ID)
 */
template<typename T,
         MessagePrefix Prefix = MessagePrefix::UserDefined,
         uint16_t LocalID = DefaultMessageDef::id>
using Event = MessageDefinition<T, Prefix, UserSubPrefix::Events, LocalID>;

// ============================================================================
// Data with Commands (simplified syntax)
// ============================================================================

/**
 * @brief Data message with associated commands
 * 
 * Convenience wrapper for DataWithCommands that provides clean syntax:
 * 
 * Usage:
 * @code
 * // Without commands (simple data)
 * using TempData = Message::Data<TemperaturePayload>;
 * 
 * // With commands (data + command list)
 * using SensorData = Message::DataWith<SensorPayload>::Commands<
 *     Message::Command<CalibrateCmd, CalibrateReply>,
 *     Message::Command<SetRateCmd, SetRateReply>
 * >;
 * 
 * // Or even simpler with using declarations
 * using CalibrateCmd = Message::Command<CalibrateCmdPayload, CalibrateReplyPayload>;
 * using SetRateCmd = Message::Command<SetRateCmdPayload, SetRateReplyPayload>;
 * using SensorData = Message::DataWith<SensorPayload>::Commands<CalibrateCmd, SetRateCmd>;
 * @endcode
 */
template<typename PayloadT>
struct DataWith {
    template<typename... CommandTypes>
    using Commands = DataWithCommands<PayloadT, CommandTypes...>;
};

} // namespace Message

} // namespace commrat
