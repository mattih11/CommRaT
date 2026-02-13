/**
 * @file introspection_example.cpp
 * @brief Demonstrates CommRaT introspection - exporting message schemas
 * 
 * Shows how to:
 * 1. Export single message schema to JSON
 * 2. Export all message schemas to JSON
 * 3. Write schemas to file
 * 4. Access compile-time metadata
 */

#include <commrat/commrat.hpp>
#include <commrat/introspection.hpp>
#include <iostream>

// Example message types
struct TemperatureData {
    float temperature_celsius;
    uint32_t sensor_id;
    uint64_t timestamp_us;
};

struct StatusData {
    uint32_t status_code;
    float cpu_load;
    sertial::fixed_string<64> message;
};

struct CommandData {
    uint32_t command_id;
    sertial::fixed_vector<uint8_t, 32> parameters;
};

// Define application with message types
using MyApp = commrat::CommRaT<
    commrat::MessageDefinition<TemperatureData>,
    commrat::MessageDefinition<StatusData>,
    commrat::MessageDefinition<CommandData>
>;

int main() {
    std::cout << "=== CommRaT Introspection Example ===\n\n";
    
    // ========================================================================
    // 1. Export single message schema to JSON
    // ========================================================================
    std::cout << "1. Single Message Schema (TemperatureData):\n";
    auto temp_schema = MyApp::Introspection::export_as<TemperatureData>();
    std::cout << temp_schema << "\n\n";
    
    // ========================================================================
    // 2. Access compile-time metadata directly
    // ========================================================================
    std::cout << "2. Compile-Time Metadata:\n";
    
    using TempSchema = commrat::MessageSchema<TemperatureData, MyApp>;
    auto schema = TempSchema{};
    
    std::cout << "  Message ID: " << schema.commrat.message_id << "\n";
    std::cout << "  Payload Type: " << schema.commrat.payload_type << "\n";
    std::cout << "  Full Type: " << schema.commrat.full_type << "\n";
    std::cout << "  Max Message Size: " << schema.commrat.max_message_size << " bytes\n";
    std::cout << "  Registry: " << schema.commrat.registry_name << "\n";
    
    std::cout << "\n  Layout Info:\n";
    std::cout << "    Base Packed Size: " << schema.layout.base_packed_size << " bytes\n";
    std::cout << "    Max Packed Size: " << schema.layout.max_packed_size << " bytes\n";
    std::cout << "    Num Fields: " << schema.layout.num_fields << "\n";
    std::cout << "    Has Variable Fields: " 
              << (schema.layout.has_variable_fields ? "yes" : "no") << "\n\n";
    
    // ========================================================================
    // 3. Export all message schemas
    // ========================================================================
    std::cout << "3. All Message Schemas (JSON):\n";
    auto all_schemas = MyApp::Introspection::export_all();
    std::cout << all_schemas << "\n\n";
    
    // ========================================================================
    // 4. Write schemas to file
    // ========================================================================
    std::cout << "4. Writing schemas to file...\n";
    MyApp::Introspection::write_to_file("message_schemas.json");
    std::cout << "   Schemas written to: message_schemas.json\n\n";
    
    // ========================================================================
    // 5. Compare different message types
    // ========================================================================
    std::cout << "5. Message Type Comparison:\n";
    
    constexpr auto temp_id = MyApp::get_message_id<TemperatureData>();
    constexpr auto status_id = MyApp::get_message_id<StatusData>();
    constexpr auto cmd_id = MyApp::get_message_id<CommandData>();
    
    std::cout << "  TemperatureData ID: " << temp_id << "\n";
    std::cout << "  StatusData ID:      " << status_id << "\n";
    std::cout << "  CommandData ID:     " << cmd_id << "\n\n";
    
    std::cout << "=== Example Complete ===\n";
    return 0;
}
