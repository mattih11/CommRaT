#pragma once

/**
 * @file command_extraction.hpp
 * @brief Extract user commands and provide simple tuple membership checks
 * 
 * System commands come directly from Registry::subscription_messages_t<>.
 * User commands extracted from DataWithCommands.
 * Simple is_in_tuple trait for visitor pattern.
 */

#include "commrat/messaging/data_with_commands.hpp"
#include <type_traits>
#include <tuple>

namespace commrat {

// ============================================================================
// Tuple Membership Check (simple helper)
// ============================================================================

/**
 * @brief Check if Type is in Tuple
 */
template<typename Type, typename Tuple>
struct IsInTuple;

template<typename Type, typename... Ts>
struct IsInTuple<Type, std::tuple<Ts...>> {
    static constexpr bool value = (std::is_same_v<Type, Ts> || ...);
};

template<typename Type, typename Tuple>
inline constexpr bool is_in_tuple_v = IsInTuple<Type, Tuple>::value;

// ============================================================================
// User Command Extraction (from DataWithCommands)
// ============================================================================

/**
 * @brief Extract user commands from message definition
 * 
 * If MessageDef is DataWithCommands<Payload, Cmd1, Cmd2, ...>, returns tuple<Cmd1, Cmd2, ...>
 * Otherwise returns empty tuple
 */
template<typename MessageDef>
struct ExtractUserCommands {
    using type = std::tuple<>; // Default: no commands
};

template<typename PayloadT, typename... CommandTypes>
struct ExtractUserCommands<DataWithCommands<PayloadT, CommandTypes...>> {
    using type = std::tuple<CommandTypes...>;
};

template<typename MessageDef>
using ExtractUserCommands_t = typename ExtractUserCommands<MessageDef>::type;

/**
 * @brief Check if MessageDef has user commands
 */
template<typename MessageDef>
inline constexpr bool has_user_commands_v = std::tuple_size_v<ExtractUserCommands_t<MessageDef>> > 0;

} // namespace commrat
