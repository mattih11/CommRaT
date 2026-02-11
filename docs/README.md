# CommRaT Documentation

**Last Updated**: February 8, 2026

---

## Quick Links

- **[Getting Started](GETTING_STARTED.md)** - Installation and first program
- **[User Guide](USER_GUIDE.md)** - Comprehensive guide (under development)
- **[API Reference](API_REFERENCE.md)** - API overview (under development)
- **[Examples](EXAMPLES.md)** - Annotated examples (under development)
- **[Architecture](ARCHITECTURE.md)** - System design overview (under development)

See **[DOCUMENTATION_STRATEGY.md](DOCUMENTATION_STRATEGY.md)** and **[IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)** for documentation roadmap.

---

## Documentation Structure

### User Documentation

- **[USER_GUIDE.md](USER_GUIDE.md)** - Comprehensive user guide (in progress)
- **[API_REFERENCE.md](API_REFERENCE.md)** - High-level API overview (in progress)
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - User-friendly architecture explanation (in progress)
- **[EXAMPLES.md](EXAMPLES.md)** - Annotated example catalog (in progress)
- **[GETTING_STARTED.md](GETTING_STARTED.md)** - Quick start guide
- **[MIGRATION_GUIDE.md](MIGRATION_GUIDE.md)** - Version migration paths (planned)
- **[FAQ.md](FAQ.md)** - Frequently asked questions (planned)
- **[CONTRIBUTING.md](CONTRIBUTING.md)** - Development guidelines (planned)

### Internal Documentation

- **[internal/](internal/)** - Design decisions, phase history, refactoring notes
  - **[design_decisions/](internal/design_decisions/)** - Why technical choices were made
  - **[phase_history/](internal/phase_history/)** - Development timeline and evolution
  - **[refactoring/](internal/refactoring/)** - Major refactoring documentation

### Visual Assets

- `CommRaT.png` - Project logo (raster)
- `CommRaT.svg` - Project logo (vector)

### Archived Documentation

See `archive/` directory for historical design documents from earlier development phases.

---

## Current Features

### Core Messaging System

**Compile-Time Message System**
- `CommRaT<MessageDefs...>` application template (user-facing API)
- `MessageRegistry<MessageDefs...>` with variadic templates (internal)
- Automatic message ID calculation (compile-time)
- Zero runtime allocation for message dispatch
- Type-safe serialization via SeRTial

**3-Mailbox Architecture**
- Hierarchical addressing: `(system_id << 8) | instance_id` + offset
- Separate mailboxes for CMD (commands), WORK (subscription), DATA (streams)
- Blocking receives, zero CPU when idle
- Thread-per-mailbox model

### Module Framework

**I/O Specifications**
```cpp
template<typename UserRegistry,
         typename OutputSpec,    // Output<T>, Outputs<Ts...>, or raw T
         typename InputSpec,     // Input<T>, Inputs<Ts...>, PeriodicInput, LoopInput
         typename... CommandTypes>
class Module;
```

**Multi-Input with Synchronized getData**
```cpp
// 3-input sensor fusion with time synchronization
class FusionModule : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>,
    PrimaryInput<IMUData>  // Designate IMU as synchronization driver
> {
protected:
    void process(
        const IMUData& imu,      // PRIMARY - blocking receive
        const GPSData& gps,      // SECONDARY - getData(imu.timestamp)
        const LidarData& lidar,  // SECONDARY - getData(imu.timestamp)
        FusedData& output        // OUTPUT - result written here
    ) override {
        output = fuse_sensors(imu, gps, lidar);
    }
};
```

**Timestamp Metadata Accessors**
```cpp
// Access input metadata in process functions
void process(const IMUData& imu, const GPSData& gps, const LidarData& lidar, FusedData& output) override {
    // Index-based access (always works)
    auto imu_meta = get_input_metadata<0>();
    auto gps_meta = get_input_metadata<1>();
    
    // Type-based access (when types unique)
    auto lidar_ts = get_input_timestamp<LidarData>();
    
    // Check data freshness
    if (!has_new_data<1>()) {
        std::cout << "GPS data is stale\n";
    }
    
    // Check data validity
    if (!is_input_valid<2>()) {
        std::cerr << "Lidar getData failed\n";
    }
    
    output = fuse_sensors(imu, gps, lidar);
}
```

**Additional Capabilities:**
- I/O specification types: `Output<T>`, `Outputs<Ts...>`, `Input<T>`, `Inputs<Ts...>`
- Multi-input synchronization via `HistoricalMailbox` with `getData(timestamp, tolerance)`
- Primary input designation: `PrimaryInput<T>` for explicit control
- Automatic timestamp propagation through message chains
- Input metadata: timestamp, sequence number, freshness, validity
- Index-based and type-based metadata access
- Virtual `process` with proper inheritance
- Compile-time validation with helpful error messages

**Input Modes:**
- `PeriodicInput` - Timer-based execution
- `LoopInput` - Maximum throughput (100% CPU)
- `Input<T>` - Processes input stream
- `Inputs<Ts...>` - Multi-input with synchronized getData

**Features:**
- Automatic subscription management
- Type-safe command dispatch
- Virtual process methods for proper override
- Lifecycle hooks (on_init, on_start, on_stop, on_cleanup)
- Zero-overhead abstractions

### Examples

**Complete Working Examples:**
```
examples/
├── continuous_input_example.cpp  # Producer→Consumer with subscription
├── clean_interface_example.cpp   # Minimal module interface
├── command_example.cpp            # Command handling (variadic)
└── loop_mode_example.cpp          # Maximum throughput demo
```

**Tests:**
```
test/
├── test_timestamp_logic.cpp      # Metadata accessors
├── test_3input_fusion.cpp        # Multi-input synchronization
├── test_historical_mailbox.cpp   # HistoricalMailbox getData
└── test_process_signature.cpp    # Type constraints
```

---

## Feature Overview

### I/O Specification Types (Complete)
- `Output<T>`, `Outputs<Ts...>`, `NoOutput`
- `Input<T>`, `Inputs<Ts...>`
- Normalization traits and concepts
- Comprehensive test suite

### Module Refactoring (Complete)
- Updated Module template signature to use I/O specs
- Fixed process_continuous virtual dispatch bug
- Helper base class `ContinuousProcessorBase<InputData, OutputData>`
- Backward compatibility maintained

### Multi-Output Support (Complete)
- `void process(T1& out1, T2& out2)` for multiple outputs
- Type-specific publishing to subscribers
- Multi-output with multi-input support

### Multi-Input Infrastructure (Complete)
- `PrimaryInput<T>` designation
- `ModuleConfig.input_sources` for multi-subscription
- Input type extraction and validation
- Multi-input module configuration

### Multi-Input Implementation (Complete)
- `HistoricalMailbox` with timestamped history
- Multi-input mailbox creation and management
- `multi_input_loop()` with getData synchronization
- Compilation tests with type safety validation

### End-to-End Multi-Input (Complete)
- 3-input sensor fusion test (IMU + GPS + LiDAR)
- Primary/secondary input coordination
- getData with tolerance-based synchronization
- Secondary input receive threads
- **Status**: Fully functional - Example 03 demonstrates successful fusion with sync ages <100ms

### Timestamp Metadata Accessors (Complete)
- `InputMetadata` structure with timestamp, sequence, freshness, validity
- Index-based accessors: `get_input_metadata<0>()`
- Type-based accessors: `get_input_metadata<IMUData>()`
- Convenience methods: `get_input_timestamp<Index>()`, `has_new_data<Index>()`
- Automatic metadata population before process() calls
- Single source of truth: `TimsHeader.timestamp` only

### Module Cleanup (Complete)
- Extracted helper modules for better organization
- registry_module.hpp reduced to 1,003 lines (49% reduction from original 1,952)
- Extracted modules: subscription, publishing, loops, metadata accessors

---

## Future Development

### Planned Features

**Optional Secondary Inputs**
- Graceful getData failure handling
- Fallback strategies for missing/stale data
- `Optional<T>` wrapper for secondary inputs

**Input Buffering Strategies**
- Sliding window buffers
- Latest-only mode
- Configurable overflow behavior

**ROS 2 Adapter** (separate repository)
- rclcpp-commrat bridge
- Automatic message conversion
- Lifecycle node integration

**Performance Tools**
- Real-time profiling
- Latency measurement
- Static analysis for RT safety

---

## 📖 Core Concepts

### Application Definition

Single source of truth for all communication types:

```cpp
// User-facing API: CommRaT<> defines your application
using MyApp = commrat::CommRaT<
    // User messages (simplified Message:: syntax)
    commrat::Message::Data<SensorData>,
    commrat::Message::Command<CommandData>
>;

// MyApp now provides:
//   MyApp::Module<OutputSpec, InputSpec, ...Commands>
//   MyApp::Mailbox<T>
//   MyApp::serialize(msg) / deserialize<T>(data)
//   MyApp::get_message_id<T>()

// System messages (subscription protocol) automatically included
```

**Key Properties:**
- Compile-time type registration
- Automatic message ID generation (0xPSMM format)
- Buffer size computation via fold expressions
- Type-safe visitor pattern for dispatch
- Clean user API hides internal MessageRegistry complexity

### 4-Mailbox System

### MailboxSet Architecture

**Each output type gets its own MailboxSet** with 3 mailboxes:

```
MailboxSet for OutputType:
  Base Address = [type_id:16][system_id:8][instance_id:8]
  
  CMD     mailbox: base + 0   → Commands for this output type
  WORK    mailbox: base + 16  → Subscription protocol for this output type  
  PUBLISH mailbox: base + 32  → Publishes this output type to subscribers
```

**Plus a shared DATA mailbox for inputs:**
```
DATA mailbox: module_base + 48  → Receives input data (shared across output types)
```

**Examples:**

**Single-Output Module:**
```cpp
class Sensor : public Module<Output<TempData>, PeriodicInput>
```
- 1 MailboxSet with 3 mailboxes (CMD, WORK, PUBLISH)
- 1 DATA mailbox if it has inputs
- Total: 3-4 mailboxes

**Multi-Output Module:**
```cpp
class Fusion : public Module<Outputs<RawData, FilteredData, Diagnostics>, Input<SensorData>>
```
- 3 MailboxSets × 3 mailboxes = 9 mailboxes (CMD, WORK, PUBLISH per output type)
- 1 shared DATA mailbox for SensorData input
- Total: 10 mailboxes
- Each output type has unique base address (includes type_id)
- Subscribers subscribe to specific output types via their WORK mailbox

**Why this design?**
- **Type-specific subscription**: Subscribers request specific output types
- **Independent control**: Each output type can have its own subscriber list
- **Scalable addressing**: Type ID in base address allows unique mailboxes per output
- **No multiplexing**: Each output type has dedicated publish path

### Module Lifecycle

```
Constructor
    ↓
on_init()        // Load config, allocate resources
    ↓
start()          // Start threads
    ↓
on_start()       // Post-startup initialization
    ↓
[Running]        // process() called in periodic_loop() or continuous_loop()
    ↓
stop()           // Signal shutdown
    ↓
on_stop()        // Cleanup, print statistics
    ↓
on_cleanup()     // Release resources
    ↓
Destructor
```

### Input Modes

**PeriodicInput**: Timer-driven execution
```cpp
class SensorModule : public Module<Output<SensorData>, PeriodicInput> {
    void process(SensorData& output) override {
        output = read_sensor();  // Called every config.period
    }
};
```

**LoopInput**: Maximum throughput
```cpp
class CounterModule : public Module<Output<CounterData>, LoopInput> {
    void process(CounterData& output) override {
        output = {++count_};  // Called as fast as possible (100% CPU)
    }
};
```

**Input<T>**: Stream processing
```cpp
class FilterModule : public Module<Output<FilteredData>, Input<RawData>> {
    void process(const RawData& input, FilteredData& output) override {
        output = apply_filter(input);  // Called for each incoming message
    }
};
```

### Command Handling

Variadic command support with automatic dispatch:

```cpp
class MyModule : public Module<Output<Data>, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd> {
    void on_command(const ResetCmd& cmd) override {
        reset_state();
    }
    
    void on_command(const CalibrateCmd& cmd) override {
        run_calibration(cmd.duration_ms);
    }
    
    void on_command(const SetModeCmd& cmd) override {
        current_mode_ = cmd.mode;
    }
};
```

Sender side:
```cpp
SetModeCmd cmd{.mode = 3};
cmd_sender.send(cmd, target_module_cmd_mailbox);
```

---

## 🛠️ Design Principles

### 1. Compile-Time Everything

**Mantra**: If it can be checked at compile time, it should be checked at compile time.

- Message IDs computed at compile time
- Registry types validated at compile time
- Buffer sizes known at compile time
- Invalid module configurations rejected at compile time

### 2. Zero-Copy Where Possible

- Direct memory access via TiMS
- No unnecessary allocations
- Span-based APIs (std::span<std::byte>)
- SeRTial for zero-overhead serialization

### 3. Real-Time Safe

**Never in hot paths:**
- `new` / `delete`
- `malloc` / `free`
- `std::vector::push_back()` (may allocate)
- `std::string` operations (may allocate)
- Blocking I/O in loops
- Exceptions

**Use instead:**
- `std::array<T, N>`
- `sertial::fixed_vector<T, N>`
- `std::atomic<T>`
- `constexpr` functions
- Template metaprogramming

### 4. Simple User-Facing API

**Complex template logic should be invisible to users.**

Users see:
```cpp
class MySensor : public Module<SensorData, PeriodicInput> {
    SensorData process() override {
        return {read_sensor()};
    }
};
```

Behind the scenes:
- Automatic mailbox creation (3 mailboxes)
- Thread management (3 threads)
- Subscription protocol handling
- Message serialization/deserialization
- Type dispatch via visitor pattern
- Buffer management

---

## Related Resources

### Internal
- **Source Code**: `/home/muddy/src/CommRaT/include/commrat/`
- **Examples**: `/home/muddy/src/CommRaT/examples/`
- **Tests**: `/home/muddy/src/CommRaT/test/`

### External Dependencies
- **TiMS**: TIMS Interprocess Message System (C library)
- **SeRTial**: Serialization library (C++20)
- **RACK**: Original robotic framework (inspiration for module pattern)

### References
- C++20 Concepts: https://en.cppreference.com/w/cpp/language/constraints
- Fold Expressions: https://en.cppreference.com/w/cpp/language/fold
- SFINAE: https://en.cppreference.com/w/cpp/language/sfinae

---

## Contributing

When updating documentation:

1. **Active docs**: Update `work/ARCHITECTURE_ANALYSIS.md` for roadmap changes
2. **Historical record**: Add to `work/FIXES_APPLIED.md` for bug fixes
3. **Examples**: Keep examples up-to-date with latest API
4. **This README**: Update current state section for major changes

When archiving docs:
```bash
mv obsolete_doc.md archive/
```

---

**For detailed architecture analysis and future roadmap, see [ARCHITECTURE_ANALYSIS.md](work/ARCHITECTURE_ANALYSIS.md)**
