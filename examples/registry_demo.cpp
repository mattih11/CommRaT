#include "commrat/commrat.hpp"
#include <iostream>
#include <iomanip>

using namespace commrat;

// ============================================================================
// Custom Payload Definitions (Must be at namespace scope)
// ============================================================================

// Define custom message types
struct TelemetryPayload {
    uint32_t vehicle_id;
    double latitude;
    double longitude;
    float altitude;
    float speed;
};

struct DiagnosticPayload {
    uint32_t component_id;
    sertial::fixed_string<128> diagnostic_code;
    sertial::fixed_vector<uint8_t, 64> diagnostic_data;
};

// Register custom payloads with message type IDs (in commrat namespace)
namespace commrat {
    template<> struct message_type_for<TelemetryPayload> {
        static constexpr MessageType value = MessageType::USER_DEFINED;
    };
    
    constexpr MessageType DIAGNOSTIC_TYPE = static_cast<MessageType>(
        static_cast<uint32_t>(MessageType::USER_DEFINED) + 1
    );
    
    template<> struct message_type_for<DiagnosticPayload> {
        static constexpr MessageType value = DIAGNOSTIC_TYPE;
    };
}

// Create custom message types
using TelemetryMessage = TimsMessage<TelemetryPayload>;
using DiagnosticMessage = TimsMessage<DiagnosticPayload>;

// ============================================================================
// Example 1: Using the Default Registry
// ============================================================================

void example_default_registry() {
    std::cout << "=== Example 1: Default Registry ===\n\n";
    
    // The default registry includes all built-in message types
    using Registry = DefaultMessageRegistry;
    
    // Display compile-time information
    std::cout << "Registry size: " << Registry::size() << " message types\n";
    std::cout << "Max buffer size: " << Registry::max_buffer_size() << " bytes\n\n";
    
    // Create and serialize a status message
    StatusMessage status;
    status.payload.status_code = 200;
    status.payload.subsystem_id = 42;
    status.payload.cpu_load = 0.65f;
    status.payload.memory_usage = 0.42f;
    status.payload.description = "System operating normally";
    
    // Serialize using registry (automatically sets msg_type)
    auto result = Registry::serialize(status);
    
    std::cout << "Serialized StatusMessage:\n";
    std::cout << "  Message type: " << status.header.msg_type << "\n";
    std::cout << "  Message size: " << result.size << " bytes\n";
    std::cout << "  Buffer capacity: " << result.buffer.max_size() << " bytes\n\n";
    
    // Deserialize with known type (compile-time, zero overhead)
    auto deserialized = Registry::deserialize<StatusMessage>(
        std::span{result.buffer.data(), result.size}
    );
    
    if (deserialized) {
        std::cout << "Deserialized StatusMessage:\n";
        std::cout << "  Status code: " << deserialized->payload.status_code << "\n";
        std::cout << "  CPU load: " << (deserialized->payload.cpu_load * 100) << "%\n";
        std::cout << "  Description: " << deserialized->payload.description.c_str() << "\n\n";
    }
}

// ============================================================================
// Example 2: Runtime Dispatch with Visitor Pattern
// ============================================================================

void example_runtime_dispatch() {
    std::cout << "=== Example 2: Runtime Dispatch ===\n\n";
    
    using Registry = DefaultMessageRegistry;
    
    // Create different message types and serialize them
    std::vector<std::pair<MessageType, std::vector<std::byte>>> messages;
    
    // Create status message
    {
        StatusMessage msg;
        msg.payload.status_code = 100;
        msg.payload.subsystem_id = 1;
        msg.payload.cpu_load = 0.45f;
        msg.payload.memory_usage = 0.30f;
        msg.payload.description = "Test status";
        
        auto result = Registry::serialize(msg);
        std::vector<std::byte> buffer(result.buffer.begin(), 
                                      result.buffer.begin() + result.size);
        messages.emplace_back(MessageType::STATUS, std::move(buffer));
    }
    
    // Create command message
    {
        CommandMessage msg;
        msg.payload.command_id = 1001;
        msg.payload.target_id = 200;
        msg.payload.parameters.push_back(0x01);
        msg.payload.parameters.push_back(0x02);
        msg.payload.parameters.push_back(0x03);
        
        auto result = Registry::serialize(msg);
        std::vector<std::byte> buffer(result.buffer.begin(), 
                                      result.buffer.begin() + result.size);
        messages.emplace_back(MessageType::COMMAND, std::move(buffer));
    }
    
    // Create sensor message
    {
        SensorMessage msg;
        msg.payload.sensor_id = 500;
        msg.payload.temperature = 22.5f;
        msg.payload.pressure = 1013.25f;
        msg.payload.humidity = 45.0f;
        msg.payload.additional_readings.push_back(1.0f);
        msg.payload.additional_readings.push_back(2.0f);
        
        auto result = Registry::serialize(msg);
        std::vector<std::byte> buffer(result.buffer.begin(), 
                                      result.buffer.begin() + result.size);
        messages.emplace_back(MessageType::SENSOR_DATA, std::move(buffer));
    }
    
    // Visit each message with a generic visitor
    std::cout << "Processing " << messages.size() << " messages:\n\n";
    
    for (const auto& [msg_type, buffer] : messages) {
        std::cout << "Message Type: " << static_cast<uint32_t>(msg_type) << "\n";
        
        // Generic visitor that handles any message type
        auto visitor = [](auto&& msg) {
            using MsgType = std::decay_t<decltype(msg)>;
            
            std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
            std::cout << "  Sequence: " << msg.header.seq_number << "\n";
            
            if constexpr (std::is_same_v<MsgType, StatusMessage>) {
                std::cout << "  Type: StatusMessage\n";
                std::cout << "  Status code: " << msg.payload.status_code << "\n";
                std::cout << "  CPU: " << (msg.payload.cpu_load * 100) << "%\n";
                std::cout << "  Description: " << msg.payload.description.c_str() << "\n";
            }
            else if constexpr (std::is_same_v<MsgType, CommandMessage>) {
                std::cout << "  Type: CommandMessage\n";
                std::cout << "  Command ID: " << msg.payload.command_id << "\n";
                std::cout << "  Target ID: " << msg.payload.target_id << "\n";
                std::cout << "  Params: " << msg.payload.parameters.size() << "\n";
            }
            else if constexpr (std::is_same_v<MsgType, SensorMessage>) {
                std::cout << "  Type: SensorMessage\n";
                std::cout << "  Sensor ID: " << msg.payload.sensor_id << "\n";
                std::cout << "  Temperature: " << msg.payload.temperature << "°C\n";
                std::cout << "  Pressure: " << msg.payload.pressure << " hPa\n";
            }
        };
        
        Registry::visit(msg_type, buffer, visitor);
        std::cout << "\n";
    }
}

// ============================================================================
// Example 3: Custom Message Registry
// ============================================================================

void example_custom_registry() {
    std::cout << "=== Example 3: Custom Registry ===\n\n";
    
    // Create a custom registry with only the messages we need
    using MyRegistry = MessageRegistry<
        StatusMessage,
        CommandMessage,
        TelemetryMessage,
        DiagnosticMessage
    >;
    
    std::cout << "Custom registry size: " << MyRegistry::size() << " types\n";
    std::cout << "Max buffer size: " << MyRegistry::max_buffer_size() << " bytes\n\n";
    
    // Create and serialize custom telemetry message
    TelemetryMessage telem;
    telem.payload.vehicle_id = 12345;
    telem.payload.latitude = 52.5200;
    telem.payload.longitude = 13.4050;
    telem.payload.altitude = 150.0f;
    telem.payload.speed = 25.5f;
    
    auto result = MyRegistry::serialize(telem);
    
    std::cout << "Telemetry Message:\n";
    std::cout << "  Message type: " << telem.header.msg_type << "\n";
    std::cout << "  Size: " << result.size << " bytes\n";
    std::cout << "  Vehicle: " << telem.payload.vehicle_id << "\n";
    std::cout << "  Position: (" << telem.payload.latitude << ", " 
              << telem.payload.longitude << ")\n";
    std::cout << "  Altitude: " << telem.payload.altitude << " m\n";
    std::cout << "  Speed: " << telem.payload.speed << " m/s\n\n";
    
    // Compile-time type checking
    static_assert(MyRegistry::is_registered<TelemetryMessage>, 
                  "TelemetryMessage should be registered");
    static_assert(MyRegistry::is_registered<DiagnosticMessage>, 
                  "DiagnosticMessage should be registered");
    // This would fail at compile time:
    // static_assert(MyRegistry::is_registered<SensorMessage>);
    
    std::cout << "Compile-time checks passed!\n\n";
}

// ============================================================================
// Example 4: Compile-Time Type Information
// ============================================================================

void example_compile_time_info() {
    std::cout << "=== Example 4: Compile-Time Information ===\n\n";
    
    using Registry = DefaultMessageRegistry;
    
    // Get all registered message types
    auto types = Registry::message_types();
    
    std::cout << "Registered message types:\n";
    for (const auto& type : types) {
        std::cout << "  - Type " << static_cast<uint32_t>(type) << "\n";
    }
    std::cout << "\n";
    
    // Display size information for each message type
    std::cout << "Message size information:\n";
    std::cout << "  StatusMessage: " 
              << max_message_buffer_size_v<StatusMessage> << " bytes\n";
    std::cout << "  CommandMessage: " 
              << max_message_buffer_size_v<CommandMessage> << " bytes\n";
    std::cout << "  SensorMessage: " 
              << max_message_buffer_size_v<SensorMessage> << " bytes\n";
    std::cout << "  ErrorMessage: " 
              << max_message_buffer_size_v<ErrorMessage> << " bytes\n";
    std::cout << "  AckMessage: " 
              << max_message_buffer_size_v<AckMessage> << " bytes\n";
    std::cout << "  RobotStateMessage: " 
              << max_message_buffer_size_v<RobotStateMessage> << " bytes\n\n";
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "CommRaT Message Registry Examples\n";
    std::cout << "==================================\n\n";
    
    try {
        example_default_registry();
        std::cout << std::string(60, '-') << "\n\n";
        
        example_runtime_dispatch();
        std::cout << std::string(60, '-') << "\n\n";
        
        example_custom_registry();
        std::cout << std::string(60, '-') << "\n\n";
        
        example_compile_time_info();
        
        std::cout << "All examples completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
