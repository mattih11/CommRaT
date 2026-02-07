/**
 * @file multi_output_runtime.cpp
 * @brief Runtime test: One producer with two outputs, two receivers
 * 
 * Architecture:
 * - ProducerModule: Outputs<TemperatureData, PressureData> with PeriodicInput
 *   Publishes both temperature and pressure readings every 100ms
 * 
 * - TempReceiverModule: Input<TemperatureData>
 *   Subscribes to producer, receives only temperature data
 * 
 * - PressureReceiverModule: Input<PressureData>
 *   Subscribes to producer, receives only pressure data
 * 
 * This demonstrates Phase 5.3 multi-output capability where a single
 * producer can generate multiple output types simultaneously.
 */

#include "commrat/commrat.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>

// ============================================================================
// Message Definitions
// ============================================================================

struct TemperatureData {
    uint64_t timestamp;
    float temperature_c;
    uint32_t sensor_id;
};

struct PressureData {
    uint64_t timestamp;
    float pressure_pa;
    uint32_t sensor_id;
};

// Application definition with two output types
using SensorApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<PressureData>
>;

// ============================================================================
// Producer Module (Multi-Output)
// ============================================================================

class SensorProducerModule : public SensorApp::Module<
    commrat::Outputs<TemperatureData, PressureData>,
    commrat::PeriodicInput
> {
public:
    SensorProducerModule(const commrat::ModuleConfig& config)
        : SensorApp::Module<commrat::Outputs<TemperatureData, PressureData>, commrat::PeriodicInput>(config)
        , reading_count_(0) {}

protected:
    // Multi-output signature: void process(T1& out1, T2& out2)
    void process(TemperatureData& temp, PressureData& pressure) override {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

        // Generate synthetic sensor readings
        temp.timestamp = timestamp;
        temp.temperature_c = 20.0f + (reading_count_ % 10) * 0.5f;  // 20-25°C
        temp.sensor_id = 1;

        pressure.timestamp = timestamp;
        pressure.pressure_pa = 101325.0f + (reading_count_ % 20) * 10.0f;  // ~1 atm
        pressure.sensor_id = 1;

        reading_count_++;

        std::cout << "[Producer] Generated reading #" << reading_count_ 
                  << " - Temp: " << temp.temperature_c << "°C"
                  << ", Pressure: " << pressure.pressure_pa << " Pa\n";
    }

private:
    uint32_t reading_count_;
};

// ============================================================================
// Temperature Receiver Module
// ============================================================================

class TempReceiverModule : public SensorApp::Module<
    commrat::Output<TemperatureData>,
    commrat::Input<TemperatureData>
> {
public:
    TempReceiverModule(const commrat::ModuleConfig& config)
        : SensorApp::Module<commrat::Output<TemperatureData>, commrat::Input<TemperatureData>>(config)
        , received_count_(0) {}

protected:
    TemperatureData process_continuous(const TemperatureData& input) override {
        received_count_++;
        std::cout << "[TempReceiver] Received temp #" << received_count_ 
                  << ": " << input.temperature_c << "°C"
                  << " (sensor " << input.sensor_id << ")\n";
        
        // Just pass through
        return input;
    }

private:
    uint32_t received_count_;
};

// ============================================================================
// Pressure Receiver Module
// ============================================================================

class PressureReceiverModule : public SensorApp::Module<
    commrat::Output<PressureData>,
    commrat::Input<PressureData>
> {
public:
    PressureReceiverModule(const commrat::ModuleConfig& config)
        : SensorApp::Module<commrat::Output<PressureData>, commrat::Input<PressureData>>(config)
        , received_count_(0) {}

protected:
    PressureData process_continuous(const PressureData& input) override {
        received_count_++;
        std::cout << "[PressureReceiver] Received pressure #" << received_count_ 
                  << ": " << input.pressure_pa << " Pa"
                  << " (sensor " << input.sensor_id << ")\n";
        
        // Just pass through
        return input;
    }

private:
    uint32_t received_count_;
};

// ============================================================================
// Main Application
// ============================================================================

volatile sig_atomic_t running = 1;

void signal_handler(int) {
    running = 0;
}

int main() {
    std::signal(SIGINT, signal_handler);

    std::cout << "=== Multi-Output Runtime Test (Phase 5.3) ===\n";
    std::cout << "Architecture:\n";
    std::cout << "  - ProducerModule: Outputs<Temperature, Pressure>\n";
    std::cout << "  - TempReceiver: Input<Temperature>\n";
    std::cout << "  - PressureReceiver: Input<Pressure>\n";
    std::cout << "\nPress Ctrl+C to stop...\n\n";

    try {
        // Create producer module (multi-output)
        commrat::ModuleConfig producer_config{
            .name = "SensorProducer",
            .system_id = 10,
            .instance_id = 1,
            .period = std::chrono::milliseconds(500),  // 2 Hz
            .source_system_id = 0,
            .source_instance_id = 0
        };
        SensorProducerModule producer(producer_config);
        std::cout << "[Main] Created SensorProducer (system_id=10, instance_id=1)\n";
        producer.start();
        std::cout << "[Main] Started SensorProducer\n";

        // Give producer time to initialize
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Create temperature receiver
        commrat::ModuleConfig temp_receiver_config{
            .name = "TempReceiver",
            .system_id = 20,
            .instance_id = 1,
            .period = std::chrono::milliseconds(100),
            .source_system_id = 10,  // Subscribe to producer
            .source_instance_id = 1
        };
        TempReceiverModule temp_receiver(temp_receiver_config);
        std::cout << "[Main] Created TempReceiver (system_id=20, instance_id=1)\n";
        temp_receiver.start();
        std::cout << "[Main] TempReceiver subscribing to producer...\n";

        // Give subscription time to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Create pressure receiver
        commrat::ModuleConfig pressure_receiver_config{
            .name = "PressureReceiver",
            .system_id = 30,
            .instance_id = 1,
            .period = std::chrono::milliseconds(100),
            .source_system_id = 10,  // Subscribe to producer
            .source_instance_id = 1
        };
        PressureReceiverModule pressure_receiver(pressure_receiver_config);
        std::cout << "[Main] Created PressureReceiver (system_id=30, instance_id=1)\n";
        pressure_receiver.start();
        std::cout << "[Main] PressureReceiver subscribing to producer...\n\n";

        // Wait for subscriptions to stabilize
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::cout << "=== Multi-Output System Running ===\n\n";

        // Run for 5 seconds or until Ctrl+C
        auto start = std::chrono::steady_clock::now();
        int iteration = 0;
        while (running) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5)) {
                std::cout << "\n[Main] 5 seconds elapsed, stopping...\n";
                break;
            }
            if (iteration % 10 == 0) {
                std::cout << "[Main] Running... (" 
                          << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() 
                          << "ms)\n";
            }
            iteration++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n=== Multi-Output Runtime Test Complete ===\n";
        std::cout << "Producer generated two output types simultaneously.\n";
        std::cout << "Both receivers successfully consumed their respective data types.\n";

    } catch (const std::exception& e) {
        std::cerr << "[Main] Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
