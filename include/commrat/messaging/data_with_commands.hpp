#pragma once

#include "message_id.hpp"
#include "message_helpers.hpp"
#include <tuple>
#include <type_traits>

namespace commrat {

/**
 * @brief Associate commands with a data message type
 * 
 * This creates a data message definition with an associated command list.
 * Commands are type-specific - each data type can have different commands.
 * 
 * Usage:
 * @code
 * // Define data with commands
 * using SensorData = DataWithCommands<
 *     SensorDataPayload,
 *     Command<CalibrateCmdPayload, CalibrateReplyPayload>,
 *     Command<SetRateCmdPayload, SetRateReplyPayload>
 * >;
 * 
 * // Or with using for cleaner syntax
 * using SensorData = DataWithCommands<SensorDataPayload,
 *     Cmd<CalibrateCmd, CalibrateReply>,
 *     Cmd<SetRateCmd, SetRateReply>
 * >;
 * 
 * // Use in application
 * using MyApp = CommRaT<SensorData, IMUData, ...>;
 * 
 * // Access commands at compile time
 * using SensorCommands = SensorData::Commands;  // std::tuple<Cmd1, Cmd2, ...>
 * constexpr bool has_cmds = SensorData::has_commands;  // true
 * @endcode
 * 
 * @tparam PayloadT Data payload type
 * @tparam CommandTypes Command message definitions (variadic)
 */
template<typename PayloadT, typename... CommandTypes>
struct DataWithCommands {
    // Marker for type trait detection (critical - needed by Mailbox!)
    using is_message_definition_tag = void;
    
    // Data message definition
    using DataMessage = MessageDefinition<PayloadT, MessagePrefix::UserDefined, 
                                         UserSubPrefix::Data, DefaultMessageDef::id>;
    
    // Command types tuple
    using Commands = std::tuple<CommandTypes...>;
    
    // Metadata
    using Payload = PayloadT;
    static constexpr bool has_commands = sizeof...(CommandTypes) > 0;
    static constexpr size_t num_commands = sizeof...(CommandTypes);
    
    // Forward MessageDefinition interface (lowercase to match MessageDefinition)
    static constexpr MessagePrefix prefix = DataMessage::prefix;
    static constexpr uint8_t subprefix = DataMessage::subprefix;
    static constexpr uint16_t local_id = DataMessage::local_id;
    static constexpr bool has_reply = DataMessage::has_reply;
    static constexpr bool is_request = DataMessage::is_request;
    static constexpr bool is_reply = DataMessage::is_reply;
    static constexpr bool needs_auto_id = DataMessage::needs_auto_id;
};

/**
 * @brief Helper to check if a type is DataWithCommands
 */
template<typename T>
struct is_data_with_commands : std::false_type {};

template<typename PayloadT, typename... CommandTypes>
struct is_data_with_commands<DataWithCommands<PayloadT, CommandTypes...>> : std::true_type {};

template<typename T>
inline constexpr bool is_data_with_commands_v = is_data_with_commands<T>::value;

/**
 * @brief Extract commands from DataWithCommands or return empty tuple
 */
template<typename T>
struct ExtractCommands {
    using type = std::tuple<>;  // No commands by default
};

template<typename PayloadT, typename... CommandTypes>
struct ExtractCommands<DataWithCommands<PayloadT, CommandTypes...>> {
    using type = std::tuple<CommandTypes...>;
};

template<typename T>
using ExtractCommands_t = typename ExtractCommands<T>::type;

/**
 * @brief Extract data message definition (unwrap DataWithCommands)
 */
template<typename T>
struct ExtractDataMessage {
    using type = T;  // Pass through if not DataWithCommands
};

template<typename PayloadT, typename... CommandTypes>
struct ExtractDataMessage<DataWithCommands<PayloadT, CommandTypes...>> {
    using type = typename DataWithCommands<PayloadT, CommandTypes...>::DataMessage;
};

template<typename T>
using ExtractDataMessage_t = typename ExtractDataMessage<T>::type;

/**
 * @brief Flatten DataWithCommands into separate data + command messages
 * 
 * Input: DataWithCommands<Payload, Cmd1, Cmd2>
 * Output: std::tuple<DataMessage<Payload>, Cmd1, Cmd2>
 */
template<typename T>
struct FlattenDataWithCommands {
    using type = std::tuple<T>;  // Not DataWithCommands, return as-is
};

template<typename PayloadT, typename... CommandTypes>
struct FlattenDataWithCommands<DataWithCommands<PayloadT, CommandTypes...>> {
    using DataMsg = typename DataWithCommands<PayloadT, CommandTypes...>::DataMessage;
    using type = decltype(std::tuple_cat(
        std::tuple<DataMsg>{},
        std::tuple<CommandTypes...>{}
    ));
};

template<typename T>
using FlattenDataWithCommands_t = typename FlattenDataWithCommands<T>::type;

/**
 * @brief Expand all DataWithCommands in a list
 * 
 * Input: DataWithCommands<D1, C1, C2>, DataWithCommands<D2, C3>, RegularMsg
 * Output: tuple<DataMsg<D1>, C1, C2, DataMsg<D2>, C3, RegularMsg>
 */
template<typename... MessageTypes>
struct ExpandAllDataWithCommands {
    using type = decltype(std::tuple_cat(
        typename FlattenDataWithCommands<MessageTypes>::type{}...
    ));
};

template<typename... MessageTypes>
using ExpandAllDataWithCommands_t = typename ExpandAllDataWithCommands<MessageTypes...>::type;

} // namespace commrat
