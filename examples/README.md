# CommRaT Examples

**Status**: Production-Ready
**Test Coverage**: All examples validated in CTest

## Overview

This directory contains working examples demonstrating CommRaT's core features. All examples are part of the automated test suite and validated on every build.

## Available Examples

### continuous_input_example.cpp
**Producer → Consumer Pattern**

Classic two-module system with automatic subscription:
- `SensorModule`: Generates temperature data at 1Hz (PeriodicInput)
- `FilterModule`: Processes incoming data (Input<T>)

**Run**: `./example_continuous_input`  
**Test**: `ctest -R example_continuous_input`

---

### clean_interface_example.cpp
**Simple API Showcase**

Demonstrates CommRaT's clean, minimal API:
- No verbose MessageDefinition syntax
- No registry template parameters in module
- Automatic message ID assignment
- Clean `Module<Output, Input>` syntax

**Run**: `./example_clean_interface`  
**Test**: `ctest -R example_clean_interface`

---

### command_example.cpp
**Variadic Command Handling**

Shows command pattern with multiple command types:
- Module with multiple `on_command()` overloads
- Type-safe command dispatch
- Command sending from external controller

**Run**: `./example_commands`  
**Test**: `ctest -R example_commands`

---

### loop_mode_example.cpp
**Maximum Throughput (LoopInput)**

Demonstrates LoopInput for maximum performance:
- No blocking receive - continuous processing
- Suitable for computation-heavy modules
- Processes as fast as possible

**Run**: `./example_loop_mode`  
**Test**: `ctest -R example_loop_mode`

---

### multi_output_runtime.cpp
**Runtime Multi-Output**

Single module producing multiple output types:
- `Outputs<TemperatureData, StatusData>` specification
- Type-based subscriber filtering
- Multiple consumers receiving different types

**Run**: `./example_multi_output_runtime`  
**Test**: `ctest -R example_multi_output_runtime`

---

### multi_output_sensor_fusion.cpp
**Complete Sensor Fusion System**

Production-like multi-sensor fusion:
- IMU + GPS + Camera sensors
- Multi-output fusion module
- Separate monitoring modules for each output type
- Realistic sensor rates and data

**Run**: `./example_sensor_fusion`  
**Test**: `ctest -R example_sensor_fusion`

---

## Documentation Examples

For more detailed, fully-documented examples, see: **`../docs/examples/`**

- `01_hello_commrat/` - Simplest possible system with full explanation
- `02_multi_output/` - Multi-output with detailed comments
- `03_multi_input_fusion/` - Multi-input time synchronization

## Building

```bash
cd build
cmake ..
make -j$(nproc)
```

## Running All Tests

```bash
ctest                    # All 22 tests (13 unit + 9 examples)
ctest -R example         # Only examples (9 tests)
ctest --verbose          # See full output
```

## Example Output

Most examples run continuously (forever) until interrupted:
- In tests: Automatically stopped after 10 seconds (timeout = success)
- Interactive: Press Ctrl+C to stop

Examples with finite runtime:
- `example_clean_interface` - Runs for 8 seconds, exits cleanly
- `example_commands` - Runs for 8 seconds, exits cleanly
- `doc_example_hello_commrat` - Runs for 6 seconds, exits cleanly

## Message Types

Examples use various message structures from `examples/messages/`:
- `user_messages.hpp` - Simple user-defined messages
- All messages are plain POD structs (no special base class needed)

## Key Patterns Demonstrated

- **PeriodicInput**: Timer-driven message generation
- **LoopInput**: Maximum throughput processing
- **Input<T>**: Event-driven single input
- **Inputs<T,U,V>**: Multi-input time synchronization
- **Output<T>**: Single output type
- **Outputs<T,U>**: Multiple output types
- **Commands**: User command handling with variadic on_command()

## Integration with Test Suite

All examples are automatically:
- Compiled on every build
- Executed in test suite (via CTest)
- Validated for runtime correctness
- Used to catch regressions

Continuous examples use wrapper script (`test/run_continuous_example.sh`) that exits successfully on timeout.

## Adding New Examples

1. Create `new_example.cpp` in this directory
2. Add to `CMakeLists.txt`:
   ```cmake
   add_executable(example_new_example examples/new_example.cpp)
   target_link_libraries(example_new_example PRIVATE commrat)
   target_include_directories(example_new_example PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tims /usr/local/include/rack)
   ```
3. Add test:
   ```cmake
   # For continuous examples
   add_test(NAME example_new_example COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/run_continuous_example.sh $<TARGET_FILE:example_new_example>)
   
   # For finite-time examples
   add_test(NAME example_new_example COMMAND example_new_example)
   ```
4. Verify: `ctest -R example_new_example -V`
