/**
 * @file test_config_generator.cpp
 * @brief Test compile-time config generation from I/O specs
 */

#include "commrat/module/io/config/module_config_generator.hpp"
#include <iostream>
#include <rfl/json.hpp>

// Example message types
struct SensorData {
    float temperature{0.0f};
    float pressure{0.0f};
};

struct FilteredData {
    float value{0.0f};
};

struct IMUData {
    float accel_x{0.0f};
};

struct GPSData {
    double lat{0.0};
    double lon{0.0};
};

using namespace commrat;

// Use SnakeCaseToCamelCase for JSON output
using Processors = rfl::Processors<rfl::SnakeCaseToCamelCase>;

int main() {
    std::cout << "Testing ModuleConfig generation with rfl::Flatten...\n\n";
    
    // Test 1: NoOutput, void input (periodic/free-running)
    {
        using Config = ModuleConfig<NoOutput, void>;
        Config config;
        config.name = "HeartbeatModule";
        config.period = std::chrono::milliseconds{1000};
        config.priority = 10;
        
        auto json = rfl::json::write<Processors>(config);
        std::cout << "1. NoOutput + void (periodic):\n" << json << "\n\n";
    }
    
    // Test 2: Output<T>, void input (periodic generator)
    {
        using Config = ModuleConfig<Output<SensorData>, void>;
        Config config;
        config.name = "SensorModule";
        config.system_id = 10;
        config.instance_id = 1;
        config.period = std::chrono::milliseconds{100};
        
        auto json = rfl::json::write<Processors>(config);
        std::cout << "2. Output<SensorData> + void (periodic generator):\n" << json << "\n\n";
    }
    
    // Test 3: Output<T>, Input<U> (continuous processing)
    {
        using Config = ModuleConfig<Output<FilteredData>, Input<SensorData>>;
        Config config;
        config.name = "FilterModule";
        config.system_id = 20;
        config.instance_id = 1;
        config.input.producer_system_id = 10;
        config.input.producer_instance_id = 1;
        config.input.poll_timeout = std::chrono::milliseconds{100};
        
        auto json = rfl::json::write<Processors>(config);
        std::cout << "3. Output<FilteredData> + Input<SensorData> (continuous):\n" << json << "\n\n";
    }
    
    // Test 4: Outputs<T, U>, Inputs<V, W> (multi-sensor fusion)
    {
        using Config = ModuleConfig<Outputs<FilteredData, SensorData>, Inputs<IMUData, GPSData>>;
        Config config;
        config.name = "FusionModule";
        
        // Multi-output configs (array)
        config.outputs[0].system_id = 30;
        config.outputs[0].instance_id = 1;
        config.outputs[1].system_id = 31;
        config.outputs[1].instance_id = 1;
        
        // Multi-input: primary + synced array
        config.primary_input.producer_system_id = 40;
        config.primary_input.producer_instance_id = 1;
        config.synced_inputs[0].producer_system_id = 50;
        config.synced_inputs[0].producer_instance_id = 1;
        config.sync_tolerance = std::chrono::milliseconds{50};
        
        auto json = rfl::json::write<Processors>(config);
        std::cout << "4. Outputs<2> + Inputs<2> (multi-sensor fusion):\n" << json << "\n\n";
    }
    
    // Test 5: Output<T>, CmdOnlyInput<U> (command-driven)
    {
        using Config = ModuleConfig<Output<SensorData>, CmdOnlyInput<SensorData>>;
        Config config;
        config.name = "OnDemandSensor";
        config.system_id = 60;
        config.instance_id = 1;
        config.command_input.producer_system_id = 70;
        config.command_input.producer_instance_id = 1;
        config.period = std::nullopt;  // Free-running
        
        auto json = rfl::json::write<Processors>(config);
        std::cout << "5. Output<SensorData> + CmdOnlyInput (command-driven):\n" << json << "\n\n";
    }
    
    // Test 6: Test field access - verify rfl::Flatten works
    {
        using Config = ModuleConfig<Output<SensorData>, Input<FilteredData>>;
        Config config;
        
        // CommonConfig fields available at top level
        config.name = "TestModule";
        config.period = std::chrono::milliseconds{200};
        config.priority = 15;
        config.realtime = true;
        
        // SingleOutputConfigFields at top level
        config.system_id = 100;
        config.instance_id = 2;
        
        // SingleInputConfigFields at top level
        config.input.producer_system_id = 90;
        config.input.producer_instance_id = 1;
        
        std::cout << "6. Field access test (rfl::Flatten):\n";
        std::cout << "   name: " << config.name << "\n";
        std::cout << "   period: " << config.period.value().count() << "ms\n";
        std::cout << "   system_id: " << (int)config.system_id << "\n";
        std::cout << "   input.producer_system_id: " << (int)config.input.producer_system_id << "\n\n";
    }
    
    std::cout << "All config generation tests passed!\n";
    
    return 0;
}
