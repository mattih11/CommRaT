#pragma once

#include <cstdint>
#include <type_traits>
#include <span>
#include <cstring>

// Real SeRTial includes
#include <sertial/sertial.hpp>
#include <sertial/containers/fixed_vector.hpp>
#include <sertial/containers/fixed_string.hpp>

namespace commrat {

// TIMS message header (required by TIMS protocol)
struct TimsHeader {
    uint32_t msg_type;
    uint32_t msg_size;      // Will be set by serialization
    uint64_t timestamp;     // Will be set by send()
    uint32_t seq_number;    // Will be set by send()
    uint32_t flags;
};

// Message type enumeration
enum class MessageType : uint32_t {
    DATA = 1,
    COMMAND = 2,
    STATUS = 3,
    ERROR = 4,
    ACK = 5,
    SENSOR_DATA = 6,
    ROBOT_STATE = 7,
    USER_DEFINED = 1000  // User messages start here
};

// ============================================================================
// Message Type Traits (Automatic Type-to-ID Mapping)
// ============================================================================

// Default: user must specialize this for custom payloads
template<typename PayloadT>
struct message_type_for {
    // No default - forces users to specify via specialization or use built-in types
};

// ============================================================================
// Templated Message Wrapper (No MsgType parameter needed!)
// ============================================================================

// Generic message wrapper combining TIMS header with user payload
// Must be aggregate type for reflection (no constructors!)
template<typename PayloadT>
struct TimsMessage {
    TimsHeader header;
    PayloadT payload;
    
    // Compile-time message type from payload type
    static constexpr MessageType message_type = message_type_for<PayloadT>::value;
    using payload_type = PayloadT;
};

// ============================================================================
// Payload Type Definitions (using SeRTial containers)
// ============================================================================

// Command payload
struct CommandPayload {
    uint32_t command_id;
    uint32_t target_id;
    sertial::fixed_vector<uint8_t, 64> parameters;
};
template<> struct message_type_for<CommandPayload> { 
    static constexpr MessageType value = MessageType::COMMAND; 
};

// Status payload
struct StatusPayload {
    uint32_t status_code;
    uint32_t subsystem_id;
    float cpu_load;
    float memory_usage;
    sertial::fixed_string<64> description;
};
template<> struct message_type_for<StatusPayload> { 
    static constexpr MessageType value = MessageType::STATUS; 
};

// Error payload
struct ErrorPayload {
    uint32_t error_code;
    uint32_t source_id;
    sertial::fixed_string<128> error_text;
};
template<> struct message_type_for<ErrorPayload> { 
    static constexpr MessageType value = MessageType::ERROR; 
};

// Acknowledgment payload
struct AckPayload {
    uint32_t acked_seq_number;
    uint32_t ack_code;
};
template<> struct message_type_for<AckPayload> { 
    static constexpr MessageType value = MessageType::ACK; 
};

// Sensor data payload (example with dynamic data)
struct SensorPayload {
    uint32_t sensor_id;
    float temperature;
    float pressure;
    float humidity;
    sertial::fixed_vector<float, 16> additional_readings;
};
template<> struct message_type_for<SensorPayload> { 
    static constexpr MessageType value = MessageType::SENSOR_DATA; 
};

// Robot state payload (example nested structures)
struct RobotStatePayload {
    struct Pose {
        double x, y, z;
        double roll, pitch, yaw;
    } pose;
    
    struct Velocity {
        double linear_x, linear_y, linear_z;
        double angular_x, angular_y, angular_z;
    } velocity;
    
    uint32_t robot_id;
    sertial::fixed_string<32> status;
};
template<> struct message_type_for<RobotStatePayload> { 
    static constexpr MessageType value = MessageType::ROBOT_STATE; 
};

// ============================================================================
// Convenient Type Aliases
// ============================================================================

using CommandMessage = TimsMessage<CommandPayload>;
using StatusMessage = TimsMessage<StatusPayload>;
using ErrorMessage = TimsMessage<ErrorPayload>;
using AckMessage = TimsMessage<AckPayload>;
using SensorMessage = TimsMessage<SensorPayload>;
using RobotStateMessage = TimsMessage<RobotStatePayload>;

// Generic data message for any type
template<typename T>
using DataMessage = TimsMessage<T>;

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
    
    // Automatically set message type from template parameter
    message.header.msg_type = static_cast<uint32_t>(T::message_type);
    
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

// Internal functions for compatibility (used by MessageService)
template<typename T>
auto serialize_message(T& message) -> typename sertial::Message<T>::Result {
    return serialize(message);
}

template<typename T>
auto deserialize_message(std::span<const std::byte> data) -> sertial::DeserializeResult<T> {
    return deserialize<T>(data);
}

template<typename T>
auto deserialize_message(const uint8_t* data, size_t size) -> sertial::DeserializeResult<T> {
    return deserialize<T>(data, size);
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