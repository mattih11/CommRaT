#include "commrat/message_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace commrat;

int main(int argc, char** argv) {
    std::cout << "CommRaT Message Sender Example\n";
    std::cout << "==============================\n\n";
    
    // Parse command line arguments
    uint32_t dest_mailbox_id = 2; // Default receiver ID
    if (argc > 1) {
        dest_mailbox_id = std::stoi(argv[1]);
    }
    
    // Configure the sender mailbox
    TimsConfig config;
    config.mailbox_name = "sender";
    config.mailbox_id = 1;
    config.max_msg_size = 4096;
    config.priority = 10;
    config.realtime = true;
    
    // Create message service
    MessageService service(config);
    
    if (!service.start()) {
        std::cerr << "Failed to start message service!\n";
        return 1;
    }
    
    std::cout << "Message service started (mailbox ID: " << config.mailbox_id << ")\n";
    std::cout << "Sending to mailbox ID: " << dest_mailbox_id << "\n\n";
    
    // Send different types of messages
    uint32_t count = 0;
    
    while (count < 10) {
        // Send a status message
        {
            StatusMessage status;
            status.payload.status_code = 100 + count;
            status.payload.subsystem_id = 42;
            status.payload.cpu_load = 0.5f + (count * 0.05f);
            status.payload.memory_usage = 0.3f + (count * 0.02f);
            status.payload.description.assign("System running normally");
            
            auto result = service.send(status, dest_mailbox_id);
            
            if (result == TimsResult::SUCCESS) {
                std::cout << "Sent StatusMessage #" << count 
                          << " (seq: " << status.header.seq_number << ")\n";
                std::cout << "  CPU: " << status.payload.cpu_load * 100 << "%"
                          << ", Memory: " << status.payload.memory_usage * 100 << "%\n";
                std::cout << "  Description: " << status.payload.description.c_str() << "\n";
            } else {
                std::cerr << "Failed to send StatusMessage!\n";
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Send a command message
        {
            CommandMessage cmd;
            cmd.payload.command_id = 1000 + count;
            cmd.payload.target_id = 200;
            cmd.payload.parameters.push_back(static_cast<uint8_t>(count));
            cmd.payload.parameters.push_back(0xFF);
            cmd.payload.parameters.push_back(0xAA);
            
            auto result = service.send(cmd, dest_mailbox_id);
            
            if (result == TimsResult::SUCCESS) {
                std::cout << "Sent CommandMessage #" << count 
                          << " (seq: " << cmd.header.seq_number << ")\n";
                std::cout << "  Command ID: " << cmd.payload.command_id 
                          << ", Target: " << cmd.payload.target_id 
                          << ", Params: " << cmd.payload.parameters.size() << "\n";
            } else {
                std::cerr << "Failed to send CommandMessage!\n";
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Send a sensor data message
        {
            SensorMessage sensor;
            sensor.payload.sensor_id = 7;
            sensor.payload.temperature = 20.0f + count;
            sensor.payload.pressure = 1013.25f + (count * 0.5f);
            sensor.payload.humidity = 50.0f + (count * 2.0f);
            
            // Add some additional readings
            for (uint32_t i = 0; i < 5; ++i) {
                sensor.payload.additional_readings.push_back(static_cast<float>(count + i) * 0.1f);
            }
            
            auto result = service.send(sensor, dest_mailbox_id);
            
            if (result == TimsResult::SUCCESS) {
                std::cout << "Sent SensorMessage #" << count 
                          << " (seq: " << sensor.header.seq_number << ")\n";
                std::cout << "  Temp: " << sensor.payload.temperature << "°C"
                          << ", Pressure: " << sensor.payload.pressure << " hPa"
                          << ", Humidity: " << sensor.payload.humidity << "%\n";
                std::cout << "  Additional readings: " << sensor.payload.additional_readings.size() << "\n";
            } else {
                std::cerr << "Failed to send SensorMessage!\n";
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        ++count;
    }
    
    std::cout << "\nSent " << service.get_messages_sent() << " messages total\n";
    
    // Cleanup
    service.stop();
    
    std::cout << "Sender stopped.\n";
    
    return 0;
}
