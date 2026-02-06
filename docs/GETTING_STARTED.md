# Getting Started with CommRaT

This guide will help you create your first CommRaT application in three simple steps.

---

## Prerequisites

- **C++20 compiler** (GCC 10+, Clang 12+)
- **CMake 3.20+**
- **CommRaT installed** (see [main README](../README.md))
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
cmake_minimum_required(VERSION 3.20)
project(MyCommRaTApp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find CommRaT (installed via sudo make install)
find_package(CommRaT REQUIRED)

# Find dependencies
find_package(SeRTial REQUIRED)
find_package(TiMS REQUIRED)  # If available as package

# Your executable
add_executable(my_app src/main.cpp)

target_link_libraries(my_app 
    PRIVATE 
        CommRaT::commrat
        SeRTial::sertial
        tims  # Or TiMS::tims if using package
)
```

---

## Step 2: Define Your Messages

Create `include/my_messages.hpp`:

```cpp
#pragma once
#include <commrat/system_registry.hpp>
#include <cstdint>

// Step 2.1: Define your data structures
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

// Step 2.2: Create message registry using simple aliases
// CombinedRegistry automatically includes system messages (subscription protocol)
using AppRegistry = commrat::CombinedRegistry<
    commrat::Message::Data<TemperatureData>,  // Automatically gets prefix/subprefix/AUTO_ID
    commrat::Message::Data<StatusData>        // Clean and simple!
>;

// Optional: Create a shorter Module alias for your app
template<typename OutputData, typename InputMode, typename... CommandTypes>
using Module = commrat::Module<AppRegistry, OutputData, InputMode, CommandTypes...>;
```

**That's it for messages!** You've:
- ✅ Defined your data types (plain structs)
- ✅ Registered them with AUTO_ID
- ✅ (Optional) Created a Module alias for convenience

The Module base class handles all mailbox management internally - you never need to touch mailboxes directly!

---

## Step 3: Create Modules

Create `src/main.cpp`:

```cpp
#include "my_messages.hpp"
#include <commrat/registry_module.hpp>
#include <iostream>
#include <thread>

// Step 3.1: Create a Producer Module
class TemperatureSensor : public Module<TemperatureData, commrat::PeriodicInput> {
public:
    using Module::Module;  // Inherit constructor

protected:
    // Called every config.period milliseconds
    TemperatureData process() override {
        static float temp = 20.0f;
        temp += (rand() % 20 - 10) * 0.1f;  // Simulate sensor noise
        
        return TemperatureData{
            .temperature_celsius = temp,
            .humidity_percent = 45.0f + (rand() % 20 - 10) * 0.5f,
            .timestamp_ms = static_cast<uint64_t>(
                std::chrono::system_clock::now().time_since_epoch().count()
            )
        };
    }
};

// Step 3.2: Create a Consumer Module
class TemperatureMonitor : public Module<StatusData, commrat::ContinuousInput<TemperatureData>> {
public:
    using Module::Module;

protected:
    // Called for each received TemperatureData message
    StatusData process_continuous(const TemperatureData& input) override {
        std::cout << "[Monitor] Temperature: " << input.temperature_celsius << "°C, "
                  << "Humidity: " << input.humidity_percent << "%\n";
        
        // Calculate status based on temperature
        uint32_t status = (input.temperature_celsius > 25.0f) ? 1 : 0;
        
        return StatusData{
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
    commrat::ModuleConfig sensor_config{
        .system_id = 1,      // System ID
        .instance_id = 0,    // Instance within system
        .period = std::chrono::milliseconds(500),  // 2Hz
        .name = "TempSensor"
    };
    
    // Configure monitor (consumer)
    commrat::ModuleConfig monitor_config{
        .system_id = 2,
        .instance_id = 0,
        .name = "TempMonitor"
    };
    
    // Set subscription target (monitor subscribes to sensor)
    monitor_config.subscription_sources = {
        {.system_id = sensor_config.system_id, 
         .instance_id = sensor_config.instance_id}
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

// Add to registry using Message::Command<>
using ExtendedRegistry = commrat::CombinedRegistry<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Command<CalibrateCommand>
>;

// Update module signature
class TemperatureSensor : public Module<
    TemperatureData, 
    commrat::PeriodicInput,
    CalibrateCommand  // ← Add command type here
> {
protected:
    // Add command handler
    void on_command(const CalibrateCommand& cmd) override {
        std::cout << "Calibrating to " << cmd.reference_temp << "°C\n";
        // Perform calibration...
    }
    
    TemperatureData process() override {
        // Existing code...
    }
};
```

### Use LoopInput for Maximum Throughput

```cpp
class HighSpeedCounter : public Module<CounterData, commrat::LoopInput> {
protected:
    CounterData process() override {
        return CounterData{.count = counter_++};
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
tims_router
```

### "Message not received"

**Problem**: Incorrect subscription configuration

**Solution**: Verify `subscription_sources` matches producer's `system_id` and `instance_id`

### Compilation errors with AUTO_ID

**Problem**: Understanding message ID assignment

**Solution**: CommRaT auto-increments within the same category. The simple aliases handle this automatically:
```cpp
// ✅ GOOD: Using simple aliases - IDs assigned automatically
Message::Data<SensorA>    // Gets ID 0x01000001
Message::Data<SensorB>    // Gets ID 0x01000002
Message::Command<ResetCmd>    // Gets ID 0x01010001 (different subprefix)

// Advanced: Manual IDs if you need specific values
Message::Data<SpecialData, MessagePrefix::UserDefined, 42>  // Gets ID 0x0100002A
```

---

## Summary

**Creating a CommRaT application requires just 3 steps:**

1. **Set up project** - CMake with `find_package(CommRaT)`
2. **Define messages** - Plain structs + `Message::Data<T>` / `Message::Command<T>`
3. **Create modules** - Inherit from `Module<Output, InputMode, Commands...>`

**Everything else is automatic:**
- ✅ Message ID assignment
- ✅ Subscription protocol
- ✅ Thread management
- ✅ Serialization/deserialization
- ✅ Type-safe dispatch

**Next**: Read [Architecture Documentation](README.md) for advanced features and Phase 5 roadmap.
