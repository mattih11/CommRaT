/**
 * @file module_main_multiformat.cpp
 * @brief Standalone module example demonstrating file and CLI configuration
 * 
 * Demonstrates:
 * - Multi-input synchronization (IMU + GPS)
 * - JSON config file or CLI args
 * - Primary input driven execution
 * - get_data synchronization
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

#include "imu_gps_app.hpp"
#include <commrat/module_main.hpp>
#include <cstdint>
#include <cmath>

/**
 * @brief Sensor fusion module - combines IMU and GPS
 * 
 * Demonstrates:
 * - Multi-input (IMU primary, GPS secondary)
 * - get_data synchronization to IMU timestamp
 * - Metadata access (is_valid, has_new_data)
 * - Config loading from JSON file or CLI
 */
class FusionModule : public ImuGpsApp::Module2<
    commrat::Output<FusedPose>,
    commrat::Input<IMUData>,  // IMU first = primary
    commrat::SyncedInput<GPSData>   // GPS second = secondary
> {
public:
    using ImuGpsApp::Module2<commrat::Output<FusedPose>, commrat::Input<IMUData>, commrat::SyncedInput<GPSData>>::Module2;

protected:
    void process(const IMUData& imu, const commrat::Synced<GPSData>& gps, FusedPose& output) override {
        // Check GPS validity.
        bool gps_valid = gps.is_valid() && gps.is_fresh();
        
        // Simple fusion (in real system: Kalman filter)
        
        if (gps_valid) {
            // GPS available - use it
            output.latitude = gps->latitude;
            output.longitude = gps->longitude;
            output.altitude = gps->altitude;
            
            // Integrate IMU for velocity (simplified)
            output.velocity_x = imu.accel_x * 0.01f;  // dt = 10ms
            output.velocity_y = imu.accel_y * 0.01f;
            output.velocity_z = imu.accel_z * 0.01f;
            
            output.gps_valid = true;
            
            RTLOG_INFO(logger_) << "Fused (GPS valid): lat=" << output.latitude
                                << " vel_x=" << output.velocity_x
                                << " vel_y=" << output.velocity_y;
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
            
            RTLOG_INFO(logger_) << "Fused (GPS invalid): IMU-only dead reckoning";
        }
        
        // Remember last valid GPS
        if (gps_valid) {
            last_latitude_ = gps->latitude;
            last_longitude_ = gps->longitude;
            last_altitude_ = gps->altitude;
        }
    }

private:
    double last_latitude_ = 0.0;
    double last_longitude_ = 0.0;
    float last_altitude_ = 0.0f;
};

// Deploy as standalone binary - supports JSON config or CLI args!
COMMRAT_MODULE_MAIN(FusionModule)
