#pragma once

/**
 * @file registry_utils.hpp
 * @brief Compile-time utilities for querying and manipulating message registries
 * 
 * Provides powerful query helpers for extracting message subsets, filtering by
 * prefix/subprefix, extracting commands, and combining registries.
 * 
 * All utilities are constexpr and work at compile-time for zero runtime overhead.
 */

#include "message_id.hpp"
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace commrat {

// Forward declarations
template<typename... MessageDefs>
class MessageRegistry;

template<typename PayloadT, typename... CommandTypes>
struct DataWithCommands;

namespace registry {

// ============================================================================
// Tuple Utilities (forwarding to std:: equivalents)
// ============================================================================

template<typename T>
inline constexpr std::size_t tuple_size_v = std::tuple_size_v<T>;

template<typename T>
inline constexpr bool is_empty_tuple_v = (std::tuple_size_v<T> == 0);

template<typename T, typename Tuple>
struct ContainsType;

template<typename T, typename... Us>
struct ContainsType<T, std::tuple<Us...>> : std::disjunction<std::is_same<T, Us>...> {};

template<typename T, typename Tuple>
inline constexpr bool contains_type_v = ContainsType<T, Tuple>::value;

// ============================================================================
// Internal Helpers
// ============================================================================

namespace detail {

/**
 * @brief Check if MessageDef matches a specific prefix
 */
template<MessagePrefix P, typename MessageDef>
struct MatchesPrefix {
    static constexpr bool value = (MessageDef::prefix == P);
};

/**
 * @brief Check if MessageDef matches prefix AND subprefix
 */
template<MessagePrefix P, uint8_t SubP, typename MessageDef>
struct MatchesSubPrefix {
    static constexpr bool value = (MessageDef::prefix == P) && 
                                  (MessageDef::subprefix == SubP);
};

/**
 * @brief Filter tuple elements by predicate
 */
template<template<typename> class Predicate, typename Tuple>
struct FilterTuple;

template<template<typename> class Predicate>
struct FilterTuple<Predicate, std::tuple<>> {
    using type = std::tuple<>;
};

template<template<typename> class Predicate, typename First, typename... Rest>
struct FilterTuple<Predicate, std::tuple<First, Rest...>> {
private:
    using RestFiltered = typename FilterTuple<Predicate, std::tuple<Rest...>>::type;
    
public:
    using type = std::conditional_t<
        Predicate<First>::value,
        decltype(std::tuple_cat(std::declval<std::tuple<First>>(), std::declval<RestFiltered>())),
        RestFiltered
    >;
};

template<template<typename> class Predicate, typename Tuple>
using filter_tuple_t = typename FilterTuple<Predicate, Tuple>::type;

/**
 * @brief Extract payload types from MessageDefs tuple
 */
template<typename MessageDefsTuple>
struct ExtractPayloads;

template<typename... MessageDefs>
struct ExtractPayloads<std::tuple<MessageDefs...>> {
    using type = std::tuple<typename MessageDefs::Payload...>;
};

template<typename MessageDefsTuple>
using extract_payloads_t = typename ExtractPayloads<MessageDefsTuple>::type;

/**
 * @brief Check if type is DataWithCommands
 */
template<typename T>
struct IsDataWithCommands : std::false_type {};

template<typename PayloadType, typename... CommandTypes>
struct IsDataWithCommands<DataWithCommands<PayloadType, CommandTypes...>> : std::true_type {};

/**
 * @brief Extract commands from DataWithCommands type
 */
template<typename T>
struct ExtractCommands {
    using type = std::tuple<>;  // Not DataWithCommands - no commands
};

template<typename PayloadType, typename... CommandTypes>
struct ExtractCommands<DataWithCommands<PayloadType, CommandTypes...>> {
    using type = std::tuple<CommandTypes...>;
};

template<typename T>
using extract_commands_t = typename ExtractCommands<T>::type;

} // namespace detail

// ============================================================================
// Message Filtering by Prefix/SubPrefix
// ============================================================================

/**
 * @brief Filter messages by prefix
 * 
 * Returns tuple of MessageDefinitions with matching prefix.
 * 
 * @tparam P Message prefix to filter by
 * @tparam Registry MessageRegistry to query
 * @return std::tuple<MessageDef1, MessageDef2, ...> of matching messages
 * 
 * @code
 * using MyApp = CommRaT<Message::Data<T1>, Message::Command<C1>>;
 * using UserMessages = filter_by_prefix_t<MessagePrefix::UserDefined, MyApp>;
 * @endcode
 */
template<MessagePrefix P, typename Registry>
struct FilterByPrefix {
private:
    template<typename T>
    using Predicate = detail::MatchesPrefix<P, T>;
    
    // Extract MessageDefsTuple from registry (works for MessageRegistry and derived types)
    using MessageDefs = typename Registry::MessageDefsTuple;
    
public:
    using type = detail::filter_tuple_t<Predicate, MessageDefs>;
};

template<MessagePrefix P, typename Registry>
using filter_by_prefix_t = typename FilterByPrefix<P, Registry>::type;

/**
 * @brief Filter messages by prefix AND subprefix
 * 
 * More specific filtering than filter_by_prefix.
 * 
 * @tparam P Message prefix
 * @tparam SubP Message subprefix
 * @tparam Registry MessageRegistry to query
 * @return std::tuple<MessageDef1, MessageDef2, ...> of matching messages
 * 
 * @code
 * using DataMessages = filter_by_subprefix_t<
 *     MessagePrefix::UserDefined,
 *     static_cast<uint8_t>(UserSubPrefix::Data),
 *     MyApp
 * >;
 * @endcode
 */
template<MessagePrefix P, uint8_t SubP, typename Registry>
struct FilterBySubPrefix {
private:
    template<typename T>
    using Predicate = detail::MatchesSubPrefix<P, SubP, T>;
    
    // Extract MessageDefsTuple from registry (works for MessageRegistry and derived types)
    using MessageDefs = typename Registry::MessageDefsTuple;
    
public:
    using type = detail::filter_tuple_t<Predicate, MessageDefs>;
};

template<MessagePrefix P, uint8_t SubP, typename Registry>
using filter_by_subprefix_t = typename FilterBySubPrefix<P, SubP, Registry>::type;

// ============================================================================
// Convenient Category Filters
// ============================================================================

/**
 * @brief Get all data messages (UserDefined::Data)
 * 
 * @tparam Registry MessageRegistry to query
 * @return Tuple of data MessageDefinitions
 */
template<typename Registry>
using data_messages_t = filter_by_subprefix_t<
    MessagePrefix::UserDefined,
    static_cast<uint8_t>(UserSubPrefix::Data),
    Registry
>;

/**
 * @brief Get all command messages (UserDefined::Commands)
 * 
 * @tparam Registry MessageRegistry to query
 * @return Tuple of command MessageDefinitions
 */
template<typename Registry>
using command_messages_t = filter_by_subprefix_t<
    MessagePrefix::UserDefined,
    static_cast<uint8_t>(UserSubPrefix::Commands),
    Registry
>;

/**
 * @brief Get all system control messages (System::Control)
 * 
 * @tparam Registry MessageRegistry to query
 * @return Tuple of system control MessageDefinitions
 */
template<typename Registry>
using system_control_messages_t = filter_by_subprefix_t<
    MessagePrefix::System,
    static_cast<uint8_t>(SystemSubPrefix::Control),
    Registry
>;

/**
 * @brief Get all subscription protocol messages (System::Subscription)
 * 
 * Includes Subscribe/Unsubscribe requests and replies.
 * 
 * @tparam Registry MessageRegistry to query
 * @return Tuple of subscription MessageDefinitions
 */
template<typename Registry>
using subscription_messages_t = filter_by_subprefix_t<
    MessagePrefix::System,
    static_cast<uint8_t>(SystemSubPrefix::Subscription),
    Registry
>;

// ============================================================================
// Command Extraction from Data Types
// ============================================================================

/**
 * @brief Get all command types associated with a data type
 * 
 * If DataType is defined with DataWithCommands<PayloadType, Cmd1, Cmd2, ...>,
 * returns std::tuple<Cmd1, Cmd2, ...>.
 * 
 * If DataType is plain (not DataWithCommands), returns empty tuple.
 * 
 * @tparam DataType Data message payload type to search for
 * @tparam Registry MessageRegistry to query
 * @return std::tuple<CommandTypes...> or std::tuple<> if no commands
 * 
 * @code
 * using SensorData = DataWithCommands<SensorPayload, CalibrateCmd, ResetCmd>;
 * using SensorCommands = get_commands_for_t<SensorData, MyApp>;
 * // Result: std::tuple<CalibrateCmd, ResetCmd>
 * @endcode
 */
template<typename DataType, typename Registry>
struct GetCommandsFor {
private:
    // Check if DataType is actually DataWithCommands
    static constexpr bool is_data_with_commands = detail::IsDataWithCommands<DataType>::value;
    
public:
    // If DataWithCommands, extract commands; otherwise empty tuple
    using type = std::conditional_t<
        is_data_with_commands,
        detail::extract_commands_t<DataType>,
        std::tuple<>
    >;
};

template<typename DataType, typename Registry>
using get_commands_for_t = typename GetCommandsFor<DataType, Registry>::type;

// ============================================================================
// Payload Type Extraction
// ============================================================================

/**
 * @brief Extract payload types from a tuple of MessageDefinitions
 * 
 * Converts std::tuple<MessageDef1, MessageDef2, ...> to 
 * std::tuple<Payload1, Payload2, ...>
 * 
 * @tparam MessageDefsTuple Tuple of MessageDefinitions
 * @return Tuple of payload types
 */
template<typename MessageDefsTuple>
using extract_payloads_t = detail::extract_payloads_t<MessageDefsTuple>;

/**
 * @brief Get all payload types in a registry
 * 
 * @tparam Registry MessageRegistry to query
 * @return std::tuple<PayloadType1, PayloadType2, ...>
 * 
 * Note: This can also use Registry::PayloadTypes directly
 */
template<typename Registry>
using get_all_payloads_t = typename Registry::PayloadTypes;

// ============================================================================
// Request/Reply Queries
// ============================================================================

/**
 * @brief Check if a MessageDefinition is a request (has reply)
 */
template<typename MessageDef>
struct IsRequest {
    static constexpr bool value = MessageDef::has_reply && MessageDef::is_request;
};

template<typename MessageDef>
inline constexpr bool is_request_v = IsRequest<MessageDef>::value;

/**
 * @brief Check if a MessageDefinition is a reply
 * 
 * Reply messages are identified by having local_id > MAX_MESSAGE_ID (sign bit set).
 * When a request with ID N is created with a reply, the reply gets ID = -N (two's complement),
 * which appears as a large positive number (> MAX_MESSAGE_ID = 0x7FFF).
 */
template<typename MessageDef>
struct IsReply {
    static constexpr bool value = (MessageDef::local_id > MAX_MESSAGE_ID);
};

template<typename MessageDef>
inline constexpr bool is_reply_v = IsReply<MessageDef>::value;

/**
 * @brief Filter registry to only request messages
 */
template<typename Registry>
struct FilterRequests {
private:
    template<typename T>
    using Predicate = IsRequest<T>;
    
    using MessageDefs = typename Registry::MessageDefsTuple;
    
public:
    using type = detail::filter_tuple_t<Predicate, MessageDefs>;
};

template<typename Registry>
using filter_requests_t = typename FilterRequests<Registry>::type;

/**
 * @brief Filter registry to only reply messages
 */
template<typename Registry>
struct FilterReplies {
private:
    template<typename T>
    using Predicate = IsReply<T>;
    
    using MessageDefs = typename Registry::MessageDefsTuple;
    
public:
    using type = detail::filter_tuple_t<Predicate, MessageDefs>;
};

template<typename Registry>
using filter_replies_t = typename FilterReplies<Registry>::type;

// ============================================================================
// Tuple Utilities
// ============================================================================

// Use std::tuple_size_v directly (no custom wrapper needed)

// ============================================================================
// Convenience Functions (Runtime-Usable)
// ============================================================================

/**
 * @brief Get count of messages matching a category
 * 
 * Runtime helper that returns the number of messages in a filtered category.
 * 
 * @code
 * size_t num_data = data_message_count<MyApp>();
 * size_t num_cmds = command_message_count<MyApp>();
 * @endcode
 */
template<typename Registry>
constexpr size_t data_message_count() {
    return std::tuple_size_v<data_messages_t<Registry>>;
}

template<typename Registry>
constexpr size_t command_message_count() {
    return std::tuple_size_v<command_messages_t<Registry>>;
}

template<typename Registry>
constexpr size_t subscription_message_count() {
    return std::tuple_size_v<subscription_messages_t<Registry>>;
}

template<typename Registry>
constexpr size_t request_message_count() {
    return std::tuple_size_v<filter_requests_t<Registry>>;
}

template<typename Registry>
constexpr size_t reply_message_count() {
    return std::tuple_size_v<filter_replies_t<Registry>>;
}

// ============================================================================
// MessageDef Lookup by Payload Type
// ============================================================================

/**
 * @brief Find MessageDefinition for a given payload type in registry
 * 
 * Searches the registry's MessageDefsTuple for a MessageDef whose Payload matches.
 * Returns void if not found.
 * 
 * @tparam PayloadT Payload type to search for
 * @tparam Registry MessageRegistry to search in
 * @return MessageDefinition type or void if not found
 */
template<typename PayloadT, typename Registry>
struct FindMessageDefForPayload {
private:
    template<typename MessageDef>
    static constexpr bool matches = std::is_same_v<typename MessageDef::Payload, PayloadT>;
    
    template<typename Tuple, size_t Index = 0>
    struct FindImpl;
    
    template<typename... MessageDefs, size_t Index>
    struct FindImpl<std::tuple<MessageDefs...>, Index> {
        using type = void;  // Not found
    };
    
    template<typename... MessageDefs, size_t Index>
        requires (Index < sizeof...(MessageDefs))
    struct FindImpl<std::tuple<MessageDefs...>, Index> {
    private:
        using Current = std::tuple_element_t<Index, std::tuple<MessageDefs...>>;
        
    public:
        using type = std::conditional_t<
            matches<Current>,
            Current,
            typename FindImpl<std::tuple<MessageDefs...>, Index + 1>::type
        >;
    };
    
public:
    using type = typename FindImpl<typename Registry::MessageDefsTuple>::type;
};

template<typename PayloadT, typename Registry>
using find_message_def_t = typename FindMessageDefForPayload<PayloadT, Registry>::type;

/**
 * @brief Check if payload type has a MessageDef in registry
 */
template<typename PayloadT, typename Registry>
struct HasMessageDef {
    static constexpr bool value = !std::is_same_v<find_message_def_t<PayloadT, Registry>, void>;
};

template<typename PayloadT, typename Registry>
inline constexpr bool has_message_def_v = HasMessageDef<PayloadT, Registry>::value;

/**
 * @brief Get reply payload type for a request payload type
 * 
 * Finds the MessageDef for RequestPayload, validates it has a reply,
 * then extracts the reply payload type.
 * 
 * @tparam RequestPayload Request payload type
 * @tparam Registry MessageRegistry
 * @return Reply payload type or void if not a request
 */
template<typename RequestPayload, typename Registry>
struct GetReplyPayloadFor {
private:
    using RequestDef = find_message_def_t<RequestPayload, Registry>;
    
    // Check if request def found and has reply
    static constexpr bool has_valid_reply = 
        !std::is_same_v<RequestDef, void> && RequestDef::has_reply;
    
public:
    using type = std::conditional_t<
        has_valid_reply,
        typename RequestDef::ReplyMessageDef::Payload,
        void
    >;
};

template<typename RequestPayload, typename Registry>
using get_reply_payload_t = typename GetReplyPayloadFor<RequestPayload, Registry>::type;

/**
 * @brief Check if a payload type is a request (has reply)
 */
template<typename PayloadT, typename Registry>
struct IsRequestPayload {
private:
    using MessageDef = find_message_def_t<PayloadT, Registry>;
    
public:
    static constexpr bool value = 
        !std::is_same_v<MessageDef, void> && 
        MessageDef::has_reply && 
        MessageDef::is_request;
};

template<typename PayloadT, typename Registry>
inline constexpr bool is_request_payload_v = IsRequestPayload<PayloadT, Registry>::value;

// ============================================================================
// Debug/Introspection Helpers
// ============================================================================

/**
 * @brief Print message category statistics (compile-time validation)
 * 
 * Use with static_assert to validate registry composition at compile-time.
 * 
 * @code
 * static_assert(data_message_count<MyApp>() > 0, "Must have at least one data message");
 * static_assert(command_message_count<MyApp>() > 0, "Must have at least one command");
 * @endcode
 */
template<typename Registry>
struct RegistryStats {
    static constexpr size_t total_messages = Registry::size();
    static constexpr size_t data_messages = data_message_count<Registry>();
    static constexpr size_t command_messages = command_message_count<Registry>();
    static constexpr size_t subscription_messages = subscription_message_count<Registry>();
    static constexpr size_t request_messages = request_message_count<Registry>();
    static constexpr size_t reply_messages = reply_message_count<Registry>();
    
    // Validate consistency
    static_assert(data_messages + command_messages + subscription_messages <= total_messages,
                  "Category counts should not exceed total messages");
};

} // namespace registry
} // namespace commrat
