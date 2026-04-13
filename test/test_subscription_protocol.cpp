/**
 * @file test_subscription_protocol.cpp
 * @brief Phase 1.9 - Minimal Module2 compilation test
 * 
 * Tests that Module2 alias in CommRaT works with subscription infrastructure.
 * NOTE: Period<Duration> not supported yet - use ModuleConfig.period instead
 */

#include "commrat/commrat.hpp"
#include <iostream>

using namespace commrat;

// Test Message Types
struct SensorData {
    uint32_t sensor_id{0};
    float value{0.0f};
};

struct FilteredData {
    float filtered_value{0.0f};
};

// Test Application
using TestApp = CommRaT<
    Message::Data<SensorData>,
    Message::Data<FilteredData>
>;

// Producer - No inputs (loop mode, period from config)
class ProducerModule : public TestApp::Module2<
    Output<SensorData>
> {
public:
    using Module2::Module2;
    
protected:
    void process(SensorData& output) override {
        static float value = 0.0f;
        value += 0.1f;
        
        output.sensor_id = 1;
        output.value = value;
    }
};

// Consumer - Input-driven
class ConsumerModule : public TestApp::Module2<
    Output<FilteredData>,
    Input<SensorData>
> {
public:
    using Module2::Module2;
    
protected:
    void process(const SensorData& input, FilteredData& output) override {
        output.filtered_value = input.value * 2.0f;
    }
};

int main() {
    std::cout << "=== Phase 1.9: Module2 Compilation Test ===\n\n";
    
    ModuleConfig producer_config{
        .name = "Producer",
        .outputs = SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = NoInputConfig{},
        .period = std::chrono::milliseconds(100)  // 10Hz via config
    };
    
    ModuleConfig consumer_config{
        .name = "Consumer",
        .outputs = SimpleOutputConfig{.system_id = 20, .instance_id = 1},
        .inputs = SingleInputConfig{
            .source_system_id = 10,
            .source_instance_id = 1
        }
    };
    
    std::cout << "Creating modules...\n";
    ProducerModule producer(producer_config);
    ConsumerModule consumer(consumer_config);
    
    std::cout << "SUCCESS: Module2 instantiation works!\n";
    
    return 0;
}
