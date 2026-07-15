/**
 * @file sensor_filter_launcher.cpp
 * @brief Process launcher for the sensor + filter example system.
 *
 * Spawns BasicSensorModule and FilterModule as separate child processes,
 * wiring them together via the AppDescription in sensor_filter_app.json.
 *
 * cmake's commrat_module() generates *.module.json descriptor files
 * alongside each binary; this launcher discovers them automatically from
 * its own directory.
 *
 * Run:
 *   ./sensor_filter_launcher  examples/configs/sensor_filter_app.json
 *
 * Run for 3 seconds (e.g. for CTest):
 *   ./sensor_filter_launcher  examples/configs/sensor_filter_app.json  --duration-ms 3000
 */

#include <commrat/launcher/process_launcher.hpp>

int main(int argc, char** argv) {
    return commrat::ProcessLauncher::main(argc, argv);
}
