/**
 * @file multi_input_fusion.cpp
 * @brief Example 03: Multi-Input Fusion with Time Synchronization
 * 
 * This example demonstrates:
 * 1. Multi-input processing (Inputs<IMUData, GPSData>)
 * 2. Primary input designation (PrimaryInput<IMUData>)
 * 3. Synchronized get_data for secondary inputs
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
class IMUSensor : public FusionApp::Module2<
    commrat::Output<IMUData>,
    commrat::Period<10>
> {
public:
    IMUSensor(const commrat::ModuleConfig& config)
        : FusionApp::Module2<commrat::Output<IMUData>, commrat::Period<10>>(config)
        , gen_(rd_())
        , accel_noise_(0.0f, 0.05f)  // 0.05 m/s² noise
        , gyro_noise_(0.0f, 0.01f)   // 0.01 rad/s noise
        , base_accel_x_(1.0f)        // Forward acceleration
        , phase_(0.0f)
    {
        std::cout << "[IMU] Initialized (100Hz)\n";
    }

protected:
    void process(IMUData& output) override {
        // Simulate sinusoidal motion with noise
        phase_ += 0.1f;
        
        output = IMUData{
            .accel_x = base_accel_x_ * std::sin(phase_) + accel_noise_(gen_),
            .accel_y = 0.2f * std::cos(phase_ * 0.5f) + accel_noise_(gen_),
            .accel_z = 9.81f + accel_noise_(gen_),  // Gravity + noise
            .gyro_x = 0.05f * std::sin(phase_ * 0.3f) + gyro_noise_(gen_),
            .gyro_y = 0.03f * std::cos(phase_ * 0.7f) + gyro_noise_(gen_),
            .gyro_z = 0.02f * std::sin(phase_ * 1.2f) + gyro_noise_(gen_)
        };
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
class GPSSensor : public FusionApp::Module2<
    commrat::Output<GPSData>,
    commrat::Period<100>
> {
public:
    GPSSensor(const commrat::ModuleConfig& config, double start_lat, double start_lon)
        : FusionApp::Module2<commrat::Output<GPSData>, commrat::Period<100>>(config)
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
    void process(GPSData& output) override {
        // Simulate northward movement (increase latitude)
        lat_ += 0.00001;  // ~1.1m north per update
        lon_ += 0.000005; // ~0.5m east per update
        
        output = GPSData{
            .latitude = lat_ + pos_noise_(gen_),
            .longitude = lon_ + pos_noise_(gen_),
            .altitude = alt_ + alt_noise_(gen_),
            .speed = speed_
        };
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
 * - IMU (first) is automatically primary - drives execution at 100Hz
 * - process_multi_input(const IMUData&, const GPSData&) receives both inputs synchronized
 * - Uses metadata accessors to check GPS freshness
 * - Handles stale GPS data gracefully
 */
class SensorFusion : public FusionApp::Module2<
    commrat::Output<FusedData>,
    commrat::Input<IMUData>, commrat::SyncedInput<GPSData>  // IMU (first) is automatically primary
> {
public:
    SensorFusion(const commrat::ModuleConfig& config)
        : FusionApp::Module2<
            commrat::Output<FusedData>,
            commrat::Input<IMUData>, commrat::SyncedInput<GPSData>
          >(config)
        , imu_count_(0)
        , gps_stale_warnings_(0)
        , gps_invalid_warnings_(0)
    {
        std::cout << "[Fusion] Initialized\n";
    }

protected:
    /**
     * @brief Fuse IMU and GPS data with time synchronization
     * 
     * Called at IMU rate (100Hz). GPS data is fetched via get_data
     * synchronized to IMU timestamp.
     * 
     * @param imu Primary input (received via blocking receive)
     * @param gps Secondary input (fetched via get_data, wrapped in Synced<>)
     * @return Fused sensor data
     */
    void process(const IMUData& imu, const commrat::Synced<GPSData>& gps, FusedData& output) override {
        imu_count_++;
        
        // ====================================================================
        // Access Input Metadata
        // ====================================================================
        
        // Index-based timestamp access
        uint64_t imu_ts = get_input_timestamp<0>();
        uint64_t gps_ts = get_input_timestamp<1>();
        
        // Check GPS freshness via Synced<> wrapper
        bool gps_fresh = gps.is_fresh();    // Exact timestamp match
        bool gps_valid = gps.is_valid();    // Any data available
        
        // Calculate GPS age
        uint64_t gps_age_ns = imu_ts - gps_ts;
        double gps_age_ms = gps_age_ns / 1'000'000.0;
        
        // ====================================================================
        // Freshness Warning
        // ====================================================================
        
        if (!gps_fresh && gps_stale_warnings_ < 5) {
            std::cout << "[Fusion] GPS stale (age: " << gps_age_ms << " ms) "
                      << "- expected at " << (gps_valid ? "multi-rate" : "startup") << "\n";
            gps_stale_warnings_++;
            if (gps_stale_warnings_ == 5) {
                std::cout << "[Fusion] (suppressing further stale messages...)\n";
            }
        }
        
        if (!gps_valid && gps_invalid_warnings_ < 3) {
            std::cerr << "[Fusion] WARNING: GPS data unavailable "
                      << "(tolerance may be too tight for GPS rate)\n";
            gps_invalid_warnings_++;
        }
        
        // ====================================================================
        // Sensor Fusion Algorithm (Simplified)
        // ====================================================================
        
        // Access GPS data via Synced wrapper (use stale_or for fallback)
        GPSData gps_data = gps.stale_or(GPSData{37.7749, -122.4194, 100.0f, 0.0f});
        
        // Use GPS position directly
        double fused_lat = gps_data.latitude;
        double fused_lon = gps_data.longitude;
        float fused_alt = gps_data.altitude;
        
        // Estimate velocity from GPS speed and IMU acceleration
        float vel_x = gps_data.speed * std::cos(0.1f);  // Simplified
        float vel_y = gps_data.speed * std::sin(0.1f);
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
                      << " | GPS: (" << gps_data.latitude << ", " << gps_data.longitude << ") "
                      << (gps_fresh ? "fresh" : "stale")
                      << " age=" << gps_age_ms << "ms\n";
        }
        
        // ====================================================================
        // Return Fused Data
        // ====================================================================
        
        output = FusedData{
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
    uint32_t gps_invalid_warnings_;
};

// ============================================================================
// Step 6: Fused Data Monitor
// ============================================================================

/**
 * @brief Monitor that displays fused data
 */
class FusionMonitor : public FusionApp::Module2<
    commrat::Output<FusedData>,  // Pass-through
    commrat::Input<FusedData>
> {
public:
    FusionMonitor(const commrat::ModuleConfig& config)
        : FusionApp::Module2<commrat::Output<FusedData>, commrat::Input<FusedData>>(config)
        , count_(0)
    {
        std::cout << "[Monitor] Initialized\n";
    }

protected:
    void process(const FusedData& input, FusedData& output) override {
        count_++;
        
        // Display every 10th sample (10Hz output for 100Hz input)
        if (count_ % 10 == 0) {
            std::cout << "[Monitor] Fused #" << input.imu_count
                      << " | Pos: (" << input.latitude << ", " << input.longitude 
                      << "), alt=" << input.altitude << "m"
                      << " | Vel: [" << input.velocity_x << ", " << input.velocity_y << "] m/s"
                      << " | GPS: " << (input.gps_fresh ? "fresh" : "stale") << "\n";
        }
        
        output = input;
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
        .outputs = commrat::SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = commrat::NoInputConfig{},
        .period = std::chrono::milliseconds(10)  // 100Hz
    };
    
    // GPS: 10Hz (100ms period)
    commrat::ModuleConfig gps_config{
        .name = "GPS",
        .outputs = commrat::SimpleOutputConfig{.system_id = 11, .instance_id = 1},
        .inputs = commrat::NoInputConfig{},
        .period = std::chrono::milliseconds(100)  // 10Hz
    };
    
    // ========================================================================
    // Configure Fusion Module (Multi-Input)
    // ========================================================================
    
    // NEW: Multi-input uses index-based type inference - no source_primary_output_type_id needed!
    // sync_tolerance must be >= producer period to avoid gaps.
    // GPS at 10Hz (100ms period) needs >= 100ms tolerance; 150ms adds margin for jitter.
    commrat::ModuleConfig fusion_config{
        .name = "SensorFusion",
        .outputs = commrat::SimpleOutputConfig{.system_id = 20, .instance_id = 1},
        .inputs = commrat::MultiInputConfig{
            .sources = {
                {.system_id = 10, .instance_id = 1},  // IMU (primary, first in list)
                {.system_id = 11, .instance_id = 1}   // GPS (secondary)
            },
            .sync_tolerance = std::chrono::milliseconds(150)  // >= GPS period (100ms) + jitter margin
        }
    };
    
    // ========================================================================
    // Configure Monitor
    // ========================================================================
    
    commrat::ModuleConfig monitor_config{
        .name = "FusionMonitor",
        .outputs = commrat::SimpleOutputConfig{.system_id = 30, .instance_id = 1},
        .inputs = commrat::SingleInputConfig{.source_system_id = 20, .source_instance_id = 1}
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
    commrat::Time::sleep(commrat::Milliseconds(500));
    
    std::cout << "Starting fusion...\n";
    fusion.start();
    
    // Wait for secondary input threads to populate buffers
    std::cout << "Waiting for fusion buffers to fill...\n";
    commrat::Time::sleep(commrat::Milliseconds(500));
    
    std::cout << "Starting monitor...\n";
    monitor.start();
    
    // ========================================================================
    // Run Until Signal
    // ========================================================================
    
    std::cout << "\nRunning sensor fusion... (Press Ctrl+C to stop)\n\n";
    
    while (!shutdown_requested.load()) {
        commrat::Time::sleep(commrat::Milliseconds(100));
    }
    
    // ========================================================================
    // Clean Shutdown
    // ========================================================================
    
    std::cout << "\n[STOP] Shutting down...\n";
    
    // Stop in reverse order
    monitor.stop();
    fusion.stop();
    gps.stop();
    imu.stop();
    
    std::cout << "[OK] Done!\n";
    
    return 0;
}
