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
 * Notice: Module<Output<TempData>, PeriodicInput>
 * - Clean I/O specification
 * - No registry parameter needed!
 * - No MessageDefinition in user code!
 */
class SensorModule : public App::Module<Output<TemperatureData>, PeriodicInput> {
public:
    explicit SensorModule(const ModuleConfig& config) : App::Module<Output<TemperatureData>, PeriodicInput>(config) {}
    
protected:
    // Return payload type directly
    void process(TemperatureData& output) override {
        float temp = base_temp_ + std::sin(counter_ * 0.1f) * 5.0f;
        counter_++;
        
        std::cout << "[Producer] Temperature: " << temp << "°C\n";
        
        output ={
            .temperature_celsius = temp
        };
    }

private:
    float base_temp_ = 20.0f;
    int counter_ = 0;
};

/**
 * @brief Filter module - consumes and produces TemperatureData
 * 
 * Notice: Module<Output<TempData>, Input<TempData>>
 * - Input<T> for continuous input
 * - Output<T> for single output
 * - No registry parameter!
 */
class FilterModule : public App::Module<Output<TemperatureData>, Input<TemperatureData>> {
public:
    explicit FilterModule(const ModuleConfig& config) : App::Module<Output<TemperatureData>, Input<TemperatureData>>(config) {}
    
protected:
    // Receives payload, returns payload
    void process(const TemperatureData& input, TemperatureData& output) override {
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
        
        output =  {
            .temperature_celsius = filtered
        };
    }

private:
    static constexpr size_t WINDOW_SIZE = 5;
    float window_[WINDOW_SIZE] = {};
    size_t index_ = 0;
    size_t count_ = 0;
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
        .outputs = commrat::SimpleOutputConfig{.system_id = 0, .instance_id = 0},
        .inputs = commrat::NoInputConfig{},
        .period = std::chrono::milliseconds(100),
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    SensorModule producer(producer_config);
    
    ModuleConfig consumer_config{
        .name = "FilterModule",
        .outputs = commrat::SimpleOutputConfig{.system_id = 0, .instance_id = 1},
        .inputs = commrat::SingleInputConfig{.source_system_id = 0, .source_instance_id = 0},
        .period = std::chrono::milliseconds(0),
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
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
