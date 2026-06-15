#pragma once

/**
 * @file registry_utils.hpp
 * @brief Compile-time utilities for querying and manipulating message registries
 *
 * Provides query helpers for extracting message subsets, filtering by
 * prefix/subprefix, extracting commands, and combining registries.
 *
 * Standard utilities delegate to corerat::registry. CommRaT adds DataWithCommands
 * command extraction support on top.
 */

#include <corerat/messaging/message_id.hpp>
#include <corerat/messaging/registry_utils.hpp>
#include <tuple>
#include <type_traits>

namespace commrat {

using corerat::MessageDefinition;
using corerat::MessagePrefix;
using corerat::SystemSubPrefix;
using corerat::UserSubPrefix;
using corerat::DefaultMessageDef;
using corerat::make_message_id;
using corerat::MAX_MESSAGE_ID;

// Forward declarations
template<typename... MessageDefs>
class MessageRegistry;

template<typename PayloadT, typename... CommandTypes>
struct DataWithCommands;

namespace registry {

// ============================================================================
// Standard utilities — delegate to corerat::registry
// ============================================================================

using corerat::registry::tuple_size_v;
using corerat::registry::is_empty_tuple_v;
using corerat::registry::ContainsType;
using corerat::registry::contains_type_v;
using corerat::registry::extract_payloads_t;
using corerat::registry::get_all_payloads_t;
using corerat::registry::FilterByPrefix;
using corerat::registry::filter_by_prefix_t;
using corerat::registry::FilterBySubPrefix;
using corerat::registry::filter_by_subprefix_t;
using corerat::registry::data_messages_t;
using corerat::registry::command_messages_t;
using corerat::registry::subscription_messages_t;
using corerat::registry::system_control_messages_t;
using corerat::registry::IsRequest;
using corerat::registry::IsReply;
using corerat::registry::is_reply_v;
using corerat::registry::FilterRequests;
using corerat::registry::filter_requests_t;
using corerat::registry::FilterReplies;
using corerat::registry::filter_replies_t;
using corerat::registry::data_message_count;
using corerat::registry::command_message_count;
using corerat::registry::subscription_message_count;
using corerat::registry::request_message_count;
using corerat::registry::reply_message_count;
using corerat::registry::FindMessageDefForPayload;
using corerat::registry::find_message_def_t;
using corerat::registry::HasMessageDef;
using corerat::registry::has_message_def_v;
using corerat::registry::GetReplyPayloadFor;
using corerat::registry::get_reply_payload_t;
using corerat::registry::IsRequestPayload;
using corerat::registry::is_request_payload_v;
using corerat::registry::RegistryStats;

// corerat exports IsRequest struct but not is_request_v helper variable
template<typename MessageDef>
inline constexpr bool is_request_v = IsRequest<MessageDef>::value;

// ============================================================================
// CommRaT extension: DataWithCommands command extraction
// ============================================================================

namespace detail {

template<typename T>
struct IsDataWithCommands : std::false_type {};

template<typename PayloadType, typename... CommandTypes>
struct IsDataWithCommands<DataWithCommands<PayloadType, CommandTypes...>> : std::true_type {};

template<typename T>
struct ExtractCommands {
    using type = std::tuple<>;
};

template<typename PayloadType, typename... CommandTypes>
struct ExtractCommands<DataWithCommands<PayloadType, CommandTypes...>> {
    using type = std::tuple<CommandTypes...>;
};

template<typename T>
using extract_commands_t = typename ExtractCommands<T>::type;

} // namespace detail

/**
 * @brief Get all command types associated with a data type
 *
 * If DataType is DataWithCommands<PayloadType, Cmd1, Cmd2, ...>, returns
 * std::tuple<Cmd1, Cmd2, ...>. Otherwise returns empty tuple.
 */
template<typename DataType, typename Registry>
struct GetCommandsFor {
    using type = std::conditional_t<
        detail::IsDataWithCommands<DataType>::value,
        detail::extract_commands_t<DataType>,
        std::tuple<>
    >;
};

template<typename DataType, typename Registry>
using get_commands_for_t = typename GetCommandsFor<DataType, Registry>::type;

} // namespace registry
} // namespace commrat
