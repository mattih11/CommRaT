#include "commrat/message_service.hpp"
#include <iostream>

using namespace commrat;

// ============================================================================
// Define Custom Message Payloads
// ============================================================================

// Simple struct payload
struct TemperatureReading {
    uint32_t sensor_id;
    float temperature;
    uint64_t reading_time;
};

// Payload with SeRTial containers
struct LogEntry {
    uint32_t log_level;
    uint64_t timestamp;
    sertial::fixed_string<256> message;
    sertial::fixed_string<64> source;
};

// Complex nested payload
struct RobotCommand {
    enum class CommandType : uint32_t {
        MOVE = 1,
        STOP = 2,
        ROTATE = 3,
        GRIPPER = 4
    };
    
    CommandType cmd_type;
    uint32_t robot_id;
    
    struct MotionParams {
        double velocity;
        double acceleration;
        sertial::fixed_vector<double, 3> target_position;
    } motion;
    
    sertial::fixed_vector<uint8_t, 32> custom_data;
};

// Payload with dynamic array data
struct PointCloud {
    uint32_t frame_id;
    uint64_t timestamp;
    
    struct Point {
        float x, y, z;
        uint8_t intensity;
    };
    
    sertial::fixed_vector<Point, 1024> points;
};

// ============================================================================
// Create Typed Messages
// ============================================================================

// Method 1: Using type alias
using TempMessage = Message<TemperatureReading, MessageType::SENSOR_DATA>;

// Method 2: Direct instantiation
using LogMessage = Message<LogEntry, MessageType::USER_DEFINED>;

// Method 3: Custom message type enum
constexpr MessageType ROBOT_CMD_TYPE = static_cast<MessageType>(MessageType::USER_DEFINED + 1);
using RobotCmdMessage = Message<RobotCommand, ROBOT_CMD_TYPE>;

constexpr MessageType POINT_CLOUD_TYPE = static_cast<MessageType>(MessageType::USER_DEFINED + 2);
using PointCloudMessage = Message<PointCloud, POINT_CLOUD_TYPE>;

// ============================================================================
// Main Demo
// ============================================================================

int main() {
    std::cout << "CommRaT Custom Messages Demo\n";
    std::cout << "============================\n\n";
    
    // ========================================================================
    // Part 1: Compile-Time Type Information
    // ========================================================================
    
    std::cout << "=== Compile-Time Type Analysis ===\n\n";
    
    std::cout << "TempMessage:\n";
    std::cout << "  Max buffer size: " << max_message_buffer_size_v<TempMessage> << " bytes\n";
    std::cout << "  Packed size: " << packed_message_size_v<TempMessage> << " bytes\n";
    std::cout << "  Has padding: " << (message_has_padding_v<TempMessage> ? "yes" : "no") << "\n\n";
    
    std::cout << "LogMessage:\n";
    std::cout << "  Max buffer size: " << max_message_buffer_size_v<LogMessage> << " bytes\n";
    std::cout << "  Packed size: " << packed_message_size_v<LogMessage> << " bytes\n";
    std::cout << "  Has padding: " << (message_has_padding_v<LogMessage> ? "yes" : "no") << "\n\n";
    
    std::cout << "RobotCmdMessage:\n";
    std::cout << "  Max buffer size: " << max_message_buffer_size_v<RobotCmdMessage> << " bytes\n";
    std::cout << "  Message type: " << static_cast<uint32_t>(RobotCmdMessage::message_type) << "\n\n";
    
    std::cout << "PointCloudMessage:\n";
    std::cout << "  Max buffer size: " << max_message_buffer_size_v<PointCloudMessage> << " bytes\n";
    std::cout << "  Packed size: " << packed_message_size_v<PointCloudMessage> << " bytes\n\n";
    
    // ========================================================================
    // Part 2: Creating and Using Messages
    // ========================================================================
    
    std::cout << "=== Creating Messages ===\n\n";
    
    // Simple message
    TempMessage temp_msg;
    temp_msg.payload.sensor_id = 42;
    temp_msg.payload.temperature = 23.5f;
    temp_msg.payload.reading_time = 1234567890;
    
    std::cout << "Created TempMessage:\n";
    std::cout << "  Sensor: " << temp_msg.payload.sensor_id << "\n";
    std::cout << "  Temp: " << temp_msg.payload.temperature << "°C\n\n";
    
    // Message with strings
    LogMessage log_msg;
    log_msg.payload.log_level = 2; // Warning
    log_msg.payload.timestamp = 9876543210;
    log_msg.payload.message.assign("System temperature exceeded threshold");
    log_msg.payload.source.assign("TempMonitor");
    
    std::cout << "Created LogMessage:\n";
    std::cout << "  Level: " << log_msg.payload.log_level << "\n";
    std::cout << "  Message: " << log_msg.payload.message.c_str() << "\n";
    std::cout << "  Source: " << log_msg.payload.source.c_str() << "\n\n";
    
    // Complex nested message
    RobotCmdMessage robot_msg;
    robot_msg.payload.cmd_type = RobotCommand::CommandType::MOVE;
    robot_msg.payload.robot_id = 5;
    robot_msg.payload.motion.velocity = 1.5;
    robot_msg.payload.motion.acceleration = 0.5;
    robot_msg.payload.motion.target_position.push_back(10.0);
    robot_msg.payload.motion.target_position.push_back(20.0);
    robot_msg.payload.motion.target_position.push_back(0.0);
    robot_msg.payload.custom_data.push_back(0xAA);
    robot_msg.payload.custom_data.push_back(0xBB);
    
    std::cout << "Created RobotCmdMessage:\n";
    std::cout << "  Robot: " << robot_msg.payload.robot_id << "\n";
    std::cout << "  Command: " << static_cast<uint32_t>(robot_msg.payload.cmd_type) << "\n";
    std::cout << "  Velocity: " << robot_msg.payload.motion.velocity << " m/s\n";
    std::cout << "  Target: [" 
              << robot_msg.payload.motion.target_position[0] << ", "
              << robot_msg.payload.motion.target_position[1] << ", "
              << robot_msg.payload.motion.target_position[2] << "]\n\n";
    
    // Message with many data points
    PointCloudMessage cloud_msg;
    cloud_msg.payload.frame_id = 100;
    cloud_msg.payload.timestamp = 1234567890;
    
    // Add some sample points
    for (uint32_t i = 0; i < 10; ++i) {
        PointCloud::Point pt;
        pt.x = static_cast<float>(i);
        pt.y = static_cast<float>(i * 2);
        pt.z = static_cast<float>(i / 2.0f);
        pt.intensity = static_cast<uint8_t>(i * 25);
        cloud_msg.payload.points.push_back(pt);
    }
    
    std::cout << "Created PointCloudMessage:\n";
    std::cout << "  Frame: " << cloud_msg.payload.frame_id << "\n";
    std::cout << "  Points: " << cloud_msg.payload.points.size() << "\n";
    std::cout << "  First point: [" 
              << cloud_msg.payload.points[0].x << ", "
              << cloud_msg.payload.points[0].y << ", "
              << cloud_msg.payload.points[0].z << "] intensity="
              << static_cast<int>(cloud_msg.payload.points[0].intensity) << "\n\n";
    
    // ========================================================================
    // Part 3: Serialization
    // ========================================================================
    
    std::cout << "=== Serialization ===\n\n";
    
    auto temp_serialized = serialize_message(temp_msg);
    std::cout << "TempMessage serialized to " << temp_serialized.size << " bytes\n";
    std::cout << "  Header msg_size updated to: " << temp_msg.header.msg_size << "\n\n";
    
    auto log_serialized = serialize_message(log_msg);
    std::cout << "LogMessage serialized to " << log_serialized.size << " bytes\n";
    std::cout << "  (Runtime size due to variable-length strings)\n\n";
    
    auto robot_serialized = serialize_message(robot_msg);
    std::cout << "RobotCmdMessage serialized to " << robot_serialized.size << " bytes\n\n";
    
    auto cloud_serialized = serialize_message(cloud_msg);
    std::cout << "PointCloudMessage serialized to " << cloud_serialized.size << " bytes\n";
    std::cout << "  (Runtime size due to variable point count: " 
              << cloud_msg.payload.points.size() << " points)\n\n";
    
    // ========================================================================
    // Part 4: Type Safety Demonstration
    // ========================================================================
    
    std::cout << "=== Type Safety ===\n\n";
    
    std::cout << "Static assertions ensure type safety:\n";
    std::cout << "  ✓ is_commrat_message_v<TempMessage> = " 
              << is_commrat_message_v<TempMessage> << "\n";
    std::cout << "  ✓ is_commrat_message_v<int> = " 
              << is_commrat_message_v<int> << "\n\n";
    
    std::cout << "Message type extraction:\n";
    std::cout << "  TempMessage::message_type = " 
              << static_cast<uint32_t>(TempMessage::message_type) << "\n";
    std::cout << "  RobotCmdMessage::message_type = " 
              << static_cast<uint32_t>(RobotCmdMessage::message_type) << "\n\n";
    
    // ========================================================================
    // Summary
    // ========================================================================
    
    std::cout << "=== Summary ===\n\n";
    std::cout << "Custom Message Features:\n";
    std::cout << "  [✓] Fully templated message types\n";
    std::cout << "  [✓] Compile-time type safety\n";
    std::cout << "  [✓] SeRTial fixed_vector and fixed_string containers\n";
    std::cout << "  [✓] Compile-time size calculations\n";
    std::cout << "  [✓] Runtime-length serialization\n";
    std::cout << "  [✓] Nested structs and complex payloads\n";
    std::cout << "  [✓] Zero-copy where possible\n";
    std::cout << "  [✓] TIMS compatibility maintained\n\n";
    
    return 0;
}
