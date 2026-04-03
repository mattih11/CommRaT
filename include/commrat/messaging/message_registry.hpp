#pragma once

#include "../messages.hpp"
#include "message_id.hpp"
#include "system/data_request_messages.hpp"  // For GetDataRequest, GetNextDataRequest
#include <type_traits>
#include <tuple>
#include <optional>
#include <span>

namespace commrat {

// Forward declarations
template<typename UserRegistry, typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
class Module;
template<typename... MessageDefs> class Mailbox;
template<typename... MessageDefs> class MessageRegistry;

// ============================================================================
// Registry to Mailbox Type Extraction
// ============================================================================

/**
 * @brief Helper to detect if a type has ::Registry member (CommRaT pattern)
 */
template<typename T, typename = void>
struct HasRegistryMember : std::false_type {};

template<typename T>
struct HasRegistryMember<T, std::void_t<typename T::Registry>> : std::true_type {};

/**
 * @brief Extract underlying Mailbox type from Registry
 * 
 * Handles both MessageRegistry<MessageDefs...> and CommRaT<MessageDefs...>:
 * - If Registry has ::Registry member (CommRaT), recurse to extract it
 * - MessageRegistry<...> directly extracts to Mailbox<...>
 * 
 * Usage:
 *   template<typename Registry>
 *   void foo(MailboxFor<Registry>& mbx) { ... }
 * 
 * This allows foo() to work with both MessageRegistry and CommRaT.
 */
template<typename R, typename Enable = void>
struct ExtractMailboxType;

// Case 1: Registry has ::Registry member (CommRaT) - recurse
template<typename R>
struct ExtractMailboxType<R, std::enable_if_t<HasRegistryMember<R>::value>> {
    using type = typename ExtractMailboxType<typename R::Registry, void>::type;
};

// Case 2: MessageRegistry<...> - extract template parameters directly
template<typename... MessageDefs>
struct ExtractMailboxType<MessageRegistry<MessageDefs...>, std::enable_if_t<!HasRegistryMember<MessageRegistry<MessageDefs...>>::value>> {
    using type = Mailbox<MessageDefs...>;
};

/**
 * @brief Helper alias for extracting Mailbox type from Registry
 * 
 * Use this instead of Mailbox<Registry> to support both MessageRegistry and CommRaT.
 */
template<typename Registry>
using MailboxFor = typename ExtractMailboxType<Registry>::type;

// ============================================================================
// GetData Message Expansion (for Message::Data types)
// ============================================================================

/**
 * @brief Detect if a MessageDefinition is a Data message (UserDefined::Data)
 * 
 * Data messages automatically get GetDataRequest and GetNextDataRequest with same local_id.
 */
template<typename T>
struct has_get_data_support {
    static constexpr bool value = T::prefix == MessagePrefix::UserDefined && 
                                  T::subprefix == static_cast<uint8_t>(UserSubPrefix::Data);
};

/**
 * @brief Expand Message::Data types to include GetData protocol messages
 * 
 * For each Message::Data<T>, automatically adds:
 *   - GetDataRequest<T>
 *   - GetDataReply<T> (via ExpandReplies)
 *   - GetNextDataRequest<T>
 *   - GetNextDataReply<T> (via ExpandReplies)
 */
template<typename... MessageDefs>
struct ExpandGetDataMessages;

template<>
struct ExpandGetDataMessages<> {
    using Result = std::tuple<>;
};

template<typename First, typename... Rest>
struct ExpandGetDataMessages<First, Rest...> {
private:
    using RestExpanded = typename ExpandGetDataMessages<Rest...>::Result;
    
public:
    // If First is Message::Data<T> (has GetDataRequestDef), add GetData messages
    using Result = std::conditional_t<
        has_get_data_support<First>::value,
        // Add First + GetDataRequest<T> + GetNextDataRequest<T> + Rest
        decltype(std::tuple_cat(
            std::declval<std::tuple<
                First,
                typename First::GetDataRequestDef,
                typename First::GetNextDataRequestDef
            >>(),
            std::declval<RestExpanded>()
        )),
        // Otherwise just add First + Rest
        decltype(std::tuple_cat(
            std::declval<std::tuple<First>>(),
            std::declval<RestExpanded>()
        ))
    >;
};

// ============================================================================
// Request-Reply Message Expansion
// ============================================================================

/**
 * @brief Expand message definitions to include reply messages
 * 
 * For each MessageDefinition with has_reply = true, adds its ReplyMessageDef
 * to the registry automatically.
 */
template<typename... MessageDefs>
struct ExpandReplies;

template<>
struct ExpandReplies<> {
    using Result = std::tuple<>;
};

template<typename First, typename... Rest>
struct ExpandReplies<First, Rest...> {
private:
    using RestExpanded = typename ExpandReplies<Rest...>::Result;
    
public:
    using Result = std::conditional_t<
        First::has_reply,
        decltype(std::tuple_cat(
            std::declval<std::tuple<First, typename First::ReplyMessageDef>>(),
            std::declval<RestExpanded>()
        )),
        decltype(std::tuple_cat(
            std::declval<std::tuple<First>>(),
            std::declval<RestExpanded>()
        ))
    >;
};

// ============================================================================
// Message ID Auto-Increment System
// ============================================================================

/**
 * @brief Safely extract ReplyPayload from a MessageDefinition, defaulting to void.
 *
 * DataWithCommands and other non-MessageDefinition types lack ReplyPayload,
 * so this helper avoids a hard compile error when they pass through AutoAssignIDsProcess.
 */
template<typename T, typename = void>
struct GetReplyPayload {
    using type = void;
};

template<typename T>
struct GetReplyPayload<T, std::void_t<typename T::ReplyPayload>> {
    using type = typename T::ReplyPayload;
};

/**
 * @brief Helper to assign auto-incremented IDs to messages marked with needs_auto_id
 * 
 * Forward processing: assigns ID to First, then recursively processes Rest with First in context.
 */
template<typename... MessageDefs>
struct AutoAssignIDs;

template<>
struct AutoAssignIDs<> {
    using Result = std::tuple<>;
};

// Forward declarations
template<typename ProcessedTuple, typename... Remaining>
struct AutoAssignIDsProcess;

// Base case: no more messages to process
template<typename... ProcessedDefs>
struct AutoAssignIDsProcess<std::tuple<ProcessedDefs...>> {
    using Result = std::tuple<>;
};

// Recursive case: process First, then Rest
template<typename... ProcessedDefs, typename First, typename... Rest>
struct AutoAssignIDsProcess<std::tuple<ProcessedDefs...>, First, Rest...> {
private:
    // Track highest ID for this prefix/subprefix (ignoring reply messages with negative IDs)
    template<typename MessageDef>
    struct HighestID {
        static constexpr uint16_t value = []() constexpr {
            if constexpr (sizeof...(ProcessedDefs) == 0) {
                return 0;
            } else {
                uint16_t max_id = 0;
                ((max_id = (ProcessedDefs::prefix == MessageDef::prefix && 
                           ProcessedDefs::subprefix == MessageDef::subprefix &&
                           !ProcessedDefs::is_reply &&  // Ignore reply messages
                           ProcessedDefs::local_id > max_id) ? 
                           ProcessedDefs::local_id : max_id), ...);
                return max_id;
            }
        }();
    };
    
    // Assign ID to current message
    // Preserve ReplyPayload so request/reply pairing survives auto-ID assignment.
    // Use GetReplyPayload<> to safely default to void for types without it (e.g. DataWithCommands).
    using CurrentProcessed = std::conditional_t<
        First::needs_auto_id,
        MessageDefinition<
            typename First::Payload,
            First::prefix,
            First::subprefix,
            HighestID<First>::value + 1,
            typename GetReplyPayload<First>::type  // preserves reply type; void if not present
        >,
        First
    >;
    
    // Validate auto-assigned ID doesn't exceed MAX_MESSAGE_ID
    static_assert(!CurrentProcessed::needs_auto_id || CurrentProcessed::local_id <= MAX_MESSAGE_ID,
                  "Auto-assigned message ID exceeds MAX_MESSAGE_ID (0x7FFF)");
    
    // If this is a Message::Data type (check ORIGINAL First), create GetData messages with SAME local_id
    using CurrentWithGetData = std::conditional_t<
        has_get_data_support<First>::value,
        // Add CurrentProcessed + GetDataRequest<T, ID> + GetNextDataRequest<T, ID>
        std::tuple<
            CurrentProcessed,
            GetDataRequest<typename CurrentProcessed::Payload, CurrentProcessed::local_id>,
            GetNextDataRequest<typename CurrentProcessed::Payload, CurrentProcessed::local_id>
        >,
        // Otherwise just CurrentProcessed
        std::tuple<CurrentProcessed>
    >;
    
    using RestResult = typename AutoAssignIDsProcess<std::tuple<ProcessedDefs..., CurrentProcessed>, Rest...>::Result;
    
public:
    using Result = decltype(std::tuple_cat(
        std::declval<CurrentWithGetData>(),
        std::declval<RestResult>()
    ));
};

template<typename... MessageDefs>
struct AutoAssignIDs {
    using Result = typename AutoAssignIDsProcess<std::tuple<>, MessageDefs...>::Result;
};

// ============================================================================
// Compile-Time Message ID Collision Detection
// ============================================================================

template<typename... MessageDefs>
struct CheckCollisions {
    static constexpr bool check() {
        if constexpr (sizeof...(MessageDefs) <= 1) {
            return true;
        } else {
            return check_all_pairs<MessageDefs...>();
        }
    }
    
private:
    template<typename First, typename Second, typename... Rest>
    static constexpr bool check_all_pairs() {
        constexpr uint32_t id1 = make_message_id(
            static_cast<uint8_t>(First::prefix),
            First::subprefix,
            First::local_id
        );
        constexpr uint32_t id2 = make_message_id(
            static_cast<uint8_t>(Second::prefix),
            Second::subprefix,
            Second::local_id
        );
        
        static_assert(id1 != id2, "Message ID collision detected!");
        
        if constexpr (sizeof...(Rest) > 0) {
            return check_all_pairs<First, Rest...>() && check_all_pairs<Second, Rest...>();
        }
        return true;
    }
};

// ============================================================================
// Compile-Time Message Type Registry
// ============================================================================

/**
 * @brief Compile-time message type registry using MessageDefinition templates
 * 
 * This registry uses the new MessageDefinition system for compile-time message ID
 * generation, auto-increment, collision detection, and type-safe dispatch.
 * 
 * Usage:
 *   using MyRegistry = MessageRegistry<
 *       MessageDefinition<StatusData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
 *       MessageDefinition<CommandMessage, MessagePrefix::UserDefined, UserSubPrefix::Commands>,
 *       MessageDefinition<SubscribeRequest, MessagePrefix::System, SystemSubPrefix::Subscription, 0x0001>
 *   >;
 *   
 *   // Auto-increment and collision detection happen at compile-time
 *   // max_message_size calculated from all payload types
 */
template<typename... MessageDefs>
class MessageRegistry {
private:
    // Step 1: Auto-assign IDs to user messages (includes automatic GetData message creation)
    // AutoAssignIDs now handles GetData message creation with matching IDs
    using IDsAssigned = typename AutoAssignIDs<MessageDefs...>::Result;
    
    // Step 2: Expand request messages to include their replies
    template<typename Tuple>
    struct TupleToList;
    
    template<typename... Defs>
    struct TupleToList<std::tuple<Defs...>> {
        using Expanded = typename ExpandReplies<Defs...>::Result;
    };
    
    using ProcessedDefs = typename TupleToList<IDsAssigned>::Expanded;
    
    // Extract payload types from MessageDefinitions
    template<typename Tuple>
    struct ExtractPayloads;
    
    template<typename... Defs>
    struct ExtractPayloads<std::tuple<Defs...>> {
        using PayloadTypes = std::tuple<typename Defs::Payload...>;
    };
    
    // Check for ID collisions at compile-time
    static constexpr bool collisions_checked = CheckCollisions<MessageDefs...>::check();

public:
    // Message definitions tuple - exposed for introspection and utilities
    using MessageDefsTuple = ProcessedDefs;
    
    // Payload types tuple - exposed for introspection
    using PayloadTypes = typename ExtractPayloads<ProcessedDefs>::PayloadTypes;
    
    // Helper to check if a payload type is in the registry
    template<typename T, typename Tuple>
    struct IsInTuple;
    
    template<typename T, typename... Types>
    struct IsInTuple<T, std::tuple<Types...>> {
        static constexpr bool value = (std::is_same_v<T, Types> || ...);
    };
    
    template<typename T>
    static constexpr bool is_registered_v = IsInTuple<T, PayloadTypes>::value;

    // Number of registered message types (use size() for actual count after expansion)
    static constexpr size_t num_types = sizeof...(MessageDefs);
    
    // Actual size after expansion (replies, GetData, etc.)
    static constexpr size_t size() {
        return std::tuple_size_v<MessageDefsTuple>;
    }
    
    // Maximum message size across all registered types (for buffer allocation)
    // NOTE: We calculate size of TimsMessage<Payload> not just Payload, because that's what gets serialized
    template<typename... Payloads>
    static constexpr size_t calc_max_size(std::tuple<Payloads...>*) {
        return std::max({sertial::Message<TimsMessage<Payloads>>::max_buffer_size...});
    }
    
    static constexpr size_t max_message_size = calc_max_size(static_cast<PayloadTypes*>(nullptr));
    
    /**
     * @brief Calculate maximum message size for specific payload types
     * 
     * This allows creating typed mailboxes with correct buffer sizes instead of
     * using max_message_size for all mailboxes (which wastes memory).
     * 
     * @tparam SpecificTypes Subset of payload types
     * @return Maximum serialized size among the specified types
     * 
     * Example:
     * @code
     * // Only need buffer for ResetCmd (16 bytes) and CalibrateCmd (24 bytes)
     * constexpr size_t cmd_buffer_size = Registry::max_size_for_types<ResetCmd, CalibrateCmd>();
     * // Result: 24 bytes instead of Registry::max_message_size (e.g., 2048 bytes)
     * @endcode
     */
    template<typename... SpecificTypes>
    static constexpr size_t max_size_for_types() {
        static_assert(sizeof...(SpecificTypes) > 0, 
                      "max_size_for_types requires at least one type");
        
        // Validate all types are registered
        static_assert((is_registered_v<SpecificTypes> && ...), 
                      "All types must be registered in the message registry");
        
        // Calculate max size for the specified types
        return std::max({sertial::Message<TimsMessage<SpecificTypes>>::max_buffer_size...});
    }

private:
    // Helper to get MessageDefinition for a payload type
    template<typename PayloadT, typename Tuple>
    struct FindMessageDef;
    
    template<typename PayloadT, typename... Defs>
    struct FindMessageDef<PayloadT, std::tuple<Defs...>> {
    private:
        template<typename Def>
        static constexpr bool matches = std::is_same_v<typename Def::Payload, PayloadT>;
        
        template<typename First, typename... Rest>
        static constexpr auto find_impl() {
            if constexpr (matches<First>) {
                return First{};
            } else if constexpr (sizeof...(Rest) > 0) {
                return find_impl<Rest...>();
            } else {
                return void{};
            }
        }
        
    public:
        using type = decltype(find_impl<Defs...>());
    };
    
    // Helper to get message ID for a payload type
    template<typename PayloadT>
    static constexpr uint32_t get_message_id_for() {
        using MessageDef = typename FindMessageDef<PayloadT, ProcessedDefs>::type;
        return make_message_id(
            static_cast<uint8_t>(MessageDef::prefix),
            MessageDef::subprefix,
            MessageDef::local_id
        );
    }
    
    // Helper to get index of payload type in tuple
    template<typename T, typename Tuple, size_t Index = 0>
    struct TypeIndex;
    
    template<typename T, typename... Types, size_t Index>
    struct TypeIndex<T, std::tuple<Types...>, Index> {
        static constexpr size_t value = []() constexpr {
            size_t idx = num_types;
            size_t current = 0;
            ((std::is_same_v<T, Types> ? (idx = current, 0) : (++current, 0)), ...);
            return idx;
        }();
    };
    
    template<typename T>
    static constexpr size_t type_index() {
        return TypeIndex<T, PayloadTypes>::value;
    }
    
    // Helper to get payload type by message ID at runtime
    template<uint32_t ID, size_t Index = 0, typename Tuple = ProcessedDefs>
    struct TypeByID;
    
    template<uint32_t ID, size_t Index, typename... Defs>
    struct TypeByID<ID, Index, std::tuple<Defs...>> {
        using type = void;
        static constexpr bool found = false;
    };
    
    template<uint32_t ID, size_t Index, typename... Defs>
        requires (Index < sizeof...(Defs))
    struct TypeByID<ID, Index, std::tuple<Defs...>> {
    private:
        using CurrentDef = std::tuple_element_t<Index, std::tuple<Defs...>>;
        static constexpr uint32_t current_id = make_message_id(
            static_cast<uint8_t>(CurrentDef::prefix),
            CurrentDef::subprefix,
            CurrentDef::local_id
        );
        
    public:
        using type = std::conditional_t<
            current_id == ID,
            typename CurrentDef::Payload,
            typename TypeByID<ID, Index + 1>::type
        >;
        static constexpr bool found = (current_id == ID) || 
                                      TypeByID<ID, Index + 1>::found;
    };

public:
    // ========================================================================
    // Type Traits
    // ========================================================================
    
    /**
     * @brief Check if a payload type is registered
     */
    template<typename T>
    static constexpr bool is_registered = is_registered_v<T>;
    
    /**
     * @brief Get the message ID for a given payload type
     */
    template<typename T>
        requires is_registered_v<T>
    static constexpr uint32_t get_message_id() {
        return get_message_id_for<T>();
    }
    
    /**
     * @brief Get the payload type by message ID (compile-time)
     */
    template<uint32_t ID>
    using PayloadTypeFor = typename TypeByID<ID, 0>::type;
    
    /**
     * @brief Check if a message ID is registered
     */
    template<uint32_t ID>
    static constexpr bool has_message_id = TypeByID<ID, 0>::found;
    
    // ========================================================================
    // Serialization Interface (Compile-Time Type-Safe)
    // ========================================================================
    
    /**
     * @brief Serialize a message with automatic type registration check
     * 
     * This function provides compile-time type safety - will not compile
     * if the message type is not registered in the registry.
     * 
     * @param message The message to serialize
     * @return SerializeResult containing buffer and size
     */
    template<typename T>
        requires is_registered_v<T>
    static auto serialize(T& message) {
        // Set message ID from MessageDefinition
        message.header.msg_type = get_message_id<T>();
        
        // Use SeRTial's serialization
        auto result = serialize_message(message);
        
        // Update header with actual serialized size
        message.header.msg_size = static_cast<uint32_t>(result.size);
        
        return result;
    }
    
    /**
     * @brief Serialize a TimsMessage<PayloadT> wrapper
     * 
     * Overload for TimsMessage<PayloadT> where PayloadT is registered.
     * This enables payload-only registration while still serializing
     * the full message with header.
     */
    template<typename PayloadT>
        requires is_registered_v<PayloadT>
    static auto serialize(TimsMessage<PayloadT>& message) {
        // Set message ID from MessageDefinition of payload type
        message.header.msg_type = get_message_id<PayloadT>();
        
        // Use SeRTial directly (bypass message_type check in serialize_message)
        auto result = sertial::Message<TimsMessage<PayloadT>>::serialize(message);
        
        // Update header with actual serialized size
        message.header.msg_size = static_cast<uint32_t>(result.size);
        
        return result;
    }
    
    /**
     * @brief Deserialize a message with known type at compile time
     * 
     * Use this when you know the message type at compile time.
     * For payload types, deserializes to TimsMessage<T> and extracts payload.
     * 
     * @param data Buffer containing serialized message
     * @return DeserializeResult containing the deserialized payload
     */
    template<typename T>
        requires is_registered_v<T> && (!requires { typename T::payload_type; })
    static auto deserialize(std::span<const std::byte> data) {
        // T is a payload type - deserialize to TimsMessage<T> and extract payload
        using MsgType = TimsMessage<T>;
        auto msg_result = sertial::Message<MsgType>::deserialize(data);
        
        // Transform DeserializeResult<TimsMessage<T>> to DeserializeResult<T>
        if (msg_result) {
            // Extract payload and return it wrapped
            return std::make_optional(std::move(msg_result->payload));
        }
        
        // Return empty optional
        return std::optional<T>{};
    }
    
    /**
     * @brief Deserialize a TimsMessage<PayloadT> wrapper
     * 
     * Overload for TimsMessage<PayloadT> where PayloadT is registered.
     * This enables payload-only registration while still deserializing
     * the full message with header.
     */
    template<typename MsgT>
        requires requires { typename MsgT::payload_type; } && 
                 is_registered_v<typename MsgT::payload_type>
    static auto deserialize(std::span<const std::byte> data) {
        // Use SeRTial directly to deserialize TimsMessage (bypass message_type check)
        return sertial::Message<MsgT>::deserialize(data);
    }
    
    // ========================================================================
    // Runtime Dispatch (Visitor Pattern)
    // ========================================================================
    
    /**
     * @brief Visit a message by its message ID using a visitor
     * 
     * This provides runtime dispatch when the message type is not known at
     * compile time. Uses template expansion to generate efficient switch-like
     * behavior without virtual functions.
     * 
     * The visitor should accept any message type in the registry:
     *   auto visitor = [](auto&& msg) {
     *       using MsgType = std::decay_t<decltype(msg)>;
     *       // Handle message...
     *   };
     * 
     * @param msg_id The message ID (32-bit)
     * @param data Buffer containing serialized message
     * @param visitor Visitor function/lambda that accepts any message type
     * @return true if message was successfully deserialized and visited
     */
    template<typename Visitor>
    static bool visit(uint32_t msg_id, std::span<const std::byte> data, Visitor&& visitor) {
        return visit_impl<0>(msg_id, data, std::forward<Visitor>(visitor));
    }
    
    /**
     * @brief Deserialize message by ID and dispatch to callback
     * 
     * This is a convenience wrapper around visit() for consistency with older API.
     * 
     * @param msg_id The message ID (32-bit)
     * @param data Buffer containing serialized message
     * @param callback Callback function that accepts any message type
     * @return true if message was successfully deserialized and dispatched
     */
    template<typename Callback>
    static bool dispatch(uint32_t msg_id, std::span<const std::byte> data, Callback&& callback) {
        return visit(msg_id, data, std::forward<Callback>(callback));
    }
    
    // ========================================================================
    // Compile-Time Information
    // ========================================================================
    
    /**
     * @brief Get maximum buffer size needed for any message in the registry
     */
    static constexpr size_t max_buffer_size() {
        return max_message_size;
    }
    
    /**
     * @brief Get list of all message IDs in the registry
     */
    template<typename... Defs>
    static constexpr auto get_all_ids_impl(std::tuple<Defs...>*) {
        return std::array<uint32_t, sizeof...(Defs)>{
            make_message_id(
                static_cast<uint8_t>(Defs::prefix),
                Defs::subprefix,
                Defs::local_id
            )...
        };
    }
    
    static constexpr auto message_ids() {
        return get_all_ids_impl(static_cast<ProcessedDefs*>(nullptr));
    }
    
    // ========================================================================
    // Phase 6 Multi-Input Helpers
    // ========================================================================
    
    /**
     * @brief Get payload type at index I
     */
    template<std::size_t I>
    using type_at = std::tuple_element_t<I, PayloadTypes>;
    
    /**
     * @brief Get type index for payload type T
     */
    template<typename T>
    static constexpr std::size_t get_type_index() {
        return type_index<T>();
    }
    
    // ========================================================================
    // System Infrastructure (for Module2 compatibility)
    // ========================================================================
    
    /**
     * @brief System infrastructure placeholder
     * 
     * Provides basic WorkMailbox type for Module2.
     * CommRaT specializes this with subscription-protocol-specific mailbox.
     */
    struct System {
        // Generic WorkMailbox - unrestricted (sends/receives any registered type)
        using WorkMailbox = Mailbox<MessageDefs...>;
    };
    
private:
    // Recursive visitor implementation
    template<size_t Index, typename Visitor, typename... Defs>
    static bool visit_impl_helper(uint32_t msg_id, std::span<const std::byte> data, 
                                   Visitor&& visitor, std::tuple<Defs...>*) {
        if constexpr (Index >= sizeof...(Defs)) {
            // Message ID not found in registry
            return false;
        } else {
            using CurrentDef = std::tuple_element_t<Index, std::tuple<Defs...>>;
            using CurrentPayload = typename CurrentDef::Payload;
            
            constexpr uint32_t current_id = make_message_id(
                static_cast<uint8_t>(CurrentDef::prefix),
                CurrentDef::subprefix,
                CurrentDef::local_id
            );
            
            if (msg_id == current_id) {
                // Found matching ID - deserialize TimsMessage<Payload> wrapper using Registry
                auto result = deserialize<TimsMessage<CurrentPayload>>(data);
                if (result) {
                    // Visit with the full TimsMessage wrapper
                    std::forward<Visitor>(visitor)(*result);
                    return true;
                }
                return false;
            }
            
            // Try next type
            return visit_impl_helper<Index + 1>(msg_id, data, 
                                                std::forward<Visitor>(visitor), 
                                                static_cast<std::tuple<Defs...>*>(nullptr));
        }
    }
    
    template<size_t Index, typename Visitor>
    static bool visit_impl(uint32_t msg_id, std::span<const std::byte> data, Visitor&& visitor) {
        return visit_impl_helper<Index>(msg_id, data, std::forward<Visitor>(visitor),
                                        static_cast<ProcessedDefs*>(nullptr));
    }
};

// ============================================================================
// Helper Functions for Convenience
// ============================================================================

/**
 * @brief Create a message registry from a list of MessageDefinition types
 * 
 * Usage:
 *   using MyRegistry = make_registry<
 *       MessageDefinition<StatusData, MessagePrefix::UserDefined, UserSubPrefix::Data>,
 *       MessageDefinition<SubscribeRequest, MessagePrefix::System, SystemSubPrefix::Subscription, 0x0001>
 *   >;
 */
template<typename... MessageDefs>
using make_registry = MessageRegistry<MessageDefs...>;

/**
 * @brief Type alias for creating custom registries
 */
template<typename... MessageDefs>
using CustomRegistry = MessageRegistry<MessageDefs...>;

} // namespace commrat
