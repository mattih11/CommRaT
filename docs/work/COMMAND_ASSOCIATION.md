# Command Association Design

## Overview

Commands are associated with specific data output types at compile time. This provides:
- **Type safety**: Only valid commands can be sent to a specific output type
- **Clarity**: Command list is part of the data type definition
- **Compile-time extraction**: Framework automatically extracts command lists
- **Clean syntax**: Simple `using` declarations keep code readable

## Architecture

### 1. Basic Pattern

```cpp
// Define payloads
struct SensorDataPayload { float temp; };
struct CalibrateCmdPayload { float offset; };
struct CalibrateReplyPayload { bool success; };

// Define command
using CalibrateCmd = Message::Command<CalibrateCmdPayload, CalibrateReplyPayload>;

// Associate command with data
using SensorData = Message::DataWith<SensorDataPayload>::Commands<CalibrateCmd>;
```

### 2. Multiple Commands

```cpp
using SensorData = Message::DataWith<SensorDataPayload>::Commands<
    CalibrateCmd,
    SetRateCmd,
    ResetCmd
>;
```

### 3. No Commands (Simple Data)

```cpp
// Simple data without commands still works
using IMUData = Message::Data<IMUDataPayload>;
```

## Implementation Details

### DataWithCommands Template

```cpp
template<typename PayloadT, typename... CommandTypes>
struct DataWithCommands {
    using DataMessage = MessageDefinition<PayloadT, ...>;  // The actual data message
    using Commands = std::tuple<CommandTypes...>;          // Command list
    using Payload = PayloadT;
    
    static constexpr bool has_commands = sizeof...(CommandTypes) > 0;
    static constexpr size_t num_commands = sizeof...(CommandTypes);
};
```

### Registry Processing

The `MessageRegistry` automatically expands `DataWithCommands`:

```cpp
// User writes:
CommRaT<DataWithCommands<SensorPayload, Cmd1, Cmd2>>

// Registry expands to:
MessageRegistry<
    MessageDefinition<SensorPayload, ...>,  // Data message
    Cmd1,                                    // Command 1
    Cmd2                                     // Command 2
>
```

### Input Class Integration

```cpp
template<typename Registry, typename OutputType>
class CmdInput {
    // Extract data message (unwrap DataWithCommands)
    using DataMessage = ExtractDataMessage_t<OutputType>;
    
    // Extract command list (std::tuple<Cmd1, Cmd2, ...> or empty tuple)
    using CommandList = ExtractCommands_t<OutputType>;
    
    // Calculate type_id from data message
    static constexpr uint8_t type_id = ...;
};
```

## Usage Examples

### Example 1: Sensor with Calibration

```cpp
// Sensor data payload
struct SensorDataPayload {
    float temperature;
    float pressure;
};

// Calibration command
using CalibrateCmd = Message::Command<
    CalibrateCmdPayload,
    CalibrateReplyPayload
>;

// Associate
using SensorData = Message::DataWith<SensorDataPayload>::Commands<CalibrateCmd>;

// Use in app
using MyApp = CommRaT<SensorData>;

// Send command
CmdInput<MyApp::Registry, SensorData> input(...);
TimsMessage<CalibrateCmdPayload> cmd{...};
TimsMessage<CalibrateReplyPayload> reply;
input.send_command(cmd, reply);
```

### Example 2: Multiple Output Types

```cpp
// Different data types with different commands
using SensorData = Message::DataWith<SensorPayload>::Commands<CalibrateCmd>;
using MotorData = Message::DataWith<MotorPayload>::Commands<SetSpeedCmd, SetTorqueCmd>;
using CameraData = Message::DataWith<CameraPayload>::Commands<SetExposureCmd, CaptureCmd>;

using RobotApp = CommRaT<SensorData, MotorData, CameraData>;

// Each input knows its own commands
CmdInput<RobotApp::Registry, SensorData> sensor(...);   // Has CalibrateCmd
CmdInput<RobotApp::Registry, MotorData> motor(...);     // Has SetSpeedCmd, SetTorqueCmd
CmdInput<RobotApp::Registry, CameraData> camera(...);   // Has SetExposureCmd, CaptureCmd
```

### Example 3: Mixed (Some with Commands, Some without)

```cpp
// Sensor has commands
using SensorData = Message::DataWith<SensorPayload>::Commands<CalibrateCmd>;

// IMU is just data
using IMUData = Message::Data<IMUPayload>;

// GPS is just data
using GPSData = Message::Data<GPSPayload>;

using MyApp = CommRaT<SensorData, IMUData, GPSData>;

// Only SensorData has command interface
CmdInput<MyApp::Registry, SensorData> sensor_input(...);  // Can send commands
ContinuousInput<MyApp::Registry, IMUData> imu_input(...);  // Data only, no commands
```

## Benefits

### Compile-Time Safety

```cpp
// This compiles (command is valid for SensorData)
sensor_input.send_command(calibrate_cmd, reply);

// This won't compile (command not in SensorData::Commands)
// sensor_input.send_command(motor_speed_cmd, reply);  // ERROR!
```

### Self-Documenting

```cpp
// Looking at the type tells you what commands are available
using SensorData = Message::DataWith<SensorPayload>::Commands<
    CalibrateCmd,
    SetRateCmd,
    ResetCmd
>;

// Compile-time introspection
static_assert(SensorData::has_commands);
static_assert(SensorData::num_commands == 3);
using SensorCommands = SensorData::Commands;  // std::tuple<...>
```

### Clean Application Definition

```cpp
// Simple, readable application definition
using MyApp = CommRaT<
    Message::DataWith<SensorPayload>::Commands<CalibrateCmd, SetRateCmd>,
    Message::DataWith<MotorPayload>::Commands<SetSpeedCmd>,
    Message::Data<IMUPayload>  // No commands
>;
```

## Implementation Status

- ✅ `DataWithCommands` template defined
- ✅ `ExtractCommands_t` helper
- ✅ `ExtractDataMessage_t` helper
- ✅ `FlattenDataWithCommands_t` helper
- ✅ `ExpandAllDataWithCommands_t` for registry
- ✅ `Message::DataWith<T>::Commands<...>` syntax
- ✅ `CmdInput` extracts commands from OutputType
- ⏳ TODO: Registry auto-expansion of DataWithCommands
- ⏳ TODO: Command validation in send_command
- ⏳ TODO: Generate command handler stubs for ModuleOutput

## Next Steps

1. Integrate `ExpandAllDataWithCommands_t` into `MessageRegistry`
2. Add command validation to `CmdInput::send_command`
3. Generate command handlers for `ModuleOutput` based on command list
4. Update examples to use new syntax
5. Add compile-time checks for command compatibility
