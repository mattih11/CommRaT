# Getting Started with CommRaT

This guide will help you create your first CommRaT application in three simple steps.

---

## Prerequisites

- **C++20 compiler** (GCC 10+, Clang 12+)
- **CMake 3.16+**
- **[SeRTial](https://github.com/mattih11/SeRTial)** installed system-wide
- **[RACK](https://github.com/smolorz/RACK)** installed system-wide (provides TiMS messaging and `tims_router`)
- **TIMS router running** (`tims_router` from RACK)

---

## Step 1: Set Up Your Project

### Create Project Structure

```bash
mkdir my_commrat_app
cd my_commrat_app
mkdir src include
```

### Create CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyCommRaTApp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find installed dependencies
find_package(CommRaT REQUIRED)
find_package(SeRTial REQUIRED)

# Your executable
add_executable(my_app src/main.cpp)

target_link_libraries(my_app 
    PRIVATE 
        CommRaT::commrat
        SeRTial::sertial
        rack  # RACK library (includes TiMS)
)

# Include RACK headers
target_include_directories(my_app PRIVATE /usr/local/include/rack)
```

---

## Step 2: Define Your Messages

Create `include/my_messages.hpp`:

```cpp
#pragma once
#include <commrat/commrat.hpp>
#include <cstdint>

// Step 2.1: Define your data structures (plain POD structs)
struct TemperatureData {
    float temperature_celsius{0.0f};
    float humidity_percent{0.0f};
    uint64_t timestamp_ms{0};
};

struct StatusData {
    uint32_t status_code{0};
    float cpu_load{0.0f};
    uint64_t uptime_ms{0};
};

// Step 2.2: Define your application with CommRaT<> template
// This replaces MessageRegistry and provides everything you need
using MyApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>
>;

// Now MyApp provides:
//   MyApp::Module<OutputSpec, InputSpec, ...Commands> - Module template
//   MyApp::Mailbox<T>                                  - Mailbox template (if needed)
//   MyApp::serialize(msg) / deserialize<T>(data)      - Serialization
//   MyApp::get_message_id<T>()                        - Message IDs
```

**That's it for messages!** You've:
- Defined your data types (plain structs)
- Created your application template with `CommRaT<>`
- Ready to use `MyApp::Module<>` for creating modules

CommRaT automatically includes system messages (subscription protocol) and handles all mailbox management internally!

---

## Step 3: Create Modules

Create `src/main.cpp`:

```cpp
#include "my_messages.hpp"
#include <iostream>
#include <thread>

using namespace commrat;

// Step 3.1: Create a Producer Module
class TemperatureSensor : public MyApp::Module<Output<TemperatureData>, PeriodicInput> {
public:
    using MyApp::Module<Output<TemperatureData>, PeriodicInput>::Module;  // Inherit constructor

protected:
    // Called every config.period milliseconds
    void process(TemperatureData& output) override {
        static float temp = 20.0f;
        temp += (rand() % 20 - 10) * 0.1f;  // Simulate sensor noise
        
        output = TemperatureData{
            .temperature_celsius = temp,
            .humidity_percent = 45.0f + (rand() % 20 - 10) * 0.5f,
            .timestamp_ms = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count()
            )
        };
    }
};

// Step 3.2: Create a Consumer Module
class TemperatureMonitor : public MyApp::Module<Output<StatusData>, Input<TemperatureData>> {
public:
    using MyApp::Module<Output<StatusData>, Input<TemperatureData>>::Module;

protected:
    // Called for each received TemperatureData message
    void process(const TemperatureData& input, StatusData& output) override {
        std::cout << "[Monitor] Temperature: " << input.temperature_celsius << "°C, "
                  << "Humidity: " << input.humidity_percent << "%\n";
        
        // Calculate status based on temperature
        uint32_t status = (input.temperature_celsius > 25.0f) ? 1 : 0;
        
        output = StatusData{
            .status_code = status,
            .cpu_load = 0.3f,
            .uptime_ms = static_cast<uint64_t>(uptime_++)
        };
    }

private:
    uint64_t uptime_ = 0;
};

// Step 3.3: Main - Connect Everything
int main() {
    std::cout << "=== CommRaT Getting Started Example ===\n\n";
    
    // Configure sensor (producer)
    ModuleConfig sensor_config{
        .name = "TempSensor",
        .system_id = 1,      // System ID
        .instance_id = 0,    // Instance within system
        .period = Milliseconds(500)  // 2Hz
    };
    
    // Configure monitor (consumer)
    ModuleConfig monitor_config{
        .name = "TempMonitor",
        .system_id = 2,
        .instance_id = 0,
        .input_sources = {
            InputSource{.system_id = 1, .instance_id = 0}
        }
    };
    
    // Create and start modules
    TemperatureSensor sensor(sensor_config);
    TemperatureMonitor monitor(monitor_config);
    
    std::cout << "Starting modules...\n";
    sensor.start();
    monitor.start();
    
    // Run for 10 seconds
    std::cout << "Running for 10 seconds...\n\n";
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // Cleanup
    std::cout << "\nStopping modules...\n";
    monitor.stop();
    sensor.stop();
    
    std::cout << "Done!\n";
    return 0;
}
```

---

## Build and Run

```bash
# Build
mkdir build && cd build
cmake ..
make

# Start TIMS router (in separate terminal)
tims_router

# Run your application
./my_app
```

**Expected Output:**
```
=== CommRaT Getting Started Example ===

Starting modules...
Running for 10 seconds...

[Monitor] Temperature: 20.3°C, Humidity: 45.2%
[Monitor] Temperature: 19.8°C, Humidity: 44.8%
[Monitor] Temperature: 20.5°C, Humidity: 45.7%
...

Stopping modules...
Done!
```

---

## What Just Happened?

1. **Message Definition**: You defined `TemperatureData` and `StatusData` as plain structs
2. **Registry Creation**: `CombinedRegistry` registered your types with AUTO_ID
3. **Producer Module**: `TemperatureSensor` publishes data every 500ms
4. **Consumer Module**: `TemperatureMonitor` automatically subscribed and receives each message
5. **Automatic Protocol**: Subscription/publish handled automatically by CommRaT

---

## Next Steps

### Add Commands

Extend your module with command handling:

```cpp
// Define a command
struct CalibrateCommand {
    float reference_temp{25.0f};
};

// Add to your application using CommRaT<>
using MyAppWithCommands = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Command<CalibrateCommand>
>;

// Update module signature - add command type as template parameter
class TemperatureSensor : public MyAppWithCommands::Module<
    Output<TemperatureData>, 
    PeriodicInput,
    CalibrateCommand  // ← Add command type here
> {
protected:
    // Add command handler
    void on_command(const CalibrateCommand& cmd) override {
        std::cout << "Calibrating to " << cmd.reference_temp << "°C\n";
        // Perform calibration...
    }
    
    void process(TemperatureData& output) override {
        // Existing code...
    }
};
```

### Use LoopInput for Maximum Throughput

```cpp
// Define counter data type
struct CounterData {
    uint64_t count{0};
};

// Add to application
using CounterApp = commrat::CommRaT<
    commrat::Message::Data<CounterData>
>;

// Module with LoopInput (no delays, runs as fast as possible)
class HighSpeedCounter : public CounterApp::Module<Output<CounterData>, LoopInput> {
protected:
    void process(CounterData& output) override {
        output = CounterData{.count = counter_++};
        // Called as fast as possible (200K-400K iter/sec)
    }
private:
    uint64_t counter_ = 0;
};
```

### Explore Examples

Check out the complete examples in `CommRaT/examples/`:
- `continuous_input_example.cpp` - Producer/consumer pattern
- `command_example.cpp` - Multiple command types
- `loop_mode_example.cpp` - Maximum throughput
- `clean_interface_example.cpp` - Minimal boilerplate

---

## Troubleshooting

### "Connection refused" or "No route to host"

**Problem**: TIMS router not running

**Solution**: Start the router in a separate terminal:
```bash
tims_router_tcp
```

### "Message not received"

**Problem**: Incorrect subscription configuration

**Solution**: Verify `subscription_sources` matches producer's `system_id` and `instance_id`

### Compilation errors with AUTO_ID

**Problem**: Understanding message ID assignment

**Solution**: CommRaT auto-increments within the same category. The simple aliases handle this automatically:
```cpp
// GOOD: Using CommRaT<> - IDs assigned automatically
using MyApp = commrat::CommRaT<
    commrat::Message::Data<SensorA>,      // Gets ID 0x01000001
    commrat::Message::Data<SensorB>,      // Gets ID 0x01000002
    commrat::Message::Command<ResetCmd>   // Gets ID 0x01010001 (different subprefix)
>;

// Advanced: Manual IDs if you need specific values
using MyApp = commrat::CommRaT<
    commrat::Message::Data<SpecialData, MessagePrefix::UserDefined, 42>  // Gets ID 0x0100002A
>;
```

---

## Summary

**Creating a CommRaT application requires just 3 steps:**

1. **Set up project** - CMake with `find_package(CommRaT)`
2. **Define application** - `CommRaT<Message::Data<T>, ...>` with your message types
3. **Create modules** - Inherit from `MyApp::Module<Output<T>, Input<U>, Commands...>`

**Everything else is automatic:**
- Message ID assignment
- Subscription protocol
- Thread management
- Serialization/deserialization
- Type-safe dispatch

**Next**: Read [USER_GUIDE.md](USER_GUIDE.md) for comprehensive documentation and [examples/](../examples/) for complete working examples.
