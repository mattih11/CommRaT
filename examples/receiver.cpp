#include "commrat/message_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

using namespace commrat;

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_running = false;
    }
}

int main(int argc, char** argv) {
    std::cout << "CommRaT Message Receiver Example\n";
    std::cout << "=================================\n\n";
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Parse command line arguments
    uint32_t mailbox_id = 2; // Default receiver ID
    if (argc > 1) {
        mailbox_id = std::stoi(argv[1]);
    }
    
    // Configure the receiver mailbox
    TimsConfig config;
    config.mailbox_name = "receiver";
    config.mailbox_id = mailbox_id;
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
    std::cout << "Waiting for messages... (Press Ctrl+C to stop)\n\n";
    
    // Register handlers for different message types
    
    // Status message handler
    service.register_handler<StatusMessage>(
        [](const StatusMessage& msg) {
            std::cout << "\n[StatusMessage received]\n";
            std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
            std::cout << "  Sequence: " << msg.header.seq_number << "\n";
            std::cout << "  Status Code: " << msg.payload.status_code << "\n";
            std::cout << "  Subsystem ID: " << msg.payload.subsystem_id << "\n";
            std::cout << "  CPU Load: " << msg.payload.cpu_load * 100 << "%\n";
            std::cout << "  Memory Usage: " << msg.payload.memory_usage * 100 << "%\n";
            std::cout << "  Description: " << msg.payload.description.c_str() << "\n";
        }
    );
    
    // Command message handler
    service.register_handler<CommandMessage>(
        [](const CommandMessage& msg) {
            std::cout << "\n[CommandMessage received]\n";
            std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
            std::cout << "  Sequence: " << msg.header.seq_number << "\n";
            std::cout << "  Command ID: " << msg.payload.command_id << "\n";
            std::cout << "  Target ID: " << msg.payload.target_id << "\n";
            std::cout << "  Parameters (" << msg.payload.parameters.size() << "): ";
            for (size_t i = 0; i < msg.payload.parameters.size() && i < 5; ++i) {
                std::cout << "0x" << std::hex << static_cast<int>(msg.payload.parameters[i]) << " ";
            }
            std::cout << std::dec << "\n";
        }
    );
    
    // Sensor message handler
    service.register_handler<SensorMessage>(
        [](const SensorMessage& msg) {
            std::cout << "\n[SensorMessage received]\n";
            std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
            std::cout << "  Sequence: " << msg.header.seq_number << "\n";
            std::cout << "  Sensor ID: " << msg.payload.sensor_id << "\n";
            std::cout << "  Temperature: " << msg.payload.temperature << "°C\n";
            std::cout << "  Pressure: " << msg.payload.pressure << " hPa\n";
            std::cout << "  Humidity: " << msg.payload.humidity << "%\n";
            std::cout << "  Additional readings: " << msg.payload.additional_readings.size() << "\n";
        }
    );
    
    // Error message handler
    service.register_handler<ErrorMessage>(
        [](const ErrorMessage& msg) {
            std::cout << "\n[ErrorMessage received]\n";
            std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
            std::cout << "  Sequence: " << msg.header.seq_number << "\n";
            std::cout << "  Error Code: " << msg.payload.error_code << "\n";
            std::cout << "  Source ID: " << msg.payload.source_id << "\n";
            std::cout << "  Error Text: " << msg.payload.error_text.c_str() << "\n";
        }
    );
    
    // Acknowledgment message handler
    service.register_handler<AckMessage>(
        [](const AckMessage& msg) {
            std::cout << "\n[AckMessage received]\n";
            std::cout << "  Timestamp: " << msg.header.timestamp << "\n";
            std::cout << "  Sequence: " << msg.header.seq_number << "\n";
            std::cout << "  Acked Sequence: " << msg.payload.acked_seq_number << "\n";
            std::cout << "  Ack Code: " << msg.payload.ack_code << "\n";
        }
    );
    
    // Generic data message handler (example)
    // Note: In practice, you'd register specific DataMessage<T> types
    
    // Main receive loop
    uint64_t last_count = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        uint64_t current_count = service.get_messages_received();
        if (current_count != last_count) {
            std::cout << "\n[Stats] Received " << current_count << " messages total\n";
            last_count = current_count;
        }
    }
    
    std::cout << "\n\nShutting down...\n";
    std::cout << "Total messages received: " << service.get_messages_received() << "\n";
    
    // Cleanup
    service.stop();
    
    std::cout << "Receiver stopped.\n";
    
    return 0;
}
