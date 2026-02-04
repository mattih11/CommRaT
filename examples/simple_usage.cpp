#include "commrat/commrat.hpp"
#include <iostream>

using namespace commrat;

int main() {
    std::cout << "CommRaT Simple Usage Examples\n";
    std::cout << "=============================\n\n";
    
    // ========================================================================
    // Example 1: Direct Serialization (User-Facing API)
    // ========================================================================
    
    std::cout << "=== Example 1: Simple Serialize/Deserialize ===\n\n";
    
    // Create a status message
    StatusMessage status;
    status.payload.status_code = 200;
    status.payload.subsystem_id = 42;
    status.payload.cpu_load = 0.65f;
    status.payload.memory_usage = 0.42f;
    status.payload.description = "System operating normally";
    
    // Serialize - automatic type ID setting
    auto result = serialize(status);
    
    std::cout << "Serialized StatusMessage:\n";
    std::cout << "  Message type: " << status.header.msg_type 
              << " (automatically set to " << static_cast<uint32_t>(MessageType::STATUS) << ")\n";
    std::cout << "  Message size: " << result.size << " bytes\n\n";
    
    // Deserialize with known type
    auto deserialized = deserialize<StatusMessage>(
        std::span{result.buffer.data(), result.size}
    );
    
    if (deserialized) {
        std::cout << "Deserialized StatusMessage:\n";
        std::cout << "  Status code: " << deserialized->payload.status_code << "\n";
        std::cout << "  CPU load: " << (deserialized->payload.cpu_load * 100) << "%\n";
        std::cout << "  Description: " << deserialized->payload.description.c_str() << "\n\n";
    }
    
    // ========================================================================
    // Example 2: Different Message Types
    // ========================================================================
    
    std::cout << "=== Example 2: Different Message Types ===\n\n";
    
    // Command message
    CommandMessage cmd;
    cmd.payload.command_id = 1001;
    cmd.payload.target_id = 200;
    cmd.payload.parameters.push_back(0x01);
    cmd.payload.parameters.push_back(0x02);
    cmd.payload.parameters.push_back(0x03);
    
    auto cmd_result = serialize(cmd);
    std::cout << "CommandMessage:\n";
    std::cout << "  Type ID: " << cmd.header.msg_type << "\n";
    std::cout << "  Size: " << cmd_result.size << " bytes\n\n";
    
    // Sensor message
    SensorMessage sensor;
    sensor.payload.sensor_id = 500;
    sensor.payload.temperature = 22.5f;
    sensor.payload.pressure = 1013.25f;
    sensor.payload.humidity = 45.0f;
    sensor.payload.additional_readings.push_back(1.0f);
    sensor.payload.additional_readings.push_back(2.0f);
    
    auto sensor_result = serialize(sensor);
    std::cout << "SensorMessage:\n";
    std::cout << "  Type ID: " << sensor.header.msg_type << "\n";
    std::cout << "  Size: " << sensor_result.size << " bytes\n";
    std::cout << "  Temperature: " << sensor.payload.temperature << "°C\n\n";
    
    // ========================================================================
    // Example 3: Custom Message Types
    // ========================================================================
    
    std::cout << "=== Example 3: Custom Message Types ===\n\n";
    
    std::cout << "See examples/registry_demo.cpp for custom message type examples.\n";
    std::cout << "Custom types require template specialization outside of function scope.\n\n";
    
    // ========================================================================
    // Example 4: Compile-Time Information
    // ========================================================================
    
    std::cout << "=== Example 4: Compile-Time Type Information ===\n\n";
    
    std::cout << "Built-in message types:\n";
    std::cout << "  CommandMessage:     type=" << static_cast<uint32_t>(CommandMessage::message_type) 
              << ", max_size=" << max_message_buffer_size_v<CommandMessage> << " bytes\n";
    std::cout << "  StatusMessage:      type=" << static_cast<uint32_t>(StatusMessage::message_type)
              << ", max_size=" << max_message_buffer_size_v<StatusMessage> << " bytes\n";
    std::cout << "  SensorMessage:      type=" << static_cast<uint32_t>(SensorMessage::message_type)
              << ", max_size=" << max_message_buffer_size_v<SensorMessage> << " bytes\n";
    std::cout << "  ErrorMessage:       type=" << static_cast<uint32_t>(ErrorMessage::message_type)
              << ", max_size=" << max_message_buffer_size_v<ErrorMessage> << " bytes\n";
    std::cout << "  AckMessage:         type=" << static_cast<uint32_t>(AckMessage::message_type)
              << ", max_size=" << max_message_buffer_size_v<AckMessage> << " bytes\n";
    std::cout << "  RobotStateMessage:  type=" << static_cast<uint32_t>(RobotStateMessage::message_type)
              << ", max_size=" << max_message_buffer_size_v<RobotStateMessage> << " bytes\n\n";
    
    // Compile-time checks
    std::cout << "Compile-time type checks:\n";
    std::cout << "  is_commrat_message_v<StatusMessage>: " 
              << (is_commrat_message_v<StatusMessage> ? "true" : "false") << "\n";
    std::cout << "  is_commrat_message_v<int>: " 
              << (is_commrat_message_v<int> ? "true" : "false") << "\n";
    std::cout << "  StatusMessage::message_type == MessageType::STATUS: " 
              << (StatusMessage::message_type == MessageType::STATUS ? "true" : "false") << "\n\n";
    
    std::cout << "All examples completed successfully!\n";
    std::cout << "\nKey Points:\n";
    std::cout << "  - serialize() automatically sets msg_type from payload type\n";
    std::cout << "  - deserialize<T>() knows the type at compile time\n";
    std::cout << "  - Type-to-ID mapping via message_type_for<PayloadT> trait\n";
    std::cout << "  - TimsMessage<Payload> - just one template parameter!\n";
    std::cout << "  - No manual ID specification needed\n";
    std::cout << "  - Zero runtime overhead for known types\n";
    
    return 0;
}
