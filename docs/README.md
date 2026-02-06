# CommRaT Documentation

**Last Updated**: February 6, 2026  
**Current Version**: Phase 4 Complete

---

## 📚 Documentation Structure

### Active Documentation

- **[ARCHITECTURE_ANALYSIS.md](work/ARCHITECTURE_ANALYSIS.md)** - Current architecture assessment and future roadmap (Phase 5: Multi-I/O)
- **[FIXES_APPLIED.md](work/FIXES_APPLIED.md)** - Historical record of bug fixes and solutions

### Visual Assets

- `CommRaT.png` - Project logo (raster)
- `CommRaT.svg` - Project logo (vector)

### Archived Documentation

See `archive/` directory for historical design documents and validation logs from earlier phases.

---

## 🎯 Current State (Phase 4 Complete)

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

✅ **Module Framework**
```cpp
template<typename UserRegistry,
         typename OutputDataT,
         typename InputModeT,
         typename... CommandTypes>
class Module;
```

**Input Modes:**
- `PeriodicInput` - Timer-based execution
- `LoopInput` - Maximum throughput (100% CPU)
- `ContinuousInput<T>` - Processes input stream

**Features:**
- Automatic subscription management
- Type-safe command dispatch
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

---

## 🚀 Next Evolution: Phase 5 (Multi-Input/Multi-Output)

### Vision

Support modules with **multiple typed inputs and outputs**, all resolved at compile time:

```cpp
// Sensor fusion: 3 inputs → 2 outputs
class FusionModule : public Module<
    Registry,
    Outputs<FusedPose, Diagnostics>,       // 2 outputs
    Inputs<IMUData, GPSData, LidarData>    // 3 inputs
> {
protected:
    void process(const IMUData& imu, const GPSData& gps, const LidarData& lidar,
                 FusedPose& pose, Diagnostics& diag) override {
        // All inputs/outputs as parameters - clean and simple!
        pose = ekf_.update(imu, gps, lidar);
        diag.latency = calculate_latency();
    }
};
```

### Key Benefits

- ✅ **Zero runtime cost** - all dispatch at compile time
- ✅ **Type safe** - invalid configurations rejected by compiler
- ✅ **Backward compatible** - existing single-I/O modules still work
- ✅ **Intuitive** - users see simple function signatures
- ✅ **Scalable** - complex fusion topologies supported

### Implementation Plan

**Phase 5** (6 weeks): Multi-I/O foundation
- Input/Output bundle types (`Inputs<Ts...>`, `Outputs<Ts...>`)
- Parameter expansion via index sequences
- Multiple mailbox management
- Updated subscription protocol

**Phase 6** (3 weeks): Command reply mechanisms
- Three patterns: fire-and-forget, parameter reply, return value
- Automatic reply routing
- Timeout handling

**Phase 7** (4 weeks): Advanced features
- Conditional outputs
- Dynamic input selection
- Priority-based processing
- Zero-copy shared memory

**Target**: Phase 5 complete by **April 30, 2026**

---

## 📖 Core Concepts

### Message Registry

Single source of truth for all communication types:

```cpp
using MyRegistry = MessageRegistry<
    // System messages (subscription protocol)
    MessageDefinition<SubscribeRequestPayload, ...>,
    MessageDefinition<SubscribeReplyPayload, ...>,
    // User messages
    MessageDefinition<SensorData, MessagePrefix::UserDefined, UserSubPrefix::Data, 0>,
    MessageDefinition<CommandData, MessagePrefix::UserDefined, UserSubPrefix::Commands, 1>
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
