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

#include <commrat/commrat.hpp>
#include <commrat/module_main.hpp>
#include <cstdint>

// Message definitions
struct SensorData {
    uint64_t sensor_id;
    float temperature_c;
    float humidity_percent;
};

// Forward declare FilteredData (even though this module doesn't use it)
struct FilteredData {
    uint32_t sensor_id;
    float smoothed_temperature_c;
    float smoothed_humidity_percent;
    uint32_t sample_count;
};

// Application definition - MUST MATCH filter's registry!
using MyApp = commrat::CommRaT<
    commrat::Message::Data<SensorData>,
    commrat::Message::Data<FilteredData>
>;

/**
 * @brief Basic sensor module - generates periodic data
 * 
 * This is a minimal example showing:
 * - PeriodicInput (driven by config.period)
 * - Single output (SensorData)
 * - Simple data generation
 */
class BasicSensorModule : public MyApp::Module<
    commrat::Output<SensorData>,
    commrat::PeriodicInput
> {
public:
    using MyApp::Module<commrat::Output<SensorData>, commrat::PeriodicInput>::Module;

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
        
        std::cout << "Published: temp=" << output.temperature_c 
                  << "°C, humidity=" << output.humidity_percent 
                  << "% [" << counter << "]\n";
    }
};

// Deploy as standalone binary!
COMMRAT_MODULE_MAIN(BasicSensorModule, MyApp)
