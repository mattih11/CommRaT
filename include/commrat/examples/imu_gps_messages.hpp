#pragma once
/**
 * @file imu_gps_messages.hpp
 * @brief Shared message types for the IMU + GPS fusion example.
 *
 * Include this in every binary that participates in an IMU/GPS fusion
 * application so that message IDs are identical across processes.
 *
 * For running ALL examples together, prefer all_examples_app.hpp which
 * combines these types with every other example registry.
 */

#include <commrat/commrat.hpp>

namespace imu_gps_example {

// ============================================================================
// Payload definitions (POD)
// ============================================================================

struct IMUData {
    float accel_x{0.0f};
    float accel_y{0.0f};
    float accel_z{0.0f};
    float gyro_x{0.0f};
    float gyro_y{0.0f};
    float gyro_z{0.0f};
};

struct GPSData {
    double latitude{0.0};
    double longitude{0.0};
    float  altitude{0.0f};
    float  speed{0.0f};
};

struct FusedPose {
    double latitude{0.0};
    double longitude{0.0};
    float  altitude{0.0f};
    float  velocity_x{0.0f};
    float  velocity_y{0.0f};
    float  velocity_z{0.0f};
    bool   gps_valid{false};
};

// ============================================================================
// Application registry (standalone usage)
// ============================================================================

using ImuGpsApp = commrat::CommRaT<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<FusedPose>
>;

} // namespace imu_gps_example
