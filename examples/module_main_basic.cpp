/**
 * @file module_main_basic.cpp
 * @brief Basic standalone module example using COMMRAT_MODULE_MAIN
 * 
 * Demonstrates minimal usage of module_main wrapper:
 * - Periodic data generation
 * - CLI argument parsing
 * - Signal handling (Ctrl+C)
 * 
 * Run:
 *   ./module_main_basic --name "BasicSensor" --system-id 10 --instance-id 1 --period 100000000
 *   ./module_main_basic --help
 */

#include <commrat/examples/all_examples_app.hpp>
#include <commrat/module_main.hpp>

using namespace sensor_filter_example;
using MyApp = AllExamplesApp;

/**
 * @brief Basic sensor module - generates periodic data
 * 
 * This is a minimal example showing:
 * - PeriodicInput (driven by config.period)
 * - Single output (SensorData)
 * - Simple data generation
 */
class BasicSensorModule : public MyApp::Module2<
    commrat::Output<SensorData>,
    commrat::Period<100>  // Default period of 100ms (can be overridden via CLI)
> {
public:
    using MyApp::Module2<commrat::Output<SensorData>, commrat::Period<100>>::Module2;

protected:
    void process(SensorData& output) override {
        // Generate dummy sensor data
        static uint64_t counter = 0;
        ++counter;
        
        output = SensorData{
            .sensor_id = config_.system_id(),
            .temperature_c = 20.0f + (counter % 10) * 0.5f,
            .humidity_percent = 45.0f + (counter % 20) * 1.0f
        };
        
        RTLOG_INFO(logger_) << "Published: temp=" << output.temperature_c
                            << "C, humidity=" << output.humidity_percent
                            << "% [" << counter << "]";
    }
};

// Deploy as standalone binary!
COMMRAT_MODULE_MAIN(BasicSensorModule)
