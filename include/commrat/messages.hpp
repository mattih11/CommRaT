#pragma once

#include <cstdint>
#include <type_traits>
#include <span>
#include <cstring>

// SeRTial serialization
#include <sertial/sertial.hpp>

namespace commrat {

// TIMS message header (required by TIMS protocol)
// Mirrors RACK's tims_msg_head structure
struct TimsHeader {
    uint32_t msg_type;
    uint32_t msg_size;      // Will be set by serialization
    uint64_t timestamp;     // Will be set by send()
    uint32_t seq_number;    // Will be set by send()
    uint32_t dest;          // Destination mailbox address
    uint32_t src;           // Source mailbox address (for replies)
    uint32_t flags;
};

// Message type ID
using MessageType = uint32_t;

// ============================================================================
// Templated Message Wrapper (No MsgType parameter needed!)
// ============================================================================

// Generic message wrapper combining TIMS header with user payload
// Must be aggregate type for reflection (no constructors!)
template<typename PayloadT>
struct TimsMessage {
    TimsHeader header;
    PayloadT payload;
    
    // Payload type accessor
    using payload_type = PayloadT;
};

// ============================================================================
// Type Traits
// ============================================================================

template<typename T>
struct is_commrat_message : std::false_type {};

template<typename P>
struct is_commrat_message<TimsMessage<P>> : std::true_type {};

template<typename T>
inline constexpr bool is_commrat_message_v = is_commrat_message<T>::value;

// Extract payload type
template<typename T>
struct message_payload;

template<typename P>
struct message_payload<TimsMessage<P>> {
    using type = P;
};

template<typename T>
using message_payload_t = typename message_payload<T>::type;

// ============================================================================
// Serialization Functions (Clean User Interface)
// ============================================================================

// Primary user-facing serialization interface
template<typename T>
auto serialize(T& message) -> typename sertial::Message<T>::Result {
    static_assert(is_commrat_message_v<T>, "T must be a CommRaT message type");
    
    // Check if T has message_type (old-style messages)
    if constexpr (requires { T::message_type; }) {
        // Automatically set message type from template parameter
        message.header.msg_type = static_cast<uint32_t>(T::message_type);
    }
    // For TimsMessage<PayloadT> without message_type, assume it's already set by Registry
    
    // Use SeRTial's serialization
    auto result = sertial::Message<T>::serialize(message);
    
    // Update header with actual serialized size
    message.header.msg_size = static_cast<uint32_t>(result.size);
    
    return result;
}

// Primary user-facing deserialization interface
template<typename T>
auto deserialize(std::span<const std::byte> data) -> sertial::DeserializeResult<T> {
    static_assert(is_commrat_message_v<T>, "T must be a CommRaT message type");
    
    // Use SeRTial's deserialization
    return sertial::Message<T>::deserialize(data);
}

// Helper for uint8_t* buffers (TIMS compatibility)
template<typename T>
auto deserialize(const uint8_t* data, size_t size) -> sertial::DeserializeResult<T> {
    // Convert uint8_t* to std::byte span for SeRTial
    auto byte_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(data), size);
    return deserialize<T>(byte_span);
}

// ============================================================================
// Compile-Time Utilities
// ============================================================================

// Get maximum buffer size needed for a message type at compile time
template<typename T>
inline constexpr size_t max_message_buffer_size_v = sertial::Message<T>::max_buffer_size;

// Get packed size (without padding) at compile time
template<typename T>
inline constexpr size_t packed_message_size_v = sertial::Message<T>::packed_size;

// Check if message has padding
template<typename T>
inline constexpr bool message_has_padding_v = sertial::Message<T>::has_padding;

} // namespace commrat