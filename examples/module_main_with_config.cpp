/**
 * @file module_main_with_config.cpp
 * @brief Standalone module example using file-based configuration
 * 
 * Demonstrates JSON config file loading:
 * - Continuous input processing
 * - Configuration from file
 * - Subscription to another module
 * 
 * Create config.json:
 * {
 *   "name": "FilterModule",
 *   "system_id": 20,
 *   "instance_id": 1,
 *   "input_sources": [
 *     {"system_id": 10, "instance_id": 1}
 *   ]
 * }
 * 
 * Run:
 *   ./module_main_with_config config.json
 */

#include <commrat/commrat.hpp>
#include <commrat/module_main.hpp>
#include <cstdint>
#include <cmath>

// Input message (from producer)
struct SensorData {
    uint64_t sensor_id;
    float temperature_c;
    float humidity_percent;
};

// Output message (filtered)
struct FilteredData {
    uint64_t sensor_id;
    float smoothed_temperature_c;
    float smoothed_humidity_percent;
    uint32_t sample_count;
};

// Application definition
using MyApp = commrat::CommRaT<
    commrat::Message::Data<SensorData>,
    commrat::Message::Data<FilteredData>
>;

/**
 * @brief Filter module - applies moving average to sensor data
 * 
 * Demonstrates:
 * - Continuous input processing (Input<T>)
 * - Single output (FilteredData)
 * - Subscription via config.input_sources
 */
class FilterModule : public MyApp::Module<
    commrat::Output<FilteredData>,
    commrat::Input<SensorData>
> {
public:
    using MyApp::Module<commrat::Output<FilteredData>, commrat::Input<SensorData>>::Module;

protected:
    void process(const SensorData& input, FilteredData& output) override {
        // Simple exponential moving average
        constexpr float alpha = 0.3f;  // Smoothing factor
        
        if (sample_count_ == 0) {
            // First sample - initialize
            avg_temp_ = input.temperature_c;
            avg_humidity_ = input.humidity_percent;
        } else {
            // Update moving average
            avg_temp_ = alpha * input.temperature_c + (1.0f - alpha) * avg_temp_;
            avg_humidity_ = alpha * input.humidity_percent + (1.0f - alpha) * avg_humidity_;
        }
        
        ++sample_count_;
        
        output = FilteredData{
            .sensor_id = input.sensor_id,
            .smoothed_temperature_c = avg_temp_,
            .smoothed_humidity_percent = avg_humidity_,
            .sample_count = sample_count_
        };
        
        std::cout << "Filtered: temp=" << output.smoothed_temperature_c 
                  << "°C (raw=" << input.temperature_c 
                  << "), humidity=" << output.smoothed_humidity_percent 
                  << "% [" << sample_count_ << "]\n";
    }

private:
    float avg_temp_ = 0.0f;
    float avg_humidity_ = 0.0f;
    uint32_t sample_count_ = 0;
};

// Deploy as standalone binary with config file!
COMMRAT_MODULE_MAIN(FilterModule, MyApp)

// Debug: Print message IDs at compile time
static_assert(MyApp::get_message_id<SensorData>() != 0, "SensorData has ID");
static_assert(MyApp::get_message_id<FilteredData>() != 0, "FilteredData has ID");
