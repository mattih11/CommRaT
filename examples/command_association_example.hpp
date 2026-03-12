/**
 * @file command_association_example.hpp
 * @brief Example of associating commands with data types
 * 
 * This demonstrates the clean syntax for defining data messages with
 * type-specific commands.
 */

#pragma once

#include "commrat/messaging/message_helpers.hpp"
#include "commrat/messaging/data_with_commands.hpp"

namespace example {

// ============================================================================
// 1. Define payload structures (plain POD)
// ============================================================================

struct SensorDataPayload {
    float temperature;
    float pressure;
    uint64_t timestamp;
};

struct CalibrateCmdPayload {
    float offset;
    float scale;
};

struct CalibrateReplyPayload {
    bool success;
    float measured_error;
};

struct SetRateCmdPayload {
    uint32_t rate_hz;
};

struct SetRateReplyPayload {
    bool success;
    uint32_t actual_rate_hz;
};

// ============================================================================
// 2. Define command messages (request-reply pairs)
// ============================================================================

using CalibrateCmd = commrat::Message::Command<CalibrateCmdPayload, CalibrateReplyPayload>;
using SetRateCmd = commrat::Message::Command<SetRateCmdPayload, SetRateReplyPayload>;

// ============================================================================
// 3. Associate commands with data type - THREE WAYS
// ============================================================================

// WAY 1: Direct DataWithCommands (most explicit)
using SensorData_V1 = commrat::DataWithCommands<
    SensorDataPayload,
    CalibrateCmd,
    SetRateCmd
>;

// WAY 2: Using Message::DataWith helper (cleaner)
using SensorData_V2 = commrat::Message::DataWith<SensorDataPayload>::Commands<
    CalibrateCmd,
    SetRateCmd
>;

// WAY 3: Inline everything (most compact for simple cases)
using SensorData_V3 = commrat::Message::DataWith<SensorDataPayload>::Commands<
    commrat::Message::Command<CalibrateCmdPayload, CalibrateReplyPayload>,
    commrat::Message::Command<SetRateCmdPayload, SetRateReplyPayload>
>;

// Recommended: Use V2 with separate command definitions for clarity
using SensorData = SensorData_V2;

// ============================================================================
// 4. Data without commands (simple case still works)
// ============================================================================

struct IMUDataPayload {
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    uint64_t timestamp;
};

using IMUData = commrat::Message::Data<IMUDataPayload>;  // No commands

// ============================================================================
// 5. Use in application definition
// ============================================================================

using MyApp = commrat::CommRaT<
    SensorData,    // Data with 2 commands
    IMUData        // Data without commands
>;

// ============================================================================
// 6. Compile-time access to command information
// ============================================================================

// Extract commands from SensorData
using SensorCommands = SensorData::Commands;  // std::tuple<CalibrateCmd, SetRateCmd>
static_assert(SensorData::has_commands == true);
static_assert(SensorData::num_commands == 2);

// IMUData has no commands
using IMUCommands = IMUData::Commands;  // Empty (IMUData is not DataWithCommands)
// static_assert(IMUData::has_commands == false);  // Won't compile - IMUData is plain MessageDefinition

// ============================================================================
// 7. Usage with input classes
// ============================================================================

/*
// Create command input (automatically knows about CalibrateCmd and SetRateCmd)
CmdInput<MyApp::Registry, SensorData> sensor_input(work_mbx, sys_id, inst_id);

// Send calibrate command
TimsMessage<CalibrateCmdPayload> calib_cmd{
    .payload = {.offset = 0.5f, .scale = 1.2f}
};
TimsMessage<CalibrateReplyPayload> calib_reply;
bool ok = sensor_input.send_command(calib_cmd, calib_reply);

// Send set rate command
TimsMessage<SetRateCmdPayload> rate_cmd{
    .payload = {.rate_hz = 100}
};
TimsMessage<SetRateReplyPayload> rate_reply;
ok = sensor_input.send_command(rate_cmd, rate_reply);
*/

} // namespace example
