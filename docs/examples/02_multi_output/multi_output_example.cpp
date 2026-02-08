/**
 * @file multi_output_example.cpp
 * @brief Example 02: Multi-Output Modules with Type-Specific Delivery
 * 
 * This example demonstrates:
 * 1. Multi-output producer (Outputs<TemperatureData, PressureData>)
 * 2. Two consumers, each receiving only their subscribed type
 * 3. Type-specific message delivery
 * 4. Proper configuration for multi-output producers
 * 
 * Architecture:
 *   WeatherStation (producer) → TemperatureData → TemperatureMonitor
 *                             → PressureData    → PressureMonitor
 * 
 * @author CommRaT Documentation Team
 * @date February 8, 2026
 */

#include <commrat/commrat.hpp>
#include <iostream>
#include <random>
#include <csignal>
#include <atomic>

// ============================================================================
// Signal handling for clean shutdown
// ============================================================================

std::atomic<bool> shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[SIGNAL] Ctrl+C received, shutting down...\n";
        shutdown_requested.store(true);
    }
}

// ============================================================================
// Step 1: Define Message Types
// ============================================================================

// Temperature reading (first output type)
struct TemperatureData {
    uint32_t station_id;
    float temperature_c;
    float humidity_percent;
};

// Pressure reading (second output type)
struct PressureData {
    uint32_t station_id;
    float pressure_hpa;
    float altitude_m;
};

// ============================================================================
// Step 2: Register Messages in Application
// ============================================================================

using WeatherApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,  // First = primary output type
    commrat::Message::Data<PressureData>
>;

// ============================================================================
// Step 3: Multi-Output Producer Module
// ============================================================================

/**
 * @brief Weather station that produces both temperature and pressure data
 * 
 * Key features:
 * - Uses Outputs<T, U> for multiple output types
 * - PeriodicInput generates data at fixed rate (10Hz)
 * - process(T& out1, U& out2) fills both outputs by reference
 * - Each subscriber receives only their expected message type
 */
class WeatherStation : public WeatherApp::Module<
    commrat::Outputs<TemperatureData, PressureData>,  // Multi-output!
    commrat::PeriodicInput
> {
public:
    WeatherStation(const commrat::ModuleConfig& config, uint32_t station_id)
        : WeatherApp::Module<commrat::Outputs<TemperatureData, PressureData>, commrat::PeriodicInput>(config)
        , station_id_(station_id)
        , gen_(rd_())
        , temp_dist_(18.0f, 28.0f)     // 18-28°C
        , humidity_dist_(30.0f, 70.0f) // 30-70%
        , pressure_dist_(980.0f, 1020.0f) // 980-1020 hPa
        , altitude_dist_(0.0f, 500.0f) // 0-500m
    {
        std::cout << "[WeatherStation] Initialized station_id=" << station_id_ << "\n";
    }

protected:
    /**
     * @brief Generate both temperature and pressure data
     * 
     * Multi-output process signature:
     * - void return type (outputs passed by reference)
     * - One reference parameter per output type
     * - Called every config_.period
     */
    void process(TemperatureData& temp_out, PressureData& pressure_out) override {
        // Generate temperature data
        temp_out = TemperatureData{
            .station_id = station_id_,
            .temperature_c = temp_dist_(gen_),
            .humidity_percent = humidity_dist_(gen_)
        };
        
        // Generate pressure data
        pressure_out = PressureData{
            .station_id = station_id_,
            .pressure_hpa = pressure_dist_(gen_),
            .altitude_m = altitude_dist_(gen_)
        };
        
        std::cout << "[WeatherStation] Generated: Temp=" << temp_out.temperature_c 
                  << "°C, Pressure=" << pressure_out.pressure_hpa << " hPa\n";
    }

private:
    uint32_t station_id_;
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<float> temp_dist_;
    std::uniform_real_distribution<float> humidity_dist_;
    std::uniform_real_distribution<float> pressure_dist_;
    std::uniform_real_distribution<float> altitude_dist_;
};

// ============================================================================
// Step 4: Temperature Consumer Module
// ============================================================================

/**
 * @brief Monitor that receives ONLY TemperatureData
 * 
 * Key features:
 * - Input<TemperatureData> specifies expected type
 * - Receives only temperature messages (pressure filtered out)
 * - Pass-through pattern: Output<TemperatureData> returns input
 */
class TemperatureMonitor : public WeatherApp::Module<
    commrat::Output<TemperatureData>,  // Pass-through output
    commrat::Input<TemperatureData>
> {
public:
    TemperatureMonitor(const commrat::ModuleConfig& config)
        : WeatherApp::Module<commrat::Output<TemperatureData>, commrat::Input<TemperatureData>>(config)
        , count_(0)
    {
        std::cout << "[TempMonitor] Initialized\n";
    }

protected:
    TemperatureData process_continuous(const TemperatureData& input) override {
        count_++;
        
        std::cout << "[TempMonitor] #" << count_ 
                  << " Station " << input.station_id
                  << ": " << input.temperature_c << "°C"
                  << ", " << input.humidity_percent << "% humidity";
        
        // Check for extreme conditions
        if (input.temperature_c > 26.0f) {
            std::cout << " ⚠ HIGH TEMP";
        }
        if (input.temperature_c < 20.0f) {
            std::cout << " ❄ LOW TEMP";
        }
        
        std::cout << "\n";
        
        return input;  // Pass-through
    }

private:
    uint32_t count_;
};

// ============================================================================
// Step 5: Pressure Consumer Module
// ============================================================================

/**
 * @brief Monitor that receives ONLY PressureData
 * 
 * Key features:
 * - Input<PressureData> specifies expected type
 * - Receives only pressure messages (temperature filtered out)
 * - Pass-through pattern for consistency
 */
class PressureMonitor : public WeatherApp::Module<
    commrat::Output<PressureData>,
    commrat::Input<PressureData>
> {
public:
    PressureMonitor(const commrat::ModuleConfig& config)
        : WeatherApp::Module<commrat::Output<PressureData>, commrat::Input<PressureData>>(config)
        , count_(0)
    {
        std::cout << "[PressureMonitor] Initialized\n";
    }

protected:
    PressureData process_continuous(const PressureData& input) override {
        count_++;
        
        std::cout << "[PressureMonitor] #" << count_
                  << " Station " << input.station_id
                  << ": " << input.pressure_hpa << " hPa"
                  << ", alt=" << input.altitude_m << "m";
        
        // Check for extreme conditions
        if (input.pressure_hpa < 990.0f) {
            std::cout << " 🌧 LOW PRESSURE";
        }
        if (input.pressure_hpa > 1010.0f) {
            std::cout << " ☀ HIGH PRESSURE";
        }
        
        std::cout << "\n";
        
        return input;  // Pass-through
    }

private:
    uint32_t count_;
};

// ============================================================================
// Main: Demonstrate Multi-Output with Type-Specific Delivery
// ============================================================================

int main() {
    std::cout << "=== CommRaT Example 02: Multi-Output Modules ===\n\n";
    
    // Setup signal handler
    std::signal(SIGINT, signal_handler);
    
    // ========================================================================
    // Configure Multi-Output Producer
    // ========================================================================
    
    commrat::ModuleConfig station_config{
        .name = "WeatherStation",
        .system_id = 10,           // Weather station system
        .instance_id = 1,
        .period = commrat::Milliseconds(100)  // 10Hz data generation
    };
    
    // ========================================================================
    // Configure Temperature Consumer
    // ========================================================================
    
    // Temperature monitor subscribes to system 10, instance 1
    // Base address encodes TemperatureData type (first output = primary)
    commrat::ModuleConfig temp_monitor_config{
        .name = "TempMonitor",
        .system_id = 20,
        .instance_id = 1,
        .source_system_id = 10,    // Subscribe to WeatherStation
        .source_instance_id = 1
        // No source_primary_output_type_id needed - TemperatureData is primary!
    };
    
    // ========================================================================
    // Configure Pressure Consumer
    // ========================================================================
    
    // Pressure monitor MUST specify source_primary_output_type_id!
    // Why? Producer's base address uses TemperatureData (first output),
    // but this consumer expects PressureData. We need to tell it where
    // to find the producer.
    commrat::ModuleConfig pressure_monitor_config{
        .name = "PressureMonitor",
        .system_id = 21,
        .instance_id = 1,
        .source_system_id = 10,
        .source_instance_id = 1,
        .source_primary_output_type_id = WeatherApp::get_message_id<TemperatureData>()  // KEY!
    };
    
    // ========================================================================
    // Create and Start Modules
    // ========================================================================
    
    std::cout << "Creating modules...\n";
    WeatherStation station(station_config, 1);
    TemperatureMonitor temp_monitor(temp_monitor_config);
    PressureMonitor pressure_monitor(pressure_monitor_config);
    
    std::cout << "\nStarting producer...\n";
    station.start();
    
    // Give producer time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "Starting consumers...\n";
    temp_monitor.start();
    pressure_monitor.start();
    
    // ========================================================================
    // Run Until Signal
    // ========================================================================
    
    std::cout << "\n🌡️  Running weather monitoring... (Press Ctrl+C to stop)\n\n";
    
    while (!shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // ========================================================================
    // Clean Shutdown
    // ========================================================================
    
    std::cout << "\n🛑 Shutting down...\n";
    
    // Stop in reverse order
    pressure_monitor.stop();
    temp_monitor.stop();
    station.stop();
    
    std::cout << "✅ Done!\n";
    
    return 0;
}
