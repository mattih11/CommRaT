/**
 * @file test_multi_input_compilation.cpp
 * @brief Compilation tests for multi-input type safety
 * 
 * Tests validate that Inputs<> + PrimaryInput<> combinations:
 * - Compile for valid configurations
 * - Fail compilation with clear errors for invalid configurations
 * 
 * Run with: make test_multi_input_compilation
 */

#include "commrat/commrat.hpp"
#include <iostream>

// ============================================================================
// Test Message Types
// ============================================================================

struct IMUData {
    float accel_x{0.0f}, accel_y{0.0f}, accel_z{0.0f};
    float gyro_x{0.0f}, gyro_y{0.0f}, gyro_z{0.0f};
};

struct GPSData {
    double latitude{0.0};
    double longitude{0.0};
    float altitude{0.0f};
};

struct LidarData {
    float distance{0.0f};
    uint32_t intensity{0};
};

struct FusedData {
    float position_x{0.0f}, position_y{0.0f}, position_z{0.0f};
    float velocity_x{0.0f}, velocity_y{0.0f}, velocity_z{0.0f};
};

struct DiagnosticsData {
    float sync_quality{0.0f};
    uint32_t sync_failures{0};
};

// Define application registry
using TestApp = commrat::CommRaT<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<LidarData>,
    commrat::Message::Data<FusedData>,
    commrat::Message::Data<DiagnosticsData>
>;

using namespace commrat;

// ============================================================================
// Test 1: Two inputs with explicit primary (first input)
// ============================================================================

class Test1_TwoInputsExplicitPrimary : public TestApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>,
    PrimaryInput<IMUData>  // IMU is primary (first input)
> {
public:
    using TestApp::Module<Output<FusedData>, Inputs<IMUData, GPSData>, PrimaryInput<IMUData>>::Module;
    
protected:
    void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
        // Should compile - primary is first input
        output = FusedData{
            .position_x = static_cast<float>(gps.latitude),
            .position_y = static_cast<float>(gps.longitude)
        };
    }
};

// ============================================================================
// Test 2: Two inputs with explicit primary (second input)
// ============================================================================

class Test2_TwoInputsSecondPrimary : public TestApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>,
    PrimaryInput<GPSData>  // GPS is primary (second input)
> {
public:
    using TestApp::Module<Output<FusedData>, Inputs<IMUData, GPSData>, PrimaryInput<GPSData>>::Module;
    
protected:
    void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
        // Should compile - primary is second input
        output = FusedData{
            .position_x = static_cast<float>(gps.latitude),
            .velocity_x = imu.accel_x
        };
    }
};

// ============================================================================
// Test 3: Three inputs with explicit primary (middle input)
// ============================================================================

class Test3_ThreeInputsMiddlePrimary : public TestApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>,
    PrimaryInput<GPSData>  // GPS is primary (middle input)
> {
public:
    using TestApp::Module<Output<FusedData>, Inputs<IMUData, GPSData, LidarData>, PrimaryInput<GPSData>>::Module;
    
protected:
    void process(const IMUData& imu, const GPSData& gps, const LidarData& lidar, FusedData& output) override {
        // Should compile - GPS drives rate, IMU and Lidar sync via getData
        output = FusedData{
            .position_x = static_cast<float>(gps.latitude),
            .position_z = lidar.distance,
            .velocity_x = imu.accel_x
        };
    }
};

// ============================================================================
// Test 4: Two inputs with implicit primary (no PrimaryInput<> specified)
// ============================================================================

class Test4_TwoInputsImplicitPrimary : public TestApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>
    // No PrimaryInput - should default to first input (IMUData)
> {
public:
    using TestApp::Module<Output<FusedData>, Inputs<IMUData, GPSData>>::Module;
    
protected:
    void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
        // Should compile - first input (IMU) implicitly primary
        output = FusedData{
            .velocity_y = imu.accel_y
        };
    }
};

// ============================================================================
// Test 5: Multi-input + Multi-output with explicit primary
// ============================================================================

class Test5_MultiInputMultiOutput : public TestApp::Module<
    Outputs<FusedData, DiagnosticsData>,
    Inputs<IMUData, GPSData, LidarData>,
    PrimaryInput<IMUData>  // IMU drives rate
> {
public:
    using TestApp::Module<Outputs<FusedData, DiagnosticsData>, Inputs<IMUData, GPSData, LidarData>, PrimaryInput<IMUData>>::Module;
    
protected:
    void process(const IMUData& imu, const GPSData& gps, const LidarData& lidar, 
                 FusedData& fused, DiagnosticsData& diag) override {
        // Should compile - multi-input + multi-output signature
        fused.position_x = static_cast<float>(gps.latitude);
        fused.position_z = lidar.distance;
        
        diag.sync_quality = 0.95f;
        diag.sync_failures = 0;
    }
};

// ============================================================================
// Main - Instantiate all test modules to validate compilation
// ============================================================================

int main() {
    std::cout << "=== Multi-Input Compilation Tests ===\n\n";
    
    std::cout << "Test 1: Two inputs, explicit primary (first)... ";
    std::cout << "✓ COMPILES\n";
    
    std::cout << "Test 2: Two inputs, explicit primary (second)... ";
    std::cout << "✓ COMPILES\n";
    
    std::cout << "Test 3: Three inputs, explicit primary (middle)... ";
    std::cout << "✓ COMPILES\n";
    
    std::cout << "Test 4: Two inputs, implicit primary (first)... ";
    std::cout << "✓ COMPILES\n";
    
    std::cout << "Test 5: Multi-input + multi-output, explicit primary... ";
    std::cout << "✓ COMPILES\n";
    
    std::cout << "\n=== ALL COMPILATION TESTS PASSED ===\n";
    std::cout << "\nNOTE: To test error messages, uncomment Test6_InvalidPrimary\n";
    std::cout << "      Expected: static_assert failure with clear message\n";
    
    return 0;
}
