# CommRaT Documentation

**Last Updated**: February 8, 2026  
**Current Version**: Phase 6.10 Complete (Timestamp Metadata Accessors)

---

## 📚 Documentation Structure

### Active Documentation

- **[ARCHITECTURE_ANALYSIS.md](work/ARCHITECTURE_ANALYSIS.md)** - Architecture assessment and Phase 5/6 roadmap
- **[FIXES_APPLIED.md](work/FIXES_APPLIED.md)** - Historical bug fixes and solutions
- **[IO_SYNC_STRATEGY.md](work/IO_SYNC_STRATEGY.md)** - Multi-input synchronization strategy (Phase 6)
- **[RACK_ANALYSIS.md](work/RACK_ANALYSIS.md)** - RACK getData mechanism analysis
- **[SERTIAL_RINGBUFFER_REQUEST.md](work/SERTIAL_RINGBUFFER_REQUEST.md)** - RingBuffer specification for SeRTial

### Visual Assets

- `CommRaT.png` - Project logo (raster)
- `CommRaT.svg` - Project logo (vector)

### Archived Documentation

See `archive/` directory for historical design documents and validation logs from earlier phases.

---

## 🎯 Current State (Phase 6.10 Complete)

### What We Have

✅ **Compile-Time Message System**
- `MessageRegistry<MessageDefs...>` with variadic templates
- Automatic message ID calculation (compile-time)
- Zero runtime allocation for message dispatch
- Type-safe serialization via SeRTial

✅ **3-Mailbox Architecture**
- Hierarchical addressing: `(system_id << 8) | instance_id` + offset
- Separate mailboxes for CMD (commands), WORK (subscription), DATA (streams)
- Blocking receives, zero CPU when idle
- Thread-per-mailbox model

✅ **Module Framework with I/O Specifications** (Phase 5.2)
```cpp
template<typename UserRegistry,
         typename OutputSpec,    // Output<T>, Outputs<Ts...>, or raw T
         typename InputSpec,     // Input<T>, Inputs<Ts...>, PeriodicInput, LoopInput
         typename... CommandTypes>
class Module;
```

✅ **Multi-Input with Synchronized getData** (Phase 6.9 NEW)
```cpp
// 3-input sensor fusion with time synchronization
class FusionModule : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>,
    PrimaryInput<IMUData>  // Designate IMU as synchronization driver
> {
protected:
    FusedData process_multi_input(
        const IMUData& imu,      // PRIMARY - blocking receive
        const GPSData& gps,      // SECONDARY - getData(imu.timestamp)
        const LidarData& lidar   // SECONDARY - getData(imu.timestamp)
    ) override {
        return fuse_sensors(imu, gps, lidar);
    }
};
```

✅ **Timestamp Metadata Accessors** (Phase 6.10 NEW)
```cpp
// Access input metadata in process functions
FusedData process_multi_input(const IMUData& imu, const GPSData& gps, const LidarData& lidar) override {
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
    
    return fuse_sensors(imu, gps, lidar);
}
```

**Phase 5-6 Features:**
- I/O specification types: `Output<T>`, `Outputs<Ts...>`, `Input<T>`, `Inputs<Ts...>`
- Multi-input synchronization via `HistoricalMailbox` with `getData(timestamp, tolerance)`
- Primary input designation: `PrimaryInput<T>` for explicit control
- Automatic timestamp propagation through message chains
- Input metadata: timestamp, sequence number, freshness, validity
- Index-based and type-based metadata access
- Backward compatibility: `ContinuousInput<T>` → `Input<T>`
- Virtual `process_continuous` with proper inheritance
- Compile-time validation with helpful error messages

**Input Modes:**
- `PeriodicInput` - Timer-based execution
- `LoopInput` - Maximum throughput (100% CPU)
- `Input<T>` / `ContinuousInput<T>` - Processes input stream
- `Inputs<Ts...>` - Multi-input with synchronized getData (Phase 6)

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
├── test_timestamp_logic.cpp      # Phase 6.10: Metadata accessors
├── test_3input_fusion.cpp        # Phase 6.9: Multi-input synchronization
├── test_historical_mailbox.cpp   # Phase 6.3: HistoricalMailbox getData
└── test_process_signature.cpp    # Phase 5: Type constraints
```

---

## 🚀 Phase 5-6 Progress

### ✅ Phase 5.1: Core I/O Specification Types (Complete)
- `Output<T>`, `Outputs<Ts...>`, `NoOutput`
- `Input<T>`, `Inputs<Ts...>`
- Normalization traits and concepts
- Comprehensive test suite

### ✅ Phase 5.2: Module Refactoring (Complete)
- Updated Module template signature to use I/O specs
- Fixed process_continuous virtual dispatch bug
- Helper base class `ContinuousProcessorBase<InputData, OutputData>`
- Backward compatibility maintained

### ✅ Phase 5.3: Multi-Output Support (Complete)
- `void process(T1& out1, T2& out2)` for multiple outputs
- Type-specific publishing to subscribers
- Multi-output with multi-input support

### ✅ Phase 6.1-6.5: Multi-Input Infrastructure (Complete)
- `PrimaryInput<T>` designation
- `ModuleConfig.input_sources` for multi-subscription
- Input type extraction and validation
- Multi-input module configuration

### ✅ Phase 6.6-6.8: Multi-Input Implementation (Complete)
- `HistoricalMailbox` with timestamped history
- Multi-input mailbox creation and management
- `multi_input_loop()` with getData synchronization
- Compilation tests with type safety validation

### ✅ Phase 6.9: End-to-End Multi-Input (Complete)
- 3-input sensor fusion test (IMU + GPS + LiDAR)
- Primary/secondary input coordination
- getData with tolerance-based synchronization
- Secondary input receive threads

### ✅ Phase 6.10: Timestamp Metadata Accessors (Complete)
- `InputMetadata` structure with timestamp, sequence, freshness, validity
- Index-based accessors: `get_input_metadata<0>()`
- Type-based accessors: `get_input_metadata<IMUData>()`
- Convenience methods: `get_input_timestamp<Index>()`, `has_new_data<Index>()`
- Automatic metadata population before process() calls
- Single source of truth: `TimsHeader.timestamp` only

### 🔄 Phase 5 Wave 6: Module Cleanup (Complete)
### 🔄 Phase 5 Wave 6: Module Cleanup (Complete)
- Attempted extraction of remaining helpers
- Determined complex stateful helpers better kept inline
- Removed unnecessary abstraction files
- Final result: registry_module.hpp at 1,003 lines (49% reduction from original 1,952)
- Extracted valuable modules: subscription, publishing, loops, metadata accessors

---

## 🚀 Future: Phase 7 (Advanced Features)

### Planned Features

**Phase 7.1**: Optional Secondary Inputs
- Graceful getData failure handling
- Fallback strategies for missing/stale data
- `Optional<T>` wrapper for secondary inputs

**Phase 7.2**: Input Buffering Strategies  
- Sliding window buffers
- Latest-only mode
- Configurable overflow behavior

**Phase 7.3**: ROS 2 Adapter (separate repository)
- rclcpp-commrat bridge
- Automatic message conversion
- Lifecycle node integration

**Phase 7.4**: Performance Tools
- Real-time profiling
- Latency measurement
- Static analysis for RT safety

**Target**: Phase 7 features by **Q2 2026**

---

## 📖 Core Concepts

### Message Registry

Single source of truth for all communication types:

```cpp
using MyRegistry = MessageRegistry<
    // System messages (subscription protocol)
    MessageDefinition<SubscribeRequestPayload, ...>,
    MessageDefinition<SubscribeReplyPayload, ...>,
    // User messages (simplified Message:: syntax)
    Message::Data<SensorData>,
    Message::Command<CommandData>
>;
```

**Key Properties:**
- Compile-time type registration
- Automatic message ID generation (0xPSMM format)
- Buffer size computation via fold expressions
- Type-safe visitor pattern for dispatch

### 3-Mailbox System (RACK-style)

Each module has three independent mailboxes:

```
Module Base Address = (system_id << 8) | instance_id

CMD  mailbox: base + 0   → User commands (imperative)
WORK mailbox: base + 16  → Subscription protocol (control plane)
DATA mailbox: base + 32  → Input data streams (data plane)
```

**Why separate mailboxes?**
- **Isolation**: Commands don't interfere with data flow
- **Priority**: Can process commands with higher priority
- **Blocking**: Each thread blocks on its mailbox (0% CPU idle)
- **Scalability**: Clear separation of concerns

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
[Running]        // process() / process_continuous() / periodic_loop()
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
class SensorModule : public Module<SensorData, PeriodicInput> {
    SensorData process() override {
        return read_sensor();  // Called every config.period
    }
};
```

**LoopInput**: Maximum throughput
```cpp
class CounterModule : public Module<CounterData, LoopInput> {
    CounterData process() override {
        return {++count_};  // Called as fast as possible (100% CPU)
    }
};
```

**ContinuousInput<T>**: Stream processing
```cpp
class FilterModule : public Module<FilteredData, ContinuousInput<RawData>> {
    FilteredData process_continuous(const RawData& input) override {
        return apply_filter(input);  // Called for each incoming message
    }
};
```

### Command Handling

Variadic command support with automatic dispatch:

```cpp
class MyModule : public Module<Data, PeriodicInput, ResetCmd, CalibrateCmd, SetModeCmd> {
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
- ❌ `new` / `delete`
- ❌ `malloc` / `free`
- ❌ `std::vector::push_back()` (may allocate)
- ❌ `std::string` operations (may allocate)
- ❌ Blocking I/O in loops
- ❌ Exceptions

**Use instead:**
- ✅ `std::array<T, N>`
- ✅ `sertial::fixed_vector<T, N>`
- ✅ `std::atomic<T>`
- ✅ `constexpr` functions
- ✅ Template metaprogramming

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

## 🔗 Related Resources

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

## 📝 Contributing

When updating documentation:

1. **Active docs**: Update `work/ARCHITECTURE_ANALYSIS.md` for roadmap changes
2. **Historical record**: Add to `work/FIXES_APPLIED.md` for bug fixes
3. **Examples**: Keep examples up-to-date with latest API
4. **This README**: Update current state section when completing phases

When archiving docs:
```bash
mv obsolete_doc.md archive/
```

---

## 🏷️ Version History

- **Phase 1** (Complete): Core module template with lifecycle hooks
- **Phase 2** (Complete): ContinuousInput with automatic subscription
- **Phase 3** (Complete): Variadic command handling with type-safe dispatch
- **Phase 4** (Complete): 3-mailbox architecture with hierarchical addressing
- **Phase 5** (Planned): Multi-input/multi-output modules (Target: April 2026)
- **Phase 6** (Planned): Command reply mechanisms
- **Phase 7** (Planned): Advanced features

---

**For detailed architecture analysis and future roadmap, see [ARCHITECTURE_ANALYSIS.md](work/ARCHITECTURE_ANALYSIS.md)**
