/**
 * @file imu_module.cpp
 * @brief Standalone IMU producer module for the IMU+GPS fusion example.
 *
 * Generates simulated IMU data at 100 Hz.
 *
 * Run standalone:
 *   ./imu_module  imu_config.json
 *
 * Or start via the fusion launcher:
 *   ./imu_gps_fusion_launcher  examples/configs/imu_gps_fusion_app.json
 */

#include "imu_gps_app.hpp"
#include <commrat/module_main.hpp>
#include <cmath>

class IMUModule : public ImuGpsApp::Module2<
    commrat::Output<IMUData>,
    commrat::Period<10>  // 100 Hz default
> {
public:
    using ImuGpsApp::Module2<commrat::Output<IMUData>, commrat::Period<10>>::Module2;

protected:
    void process(IMUData& output) override {
        float t = counter_++ * 0.01f;
        output = IMUData{
            .accel_x = std::sin(t) * 9.8f,
            .accel_y = std::cos(t) * 9.8f,
            .accel_z = 9.8f + std::sin(t * 2.0f) * 0.1f,
            .gyro_x  = std::cos(t) * 0.1f,
            .gyro_y  = std::sin(t) * 0.1f,
            .gyro_z  = std::sin(t * 0.5f) * 0.05f
        };
        if (counter_ % 100 == 0)
            std::cout << "[IMU] sample " << counter_ << "\n";
    }

private:
    uint32_t counter_{0};
};

COMMRAT_MODULE_MAIN(IMUModule)
