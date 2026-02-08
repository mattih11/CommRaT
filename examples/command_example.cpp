/**
 * @file command_example.cpp
 * @brief Demonstrates variadic command handling in modules
 */

#include "messages/messages.hpp"
#include <iostream>
#include <thread>
#include <cmath>

using namespace user_app;

// ============================================================================
// Define Command Payloads
// ============================================================================

struct ResetCmd {
    bool hard_reset{false};
};

struct CalibrateCmd {
    float offset{0.0f};
};

struct SetModeCmd {
    uint32_t mode{0};
};

// Add commands to registry (in real code, this goes in user_messages.hpp)
namespace user_app {
    // Extended CommRaT application with commands
    using ExtendedApp = commrat::CommRaT<
        commrat::Message::Data<TemperatureData>,
        commrat::Message::Command<ResetCmd>,
        commrat::Message::Command<CalibrateCmd>,
        commrat::Message::Command<SetModeCmd>
    >;
}

// ============================================================================
// Module with Multiple Command Handlers
// ============================================================================

/**
 * @brief Sensor module that handles multiple command types
 * 
 * Notice the variadic CommandTypes at the end:
 *   Module<Output<TempData>, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd>
 * 
 * The module automatically dispatches commands to the correct on_command() handler.
 */
class CommandableSensor : public ExtendedApp::Module<Output<TemperatureData>, PeriodicInput, 
                                                       ResetCmd, CalibrateCmd, SetModeCmd> {
public:
    explicit CommandableSensor(const ModuleConfig& config) 
        : ExtendedApp::Module<Output<TemperatureData>, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd>(config) {}
    
protected:
    TemperatureData process() override {
        float raw_temp = 20.0f + std::sin(counter_++ * 0.1f) * 5.0f;
        float calibrated_temp = raw_temp + calibration_offset_;
        
        std::cout << "[Sensor] Mode=" << mode_ 
                  << " Temp=" << calibrated_temp << "°C"
                  << " (offset=" << calibration_offset_ << ")\n";
        
        return TemperatureData{
            .temperature_celsius = calibrated_temp
        };
    }
    
    // Command handlers - framework calls the right one automatically!
    
    void on_command(const ResetCmd& cmd) {
        std::cout << "[Sensor] Reset command received (hard=" 
                  << cmd.hard_reset << ")\n";
        
        if (cmd.hard_reset) {
            calibration_offset_ = 0.0f;
            mode_ = 0;
            counter_ = 0;
        }
    }
    
    void on_command(const CalibrateCmd& cmd) {
        std::cout << "[Sensor] Calibrate command received (offset=" 
                  << cmd.offset << ")\n";
        
        calibration_offset_ = cmd.offset;
    }
    
    void on_command(const SetModeCmd& cmd) {
        std::cout << "[Sensor] SetMode command received (mode=" 
                  << cmd.mode << ")\n";
        
        mode_ = cmd.mode;
    }

private:
    float calibration_offset_ = 0.0f;
    uint32_t mode_ = 0;
    int counter_ = 0;
};

// ============================================================================
// Main - Send Commands to Module
// ============================================================================

int main() {
    std::cout << "=== Variadic Command Handling Example ===\n\n";
    
    // Create sensor module with command handling
    ModuleConfig sensor_config{
        .name = "CommandableSensor",
        .system_id = 0,
        .instance_id = 0,
        .period = std::chrono::milliseconds(200),
        .message_slots = 10,
        .max_subscribers = 8,
        .priority = 10,
        .realtime = false
    };
    
    CommandableSensor sensor(sensor_config);
    sensor.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Create control mailbox to send commands
    commrat::MailboxConfig control_config{
        .mailbox_id = 200,
        .message_slots = 10,
        .max_message_size = 4096,
        .send_priority = 10,
        .realtime = false,
        .mailbox_name = "ControlMailbox"
    };
    
    ExtendedApp::Mailbox<ResetCmd> control(control_config);
    control.start();
    
    std::cout << "\n=== Sending Commands ===\n\n";
    
    // Send SetMode command
    // Send SetMode command (to CMD mailbox = base + 0)
    uint32_t sensor_cmd_mailbox = 131072;  // system_id=0, instance_id=0, CMD=0
    std::cout << ">>> Sending SetMode(mode=1)\n";
    SetModeCmd set_mode{.mode = 1};
    control.send(set_mode, sensor_cmd_mailbox);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Send Calibrate command
    std::cout << "\n>>> Sending Calibrate(offset=2.5)\n";
    CalibrateCmd calibrate{.offset = 2.5f};
    control.send(calibrate, sensor_cmd_mailbox);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Send SetMode again
    std::cout << "\n>>> Sending SetMode(mode=2)\n";
    set_mode.mode = 2;
    control.send(set_mode, sensor_cmd_mailbox);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Send Reset command
    std::cout << "\n>>> Sending Reset(hard=true)\n";
    ResetCmd reset{.hard_reset = true};
    control.send(reset, sensor_cmd_mailbox);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Cleanup
    std::cout << "\n=== Stopping ===\n";
    control.stop();
    sensor.stop();
    
    std::cout << "\n=== Summary ===\n";
    std::cout << "✓ Module declared with variadic CommandTypes:\n";
    std::cout << "  Module<TempData, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd>\n\n";
    std::cout << "✓ Framework automatically dispatches to correct on_command() handler\n";
    std::cout << "✓ Type-safe command handling at compile-time\n";
    std::cout << "✓ Commands sent as payload types (ResetCmd, CalibrateCmd, etc.)\n";
    std::cout << "✓ No manual command ID checking or casting!\n";
    
    return 0;
}
