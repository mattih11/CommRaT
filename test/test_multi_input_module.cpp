/**
 * @file test_multi_input_module.cpp
 * @brief Phase 6.6 - Multi-input Module integration test
 * 
 * Tests Module with Inputs<T1, T2> + PrimaryInput<T1> fully integrated:
 * - Multi-subscription protocol
 * - HistoricalMailbox per input type
 * - Primary input drives execution
 * - Secondary inputs synchronized via getData()
 */

#include <commrat/commrat.hpp>
#include <commrat/registry_module.hpp>
#include <commrat/mailbox/historical_mailbox.hpp>
#include <chrono>
#include <thread>
#include <iostream>

using namespace commrat;
using namespace std::chrono_literals;

// ============================================================================
// Test Message Types
// ============================================================================

struct IMUData {
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
};

struct GPSData {
    double latitude, longitude, altitude;
};

struct FusedData {
    float position_x, position_y, position_z;
    float velocity_x, velocity_y, velocity_z;
};

// ============================================================================
// Application Definition
// ============================================================================

using TestApp = CommRaT<
    Message::Data<IMUData>,
    Message::Data<GPSData>,
    Message::Data<FusedData>
>;

// ============================================================================
// Producer Modules
// ============================================================================

class IMUModule : public TestApp::Module<Output<IMUData>, PeriodicInput> {
protected:
    void process(IMUData& output) override {
        output = IMUData{
            .accel_x = 1.0f, .accel_y = 0.0f, .accel_z = 9.81f,
            .gyro_x = 0.0f, .gyro_y = 0.0f, .gyro_z = 0.0f
        };
    }
    
private:
    int counter_{0};
};

class GPSModule : public TestApp::Module<Output<GPSData>, PeriodicInput> {
protected:
    void process(GPSData& output) override {
        output = GPSData{
            .latitude = 37.7749,
            .longitude = -122.4194,
            .altitude = 100.0
        };
    }
    
private:
    int counter_{0};
};

// ============================================================================
// Multi-Input Consumer Module (FUTURE - Phase 6.6)
// ============================================================================

// TODO: Once Phase 6.6 is complete, this should work:
/*
class SensorFusionModule : public TestApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>,
    PrimaryInput<IMUData>
> {
protected:
    FusedData process(const IMUData& imu, const GPSData& gps) override {
        // Fusion logic
        return FusedData{
            .position_x = static_cast<float>(gps.latitude),
            .position_y = static_cast<float>(gps.longitude),
            .position_z = static_cast<float>(gps.altitude),
            .velocity_x = imu.accel_x * 0.1f,
            .velocity_y = imu.accel_y * 0.1f,
            .velocity_z = imu.accel_z * 0.1f
        };
    }
};
*/

// ============================================================================
// Test
// ============================================================================

int main() {
    std::cout << "\n=== Phase 6.6: Multi-Input Module Integration Test ===\n\n";
    
    // Phase 6.6 TODO: Test multi-input subscription
    std::cout << "Phase 6.6: Multi-input Module integration not yet implemented\n";
    std::cout << "Current test validates Phase 6.5 config changes only\n\n";
    
    // Test 1: InputSource struct compiles
    commrat::MultiInputConfig::InputSource imu_source{
        .system_id = 10,
        .instance_id = 1
    };
    
    commrat::MultiInputConfig::InputSource gps_source{
        .system_id = 20,
        .instance_id = 1
    };
    
    std::cout << "TEST 1: InputSource struct compiles ✓\n";
    
    // Test 2: ModuleConfig with input_sources compiles
    ModuleConfig fusion_config{
        .name = "SensorFusion",
        .outputs = commrat::SimpleOutputConfig{.system_id = 30, .instance_id = 1},
        .inputs = commrat::MultiInputConfig{
            .sources = {imu_source, gps_source},
            .history_buffer_size = 100,
            .sync_tolerance = 50ms
        },
        .period = 10ms,
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    std::cout << "TEST 2: ModuleConfig with input_sources compiles ✓\n";
    std::cout << "  - input_sources.size() = " << fusion_config.input_sources().size() << "\n";
    std::cout << "  - history_buffer_size = " << fusion_config.history_buffer_size() << "\n";
    std::cout << "  - sync_tolerance = " << fusion_config.sync_tolerance().count() << " ms\n";
    
    // Test 3: Single-input modules still work (backward compatibility)
    ModuleConfig imu_config{
        .name = "IMU",
        .outputs = commrat::SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = commrat::NoInputConfig{},
        .period = 10ms,
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    std::cout << "\nTEST 3: Single-input Module config still compiles ✓\n";
    
    std::cout << "\n=== Phase 6.5 Config Validation: ALL TESTS PASSED ===\n";
    std::cout << "Phase 6.6 implementation: IN PROGRESS\n\n";
    
    return 0;
}
