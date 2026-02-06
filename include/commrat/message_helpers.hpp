#pragma once

#include "commrat/message_registry.hpp"
#include <cstdint>

namespace commrat {

// Sentinel value for auto-assigned IDs
constexpr uint16_t AUTO_ID = 0xFFFF;

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
 * @brief Data message definition
 * 
 * Usage:
 *   using TempMsg = Message::Data<TemperatureData>;
 *   using SensorMsg = Message::Data<SensorData>;
 * 
 * @tparam T Payload type
 * @tparam Prefix Message prefix (default: UserDefined)
 * @tparam LocalID Local message ID (default: AUTO_ID)
 */
template<typename T, 
         MessagePrefix Prefix = MessagePrefix::UserDefined,
         uint16_t LocalID = AUTO_ID>
using Data = MessageDefinition<T, Prefix, UserSubPrefix::Data, LocalID>;

// ============================================================================
// Command Messages (UserDefined prefix, Commands subprefix, AUTO_ID by default)
// ============================================================================

/**
 * @brief Command message definition
 * 
 * Usage:
 *   using ResetCmd = Message::Command<ResetCommand>;
 *   using CalibrateCmd = Message::Command<CalibrateCommand>;
 * 
 * @tparam T Payload type
 * @tparam Prefix Message prefix (default: UserDefined)
 * @tparam LocalID Local message ID (default: AUTO_ID)
 */
template<typename T,
         MessagePrefix Prefix = MessagePrefix::UserDefined,
         uint16_t LocalID = AUTO_ID>
using Command = MessageDefinition<T, Prefix, UserSubPrefix::Commands, LocalID>;

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
         uint16_t LocalID = AUTO_ID>
using Event = MessageDefinition<T, Prefix, UserSubPrefix::Events, LocalID>;

} // namespace Message

} // namespace commrat
