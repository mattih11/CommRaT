/**
 * @file multi_input_fusion.cpp
 * @brief Example 03: Multi-Input Fusion with Time Synchronization
 * 
 * This example demonstrates:
 * 1. Multi-input processing (Inputs<IMUData, GPSData>)
 * 2. Primary input designation (PrimaryInput<IMUData>)
 * 3. Synchronized getData for secondary inputs
 * 4. Freshness and validity checking
 * 5. Input metadata accessors
 * 
 * Architecture:
 *   IMU Sensor (100Hz)  ──┐
 *                         ├──> SensorFusion ──> FusedData
 *   GPS Sensor (10Hz)   ──┘
 * 
 * @author CommRaT Documentation Team
 * @date February 8, 2026
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <random>
#include <csignal>
#include <atomic>
#include <cmath>

// ============================================================================
// Signal handling for clean shutdown
// ============================================================================

std::atomic<bool> shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[SIGNAL] Ctrl+C received, shutting down...\n";
        shutdown_requested.store(true);
    }
}

// ============================================================================
// Step 1: Define Message Types
// ============================================================================

// High-rate IMU data (100Hz)
struct IMUData {
    float accel_x, accel_y, accel_z;      // m/s²
    float gyro_x, gyro_y, gyro_z;         // rad/s
};

// Low-rate GPS data (10Hz)
struct GPSData {
    double latitude, longitude;            // degrees
    float altitude;                        // meters
    float speed;                           // m/s
};

// Fused output combining both sensors
struct FusedData {
    // Position from GPS
    double latitude, longitude;
    float altitude;
    
    // Motion from IMU + GPS
    float velocity_x, velocity_y, velocity_z;
    float acceleration_x, acceleration_y, acceleration_z;
    
    // Quality indicators
    bool gps_fresh;        // GPS data is recent
    uint32_t imu_count;    // Number of IMU samples processed
};

// ============================================================================
// Step 2: Register Messages in Application
// ============================================================================

using FusionApp = commrat::CommRaT<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<FusedData>
>;

// ============================================================================
// Step 3: IMU Sensor Module (High-Rate Producer)
// ============================================================================

/**
 * @brief Simulated IMU sensor producing data at 100Hz
 * 
 * Key features:
 * - PeriodicInput with 10ms period (100Hz)
 * - Generates realistic acceleration and gyro data
 * - Simulates vehicle motion
 */
class IMUSensor : public FusionApp::Module<
    commrat::Output<IMUData>,
    commrat::PeriodicInput
> {
public:
    IMUSensor(const commrat::ModuleConfig& config)
        : FusionApp::Module<commrat::Output<IMUData>, commrat::PeriodicInput>(config)
        , gen_(rd_())
        , accel_noise_(0.0f, 0.05f)  // 0.05 m/s² noise
        , gyro_noise_(0.0f, 0.01f)   // 0.01 rad/s noise
        , base_accel_x_(1.0f)        // Forward acceleration
        , phase_(0.0f)
    {
        std::cout << "[IMU] Initialized (100Hz)\n";
    }

protected:
    IMUData process() override {
        // Simulate sinusoidal motion with noise
        phase_ += 0.1f;
        
        IMUData data{
            .accel_x = base_accel_x_ * std::sin(phase_) + accel_noise_(gen_),
            .accel_y = 0.2f * std::cos(phase_ * 0.5f) + accel_noise_(gen_),
            .accel_z = 9.81f + accel_noise_(gen_),  // Gravity + noise
            .gyro_x = 0.05f * std::sin(phase_ * 0.3f) + gyro_noise_(gen_),
            .gyro_y = 0.03f * std::cos(phase_ * 0.7f) + gyro_noise_(gen_),
            .gyro_z = 0.02f * std::sin(phase_ * 1.2f) + gyro_noise_(gen_)
        };
        
        return data;
    }

private:
    std::random_device rd_;
    std::mt19937 gen_;
    std::normal_distribution<float> accel_noise_;
    std::normal_distribution<float> gyro_noise_;
    float base_accel_x_;
    float phase_;
};

// ============================================================================
// Step 4: GPS Sensor Module (Low-Rate Producer)
// ============================================================================

/**
 * @brief Simulated GPS sensor producing data at 10Hz
 * 
 * Key features:
 * - PeriodicInput with 100ms period (10Hz)
 * - Generates realistic GPS coordinates
 * - Simulates vehicle trajectory
 */
class GPSSensor : public FusionApp::Module<
    commrat::Output<GPSData>,
    commrat::PeriodicInput
> {
public:
    GPSSensor(const commrat::ModuleConfig& config, double start_lat, double start_lon)
        : FusionApp::Module<commrat::Output<GPSData>, commrat::PeriodicInput>(config)
        , gen_(rd_())
        , pos_noise_(0.0, 0.00001)   // ~1m GPS noise
        , alt_noise_(0.0f, 2.0f)     // 2m altitude noise
        , lat_(start_lat)
        , lon_(start_lon)
        , alt_(100.0f)
        , speed_(15.0f)              // 15 m/s = 54 km/h
    {
        std::cout << "[GPS] Initialized (10Hz) at (" << lat_ << ", " << lon_ << ")\n";
    }

protected:
    GPSData process() override {
        // Simulate northward movement (increase latitude)
        lat_ += 0.00001;  // ~1.1m north per update
        lon_ += 0.000005; // ~0.5m east per update
        
        GPSData data{
            .latitude = lat_ + pos_noise_(gen_),
            .longitude = lon_ + pos_noise_(gen_),
            .altitude = alt_ + alt_noise_(gen_),
            .speed = speed_
        };
        
        return data;
    }

private:
    std::random_device rd_;
    std::mt19937 gen_;
    std::normal_distribution<double> pos_noise_;
    std::normal_distribution<float> alt_noise_;
    double lat_, lon_;
    float alt_;
    float speed_;
};

// ============================================================================
// Step 5: Multi-Input Fusion Module
// ============================================================================

/**
 * @brief Sensor fusion combining IMU (100Hz) and GPS (10Hz)
 * 
 * Key features:
 * - Inputs<IMUData, GPSData> declares multiple inputs
 * - PrimaryInput<IMUData> designates IMU as execution driver
 * - process(const IMUData&, const GPSData&) receives both inputs synchronized
 * - Uses metadata accessors to check GPS freshness
 * - Handles stale GPS data gracefully
 */
class SensorFusion : public FusionApp::Module<
    commrat::Output<FusedData>,
    commrat::Inputs<IMUData, GPSData>,
    commrat::PrimaryInput<IMUData>         // IMU drives execution (100Hz)
> {
public:
    SensorFusion(const commrat::ModuleConfig& config)
        : FusionApp::Module<
            commrat::Output<FusedData>,
            commrat::Inputs<IMUData, GPSData>,
            commrat::PrimaryInput<IMUData>
          >(config)
        , imu_count_(0)
        , gps_stale_warnings_(0)
    {
        std::cout << "[Fusion] Initialized with sync_tolerance=" 
                  << config.sync_tolerance.count() << "ms\n";
    }

protected:
    /**
     * @brief Fuse IMU and GPS data with time synchronization
     * 
     * Called at IMU rate (100Hz). GPS data is fetched via getData
     * synchronized to IMU timestamp.
     * 
     * @param imu Primary input (received via blocking receive)
     * @param gps Secondary input (fetched via getData)
     * @return Fused sensor data
     */
    FusedData process(const IMUData& imu, const GPSData& gps) override {
        imu_count_++;
        
        // ====================================================================
        // Access Input Metadata (Phase 6.10)
        // ====================================================================
        
        // Index-based access (always works)
        auto imu_meta = get_input_metadata<0>();  // IMU = index 0
        auto gps_meta = get_input_metadata<1>();  // GPS = index 1
        
        // Type-based access (cleaner when types unique)
        uint64_t imu_ts = get_input_timestamp<IMUData>();
        uint64_t gps_ts = get_input_timestamp<GPSData>();
        
        // Check GPS freshness
        bool gps_fresh = has_new_data<1>();       // Index-based
        bool gps_valid = is_input_valid<1>();     // getData succeeded?
        
        // Calculate GPS age
        uint64_t gps_age_ns = imu_ts - gps_ts;
        double gps_age_ms = gps_age_ns / 1'000'000.0;
        
        // ====================================================================
        // Freshness Warning
        // ====================================================================
        
        if (!gps_fresh && gps_stale_warnings_ < 5) {
            std::cout << "[Fusion] ⚠ GPS stale (age: " << gps_age_ms << " ms, "
                      << "reused from previous iteration)\n";
            gps_stale_warnings_++;
            if (gps_stale_warnings_ == 5) {
                std::cout << "[Fusion] (suppressing further stale warnings...)\n";
            }
        }
        
        if (!gps_valid) {
            std::cerr << "[Fusion] ❌ GPS getData FAILED!\n";
        }
        
        // ====================================================================
        // Sensor Fusion Algorithm (Simplified)
        // ====================================================================
        
        // Use GPS position directly
        double fused_lat = gps.latitude;
        double fused_lon = gps.longitude;
        float fused_alt = gps.altitude;
        
        // Estimate velocity from GPS speed and IMU acceleration
        float vel_x = gps.speed * std::cos(0.1f);  // Simplified
        float vel_y = gps.speed * std::sin(0.1f);
        float vel_z = 0.0f;
        
        // Use IMU acceleration directly
        float accel_x = imu.accel_x;
        float accel_y = imu.accel_y;
        float accel_z = imu.accel_z - 9.81f;  // Remove gravity
        
        // ====================================================================
        // Output Periodic Status
        // ====================================================================
        
        if (imu_count_ % 100 == 0) {  // Every second (100 samples @ 100Hz)
            std::cout << "[Fusion] #" << imu_count_ 
                      << " | IMU: [" << imu.accel_x << ", " << imu.accel_y << ", " << imu.accel_z << "] m/s²"
                      << " | GPS: (" << gps.latitude << ", " << gps.longitude << ") "
                      << (gps_fresh ? "✓fresh" : "⚠stale")
                      << " age=" << gps_age_ms << "ms\n";
        }
        
        // ====================================================================
        // Return Fused Data
        // ====================================================================
        
        return FusedData{
            .latitude = fused_lat,
            .longitude = fused_lon,
            .altitude = fused_alt,
            .velocity_x = vel_x,
            .velocity_y = vel_y,
            .velocity_z = vel_z,
            .acceleration_x = accel_x,
            .acceleration_y = accel_y,
            .acceleration_z = accel_z,
            .gps_fresh = gps_fresh,
            .imu_count = imu_count_
        };
    }

private:
    uint32_t imu_count_;
    uint32_t gps_stale_warnings_;
};

// ============================================================================
// Step 6: Fused Data Monitor
// ============================================================================

/**
 * @brief Monitor that displays fused data
 */
class FusionMonitor : public FusionApp::Module<
    commrat::Output<FusedData>,  // Pass-through
    commrat::Input<FusedData>
> {
public:
    FusionMonitor(const commrat::ModuleConfig& config)
        : FusionApp::Module<commrat::Output<FusedData>, commrat::Input<FusedData>>(config)
        , count_(0)
    {
        std::cout << "[Monitor] Initialized\n";
    }

protected:
    FusedData process_continuous(const FusedData& input) override {
        count_++;
        
        // Display every 10th sample (10Hz output for 100Hz input)
        if (count_ % 10 == 0) {
            std::cout << "[Monitor] Fused #" << input.imu_count
                      << " | Pos: (" << input.latitude << ", " << input.longitude 
                      << "), alt=" << input.altitude << "m"
                      << " | Vel: [" << input.velocity_x << ", " << input.velocity_y << "] m/s"
                      << " | GPS: " << (input.gps_fresh ? "✓" : "⚠") << "\n";
        }
        
        return input;
    }

private:
    uint32_t count_;
};

// ============================================================================
// Main: Demonstrate Multi-Input Fusion
// ============================================================================

int main() {
    std::cout << "=== CommRaT Example 03: Multi-Input Fusion ===\n\n";
    
    // Setup signal handler
    std::signal(SIGINT, signal_handler);
    
    // ========================================================================
    // Configure Sensors
    // ========================================================================
    
    // IMU: 100Hz (10ms period)
    commrat::ModuleConfig imu_config{
        .name = "IMU",
        .system_id = 10,
        .instance_id = 1,
        .period = commrat::Milliseconds(10)  // 100Hz
    };
    
    // GPS: 10Hz (100ms period)
    commrat::ModuleConfig gps_config{
        .name = "GPS",
        .system_id = 11,
        .instance_id = 1,
        .period = commrat::Milliseconds(100)  // 10Hz
    };
    
    // ========================================================================
    // Configure Fusion Module (Multi-Input)
    // ========================================================================
    
    commrat::ModuleConfig fusion_config{
        .name = "SensorFusion",
        .system_id = 20,
        .instance_id = 1,
        .input_sources = {
            {.system_id = 10, .instance_id = 1, .is_primary = true, .source_primary_output_type_id = FusionApp::get_message_id<IMUData>()},   // IMU (primary)
            {.system_id = 11, .instance_id = 1, .is_primary = false, .source_primary_output_type_id = FusionApp::get_message_id<GPSData>()}   // GPS (secondary)
        },
        .sync_tolerance = commrat::Milliseconds(100)  // 100ms tolerance for getData (GPS is 10Hz)
    };
    
    // ========================================================================
    // Configure Monitor
    // ========================================================================
    
    commrat::ModuleConfig monitor_config{
        .name = "FusionMonitor",
        .system_id = 30,
        .instance_id = 1,
        .source_system_id = 20,
        .source_instance_id = 1
    };
    
    // ========================================================================
    // Create and Start Modules
    // ========================================================================
    
    std::cout << "Creating modules...\n";
    IMUSensor imu(imu_config);
    GPSSensor gps(gps_config, 37.7749, -122.4194);  // San Francisco
    SensorFusion fusion(fusion_config);
    FusionMonitor monitor(monitor_config);
    
    std::cout << "\nStarting sensors...\n";
    imu.start();
    gps.start();
    
    // Give sensors time to initialize and build up data
    std::cout << "Waiting for producers to publish data...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Starting fusion...\n";
    fusion.start();
    
    // Wait for secondary input threads to populate buffers
    std::cout << "Waiting for fusion buffers to fill...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    std::cout << "Starting monitor...\n";
    monitor.start();
    
    // ========================================================================
    // Run Until Signal
    // ========================================================================
    
    std::cout << "\n🚗 Running sensor fusion... (Press Ctrl+C to stop)\n\n";
    
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // ========================================================================
    // Clean Shutdown
    // ========================================================================
    
    std::cout << "\n🛑 Shutting down...\n";
    
    // Stop in reverse order
    monitor.stop();
    fusion.stop();
    gps.stop();
    imu.stop();
    
    std::cout << "✅ Done!\n";
    
    return 0;
}
