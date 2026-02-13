/**
 * @file module_main_multiformat.cpp
 * @brief Standalone module example demonstrating file and CLI configuration
 * 
 * Demonstrates:
 * - Multi-input synchronization (IMU + GPS)
 * - JSON config file or CLI args
 * - Primary input driven execution
 * - getData synchronization
 * 
 * Create config.json:
 * {
 *   "name": "FusionModule",
 *   "system_id": 30,
 *   "instance_id": 1,
 *   "period": 10000000,
 *   "input_sources": [
 *     {"system_id": 10, "instance_id": 1},
 *     {"system_id": 11, "instance_id": 1}
 *   ],
 *   "sync_tolerance": 50000000
 * }
 * 
 * Run:
 *   ./module_main_multiformat config.json
 *   ./module_main_multiformat --name "FusionModule" --system-id 30 --instance-id 1 --period 10000000
 */

#include <commrat/commrat.hpp>
#include <commrat/module_main.hpp>
#include <cstdint>
#include <cmath>

// Input message types
struct IMUData {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
};

struct GPSData {
    double latitude;
    double longitude;
    float altitude;
    float speed;
};

// Output message (fused)
struct FusedPose {
    double latitude;
    double longitude;
    float altitude;
    float velocity_x;
    float velocity_y;
    float velocity_z;
    bool gps_valid;
};

// Application definition
using MyApp = commrat::CommRaT<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<FusedPose>
>;

/**
 * @brief Sensor fusion module - combines IMU and GPS
 * 
 * Demonstrates:
 * - Multi-input (IMU primary, GPS secondary)
 * - getData synchronization to IMU timestamp
 * - Metadata access (is_valid, has_new_data)
 * - Config loading from JSON file or CLI
 */
class FusionModule : public MyApp::Module<
    commrat::Output<FusedPose>,
    commrat::Inputs<IMUData, GPSData>  // IMU first = primary
> {
public:
    using MyApp::Module<commrat::Output<FusedPose>, commrat::Inputs<IMUData, GPSData>>::Module;

protected:
    void process(const IMUData& imu, const GPSData& gps, FusedPose& output) override {
        // Check GPS validity
        auto gps_meta = get_input_metadata<1>();
        bool gps_valid = gps_meta.is_valid && gps_meta.is_new_data;
        
        // Simple fusion (in real system: Kalman filter)
        
        if (gps_valid) {
            // GPS available - use it
            output.latitude = gps.latitude;
            output.longitude = gps.longitude;
            output.altitude = gps.altitude;
            
            // Integrate IMU for velocity (simplified)
            output.velocity_x = imu.accel_x * 0.01f;  // dt = 10ms
            output.velocity_y = imu.accel_y * 0.01f;
            output.velocity_z = imu.accel_z * 0.01f;
            
            output.gps_valid = true;
            
            std::cout << "Fused (GPS valid): lat=" << output.latitude 
                      << ", lon=" << output.longitude 
                      << ", vel=(" << output.velocity_x << "," << output.velocity_y << ")\n";
        } else {
            // GPS unavailable - dead reckoning from IMU only
            output.latitude = last_latitude_;
            output.longitude = last_longitude_;
            output.altitude = last_altitude_;
            
            // Integrate IMU
            output.velocity_x = imu.accel_x * 0.01f;
            output.velocity_y = imu.accel_y * 0.01f;
            output.velocity_z = imu.accel_z * 0.01f;
            
            output.gps_valid = false;
            
            std::cout << "Fused (GPS invalid): IMU-only dead reckoning\n";
        }
        
        // Remember last valid GPS
        if (gps_valid) {
            last_latitude_ = gps.latitude;
            last_longitude_ = gps.longitude;
            last_altitude_ = gps.altitude;
        }
    }

private:
    double last_latitude_ = 0.0;
    double last_longitude_ = 0.0;
    float last_altitude_ = 0.0f;
};

// Deploy as standalone binary - supports JSON config or CLI args!
COMMRAT_MODULE_MAIN(FusionModule, MyApp)
