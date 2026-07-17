/**
 * @file gps_module.cpp
 * @brief Standalone GPS producer module for the IMU+GPS fusion example.
 *
 * Generates simulated GPS data at 10 Hz.
 *
 * Run standalone:
 *   ./gps_module  gps_config.json
 *
 * Or start via the fusion launcher:
 *   ./imu_gps_fusion_launcher  examples/configs/imu_gps_fusion_app.json
 */

#include "imu_gps_app.hpp"
#include <commrat/module_main.hpp>
#include <cmath>

class GPSModule : public ImuGpsApp::Module2<
    commrat::Output<GPSData>,
    commrat::Period<100>  // 10 Hz default
> {
public:
    using ImuGpsApp::Module2<commrat::Output<GPSData>, commrat::Period<100>>::Module2;

protected:
    void process(GPSData& output) override {
        float t = counter_++ * 0.1f;
        output = GPSData{
            .latitude  = 47.3769 + std::sin(t * 0.05) * 0.001,
            .longitude = 8.5417  + std::cos(t * 0.05) * 0.001,
            .altitude  = 400.0f  + std::sin(t * 0.3f) * 5.0f,
            .speed     = 1.5f    + std::sin(t) * 0.2f
        };
        if (counter_ % 10 == 0)
            std::cout << "[GPS] sample " << counter_
                      << " lat=" << output.latitude << "\n";
    }

private:
    uint32_t counter_{0};
};

COMMRAT_MODULE_MAIN(GPSModule)
