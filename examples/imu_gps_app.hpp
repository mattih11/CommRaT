#pragma once
/**
 * @file imu_gps_app.hpp
 * @brief Shared application definition for the IMU + GPS fusion example.
 *
 * All binaries that participate in the same CommRaT application (imu_module,
 * gps_module, module_main_multiformat / FusionModule) must share this header
 * so that message IDs are identical across processes.
 */

#include <commrat/commrat.hpp>

// ============================================================================
// Message definitions
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
// Application registry — must be identical in every participant binary
// ============================================================================

using ImuGpsApp = commrat::CommRaT<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<FusedPose>
>;
