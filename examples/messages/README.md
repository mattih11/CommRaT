# Example Message Definitions

Shared message type definitions and application setup for CommRaT examples.

## Structure

### user_messages.hpp

Defines the example application with `CommRaT<>`:

```cpp
namespace user_app {

struct TemperatureData {
    float temperature_celsius{0.0f};
};

using App = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>
>;

} // namespace user_app
```

## Usage Pattern

### 1. Define Application

```cpp
// your_messages.hpp
#pragma once
#include <commrat/commrat.hpp>

namespace my_app {

struct SensorData {
    float value{0.0f};
    uint32_t sensor_id{0};
};

struct ProcessedData {
    float filtered_value{0.0f};
};

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

// Timer-driven producer
class SensorModule : public App::Module2<
    commrat::Output<SensorData>,
    commrat::Period<commrat::Milliseconds(100)>
> {
protected:
    void process(SensorData& output) override {
        output = SensorData{.value = read_sensor(), .sensor_id = 1};
    }
};

// Input-driven consumer
class ProcessorModule : public App::Module2<
    commrat::Output<ProcessedData>,
    commrat::Input<SensorData>
> {
protected:
    void process(const SensorData& input, ProcessedData& output) override {
        output = ProcessedData{.filtered_value = apply_filter(input.value)};
    }
};
```

## Best Practices

- Use `CommRaT<>` to define your application (not `MessageRegistry<>`)
- Keep message structs as plain POD with inline defaults
- No pointers or dynamic allocation in message structs
- Use `Message::Data<T>` for data, `Message::Command<T>` for commands
- Separate message definitions from module implementations

## Multi-Output Example

```cpp
class WeatherStation : public App::Module2<
    commrat::Output<TemperatureData>,
    commrat::Output<PressureData>,
    commrat::Period<commrat::Milliseconds(100)>
> {
protected:
    void process(TemperatureData& temp, PressureData& pressure) override {
        temp.temperature_c = read_temp();
        pressure.pressure_pa = read_pressure();
    }
};
```

## I/O Specifications

| Spec | Purpose |
|------|---------|
| `Output<T>` | Single output (one or more per module) |
| `Input<T>` | Continuous input (event-driven) |
| `SyncedInput<T>` | Secondary input (get_data synchronized) |
| `Period<Duration>` | Timer-driven execution |
| _(none of the above)_ | Loop mode (maximum throughput) |

## See Also

- `../clean_interface_example.cpp` -- Clean API demonstration
- `../continuous_input_example.cpp` -- Producer-consumer pattern
- `../../docs/USER_GUIDE.md` -- Framework documentation
- `../../docs/GETTING_STARTED.md` -- Quick start guide
