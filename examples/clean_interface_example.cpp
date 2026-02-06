/**
 * @file clean_interface_example.cpp
 * @brief Demonstrates the ultra-clean user interface
 * 
 * Users just include messages/messages.hpp and use payload types!
 * - No MessageDefinition in module code
 * - No registry parameter in Module<>
 * - System messages automatically included
 */

#include "messages/messages.hpp"  // One include - that's it!
#include <iostream>
#include <cmath>

using namespace user_app;  // Module, Mailbox, PeriodicInput, ContinuousInput

// ============================================================================
// Ultra-Clean Module Definitions
// ============================================================================

/**
 * @brief Temperature sensor module
 * 
 * Notice: Module<TempData, PeriodicInput>
 * - Only payload type (TempData)
 * - Only input mode (PeriodicInput)
 * - No registry parameter! (automatic)
 * - No MessageDefinition anywhere!
 */
class SensorModule : public Module<TemperatureData, PeriodicInput> {
public:
    using Module::Module;  // Inherit constructor
    
private:
    float base_temp_ = 20.0f;
    int counter_ = 0;

protected:
    // Return payload type directly
    TemperatureData process() {
        float temp = base_temp_ + std::sin(counter_ * 0.1f) * 5.0f;
        counter_++;
        
        std::cout << "[Producer] Temperature: " << temp << "°C\n";
        
        return TemperatureData{
            .temperature_celsius = temp,
            .timestamp_ms = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count()
            )
        };
    }
};

/**
 * @brief Filter module - consumes and produces TemperatureData
 * 
 * Notice: Module<TempData, ContinuousInput<TempData>>
 * - Payload type for input: ContinuousInput<TemperatureData>
 * - Payload type for output: TemperatureData
 * - No registry parameter!
 */
class FilterModule : public Module<TemperatureData, ContinuousInput<TemperatureData>> {
public:
    using Module::Module;  // Inherit constructor
    
private:
    static constexpr size_t WINDOW_SIZE = 5;
    float window_[WINDOW_SIZE] = {0};
    size_t index_ = 0;
    size_t count_ = 0;

protected:
    // Receives payload, returns payload
    TemperatureData process_continuous(const TemperatureData& input) {
        window_[index_] = input.temperature_celsius;
        index_ = (index_ + 1) % WINDOW_SIZE;
        if (count_ < WINDOW_SIZE) count_++;
        
        float sum = 0.0f;
        for (size_t i = 0; i < count_; i++) {
            sum += window_[i];
        }
        float filtered = sum / count_;
        
        std::cout << "[Consumer] Filtered: " << filtered << "°C (raw: " 
                  << input.temperature_celsius << "°C)\n";
        
        return TemperatureData{
            .temperature_celsius = filtered,
            .timestamp_ms = input.timestamp_ms
        };
    }
};

// ============================================================================
// Main - Ultra-Clean Usage
// ============================================================================

int main() {
    std::cout << "=== Ultra-Clean Interface Example ===\n";
    std::cout << "✓ Include messages/messages.hpp - that's it!\n";
    std::cout << "✓ Module<TemperatureData, PeriodicInput> - no registry!\n";
    std::cout << "✓ ContinuousInput<TemperatureData> - payload type!\n";
    std::cout << "✓ process() returns payload type\n";
    std::cout << "✓ System messages automatically included\n\n";
    
    // Create modules - notice clean config!
    ModuleConfig producer_config{
        .name = "SensorModule",
        .system_id = 0,
        .instance_id = 0,
        .period = std::chrono::milliseconds(100),
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    SensorModule producer(producer_config);
    
    ModuleConfig consumer_config{
        .name = "FilterModule",
        .system_id = 0,
        .instance_id = 1,
        .period = std::chrono::milliseconds(0),
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false,
        .source_system_id = 0,
        .source_instance_id = 0  // Subscribe to producer
    };
    
    FilterModule consumer(consumer_config);
    
    // Start modules
    std::cout << "Starting modules...\n";
    producer.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    consumer.start();
    
    // Run for 5 seconds
    std::cout << "\nRunning for 5 seconds...\n\n";
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Stop modules
    std::cout << "\nStopping modules...\n";
    consumer.stop();
    producer.stop();
    
    std::cout << "\n=== Ultra-Clean Interface Summary ===\n";
    std::cout << "What you wrote:\n";
    std::cout << "  class SensorModule : public Module<TemperatureData, PeriodicInput> {\n";
    std::cout << "      TemperatureData process() { return TemperatureData{...}; }\n";
    std::cout << "  };\n\n";
    std::cout << "What you DIDN'T write:\n";
    std::cout << "  ✗ Clean Message::Data<T> syntax (no verbose MessageDefinition)\n";
    std::cout << "  ✗ No registry template parameter\n";
    std::cout << "  ✗ No TimsMessage<> wrappers\n";
    std::cout << "  ✗ No manual message IDs\n";
    std::cout << "  ✗ No subscription protocol imports\n\n";
    std::cout << "The framework handles everything automatically!\n";
    
    return 0;
}
