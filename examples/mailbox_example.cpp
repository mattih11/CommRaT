/**
 * @file mailbox_example.cpp
 * @brief Example demonstrating the modern Mailbox interface
 * 
 * This example shows:
 * - Creating strongly-typed mailboxes
 * - Sending typed messages
 * - Receiving specific message types
 * - Using receive_any with visitor pattern
 * - Error handling with std::expected
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <thread>
#include <chrono>

using namespace commrat;
using namespace std::chrono_literals;

// Example 1: Simple point-to-point communication
void example_simple_communication() {
    std::cout << "\n=== Example 1: Simple Communication ===\n";
    
    // Create two mailboxes
    MailboxConfig sender_config{
        .mailbox_id = 100,
        .mailbox_name = "sender"
    };
    
    MailboxConfig receiver_config{
        .mailbox_id = 200,
        .mailbox_name = "receiver"
    };
    
    // Create strongly-typed mailboxes
    Mailbox<CommandMessage, StatusMessage> sender(sender_config);
    Mailbox<CommandMessage, StatusMessage> receiver(receiver_config);
    
    // Start both mailboxes
    if (auto result = sender.start(); !result) {
        std::cerr << "Failed to start sender: " << to_string(result.error()) << "\n";
        return;
    }
    
    if (auto result = receiver.start(); !result) {
        std::cerr << "Failed to start receiver: " << to_string(result.error()) << "\n";
        return;
    }
    
    std::cout << "Both mailboxes started successfully\n";
    
    // Send a command message
    CommandMessage cmd;
    cmd.payload.command_id = 42;
    cmd.payload.target_id = 100;
    cmd.payload.parameters.push_back(1);
    cmd.payload.parameters.push_back(2);
    
    std::cout << "Sending command message...\n";
    if (auto result = sender.send(cmd, receiver_config.mailbox_id); !result) {
        std::cerr << "Failed to send: " << to_string(result.error()) << "\n";
        return;
    }
    
    // Receive the command
    std::cout << "Waiting for command message...\n";
    if (auto result = receiver.receive<CommandMessage>(); result) {
        std::cout << "Received command ID: " << result->message.payload.command_id 
                  << " (target: " << result->message.payload.target_id << ")\n";
        
        // Send a status reply
        StatusMessage status;
        status.payload.status_code = 100;
        status.payload.subsystem_id = 1;
        status.payload.description = "Command received";
        
        std::cout << "Sending status reply...\n";
        receiver.send(status, sender_config.mailbox_id);
        
    } else {
        std::cerr << "Failed to receive: " << to_string(result.error()) << "\n";
    }
    
    // Receive the status reply
    std::cout << "Waiting for status reply...\n";
    if (auto result = sender.receive_for<StatusMessage>(1s); result) {
        std::cout << "Received status code: " << result->message.payload.status_code 
                  << " - " << result->message.payload.description.c_str() << "\n";
    } else {
        std::cerr << "Failed to receive reply: " << to_string(result.error()) << "\n";
    }
}

// Example 2: Multiple message types with visitor
void example_visitor_pattern() {
    std::cout << "\n=== Example 2: Visitor Pattern ===\n";
    
    MailboxConfig sender_config{.mailbox_id = 300};
    MailboxConfig receiver_config{.mailbox_id = 400};
    
    // Mailbox that handles multiple message types
    Mailbox<CommandMessage, StatusMessage, SensorMessage, ErrorMessage> sender(sender_config);
    Mailbox<CommandMessage, StatusMessage, SensorMessage, ErrorMessage> receiver(receiver_config);
    
    sender.start();
    receiver.start();
    
    // Send different message types
    CommandMessage cmd;
    cmd.payload.command_id = 1;
    cmd.payload.target_id = 10;
    sender.send(cmd, receiver_config.mailbox_id);
    
    std::this_thread::sleep_for(100ms);
    
    SensorMessage sensor;
    sensor.payload.sensor_id = 5;
    sensor.payload.temperature = 23.5;
    sensor.payload.pressure = 1013.25;
    sender.send(sensor, receiver_config.mailbox_id);
    
    std::this_thread::sleep_for(100ms);
    
    ErrorMessage error;
    error.payload.error_code = 404;
    error.payload.source_id = 1;
    error.payload.error_text = "Resource not found";
    sender.send(error, receiver_config.mailbox_id);
    
    // Use visitor to receive any message type
    std::cout << "Receiving messages with visitor pattern...\n";
    for (int i = 0; i < 3; ++i) {
        auto result = receiver.receive_any([](auto&& msg) {
            using T = std::decay_t<decltype(msg)>;
            
            if constexpr (std::is_same_v<T, ReceivedMessage<CommandMessage>>) {
                std::cout << "  [Command] ID " << msg->payload.command_id 
                          << " (target: " << msg->payload.target_id << ")\n";
            }
            else if constexpr (std::is_same_v<T, ReceivedMessage<SensorMessage>>) {
                std::cout << "  [Sensor] ID " << msg->payload.sensor_id 
                          << " temp=" << msg->payload.temperature << "°C\n";
            }
            else if constexpr (std::is_same_v<T, ReceivedMessage<ErrorMessage>>) {
                std::cout << "  [Error] Code " << msg->payload.error_code 
                          << ": " << msg->payload.error_text.c_str() << "\n";
            }
            else {
                std::cout << "  [Unknown message type]\n";
            }
        });
        
        if (!result) {
            std::cerr << "Failed to receive: " << to_string(result.error()) << "\n";
            break;
        }
    }
}

// Example 3: Non-blocking receive
void example_nonblocking() {
    std::cout << "\n=== Example 3: Non-blocking Receive ===\n";
    
    MailboxConfig config{.mailbox_id = 500};
    Mailbox<StatusMessage> mailbox(config);
    mailbox.start();
    
    // Try to receive without blocking (should timeout)
    std::cout << "Trying non-blocking receive (should be empty)...\n";
    if (auto msg = mailbox.try_receive<StatusMessage>()) {
        std::cout << "Unexpected message received!\n";
    } else {
        std::cout << "No message available (as expected)\n";
    }
    
    // Try with short timeout
    std::cout << "Trying receive with 100ms timeout...\n";
    if (auto result = mailbox.receive_for<StatusMessage>(100ms); !result) {
        std::cout << "Timed out (as expected): " << to_string(result.error()) << "\n";
    }
}

// Example 4: Compile-time type safety
void example_type_safety() {
    std::cout << "\n=== Example 4: Type Safety ===\n";
    
    // This mailbox only handles CommandMessage and StatusMessage
    using RestrictedMailbox = Mailbox<CommandMessage, StatusMessage>;
    
    std::cout << "RestrictedMailbox can handle " 
              << RestrictedMailbox::num_message_types() << " message types\n";
    
    std::cout << "Can handle CommandMessage: " 
              << std::boolalpha << RestrictedMailbox::is_registered<CommandMessage> << "\n";
    std::cout << "Can handle StatusMessage: " 
              << std::boolalpha << RestrictedMailbox::is_registered<StatusMessage> << "\n";
    std::cout << "Can handle SensorMessage: " 
              << std::boolalpha << RestrictedMailbox::is_registered<SensorMessage> << "\n";
    
    // The following would fail at compile time:
    // RestrictedMailbox mbx(config);
    // mbx.send(sensor_msg, dest);  // ERROR: SensorMessage not registered!
    
    // But this works fine:
    using FlexibleMailbox = Mailbox<CommandMessage, StatusMessage, SensorMessage>;
    std::cout << "\nFlexibleMailbox can handle " 
              << FlexibleMailbox::num_message_types() << " message types\n";
}

// Example 5: StandardMailbox convenience type
void example_standard_mailbox() {
    std::cout << "\n=== Example 5: StandardMailbox ===\n";
    
    MailboxConfig config{
        .mailbox_id = 600,
        .message_slots = 20,
        .max_message_size = 8192,
        .send_priority = 15,
        .mailbox_name = "standard_mbx"
    };
    
    // StandardMailbox includes common message types
    StandardMailbox mailbox(config);
    
    std::cout << "StandardMailbox handles " 
              << StandardMailbox::num_message_types() << " message types\n";
    
    if (auto result = mailbox.start(); result) {
        std::cout << "Mailbox " << mailbox.mailbox_id() << " started\n";
        std::cout << "Messages sent: " << mailbox.messages_sent() << "\n";
        std::cout << "Messages received: " << mailbox.messages_received() << "\n";
        
        // Clean any pending messages
        mailbox.clean();
        std::cout << "Mailbox cleaned\n";
    }
}

int main() {
    std::cout << "CommRaT Mailbox Examples\n";
    std::cout << "========================\n";
    
    example_simple_communication();
    example_visitor_pattern();
    example_nonblocking();
    example_type_safety();
    example_standard_mailbox();
    
    std::cout << "\nAll examples completed!\n";
    return 0;
}
