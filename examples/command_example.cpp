/**
 * @file command_example.cpp
 * @brief Demonstrates simple command handling with DataWithCommands
 */

#include "messages/common_messages.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <cmath>
#include <corerat/platform/timestamp.hpp>

using namespace example_messages;
using commrat::Time;
using commrat::Milliseconds;
using commrat::ModuleConfig;

// ============================================================================
// Define Command Payloads with Reply types
// ============================================================================

struct ResetCmd {
    bool hard_reset{false};
    
    struct Reply {
        bool success{false};
        uint32_t previous_mode{0};
    };
};

struct CalibrateCmd {
    float offset{0.0f};
    
    struct Reply {
        bool success{false};
        float previous_offset{0.0f};
    };
};

struct SetModeCmd {
    uint32_t mode{0};
    
    struct Reply {
        bool success{false};
        uint32_t previous_mode{0};
    };
};

// ============================================================================
// Extended Application with Commands
// ============================================================================

namespace example_messages {
    // Simple approach: Associate commands with data type using DataWithCommands
    using TempDataWithCommands = commrat::DataWithCommands<
        TemperatureData,
        ResetCmd,
        CalibrateCmd,
        SetModeCmd
    >;
    
    // Application with data+commands in one clean definition
    using CommandApp = commrat::CommRaT<
        TempDataWithCommands,
        commrat::Message::Data<StatusData>
    >;
}

// ============================================================================
// Module with Command Handlers - Simple!
// ============================================================================

/**
 * @brief Sensor module with command handling
 * 
 * Notice how clean this is:
 *   - Just specify Output<TempDataWithCommands> and Period<>
 *   - Commands automatically handled by framework
 *   - Implement on_command<0>() for each command type (output index 0)
 */
class CommandableSensor : public CommandApp::Module2<
    Output<TemperatureData>,  // Output type (commands associated via DataWithCommands)
    Period<200>               // 200ms periodic execution
> {
public:
    explicit CommandableSensor(const ModuleConfig& config) 
        : CommandApp::Module2<Output<TemperatureData>, Period<200>>(config) {}
    
protected:
    // Periodic data generation
    void process(TemperatureData& output) override {
        float raw_temp = 20.0f + std::sin(counter_++ * 0.1f) * 5.0f;
        float calibrated_temp = raw_temp + calibration_offset_;
        
        std::cout << "[Sensor] Mode=" << mode_ 
                  << " Temp=" << calibrated_temp << "°C"
                  << " (offset=" << calibration_offset_ << ")\n";
        
        output.sensor_id = 1;
        output.temperature_c = calibrated_temp;
        output.confidence = 1.0f;
    }
    
    // Command handlers - framework calls the right one automatically!
    // OutputIndex template parameter allows multi-output modules with different commands per output
    
    template<size_t OutputIndex>
    void on_command(const ResetCmd& cmd, typename ResetCmd::Reply& reply) {
        std::cout << "[Sensor] Reset command (hard=" << cmd.hard_reset << ")\n";
        
        reply.previous_mode = mode_;
        
        if (cmd.hard_reset) {
            calibration_offset_ = 0.0f;
            mode_ = 0;
            counter_ = 0;
            reply.success = true;
        } else {
            // Soft reset - just reset counter
            counter_ = 0;
            reply.success = true;
        }
    }
    
    template<size_t OutputIndex>
    void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
        std::cout << "[Sensor] Calibrate (offset=" << cmd.offset << ")\n";
        
        reply.previous_offset = calibration_offset_;
        calibration_offset_ = cmd.offset;
        reply.success = true;
    }
    
    template<size_t OutputIndex>
    void on_command(const SetModeCmd& cmd, typename SetModeCmd::Reply& reply) {
        std::cout << "[Sensor] SetMode (mode=" << cmd.mode << ")\n";
        
        reply.previous_mode = mode_;
        mode_ = cmd.mode;
        reply.success = true;
    }

private:
    float calibration_offset_ = 0.0f;
    uint32_t mode_ = 0;
    int counter_ = 0;
};

// ============================================================================
// Global Shutdown Signal
// ============================================================================
std::atomic<bool> shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        shutdown_requested.store(true);
    }
}

// ============================================================================
// Main - Send Commands to Module
// ============================================================================

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== Simple Command Handling Example ===\n\n";
    
    // Create sensor module with command handling
    ModuleConfig sensor_config{
        .name = "CommandableSensor",
        .outputs = commrat::SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = commrat::NoInputConfig{},
        .period = std::chrono::milliseconds(200)
    };
    
    CommandableSensor sensor(sensor_config);
    sensor.start();
    
    std::cout << "Sensor running with command handlers registered.\n";
    std::cout << "Auto-stopping after 2 seconds...\n\n";
    
    // Run for ~2 seconds to show periodic output, then auto-stop
    int seconds = 0;
    while (!shutdown_requested.load() && seconds < 2) {
        Time::sleep(Milliseconds(1000));
        seconds++;
    }
    
    // Cleanup
    std::cout << "\n=== Stopping ===\n";
    sensor.stop();
    
    std::cout << "\n=== Summary ===\n";
    std::cout << "Simple Module2 approach:\n";
    std::cout << "  Module2<Output<TempData>, Period<200>>\n\n";
    std::cout << "Commands associated with data:\n";
    std::cout << "  DataWithCommands<TempData, ResetCmd, CalibrateCmd, SetModeCmd>\n\n";
    std::cout << "Command handlers:\n";
    std::cout << "  template<size_t OutIdx> void on_command(const CmdType&, Reply&)\n\n";
    std::cout << "Benefits:\n";
    std::cout << "  - No variadic template pollution in module declaration\n";
    std::cout << "  - Commands grouped with their data type\n";
    std::cout << "  - Automatic request/reply handling\n";
    std::cout << "  - Type-safe at compile-time\n";
    
    return 0;
}
