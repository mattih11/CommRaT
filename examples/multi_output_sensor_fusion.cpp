/**
 * @file multi_output_sensor_fusion.cpp
 * @brief Advanced multi-output example: Sensor fusion with 3 output types
 * 
 * Architecture:
 * - FusionModule: Outputs<RawSensorData, FilteredData, DiagnosticsData>
 *   Processes sensor readings and generates 3 different output streams
 * 
 * - RawDataLogger: Input<RawSensorData>
 *   Logs raw sensor readings for debugging
 * 
 * - FilterConsumer: Input<FilteredData>
 *   Uses filtered data for control decisions
 * 
 * - DiagnosticsMonitor: Input<DiagnosticsData>
 *   Monitors system health and performance
 * 
 * Demonstrates:
 * - 3 simultaneous output types from one producer
 * - Type-specific subscription (each consumer gets only their type)
 * - Different processing for each output type
 * - Real-world sensor fusion pattern
 */

#include "commrat/commrat.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <cmath>

// ============================================================================
// Message Definitions
// ============================================================================

struct RawSensorData {
    uint64_t timestamp;
    float accelerometer_x;
    float accelerometer_y;
    float accelerometer_z;
    float gyroscope_x;
    float gyroscope_y;
    float gyroscope_z;
    uint32_t sample_count;
};

struct FilteredData {
    uint64_t timestamp;
    float orientation_roll;   // rad
    float orientation_pitch;  // rad
    float orientation_yaw;    // rad
    float confidence;         // 0.0-1.0
};

struct DiagnosticsData {
    uint64_t timestamp;
    uint32_t total_samples_processed;
    float average_processing_time_us;
    float sensor_noise_level;
    bool calibration_valid;
    uint32_t dropped_samples;
};

// Application definition with 3 output types
using FusionApp = commrat::CommRaT<
    commrat::Message::Data<RawSensorData>,
    commrat::Message::Data<FilteredData>,
    commrat::Message::Data<DiagnosticsData>
>;

// ============================================================================
// Sensor Fusion Module (3 Outputs)
// ============================================================================

class SensorFusionModule : public FusionApp::Module<
    commrat::Outputs<RawSensorData, FilteredData, DiagnosticsData>,
    commrat::PeriodicInput
> {
public:
    SensorFusionModule(const commrat::ModuleConfig& config)
        : FusionApp::Module<
            commrat::Outputs<RawSensorData, FilteredData, DiagnosticsData>,
            commrat::PeriodicInput
          >(config)
        , sample_count_(0)
        , total_processing_time_us_(0)
        , dropped_samples_(0) {}

protected:
    // Multi-output signature: void process(T1& out1, T2& out2, T3& out3)
    void process(RawSensorData& raw, FilteredData& filtered, DiagnosticsData& diag) override {
        auto start = std::chrono::high_resolution_clock::now();
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();

        // Simulate sensor readings with some noise
        float t = sample_count_ * 0.1f;
        float noise = 0.05f * (std::rand() % 100 - 50) / 50.0f;
        
        // Output 1: Raw sensor data
        raw.timestamp = timestamp;
        raw.accelerometer_x = std::sin(t) + noise;
        raw.accelerometer_y = std::cos(t) + noise;
        raw.accelerometer_z = 9.81f + noise * 0.1f;
        raw.gyroscope_x = 0.1f * std::sin(t * 2) + noise * 0.5f;
        raw.gyroscope_y = 0.1f * std::cos(t * 2) + noise * 0.5f;
        raw.gyroscope_z = 0.05f * std::sin(t) + noise * 0.5f;
        raw.sample_count = sample_count_;

        // Output 2: Filtered/fused data (complementary filter simulation)
        filtered.timestamp = timestamp;
        filtered.orientation_roll = std::atan2(raw.accelerometer_y, raw.accelerometer_z);
        filtered.orientation_pitch = std::atan2(-raw.accelerometer_x, 
            std::sqrt(raw.accelerometer_y * raw.accelerometer_y + 
                     raw.accelerometer_z * raw.accelerometer_z));
        filtered.orientation_yaw = 0.0f;  // Would need magnetometer
        
        // Calculate confidence based on accelerometer magnitude (should be ~9.81)
        float acc_mag = std::sqrt(
            raw.accelerometer_x * raw.accelerometer_x +
            raw.accelerometer_y * raw.accelerometer_y +
            raw.accelerometer_z * raw.accelerometer_z
        );
        filtered.confidence = std::max(0.0f, 1.0f - std::abs(acc_mag - 9.81f) / 9.81f);

        // Output 3: Diagnostics data
        auto end = std::chrono::high_resolution_clock::now();
        auto processing_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        total_processing_time_us_ += processing_time_us;
        
        diag.timestamp = timestamp;
        diag.total_samples_processed = sample_count_;
        diag.average_processing_time_us = 
            static_cast<float>(total_processing_time_us_) / (sample_count_ + 1);
        diag.sensor_noise_level = std::abs(noise);
        diag.calibration_valid = (sample_count_ > 10);  // Simulate calibration period
        diag.dropped_samples = dropped_samples_;

        sample_count_++;

        // Log occasionally
        if (sample_count_ % 10 == 0) {
            std::cout << "[Fusion] Sample #" << sample_count_ 
                      << " - Roll: " << filtered.orientation_roll * 180.0f / M_PI << "°"
                      << ", Confidence: " << filtered.confidence * 100.0f << "%"
                      << ", AvgTime: " << diag.average_processing_time_us << "µs\n";
        }
    }

private:
    uint32_t sample_count_;
    uint64_t total_processing_time_us_;
    uint32_t dropped_samples_;
};

// ============================================================================
// Raw Data Logger (Subscriber 1)
// ============================================================================

class RawDataLogger : public FusionApp::Module<
    commrat::Output<RawSensorData>,
    commrat::Input<RawSensorData>
> {
public:
    RawDataLogger(const commrat::ModuleConfig& config)
        : FusionApp::Module<commrat::Output<RawSensorData>, commrat::Input<RawSensorData>>(config)
        , log_count_(0) {}

protected:
    RawSensorData process_continuous(const RawSensorData& input) override {
        log_count_++;
        
        if (log_count_ % 20 == 0) {
            std::cout << "[RawLogger] Logged sample #" << input.sample_count
                      << " - Accel: [" << input.accelerometer_x 
                      << ", " << input.accelerometer_y
                      << ", " << input.accelerometer_z << "]\n";
        }
        
        return input;  // Pass through
    }

private:
    uint32_t log_count_;
};

// ============================================================================
// Filter Consumer (Subscriber 2)
// ============================================================================

class FilterConsumer : public FusionApp::Module<
    commrat::Output<FilteredData>,
    commrat::Input<FilteredData>
> {
public:
    FilterConsumer(const commrat::ModuleConfig& config)
        : FusionApp::Module<commrat::Output<FilteredData>, commrat::Input<FilteredData>>(config)
        , consume_count_(0) {}

protected:
    FilteredData process_continuous(const FilteredData& input) override {
        consume_count_++;
        
        if (consume_count_ % 15 == 0) {
            std::cout << "[FilterConsumer] Using filtered data #" << consume_count_
                      << " - Orientation: Roll=" << input.orientation_roll * 180.0f / M_PI << "°"
                      << ", Pitch=" << input.orientation_pitch * 180.0f / M_PI << "°"
                      << " (Confidence: " << input.confidence * 100.0f << "%)\n";
        }
        
        return input;  // Pass through
    }

private:
    uint32_t consume_count_;
};

// ============================================================================
// Diagnostics Monitor (Subscriber 3)
// ============================================================================

class DiagnosticsMonitor : public FusionApp::Module<
    commrat::Output<DiagnosticsData>,
    commrat::Input<DiagnosticsData>
> {
public:
    DiagnosticsMonitor(const commrat::ModuleConfig& config)
        : FusionApp::Module<commrat::Output<DiagnosticsData>, commrat::Input<DiagnosticsData>>(config)
        , monitor_count_(0) {}

protected:
    DiagnosticsData process_continuous(const DiagnosticsData& input) override {
        monitor_count_++;
        
        if (monitor_count_ % 25 == 0) {
            std::cout << "[DiagMonitor] Health check #" << monitor_count_
                      << " - Samples: " << input.total_samples_processed
                      << ", AvgTime: " << input.average_processing_time_us << "µs"
                      << ", Calibrated: " << (input.calibration_valid ? "YES" : "NO")
                      << ", Dropped: " << input.dropped_samples << "\n";
        }
        
        return input;  // Pass through
    }

private:
    uint32_t monitor_count_;
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

    std::cout << "=== Multi-Output Sensor Fusion Demo (Phase 5.3) ===\n";
    std::cout << "Architecture:\n";
    std::cout << "  FusionModule: Outputs<RawSensorData, FilteredData, DiagnosticsData>\n";
    std::cout << "  ├─> RawDataLogger: Input<RawSensorData>\n";
    std::cout << "  ├─> FilterConsumer: Input<FilteredData>\n";
    std::cout << "  └─> DiagnosticsMonitor: Input<DiagnosticsData>\n";
    std::cout << "\nPress Ctrl+C to stop...\n\n";

    try {
        // Create fusion module (3 outputs)
        commrat::ModuleConfig fusion_config{
            .name = "SensorFusion",
            .system_id = 10,
            .instance_id = 1,
            .period = std::chrono::milliseconds(100)  // 10 Hz
        };
        SensorFusionModule fusion(fusion_config);
        std::cout << "[Main] Created SensorFusion (system_id=10, instance_id=1)\n";
        fusion.start();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Get the primary output type ID for subscription
        uint32_t primary_type_id = FusionApp::get_message_id<RawSensorData>();

        // Create raw data logger
        commrat::ModuleConfig raw_logger_config{
            .name = "RawLogger",
            .system_id = 20,
            .instance_id = 1,
            .period = std::chrono::milliseconds(100),
            .source_system_id = 10,
            .source_instance_id = 1,
            .source_primary_output_type_id = primary_type_id
        };
        RawDataLogger raw_logger(raw_logger_config);
        std::cout << "[Main] Created RawLogger (subscribes to RawSensorData)\n";
        raw_logger.start();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Create filter consumer
        commrat::ModuleConfig filter_config{
            .name = "FilterConsumer",
            .system_id = 30,
            .instance_id = 1,
            .period = std::chrono::milliseconds(100),
            .source_system_id = 10,
            .source_instance_id = 1,
            .source_primary_output_type_id = primary_type_id
        };
        FilterConsumer filter_consumer(filter_config);
        std::cout << "[Main] Created FilterConsumer (subscribes to FilteredData)\n";
        filter_consumer.start();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Create diagnostics monitor
        commrat::ModuleConfig diag_config{
            .name = "DiagMonitor",
            .system_id = 40,
            .instance_id = 1,
            .period = std::chrono::milliseconds(100),
            .source_system_id = 10,
            .source_instance_id = 1,
            .source_primary_output_type_id = primary_type_id
        };
        DiagnosticsMonitor diag_monitor(diag_config);
        std::cout << "[Main] Created DiagnosticsMonitor (subscribes to DiagnosticsData)\n";
        diag_monitor.start();

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::cout << "\n=== Sensor Fusion System Running ===\n\n";

        // Run for 10 seconds or until Ctrl+C
        auto start = std::chrono::steady_clock::now();
        while (running) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(10)) {
                std::cout << "\n[Main] 10 seconds elapsed, stopping...\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n=== Sensor Fusion Demo Complete ===\n";
        std::cout << "Successfully demonstrated:\n";
        std::cout << "  ✓ 3 simultaneous output types from one producer\n";
        std::cout << "  ✓ Type-specific subscription (each consumer gets only their type)\n";
        std::cout << "  ✓ Independent processing for each output stream\n";
        std::cout << "  ✓ Real-world sensor fusion pattern\n";

    } catch (const std::exception& e) {
        std::cerr << "[Main] Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
