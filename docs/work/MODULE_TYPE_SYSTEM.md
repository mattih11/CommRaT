# Unified Module Type System

**Status**: Design Phase  
**Priority**: Medium  
**Created**: February 12, 2026

## Overview

Extend the message registry concept to define complete module interfaces, linking commands to output types. This creates strongly-typed module contracts that can be validated at compile time.

## Current State

**Message Registry**: Defines data types and message IDs
```cpp
using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<PressureData>,
    Message::Command<ResetCmd>
>;
```

**Limitation**: Commands are global - no association with specific module types.

## Proposed Design

### ModuleType Definition

```cpp
// Define a complete module interface
template<typename OutputT, typename... CommandTs>
struct ModuleType {
    using OutputType = OutputT;
    using Commands = std::tuple<CommandTs...>;
    
    static constexpr const char* name = OutputT::module_name;
};

// Usage: Define standard module types
struct IMU : ModuleType<IMUData, CalibrateCmd, ResetCmd> {};
struct GPS : ModuleType<GPSData, SetFixModeCmd, GetStatusCmd> {};
struct Lidar : ModuleType<LidarScan, SetRangeCmd, TriggerScanCmd> {};

// Application registry
using MyApp = CommRaT<IMU, GPS, Lidar>;
```

### Module Implementation

Modules can implement one or multiple types:

```cpp
// Single type implementation
class IMUModule : public MyApp::Module<Output<IMU>> {
protected:
    IMUData process() override { /* ... */ }
    
    // Automatically generated command handlers for IMU commands
    void on_command(const CalibrateCmd& cmd) override { /* ... */ }
    void on_command(const ResetCmd& cmd) override { /* ... */ }
};

// Multi-type implementation (one module, multiple interfaces)
class SensorHub : public MyApp::Module<Outputs<IMU, GPS>> {
protected:
    void process(IMUData& imu, GPSData& gps) override { /* ... */ }
    
    // Commands for both types
    void on_command(const CalibrateCmd& cmd) override { /* IMU calibration */ }
    void on_command(const SetFixModeCmd& cmd) override { /* GPS config */ }
};
```

## Benefits

### 1. Compile-Time Validation

```cpp
// ERROR: Trying to send GPS command to IMU module
imu_module.send_command(SetFixModeCmd{});  // Compile error!

// OK: Sending valid command
imu_module.send_command(CalibrateCmd{});
```

### 2. Interface Documentation

Module type = self-documenting interface:
```cpp
struct IMU : ModuleType<IMUData, CalibrateCmd, ResetCmd> {};
// Clearly shows: produces IMUData, accepts Calibrate and Reset commands
```

### 3. Auto-Generated Metadata

```cpp
// Introspection API
MyApp::for_each_module_type([](auto module_type) {
    std::cout << module_type.name << " produces " 
              << module_type.OutputType::name << "\n";
    std::cout << "Commands: ";
    // List all command types
});
```

### 4. Subscription Type Safety

```cpp
// Consumer only needs to know module type, not message IDs
class FilterModule : public MyApp::Module<Output<FilteredData>, Input<IMU>> {
    // Automatically subscribes to IMU output type
};
```

## Implementation Strategy

### Phase 1: ModuleType Template (2 weeks)

- Define `ModuleType<OutputT, CommandTs...>` template
- Integrate with existing `Message::Data<T>` pattern
- Maintain backward compatibility (raw message types still work)

### Phase 2: Command Linking (2 weeks)

- Associate commands with module types
- Generate compile-time command dispatch validation
- Update `on_command()` to check command-module compatibility

### Phase 3: Subscription Refactoring (3 weeks)

- Allow `Input<ModuleTypeT>` in addition to `Input<MessageT>`
- Automatically resolve ModuleType → MessageType → MessageID
- Update subscription protocol to handle both patterns

### Phase 4: Introspection Integration (1 week)

- Export module type definitions to JSON schema
- Include commands in module metadata
- Update composer system to use module types

## Migration Path

**Backward Compatible**: Existing code continues working

```cpp
// Old way (still works)
using OldApp = CommRaT<
    Message::Data<IMUData>,
    Message::Command<CalibrateCmd>
>;

// New way (recommended)
struct IMU : ModuleType<IMUData, CalibrateCmd, ResetCmd> {};
using NewApp = CommRaT<IMU>;

// Mixed (transitional)
using MixedApp = CommRaT<
    IMU,  // Module type
    Message::Data<SomeOtherData>  // Raw message
>;
```

## Related Work

- Current message registry: `include/commrat/message_registry.hpp`
- Message definitions: `include/commrat/messages.hpp`
- Command handling: Phase 6 analysis in `docs/internal/phase_history/ARCHITECTURE_ANALYSIS.md`
- Introspection: `docs/work/INTROSPECTION_INTEGRATION_PLAN.md`
