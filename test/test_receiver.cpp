#include "commrat/message_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

std::atomic<bool> running{true};

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down...\n";
    running = false;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <mailbox_id>\n";
        return 1;
    }
    
    uint32_t mailbox_id = std::stoul(argv[1]);
    
    // Setup signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Configure receiver mailbox
        commrat::TimsConfig config;
        config.mailbox_name = "test_receiver";
        config.mailbox_id = mailbox_id;
        config.max_msg_size = 4096;
        config.priority = 10;
        config.realtime = false;  // Start with non-RT for testing
        
        std::cout << "Initializing receiver on mailbox " << mailbox_id << "...\n";
        
        commrat::MessageService service(config);
        
        std::atomic<int> status_count{0};
        std::atomic<int> command_count{0};
        std::atomic<int> sensor_count{0};
        std::atomic<int> error_count{0};
        
        // Register handler for StatusMessage
        service.register_handler<commrat::StatusMessage>(
            [&status_count](const commrat::StatusMessage& msg) {
                status_count++;
                std::cout << "\n[StatusMessage] Received:\n";
                std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
                std::cout << "  Seq: " << msg.header.seq_number << "\n";
                std::cout << "  Status code: " << msg.payload.status_code << "\n";
                std::cout << "  CPU load: " << (msg.payload.cpu_load * 100) << "%\n";
                std::cout << "  Memory: " << (msg.payload.memory_usage * 100) << "%\n";
                std::cout << "  Description: " << msg.payload.description.c_str() << "\n";
            }
        );
        
        // Register handler for CommandMessage
        service.register_handler<commrat::CommandMessage>(
            [&command_count](const commrat::CommandMessage& msg) {
                command_count++;
                std::cout << "\n[CommandMessage] Received:\n";
                std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
                std::cout << "  Seq: " << msg.header.seq_number << "\n";
                std::cout << "  Command ID: " << msg.payload.command_id << "\n";
                std::cout << "  Target ID: " << msg.payload.target_id << "\n";
                std::cout << "  Parameters (" << msg.payload.parameters.size() << "): ";
                for (size_t i = 0; i < msg.payload.parameters.size(); i++) {
                    std::cout << static_cast<int>(msg.payload.parameters[i]);
                    if (i < msg.payload.parameters.size() - 1) std::cout << ", ";
                }
                std::cout << "\n";
            }
        );
        
        // Register handler for SensorMessage
        service.register_handler<commrat::SensorMessage>(
            [&sensor_count](const commrat::SensorMessage& msg) {
                sensor_count++;
                std::cout << "\n[SensorMessage] Received:\n";
                std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
                std::cout << "  Seq: " << msg.header.seq_number << "\n";
                std::cout << "  Sensor ID: " << msg.payload.sensor_id << "\n";
                std::cout << "  Temperature: " << msg.payload.temperature << "\n";
                std::cout << "  Pressure: " << msg.payload.pressure << "\n";
                std::cout << "  Humidity: " << msg.payload.humidity << "\n";
                std::cout << "  Additional readings (" << msg.payload.additional_readings.size() << "): ";
                for (size_t i = 0; i < msg.payload.additional_readings.size(); i++) {
                    std::cout << msg.payload.additional_readings[i];
                    if (i < msg.payload.additional_readings.size() - 1) std::cout << ", ";
                }
                std::cout << "\n";
            }
        );
        
        // Register handler for ErrorMessage
        service.register_handler<commrat::ErrorMessage>(
            [&error_count](const commrat::ErrorMessage& msg) {
                error_count++;
                std::cout << "\n[ErrorMessage] Received:\n";
                std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
                std::cout << "  Seq: " << msg.header.seq_number << "\n";
                std::cout << "  Error code: " << msg.payload.error_code << "\n";
                std::cout << "  Source ID: " << msg.payload.source_id << "\n";
                std::cout << "  Message: " << msg.payload.error_text.c_str() << "\n";
            }
        );
        
        std::cout << "Starting receiver service...\n";
        std::cout << "Waiting for messages (Ctrl+C to stop)...\n";
        std::cout << "----------------------------------------\n";
        
        service.start();
        
        // Keep running until signal received
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "\nStopping receiver service...\n";
        service.stop();
        
        std::cout << "\nMessage Type Counts:\n";
        std::cout << "  StatusMessage: " << status_count << "\n";
        std::cout << "  CommandMessage: " << command_count << "\n";
        std::cout << "  SensorMessage: " << sensor_count << "\n";
        std::cout << "  ErrorMessage: " << error_count << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
