# Example Message Definitions

This directory contains shared message type definitions and application setup for CommRaT examples.

## Purpose

Message definitions are kept in **shared headers** separate from module implementations to:

1. **Ensure consistency** - All modules use the same data structures
2. **Enable type safety** - Compile-time checking of message types
3. **Promote reusability** - Messages can be shared across multiple modules
4. **Clarify architecture** - Separates data contracts from business logic
5. **Support evolution** - Message definitions can be versioned independently

## Structure

### `user_messages.hpp`

Defines the application and its message types using `CommRaT<>`:

```cpp
namespace user_app {

// Message structures (plain POD)
struct TemperatureData {
    float temperature_celsius{0.0f};
};

// Application definition with CommRaT<>
using App = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>
>;

} // namespace user_app
```

### `common_messages.hpp`

Similar pattern for examples that share common message types:

```cpp
namespace example_messages {

struct TemperatureData {
    uint32_t sensor_id{0};
    float temperature_c{0.0f};
};

using ExampleApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<CounterData>
>;

} // namespace example_messages
```

### `messages.hpp`

Convenience re-exports for cleaner code:

```cpp
namespace user_app {

using commrat::Output;
using commrat::Input;
using commrat::PeriodicInput;
using commrat::ModuleConfig;

} // namespace user_app
```

## Usage Pattern

### 1. Define Your Application

```cpp
// In your_messages.hpp
#pragma once
#include <commrat/commrat.hpp>

namespace my_app {

// Step 1: Define message structures (plain POD)
struct SensorData {
    float value{0.0f};
    uint32_t sensor_id{0};
};

struct ProcessedData {
    float filtered_value{0.0f};
};

// Step 2: Create CommRaT application
using App = commrat::CommRaT<
    commrat::Message::Data<SensorData>,
    commrat::Message::Data<ProcessedData>
>;

} // namespace my_app
```

### 2. Create Modules

```cpp
#include "your_messages.hpp"

using namespace my_app;

// Producer module
class SensorModule : public App::Module<Output<SensorData>, PeriodicInput> {
public:
    explicit SensorModule(const ModuleConfig& config) 
        : App::Module<Output<SensorData>, PeriodicInput>(config) {}
    
protected:
    SensorData process() override {
        return SensorData{
            .value = read_sensor(),
            .sensor_id = config_.instance_id
        };
    }
};

// Consumer module
class ProcessorModule : public App::Module<Output<ProcessedData>, Input<SensorData>> {
public:
    explicit ProcessorModule(const ModuleConfig& config)
        : App::Module<Output<ProcessedData>, Input<SensorData>>(config) {}
    
protected:
    ProcessedData process_continuous(const SensorData& input) override {
        return ProcessedData{
            .filtered_value = apply_filter(input.value)
        };
    }
};
```

## Best Practices

### ✅ DO

- Use `CommRaT<>` to define your application with all message types
- Inherit directly from `App::Module<OutputSpec, InputSpec>` (not template aliases)
- Keep message structures simple POD types with inline defaults
- Use `Message::Data<T>` for data messages, `Message::Command<T>` for commands
- Document message fields with comments
- Use consistent naming conventions
- Include namespace for organization
- Use minimal explicit constructors: `Ctor(const ModuleConfig& config) : BaseModule(config) {}`

### ❌ DON'T

- Use `MessageRegistry<>` directly (use `CommRaT<>` instead)
- Create template aliases like `using Module = App::Module<...>` (use `App::Module` directly)
- Define messages inside module classes
- Use pointers or dynamic allocations in message structs
- Mix business logic with message definitions
- Use platform-specific types (use `uint32_t` not `unsigned int`)

## Example: Multi-Output Module

```cpp
// Application with multiple message types
using App = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<PressureData>
>;

// Multi-output producer
class WeatherStation : public App::Module<
    Outputs<TemperatureData, PressureData>, 
    PeriodicInput
> {
public:
    explicit WeatherStation(const ModuleConfig& config)
        : App::Module<Outputs<TemperatureData, PressureData>, PeriodicInput>(config) {}
    
protected:
    void process(TemperatureData& temp, PressureData& pressure) override {
        temp.temperature_c = read_temp();
        pressure.pressure_pa = read_pressure();
    }
};
```

## Available I/O Specifications

- `Output<T>` - Single output of type T
- `Outputs<T, U, ...>` - Multiple outputs
- `Input<T>` - Continuous input (process_continuous called for each message)
- `Inputs<T, U, ...>` - Multiple synchronized inputs
- `PeriodicInput` - Periodic execution (process called at config.period rate)
- `LoopInput` - Maximum throughput (process called as fast as possible)

## See Also

- `../clean_interface_example.cpp` - Clean API demonstration
- `../continuous_input_example.cpp` - Producer-consumer pattern
- `../command_example.cpp` - Command handling
- `../../docs/USER_GUIDE.md` - Comprehensive framework documentation
- `../../docs/GETTING_STARTED.md` - Quick start guide
- `../../docs/MODULE_FRAMEWORK.md` - Module system documentation
