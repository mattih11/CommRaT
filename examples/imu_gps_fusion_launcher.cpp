/**
 * @file imu_gps_fusion_launcher.cpp
 * @brief Process launcher for the IMU + GPS sensor fusion example system.
 *
 * Spawns IMUModule (100 Hz), GPSModule (10 Hz), and FusionModule as separate
 * child processes, wiring them together via imu_gps_fusion_app.json.
 *
 * Run:
 *   ./imu_gps_fusion_launcher  examples/configs/imu_gps_fusion_app.json
 *
 * Run for 3 seconds (e.g. for CTest):
 *   ./imu_gps_fusion_launcher  examples/configs/imu_gps_fusion_app.json  --duration-ms 3000
 */

#include <commrat/launcher/process_launcher.hpp>

int main(int argc, char** argv) {
    return commrat::ProcessLauncher::main(argc, argv);
}
