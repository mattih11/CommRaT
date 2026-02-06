/**
 * @file continuous_input_example.cpp
 * @brief Demonstrates ContinuousInput with automatic subscription
 */

#include "messages/common_messages.hpp"
#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>

using namespace example_messages;

// ============================================================================
// Global shutdown signal
// ============================================================================
std::atomic<bool> shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        shutdown_requested.store(true);
    }
}

// ============================================================================
// Producer Module - Publishes temperature data @ 100ms
// ============================================================================

class SensorModule : public Module<TemperatureData, PeriodicInput> {
public:
    using Module::Module;  // Inherit constructor
    
protected:
    TemperatureData process() {
        // Simulate temperature sensor
        static float temp = 20.0f;
        temp += 0.1f * (std::rand() % 10 - 5);  // Random walk
        
        std::cout << "[Producer] Published temperature: " << temp << "°C\n";
        
        return TemperatureData{
            .timestamp = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()),
            .sensor_id = config_.instance_id,  // Use instance_id as sensor identifier
            .temperature_c = temp,
            .confidence = 1.0f
        };
    }
};

// ============================================================================
// Consumer Module - Receives temperature data via automatic subscription
// ============================================================================

class FilterModule : public Module<TemperatureData, ContinuousInput<TemperatureData>> {
public:
    using Module::Module;  // Inherit constructor
    
private:
    static constexpr size_t HISTORY_SIZE = 5;
    std::array<float, HISTORY_SIZE> history_{};
    size_t history_index_{0};

protected:
    TemperatureData process_continuous(const TemperatureData& input) {
        // Apply simple moving average filter
        history_[history_index_] = input.temperature_c;
        history_index_ = (history_index_ + 1) % HISTORY_SIZE;
        
        float sum = 0.0f;
        for (float val : history_) {
            sum += val;
        }
        float filtered = sum / HISTORY_SIZE;
        
        std::cout << "[Consumer] Received: " << input.temperature_c << "°C "
                  << "→ Filtered: " << filtered << "°C\n";
        
        return TemperatureData{
            .timestamp = input.timestamp,
            .sensor_id = input.sensor_id,
            .temperature_c = filtered,
            .confidence = input.confidence
        };
    }
    
    void on_start() override {
        std::cout << "[Consumer] Started - auto-subscribed to producer\n";
    }
    
    void on_stop() override {
        std::cout << "[Consumer] Stopped - auto-unsubscribed\n";
    }
};

// ============================================================================
// Main
// ============================================================================

int main() {
    // Install signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "=== Continuous Input Example ===\n";
    std::cout << "Producer → Consumer with automatic subscription\n";
    std::cout << "Press Ctrl+C to stop\n\n";
    
    // Debug: Print message IDs
    std::cout << "Message IDs:\n";
    std::cout << "  SubscribeRequest:   0x" << std::hex << std::setw(8) << std::setfill('0') 
              << ExampleRegistry::get_message_id<commrat::SubscribeRequestPayload>() << "\n";
    std::cout << "  SubscribeReply:     0x" << std::hex << std::setw(8) << std::setfill('0')
              << ExampleRegistry::get_message_id<commrat::SubscribeReplyPayload>() << "\n";
    std::cout << "  TemperatureData:    0x" << std::hex << std::setw(8) << std::setfill('0')
              << ExampleRegistry::get_message_id<TemperatureData>() << "\n";
    std::cout << std::dec << "\n";
    
    try {
        // Create producer module (publishes @ 100ms)
        // system_id=0, instance_id=0 (PRODUCER instance)
        ModuleConfig producer_config{
            .name = "SensorModule",
            .system_id = 0,
            .instance_id = 0,
            .period = std::chrono::milliseconds{100},
            .message_slots = 10,
            .max_subscribers = 8,
            .priority = 10,
            .realtime = false
        };
        SensorModule producer(producer_config);
        
        // Create consumer module (subscribes to producer)
        // system_id=0, instance_id=1 (CONSUMER instance)
        ModuleConfig consumer_config{
            .name = "FilterModule",
            .system_id = 0,
            .instance_id = 1,
            .message_slots = 10,
            .max_subscribers = 8,
            .priority = 10,
            .realtime = false,
            .source_system_id = 0,    // Subscribe to producer in system 0
            .source_instance_id = 0   // Producer is instance 0
        };
        FilterModule consumer(consumer_config);
        
        // Start modules
        producer.start();
        
        // Give threads time to start and mailboxes to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        consumer.start();  // This triggers automatic subscription
        
        std::cout << "\nModules running...\n\n";
        
        // Run until shutdown signal
        while (!shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "\n\nShutdown requested...\n";
        
        // Stop modules
        consumer.stop();
        producer.stop();
        
        std::cout << "Example completed successfully\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
