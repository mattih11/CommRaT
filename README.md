<div align="center">
  <img src="docs/CommRaT.png" alt="CommRaT Logo" width="400"/>
</div>

# CommRaT - Modern C++ Real-Time Communication Framework

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Phase 5.3 Complete](https://img.shields.io/badge/Phase-5.3%20Complete-green.svg)](docs/README.md)

A modern C++20 communication framework that combines **RACK's TiMS IPC** message service with **SeRTial's** zero-allocation serialization, providing compile-time safe, real-time capable messaging with templated message types and a powerful mailbox interface for efficient type dispatch.

**[📚 Full Documentation](docs/README.md)** | **[🚀 Getting Started](docs/GETTING_STARTED.md)** | **[📖 Examples](examples/)**

## Features

- **Multi-Output Modules** ⭐ NEW (Phase 5.3): Produce multiple message types simultaneously with type-specific delivery
- **Ultra-Clean User Interface**: Define messages ONCE, use payload types everywhere (no MessageDefinition in user code)
- **Payload-Only API**: Module<OutputSpec, InputSpec> with Output<T>/Outputs<T, U, V> specification
- **Auto-Subscription**: Input<PayloadT> automatically handles subscription protocol with type filtering
- **Variadic Commands**: Module<..., Cmd1, Cmd2, Cmd3> with type-safe on_command() handlers
- **System Messages Auto-Included**: CombinedRegistry automatically adds subscription protocol messages
- **Compile-Time Message IDs**: 0xPSMM format (Prefix, SubPrefix, Message ID) with auto-increment
- **Modern C++20**: Full template metaprogramming with concepts, `std::span`, and type safety
- **Zero-Allocation Serialization**: Stack-allocated `std::byte` buffers via SeRTial with compile-time size computation
- **Compile-Time Type Safety**: Templated message types with static validation and collision detection
- **Module Framework**: RAII-based Module<> with PeriodicInput/LoopInput/Input<T> modes
- **Message Registry**: Compile-time type registry for zero-overhead message dispatch
- **Runtime Visitor Pattern**: Efficient runtime dispatch without virtual functions (receive_any)
- **SeRTial Integration**: Automatic serialization using `fixed_vector`, `fixed_string`, and `buffer_type`
- **TiMS IPC Backend**: Socket-based real-time messaging from RACK (casting at C API boundary only)
- **RT-Capable**: No dynamic allocation in message paths, deterministic behavior
- **Clean Interfaces**: `std::span<std::byte>` throughout, pointer+size only at TiMS boundary

## 📚 Documentation

- **[Getting Started Guide](docs/GETTING_STARTED.md)** - Build your first CommRaT application
- **[Architecture & Concepts](docs/README.md)** - Detailed documentation and Phase 5 roadmap
- **[Examples](examples/)** - Working examples demonstrating all features
- **[API Reference](include/commrat/)** - Header documentation

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/mattih11/CommRaT.git
cd CommRaT

# Set up dependencies (SeRTial and RACK TiMS)
ln -s /path/to/SeRTial SeRTial
ln -s /path/to/rack/tims tims

# Build and install
mkdir -p build && cd build
cmake ..
make -j$(nproc)
sudo make install
```

### Your First CommRaT Application

**Step 1: Define Messages**
```cpp
#include <commrat/commrat.hpp>

struct TemperatureData {
    float temperature_celsius;
    uint64_t timestamp_ms;
};

// Simple! Just Message::Data<YourType>
using AppRegistry = commrat::CombinedRegistry<
    commrat::Message::Data<TemperatureData>,
    commrat::Message::Data<StatusData>
>;

template<typename OutputData, typename InputMode, typename... Commands>
using Module = commrat::Module<AppRegistry, OutputData, InputMode, Commands...>;
```

**Step 2: Create Modules**
```cpp
// Producer: publishes temperature every 500ms
class SensorModule : public Module<Output<TemperatureData>, PeriodicInput> {
protected:
    TemperatureData process() override {
        return {.temperature_celsius = read_sensor()};
    }
};

// Consumer: processes incoming temperature data
class MonitorModule : public Module<Output<StatusData>, Input<TemperatureData>> {
protected:
    StatusData process_continuous(const TemperatureData& input) override {
        std::cout << "Temperature: " << input.temperature_celsius << "°C\n";
        return calculate_status(input);
    }
};

// Multi-Output Producer: generates multiple message types simultaneously (Phase 5.3)
class SensorFusion : public Module<Outputs<RawData, FilteredData, Diagnostics>, PeriodicInput> {
protected:
    void process(RawData& raw, FilteredData& filtered, Diagnostics& diag) override {
        raw = read_sensors();
        filtered = apply_filter(raw);
        diag = compute_diagnostics(raw, filtered);
        // Each subscriber receives ONLY their type (type-specific filtering)
    }
};
```

**Step 3: Run**
```cpp
int main() {
    SensorModule sensor(config);
    MonitorModule monitor(config);
    
    sensor.start();
    monitor.start();  // Auto-subscribes to sensor
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    monitor.stop();
    sensor.stop();
}
```

**See [Getting Started Guide](docs/GETTING_STARTED.md) for complete tutorial.**

## Running Examples

All examples demonstrate Phase 5 features with clean, professional output:

```bash
cd build

# Producer→Consumer with auto-subscription
./example_continuous_input

# Minimal boilerplate example
./example_clean_interface

# Type-safe command handling
./example_commands

# Maximum throughput demo (~200K-400K iter/sec)
./example_loop_mode

# Multi-output with 2 types (Phase 5.3)
./example_multi_output_runtime

# Advanced sensor fusion with 3 outputs (Phase 5.3)
./example_sensor_fusion
```

**See [examples/](examples/) directory for source code.**

## Architecture Highlights

- **3-Mailbox System**: Separate CMD/WORK/DATA mailboxes per module (RACK-style)
- **Blocking Receives**: 0% CPU when idle, immediate response when active
- **Compile-Time IDs**: Message IDs calculated at compile time with collision detection
- **Auto-Subscription**: `ContinuousInput<T>` automatically handles subscription protocol
- **Type-Safe Dispatch**: Visitor pattern for runtime dispatch without virtual functions
- **Real-Time Safe**: No dynamic allocation in hot paths, deterministic behavior

**[Read Full Architecture Documentation →](docs/README.md)**

## License

See LICENSE file for details.

## References

- [RACK Project](https://github.com/smolorz/RACK) - Robotics Application Construction Kit
- [SeRTial Library](https://github.com/mattih11/SeRTial) - Reflective C++ serialization
