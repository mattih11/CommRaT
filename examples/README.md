# CommRaT Examples

All examples validated in CTest.

## Available Examples

### continuous_input_example.cpp
**Producer -> Consumer Pattern**

Two-module system with automatic subscription:
- `SensorModule`: Timer-driven producer (`Period<>`)
- `FilterModule`: Input-driven consumer (`Input<T>`)

**Run**: `./example_continuous_input`

---

### clean_interface_example.cpp
**Simple API Showcase**

Minimal boilerplate CommRaT application using `Module2<Output<T>, Period<>>`.

**Run**: `./example_clean_interface`

---

### command_example.cpp
**Command Handling**

Command pattern with multiple command types and type-safe dispatch.

**Run**: `./example_commands`

---

### loop_mode_example.cpp
**Maximum Throughput**

Loop mode (no Period, no Input) for continuous processing at maximum speed.

**Run**: `./example_loop_mode`

---

### multi_output_runtime.cpp
**Multi-Output Producer**

Single module with multiple `Output<T>` specs and type-based subscriber filtering.

**Run**: `./example_multi_output_runtime`

---

### multi_output_sensor_fusion.cpp
**Complete Sensor Fusion System**

Multi-sensor multi-output fusion with separate monitoring modules.

**Run**: `./example_sensor_fusion`

---

## Documentation Examples

For detailed, documented examples with READMEs: **`../docs/examples/`**

- `01_hello_commrat/` -- Simplest possible system
- `02_multi_output/` -- Multi-output with type filtering
- `03_multi_input_fusion/` -- Multi-input time synchronization

## Building

```bash
cd build && cmake .. && make -j$(nproc)
```

## Running Tests

```bash
ctest                    # All tests
ctest -R example         # Only examples
```

## Message Types

Examples use shared message types from `examples/messages/`:
- `user_messages.hpp` -- User-defined messages
- All messages are plain POD structs

## Key Patterns

| Pattern | I/O Specs | Description |
|---------|-----------|-------------|
| Timer-driven | `Output<T>, Period<N>` | Fixed-rate data generation |
| Input-driven | `Output<T>, Input<U>` | Event-driven processing |
| Loop mode | `Output<T>` (no Period/Input) | Maximum throughput |
| Multi-output | `Output<A>, Output<B>, Period<N>` | Multiple data streams |
| Multi-input | `Output<T>, Input<U>, SyncedInput<V>` | Sensor fusion |
