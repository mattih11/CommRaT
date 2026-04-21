# Getting Started with CommRaT

Create your first CommRaT application in three steps.

---

## Prerequisites

- **C++20 compiler** (GCC 10+, Clang 12+)
- **CMake 3.16+**
- **[SeRTial](https://github.com/mattih11/SeRTial)** installed system-wide
- **[RACK](https://github.com/smolorz/RACK)** installed system-wide (provides TiMS messaging)
- **TIMS router running** (`tims_router_tcp` from RACK)

---

## Step 1: Set Up Your Project

```bash
mkdir my_commrat_app && cd my_commrat_app
mkdir src include
```

**CMakeLists.txt:**
```cmake
cmake_minimum_required(VERSION 3.16)
project(MyCommRaTApp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CommRaT REQUIRED)
find_package(SeRTial REQUIRED)

add_executable(my_app src/main.cpp)
target_link_libraries(my_app PRIVATE CommRaT::commrat SeRTial::sertial rack)
target_include_directories(my_app PRIVATE /usr/local/include/rack)
```

---

## Step 2: Define Your Messages

Create `include/my_messages.hpp`:

```cpp
#pragma once
#include <commrat/commrat.hpp>
#include <cstdint>

// Plain POD structs -- no timestamp fields (TimsHeader handles that)
struct TemperatureData {
    float temperature_celsius{0.0f};
    float humidity_percent{0.0f};
};

struct StatusData {
    uint32_t status_code{0};
    float cpu_load{0.0f};
};

// Define your application
using MyApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>
>;
// MyApp provides: Module2<IOSpecs...>, get_message_id<T>(), serialize(), deserialize<T>()
// System messages (Subscribe, GetData, etc.) are auto-included.
```

---

## Step 3: Create Modules

Create `src/main.cpp`:

```cpp
#include "my_messages.hpp"
#include <commrat/platform/timestamp.hpp>
#include <iostream>

using namespace commrat;

// Timer-driven producer: publishes temperature every 500ms
class TemperatureSensor : public MyApp::Module2<
    Output<TemperatureData>,
    Period<Milliseconds(500)>
> {
public:
    using MyApp::Module2<Output<TemperatureData>, Period<Milliseconds(500)>>::Module2;

protected:
    void process(TemperatureData& output) override {
        static float temp = 20.0f;
        temp += (rand() % 20 - 10) * 0.1f;
        
        output = TemperatureData{
            .temperature_celsius = temp,
            .humidity_percent = 45.0f + (rand() % 20 - 10) * 0.5f
        };
    }
};

// Input-driven consumer: processes each incoming TemperatureData
class TemperatureMonitor : public MyApp::Module2<
    Output<StatusData>,
    Input<TemperatureData>
> {
public:
    using MyApp::Module2<Output<StatusData>, Input<TemperatureData>>::Module2;

protected:
    void process(const TemperatureData& input, StatusData& output) override {
        std::cout << "[Monitor] Temperature: " << input.temperature_celsius << " C, "
                  << "Humidity: " << input.humidity_percent << "%\n";
        
        output = StatusData{
            .status_code = (input.temperature_celsius > 25.0f) ? 1u : 0u,
            .cpu_load = 0.3f
        };
    }
};

int main() {
    std::cout << "=== CommRaT Getting Started ===\n\n";
    
    // Configure producer
    SimpleOutputConfig sensor_config{
        .name = "TempSensor",
        .system_id = 1,
        .instance_id = 0
    };
    
    // Configure consumer (source = producer)
    SimpleOutputConfig monitor_config{
        .name = "TempMonitor",
        .system_id = 2,
        .instance_id = 0,
        .source_system_id = 1,
        .source_instance_id = 0
    };
    
    TemperatureSensor sensor(sensor_config);
    TemperatureMonitor monitor(monitor_config);
    
    sensor.start();
    monitor.start();  // Auto-subscribes to sensor
    
    Time::sleep(Seconds(10));
    
    monitor.stop();
    sensor.stop();
    
    std::cout << "Done!\n";
    return 0;
}
```

---

## Build and Run

```bash
# Build (standard platform, default)
mkdir build && cd build
cmake .. && make

# To build for EVL / Xenomai 4 (requires libevl on build host):
cmake .. -DCOMMRAT_PLATFORM=EVL && make

# Start TIMS router (separate terminal)
tims_router_tcp

# Run
./my_app
```

**Expected Output:**
```
=== CommRaT Getting Started ===

[Monitor] Temperature: 20.3 C, Humidity: 45.2%
[Monitor] Temperature: 19.8 C, Humidity: 44.8%
...

Done!
```

---

## What Just Happened?

1. **Message definition**: Plain POD structs registered via `CommRaT<Message::Data<T>, ...>`
2. **Producer**: `TemperatureSensor` with `Period<Milliseconds(500)>` publishes every 500ms
3. **Consumer**: `TemperatureMonitor` with `Input<TemperatureData>` auto-subscribes and receives each message
4. **Automatic**: Subscription protocol, mailbox creation, thread management, serialization

---

## Next Steps

### Multi-Input Synchronization

Fuse data from multiple producers using `SyncedInput<T>` and `Synced<T>`:

```cpp
class Fusion : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,          // Primary (drives execution)
    SyncedInput<GPSData>     // Secondary (time-synchronized)
> {
protected:
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) override {
        if (gps) {
            output = fuse(imu, gps.value());   // Fresh GPS data
        } else if (gps.is_valid()) {
            output = fuse(imu, gps.stale());   // Stale but usable
        } else {
            output = dead_reckoning(imu);      // No GPS at all
        }
    }
};
```

### Multi-Output Modules

Produce multiple message types simultaneously:

```cpp
class MultiSensor : public MyApp::Module2<
    Output<RawData>,
    Output<FilteredData>,
    Period<Milliseconds(100)>
> {
protected:
    void process(RawData& raw, FilteredData& filtered) override {
        raw = read_sensor();
        filtered = apply_filter(raw);
    }
};
```

### Loop Mode (Maximum Throughput)

Omit `Period<>` and `Input<>` for continuous execution:

```cpp
class Spinner : public MyApp::Module2<Output<CounterData>> {
protected:
    void process(CounterData& output) override {
        output = CounterData{.count = counter_++};
    }
private:
    uint64_t counter_ = 0;
};
```

### Explore Examples

See `examples/` directory:
- `continuous_input_example.cpp` -- Producer/consumer pattern
- `loop_mode_example.cpp` -- Maximum throughput
- `multi_output_sensor_fusion.cpp` -- Multi-output fusion
- `clean_interface_example.cpp` -- Minimal boilerplate

---

## Troubleshooting

**"Connection refused"**: Start `tims_router_tcp` in a separate terminal.

**"Message not received"**: Verify `source_system_id`/`source_instance_id` match the producer's config.

**Compilation errors**: Ensure C++20 is enabled and SeRTial/CommRaT are installed system-wide.

---

**Next**: [User Guide](USER_GUIDE.md) for comprehensive documentation, [API Reference](API_REFERENCE.md) for complete API.
