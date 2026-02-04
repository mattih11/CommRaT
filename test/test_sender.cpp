#include "commrat/message_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <receiver_mailbox_id>\n";
        return 1;
    }
    
    uint32_t receiver_id = std::stoul(argv[1]);
    
    try {
        // Configure sender mailbox
        commrat::TimsConfig config;
        config.mailbox_name = "test_sender";
        config.mailbox_id = 100;  // Sender uses mailbox 100
        config.max_msg_size = 4096;
        config.priority = 10;
        config.realtime = false;  // Start with non-RT for testing
        
        std::cout << "Initializing sender on mailbox " << config.mailbox_id << "...\n";
        
        commrat::MessageService service(config);
        service.start();
        
        std::cout << "Sender started. Sending messages to mailbox " << receiver_id << "...\n";
        
        // Send different message types
        for (int i = 0; i < 5; i++) {
            // Send StatusMessage
            commrat::StatusMessage status;
            status.header = {};
            status.payload.status_code = 100 + i;
            status.payload.subsystem_id = 1;
            status.payload.cpu_load = 0.45f + (i * 0.05f);
            status.payload.memory_usage = 0.30f + (i * 0.03f);
            status.payload.description = "Test status message #" + std::to_string(i);
            
            std::cout << "\nSending StatusMessage #" << i << "\n";
            std::cout << "  Status code: " << status.payload.status_code << "\n";
            std::cout << "  CPU load: " << (status.payload.cpu_load * 100) << "%\n";
            std::cout << "  Description: " << status.payload.description.c_str() << "\n";
            
            service.send(status, receiver_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Send CommandMessage
            commrat::CommandMessage cmd;
            cmd.header = {};
            cmd.payload.command_id = 1000 + i;
            cmd.payload.target_id = 5;
            cmd.payload.parameters.push_back(static_cast<uint8_t>(i));
            cmd.payload.parameters.push_back(static_cast<uint8_t>(i * 2));
            cmd.payload.parameters.push_back(static_cast<uint8_t>(i * 3));
            
            std::cout << "\nSending CommandMessage #" << i << "\n";
            std::cout << "  Command ID: " << cmd.payload.command_id << "\n";
            std::cout << "  Target ID: " << cmd.payload.target_id << "\n";
            std::cout << "  Parameters: " << static_cast<int>(cmd.payload.parameters.size()) << "\n";
            
            service.send(cmd, receiver_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Send SensorMessage
            commrat::SensorMessage sensor;
            sensor.header = {};
            sensor.payload.sensor_id = 500 + i;
            sensor.payload.temperature = 20.5f + i;
            sensor.payload.pressure = 1013.25f + i;
            sensor.payload.humidity = 50.0f + i;
            sensor.payload.additional_readings.push_back(1.0f * i);
            sensor.payload.additional_readings.push_back(2.0f * i);
            
            std::cout << "\nSending SensorMessage #" << i << "\n";
            std::cout << "  Sensor ID: " << sensor.payload.sensor_id << "\n";
            std::cout << "  Temperature: " << sensor.payload.temperature << "\n";
            std::cout << "  Pressure: " << sensor.payload.pressure << "\n";
            
            service.send(sensor, receiver_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        std::cout << "\nAll messages sent. Shutting down...\n";
        service.stop();
        
        std::cout << "\nSender completed successfully.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
