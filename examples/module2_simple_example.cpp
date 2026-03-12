/**
 * @file module2_simple_example.cpp
 * @brief Simple Module2 usage example
 * 
 * Demonstrates the three execution modes:
 * 1. Timer-driven (periodic sensor reading)
 * 2. Input-driven (data processing pipeline)
 * 3. Loop-driven (maximum throughput generator)
 */

#include "commrat/module2.hpp"
#include "commrat/messaging/message_registry.hpp"
#include <iostream>

using namespace commrat;

// ============================================================================
// Example Message Types
// ============================================================================

struct SensorPayload {
    float temperature;
    float pressure;
    uint64_t timestamp;
};

struct FilteredPayload {
    float filtered_value;
    uint64_t timestamp;
};

struct CommandPayload {
    uint32_t command_code;
};

// ============================================================================
// Example Registry
// ============================================================================

using ExampleApp = commrat::CommRaT<
    Message::Data<SensorPayload>,
    Message::Data<FilteredPayload>,
    Message::Data<CommandPayload>
>;

// ============================================================================
// Example 1: Timer-Driven Module (Periodic Sensor)
// ============================================================================

class PeriodicSensor : public ExampleApp::Module2<
    Output<SensorPayload>,
    Period<Milliseconds(100)>  // 100Hz = 10ms period
> {
protected:
    /**
     * @brief Process function for timer-driven module
     * 
     * Called every 100ms. Returns output data.
     */
    SensorPayload process() override {
        // Read sensor
        return SensorPayload{
            .temperature = read_temperature(),
            .pressure = read_pressure(),
            .timestamp = Time::now().nanoseconds()
        };
    }

private:
    float read_temperature() {
        // Simulate sensor read
        return 25.0f + (rand() % 100) / 100.0f;
    }
    
    float read_pressure() {
        // Simulate sensor read  
        return 1013.25f + (rand() % 100) / 100.0f;
    }
};

// ============================================================================
// Example 2: Input-Driven Module (Data Filter)
// ============================================================================

class DataFilter : public ExampleApp::Module2<
    Output<FilteredPayload>,
    Input<SensorPayload>  // Input-driven by sensor data
> {
protected:
    /**
     * @brief Process function for input-driven module
     * 
     * Called when input data arrives. Receives input, returns output.
     */
    FilteredPayload process(const SensorPayload& input) override {
        // Apply simple moving average
        history_[history_idx_] = input.temperature;
        history_idx_ = (history_idx_ + 1) % HISTORY_SIZE;
        
        float sum = 0.0f;
        for (float val : history_) {
            sum += val;
        }
        
        return FilteredPayload{
            .filtered_value = sum / HISTORY_SIZE,
            .timestamp = input.timestamp
        };
    }

private:
    static constexpr size_t HISTORY_SIZE = 10;
    std::array<float, HISTORY_SIZE> history_{};
    size_t history_idx_ = 0;
};

// ============================================================================
// Example 3: Loop-Driven Module (Command Generator)
// ============================================================================

class CommandGenerator : public ExampleApp::Module2<
    Output<CommandPayload>
    // No Input, no Period → Loop-driven (auto-inferred)
> {
protected:
    /**
     * @brief Process function for loop-driven module
     * 
     * Called continuously in tight loop (max throughput).
     * Returns output data.
     */
    CommandPayload process() override {
        return CommandPayload{
            .command_code = counter_++
        };
    }

private:
    uint32_t counter_ = 0;
};

// ============================================================================
// Example 4: Multi-Output Module
// ============================================================================

struct DiagnosticPayload {
    uint32_t error_count;
    uint32_t status_flags;
};

class DiagnosticMonitor : public ExampleApp::Module2<
    Output<SensorPayload>,      // Primary output
    Output<DiagnosticPayload>,  // Secondary output
    Period<Seconds(1)>          // 1Hz diagnostics
> {
protected:
    /**
     * @brief Process function for multi-output module
     * 
     * Returns tuple of outputs (one for each Output<> spec).
     */
    std::tuple<SensorPayload, DiagnosticPayload> process() override {
        return {
            SensorPayload{
                .temperature = 25.0f,
                .pressure = 1013.25f,
                .timestamp = Time::now().nanoseconds()
            },
            DiagnosticPayload{
                .error_count = error_count_,
                .status_flags = 0x00000001
            }
        };
    }

private:
    uint32_t error_count_ = 0;
};

// ============================================================================
// Example 5: Structured I/O Access (outputs() and inputs() API)
// ============================================================================

class StructuredIOExample : public ExampleApp::Module2<
    Output<FilteredPayload>,
    Input<SensorPayload>
> {
protected:
    FilteredPayload process(const SensorPayload& input) override {
        // NEW API: Structured I/O access with rfl::NamedTuple
        // (Note: This is for demonstration - not used in process() signature)
        
        // Access inputs by name (auto-complete friendly!)
        auto ins = inputs();
        // ins.sensor_payload.receive();  // Named access
        // Or index access: rfl::get<0>(ins).receive();
        
        // Access outputs by name
        auto outs = outputs();
        // outs.filtered_payload.publish(...);  // Named access
        // Or index access: rfl::get<0>(outs).publish(...);
        
        // Still use process() parameters for actual processing
        return FilteredPayload{
            .filtered_value = input.temperature * 0.9f,  // Simple filter
            .timestamp = input.timestamp
        };
    }
};

// ============================================================================
// Multi-Input Example: Demonstrates type-based and index-based naming
// ============================================================================

struct GPSPayload {
    double latitude;
    double longitude;
};

struct IMUPayload {
    float accel_x;
    float accel_y;
    float accel_z;
};

struct FusedPayload {
    double position[2];
    float velocity[3];
};

class SensorFusion : public ExampleApp::Module2<
    Output<FusedPayload>,
    Input<IMUPayload>,              // Primary (continuous)
    SyncedInput<GPSPayload>,        // Secondary (synced)
    SyncedInput<SensorPayload>      // Another secondary
> {
protected:
    FusedPayload process(
        const IMUPayload& imu,
        const std::optional<GPSPayload>& gps,
        const std::optional<SensorPayload>& sensor
    ) override {
        // Structured access to inputs (field names auto-generated)
        auto ins = inputs();
        // ins has fields:
        //   - imu_payload         (IMUPayload - first occurrence)
        //   - gps_payload         (GPSPayload)
        //   - sensor_payload      (SensorPayload)
        
        // Metadata access still index-based (compile-time)
        auto imu_ts = get_input_timestamp<0>();
        bool gps_fresh = has_new_data<1>();
        bool sensor_valid = is_input_valid<2>();
        
        // Process with optional GPS
        FusedPayload result{};
        if (gps) {
            result.position[0] = gps->latitude;
            result.position[1] = gps->longitude;
        }
        result.velocity[0] = imu.accel_x;
        result.velocity[1] = imu.accel_y;
        result.velocity[2] = imu.accel_z;
        
        return result;
    }
};

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "Module2 Examples\n";
    std::cout << "================\n\n";
    
    // Example 1: Timer-driven
    std::cout << "1. Timer-Driven Module (Periodic Sensor)\n";
    ModuleConfig sensor_config{
        .system_id = 10,
        .instance_id = 1
    };
    PeriodicSensor sensor(sensor_config);
    // sensor.start();
    // ... let it run ...
    // sensor.stop();
    std::cout << "   - Runs at 100Hz (10ms period)\n";
    std::cout << "   - Process signature: SensorPayload process()\n\n";
    
    // Example 2: Input-driven
    std::cout << "2. Input-Driven Module (Data Filter)\n";
    ModuleConfig filter_config{
        .system_id = 20,
        .instance_id = 1
    };
    DataFilter filter(filter_config);
    // filter.start();
    std::cout << "   - Runs when input arrives (blocking receive)\n";
    std::cout << "   - Process signature: FilteredPayload process(const SensorPayload&)\n\n";
    
    // Example 3: Loop-driven
    std::cout << "3. Loop-Driven Module (Command Generator)\n";
    ModuleConfig gen_config{
        .system_id = 30,
        .instance_id = 1
    };
    CommandGenerator generator(gen_config);
    // generator.start();
    std::cout << "   - Runs continuously (max throughput)\n";
    std::cout << "   - Process signature: CommandPayload process()\n\n";
    
    // Example 4: Multi-output
    std::cout << "4. Multi-Output Module (Diagnostic Monitor)\n";
    ModuleConfig diag_config{
        .system_id = 40,
        .instance_id = 1
    };
    DiagnosticMonitor diagnostics(diag_config);
    // diagnostics.start();
    std::cout << "   - Runs at 1Hz (1 second period)\n";
    std::cout << "   - Process signature: std::tuple<SensorPayload, DiagnosticPayload> process()\n\n";
    
    // Example 5: Structured I/O access
    std::cout << "5. Structured I/O Access (outputs() and inputs() API)\n";
    ModuleConfig struct_config{
        .system_id = 50,
        .instance_id = 1
    };
    StructuredIOExample structured(struct_config);
    
    // Demonstrate structured access
    auto outs = structured.outputs();
    auto ins = structured.inputs();
    
    std::cout << "   - Access outputs by name: outs.filtered_payload\n";
    std::cout << "   - Access inputs by name: ins.sensor_payload\n";
    std::cout << "   - Or use index: rfl::get<0>(outs), rfl::get<0>(ins)\n";
    std::cout << "   - Zero-copy: Named struct contains references only!\n\n";
    
    return 0;
}
