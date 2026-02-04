#pragma once

#include "messages.hpp"
#include <type_traits>
#include <tuple>
#include <optional>
#include <span>

namespace commrat {

// ============================================================================
// Compile-Time Message Type Registry
// ============================================================================

/**
 * @brief Compile-time message type registry for efficient type-based dispatch
 * 
 * This registry allows compile-time mapping between message types and their
 * corresponding MessageType enum values, enabling zero-overhead type dispatch
 * without runtime lookups or virtual functions.
 * 
 * Usage:
 *   // Register custom message types
 *   using MyRegistry = MessageRegistry<
 *       StatusMessage,
 *       CommandMessage,
 *       SensorMessage,
 *       MyCustomMessage
 *   >;
 *   
 *   // Serialize with type deduction
 *   auto result = MyRegistry::serialize(my_message);
 *   
 *   // Deserialize by type enum
 *   auto msg = MyRegistry::deserialize<StatusMessage>(buffer);
 *   
 *   // Deserialize by MessageType enum (runtime dispatch)
 *   auto visitor = [](auto&& msg) { handle_message(msg); };
 *   MyRegistry::visit(MessageType::STATUS, buffer, visitor);
 */
template<typename... MessageTypes>
class MessageRegistry {
private:
    // Type list for message types
    using TypeList = std::tuple<MessageTypes...>;
    
    // Number of registered message types
    static constexpr size_t num_types = sizeof...(MessageTypes);
    
    // Helper to check if a type is in the registry
    template<typename T>
    static constexpr bool is_registered_v = (std::is_same_v<T, MessageTypes> || ...);

public:
    // Maximum message size across all registered types (for buffer allocation)
    static constexpr size_t max_message_size = std::max({sertial::Message<MessageTypes>::max_buffer_size...});

private:
    
    // Helper to get index of type in tuple
    template<typename T, size_t Index = 0>
    static constexpr size_t type_index() {
        if constexpr (Index >= num_types) {
            return num_types; // Not found
        } else if constexpr (std::is_same_v<T, std::tuple_element_t<Index, TypeList>>) {
            return Index;
        } else {
            return type_index<T, Index + 1>();
        }
    }
    
    // Helper to get type by MessageType enum at compile time
    template<MessageType MT, size_t Index = 0>
    struct TypeByMessageType {
        using type = void;
        static constexpr bool found = false;
    };
    
    template<MessageType MT, size_t Index>
        requires (Index < num_types)
    struct TypeByMessageType<MT, Index> {
    private:
        using CurrentType = std::tuple_element_t<Index, TypeList>;
        static constexpr MessageType current_msg_type = CurrentType::message_type;
        
    public:
        using type = std::conditional_t<
            current_msg_type == MT,
            CurrentType,
            typename TypeByMessageType<MT, Index + 1>::type
        >;
        static constexpr bool found = (current_msg_type == MT) || 
                                      TypeByMessageType<MT, Index + 1>::found;
    };

public:
    // ========================================================================
    // Type Traits
    // ========================================================================
    
    /**
     * @brief Check if a message type is registered
     */
    template<typename T>
    static constexpr bool is_registered = is_registered_v<T>;
    
    /**
     * @brief Get the MessageType enum for a given message type
     */
    template<typename T>
        requires is_registered_v<T>
    static constexpr MessageType get_message_type() {
        return T::message_type;
    }
    
    /**
     * @brief Get the message type by MessageType enum (compile-time)
     */
    template<MessageType MT>
    using MessageTypeFor = typename TypeByMessageType<MT>::type;
    
    /**
     * @brief Check if a MessageType enum is registered
     */
    template<MessageType MT>
    static constexpr bool has_message_type = TypeByMessageType<MT>::found;
    
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
        // Ensure msg_type is set correctly based on template parameter
        message.header.msg_type = static_cast<uint32_t>(T::message_type);
        
        // Use SeRTial's serialization
        auto result = serialize_message(message);
        
        // Update header with actual serialized size
        message.header.msg_size = static_cast<uint32_t>(result.size);
        
        return result;
    }
    
    /**
     * @brief Deserialize a message with known type at compile time
     * 
     * Use this when you know the message type at compile time.
     * This is the most efficient path with zero runtime overhead.
     * 
     * @param data Buffer containing serialized message
     * @return DeserializeResult containing the deserialized message
     */
    template<typename T>
        requires is_registered_v<T>
    static auto deserialize(std::span<const std::byte> data) {
        return deserialize_message<T>(data);
    }
    
    // ========================================================================
    // Runtime Dispatch (Visitor Pattern)
    // ========================================================================
    
    /**
     * @brief Visit a message by its MessageType enum using a visitor
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
     * @param msg_type The MessageType enum value
     * @param data Buffer containing serialized message
     * @param visitor Visitor function/lambda that accepts any message type
     * @return true if message was successfully deserialized and visited
     */
    template<typename Visitor>
    static bool visit(MessageType msg_type, std::span<const std::byte> data, Visitor&& visitor) {
        return visit_impl<0>(msg_type, data, std::forward<Visitor>(visitor));
    }
    
    /**
     * @brief Deserialize message by MessageType enum and return as optional
     * 
     * This is a convenience wrapper around visit() that returns the message
     * in a type-erased container. Less efficient than visit() but simpler API.
     * 
     * @param msg_type The MessageType enum value
     * @param data Buffer containing serialized message
     * @return Optional containing callback with deserialized message
     */
    template<typename Callback>
    static bool dispatch(MessageType msg_type, std::span<const std::byte> data, Callback&& callback) {
        return visit(msg_type, data, std::forward<Callback>(callback));
    }
    
    // ========================================================================
    // Compile-Time Information
    // ========================================================================
    
    /**
     * @brief Get maximum buffer size needed for any message in the registry
     */
    static constexpr size_t max_buffer_size() {
        return max_of(max_message_buffer_size_v<MessageTypes>...);
    }
    
    /**
     * @brief Get number of registered message types
     */
    static constexpr size_t size() {
        return num_types;
    }
    
    /**
     * @brief Get list of all MessageType enum values in the registry
     */
    static constexpr std::array<MessageType, num_types> message_types() {
        return {MessageTypes::message_type...};
    }
    
private:
    // Helper for max value at compile time
    static constexpr size_t max_of() { return 0; }
    
    template<typename T, typename... Rest>
    static constexpr size_t max_of(T first, Rest... rest) {
        size_t rest_max = max_of(rest...);
        return first > rest_max ? first : rest_max;
    }
    
    // Recursive visitor implementation
    template<size_t Index, typename Visitor>
    static bool visit_impl(MessageType msg_type, std::span<const std::byte> data, Visitor&& visitor) {
        if constexpr (Index >= num_types) {
            // Message type not found in registry
            return false;
        } else {
            using CurrentType = std::tuple_element_t<Index, TypeList>;
            
            if (msg_type == CurrentType::message_type) {
                // Found matching type - deserialize and visit
                auto result = deserialize_message<CurrentType>(data);
                if (result) {
                    std::forward<Visitor>(visitor)(*result);
                    return true;
                }
                return false;
            }
            
            // Try next type
            return visit_impl<Index + 1>(msg_type, data, std::forward<Visitor>(visitor));
        }
    }
};

// ============================================================================
// Default Registry with Built-in Message Types
// ============================================================================

/**
 * @brief Default message registry with all built-in CommRaT message types
 * 
 * This registry includes all the standard message types defined in messages.hpp.
 * Use this for convenience, or create your own registry with custom types.
 */
using DefaultMessageRegistry = MessageRegistry<
    CommandMessage,
    StatusMessage,
    ErrorMessage,
    AckMessage,
    SensorMessage,
    RobotStateMessage
>;

// ============================================================================
// Helper Functions for Convenience
// ============================================================================

/**
 * @brief Create a message registry from a list of message types
 * 
 * Usage:
 *   auto registry = make_registry<StatusMessage, CommandMessage>();
 */
template<typename... MessageTypes>
constexpr auto make_registry() {
    return MessageRegistry<MessageTypes...>{};
}

/**
 * @brief Type alias for creating custom registries
 */
template<typename... MessageTypes>
using CustomRegistry = MessageRegistry<MessageTypes...>;

} // namespace commrat
