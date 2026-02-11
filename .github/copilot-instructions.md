# GitHub Copilot Instructions for CommRaT

## Project Overview

**CommRaT** (Communication Runtime) is a C++20 real-time messaging framework built on TiMS (TIMS Interprocess Message System). Provides type-safe, compile-time message passing with zero runtime overhead.

**Current Status**: Phase 7.1 COMPLETE (new module architecture proof of concept)  
**Next**: Phase 7.2 (implement remaining 7 specialized bases)

### Core Philosophy
- **Compile-time everything**: Message IDs, type safety computed at compile time
- **Zero-copy where possible**: Direct memory access, no allocations
- **Real-time safe**: No dynamic allocation in hot paths
- **Simple user API**: Users only see message types and Module interface - metaprogramming hidden

## Architecture

### 3-Mailbox System
Each module has three mailboxes:

```cpp
CMD  mailbox: base_address + 0   // User commands
WORK mailbox: base_address + 16  // Subscription protocol
DATA mailbox: base_address + 32  // Input data streams
```

**Threading**: command_loop() + work_loop() + data_thread_, all use blocking receives (0% CPU when idle)

**Message Flow**: Consumer subscribes → Producer acknowledges → Producer publishes to subscriber's DATA mailbox

## Code Style & Conventions

### Real-Time Constraints

**FORBIDDEN in hot paths:**
```cpp
new/delete, malloc/free
std::vector::push_back()  // May allocate
std::string operations    // May allocate
std::cout in loops        // Blocking I/O
throw exceptions          // Unpredictable timing
```

**ALWAYS use:**
```cpp
std::array<T, N>              // Fixed-size
sertial::fixed_vector<T, N>   // Fixed capacity
sertial::RingBuffer<T, N>     // Circular buffer
std::atomic<T>                // Lock-free
constexpr / static_assert     // Compile-time
```

### Threading and Timestamp Abstractions

**ALWAYS use CommRaT abstractions** instead of std:: types:

```cpp
// NEVER:
#include <thread>
#include <mutex>
#include <chrono>
std::thread, std::mutex, std::chrono::steady_clock::now()

// ALWAYS:
#include <commrat/threading.hpp>
#include <commrat/timestamp.hpp>
Thread, Mutex, SharedMutex, Lock, SharedLock
Timestamp ts = Time::now();
Duration timeout = Milliseconds(100);
Time::sleep(Milliseconds(10));
```

### Message Definitions

Messages use **Message::** namespace for clean syntax:

```cpp
// Message structure (plain POD, sertial-serializable)
struct TemperatureData {
    uint64_t timestamp;
    uint32_t sensor_id;
    float temperature_c;
    float confidence;
};

// Registry entry (compile-time ID calculation)
using TempMsg = Message::Data<TemperatureData>;
```

### Application Definition & Module Pattern

**CommRaT<>** defines your application - combines message registry with Module/Mailbox:

```cpp
// 1. DEFINE YOUR APPLICATION
using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<FilteredData>,
    Message::Command<ResetCmd>
>;

// 2. CREATE MODULES
// Two module implementations available:
// - Module (legacy monolithic - registry_module.hpp, 575 lines, 13+ bases)
// - ModuleV2 (new specialized - module.hpp, selective inheritance per mode)

// Producer (periodic publishing) - NEW ARCHITECTURE
class SensorModule : public MyApp::ModuleV2<Output<TemperatureData>, PeriodicInput> {
public:
    explicit SensorModule(const ModuleConfig& config) : ModuleV2(config) {}
protected:
    void process(TemperatureData& output) override {
        // Called every config_.period
        output = TemperatureData{...};
    }
};

// Consumer (continuous input processing) - LEGACY (Phase 7.2 will add ModuleV2 version)
class FilterModule : public MyApp::Module<Output<FilteredData>, Input<TemperatureData>> {
protected:
    FilteredData process(const TemperatureData& input) override {
        // Called for each received message
        return apply_filter(input);
    }
};

// Multi-output (void process with output references)
class MultiOutputModule : public MyApp::Module<Outputs<DataA, DataB>, PeriodicInput> {
protected:
    void process(DataA& out1, DataB& out2) override {
        out1 = DataA{...};
        out2 = DataB{...};
    }
};

// Multi-input (first type is automatically primary)
class SensorFusion : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>  // IMU (first) drives execution
> {
protected:
    FusedData process(const IMUData& imu, const GPSData& gps) override {
        // GPS synchronized to IMU timestamp via getData
        return fuse_sensors(imu, gps);
    }
};
```

**Key Points:**
- Use `MyApp::Module<OutputSpec, InputSpec, ...Commands>` (legacy) or `MyApp::ModuleV2<...>` (new)
- **ModuleV2 requires explicit constructor**: `explicit MyModule(const ModuleConfig& config) : ModuleV2(config) {}`
- Output specs: `Output<T>`, `Outputs<T, U>`
- Input specs: `Input<T>`, `Inputs<T, U, V>`, `PeriodicInput`, `LoopInput`
- Multi-input: First type in `Inputs<>` is automatically primary
- Virtual `process()` **must use `override` keyword**
- Multi-output: `void process(T1& out1, T2& out2)`

### New Architecture (Phase 7.1+)

**Phase 7.1 Complete**: PeriodicSingleOutputBase validated
- Two-layer design: `ModuleV2` → `SelectModuleBase` → specialized base
- Only inherits mixins needed for specific I/O mode
- 85% reduction in user-facing template errors (proposed)
- Both Module and ModuleV2 coexist during transition

**Phase 7.2 Planned**: Implement remaining 7 specialized bases
1. LoopSingleOutputBase
2. ContinuousSingleOutputBase
3. PeriodicMultiOutputBase
4. LoopMultiOutputBase
5. ContinuousMultiOutputBase
6. MultiInputSingleOutputBase
7. MultiInputMultiOutputBase

### Multi-Input Synchronization

**Multi-input modules** synchronize different-rate sensors:

```cpp
class SensorFusion : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData>  // IMU first = primary
> {
protected:
    FusedData process(const IMUData& imu, const GPSData& gps) override {
        // Access metadata for freshness/validity
        auto gps_meta = get_input_metadata<1>();  // Index-based
        bool fresh = has_new_data<1>();
        bool valid = is_input_valid<1>();
        
        // GPS automatically synchronized to IMU timestamp via getData
        return fuse_sensors(imu, gps);
    }
};

// Configuration
ModuleConfig fusion_config{
    .name = "SensorFusion",
    .system_id = 20,
    .instance_id = 1,
    .period = Milliseconds(10),  // Primary input rate (100Hz)
    .input_sources = {
        {10, 1},  // IMU (primary)
        {11, 1}   // GPS (secondary)
    },
    .sync_tolerance = 50'000'000  // 50ms tolerance for getData
};
```

**How it works:**
1. Primary input blocks on receive() - drives execution
2. Secondary inputs use getData(primary_timestamp, tolerance)
3. HistoricalMailbox buffers recent messages
4. Metadata tracks freshness and validity

### Timestamp Management

**Single Source of Truth**: `TimsHeader.timestamp` ONLY - no payload timestamp fields!

**Automatic Timestamp Assignment:**
- `PeriodicInput`: `timestamp = Time::now()` at generation
- `Input<T>`: `timestamp = input.header.timestamp` (propagation)
- `Multi-input`: `timestamp = primary_input.header.timestamp` (sync point)

**Accessing Metadata:**

```cpp
class FilterModule : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {
protected:
    FilteredData process(const SensorData& input) override {
        // Index-based access (always works)
        auto meta = get_input_metadata<0>();
        uint64_t ts = get_input_timestamp<0>();
        bool fresh = has_new_data<0>();
        bool valid = is_input_valid<0>();
        
        return apply_filter(input);
    }
};
```

**Metadata Structure:**
```cpp
struct InputMetadata<T> {
    uint64_t timestamp;       // From TimsHeader (ns since epoch)
    uint32_t sequence_number;
    uint32_t message_id;
    bool is_new_data;         // True if fresh
    bool is_valid;            // True if getData succeeded
};
```

### Multi-Output Type Filtering

Multi-output producers generate multiple message types. Subscribers filter by type:

```cpp
// Multi-output configuration
ModuleConfig pressure_receiver_config{
    .name = "PressureReceiver",
    .system_id = 30,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = MyApp::get_message_id<TemperatureData>()  // Producer's primary type
};
```

Result: Each subscriber receives ONLY their subscribed message type!

## Directory Structure

```
CommRaT/
├── include/commrat/          # Public headers
│   ├── commrat.hpp          # Main include (CommRaT<> application template)
│   ├── registry_module.hpp  # Module base class (legacy - 575 lines, 13+ bases)
│   ├── module.hpp           # ModuleV2 wrapper (new architecture)
│   ├── module/base/         # Specialized base implementations (Phase 7.1+)
│   │   ├── periodic_single_base.hpp  # PeriodicInput + single output
│   │   └── module_base_selector.hpp  # Compile-time dispatcher
│   ├── io_spec.hpp          # I/O specification types (Phase 5.1)
│   ├── mailbox.hpp          # Payload-only mailbox wrapper
│   ├── message_registry.hpp # Compile-time registry (internal, use CommRaT)
│   ├── messages.hpp         # Core message types
│   └── tims_wrapper.hpp     # TiMS C API wrapper
├── src/                     # Implementation files
│   └── tims_wrapper.cpp     # TiMS initialization
├── examples/                # Usage examples
│   ├── continuous_input_example.cpp  # Producer→Consumer pattern
│   ├── clean_interface_example.cpp   # Simple API showcase
│   ├── command_example.cpp           # Variadic command handling
│   └── loop_mode_example.cpp         # LoopInput maximum throughput demo
├── test/                    # Test executables
│   └── test_new_module_arch.cpp      # Phase 7.1 proof of concept
├── docs/                    # Documentation
│   ├── README.md           # Documentation overview and quick reference
│   ├── work/               # Active development documentation
│   │   ├── ARCHITECTURE_ANALYSIS.md  # Phase 5 roadmap (multi-I/O)
│   │   └── FIXES_APPLIED.md          # Historical bug fixes
│   └── archive/            # Archived historical documentation
├── tims/                   # TiMS library (external dependency)
└── SeRTial/                # Serialization library (submodule)
```

## Documentation Strategy

### Emoji Usage Policy

**NEVER use emojis anywhere in the codebase:**
- **Forbidden in**: All .md files (README.md, docs/*.md, examples/*.md), all source code (.cpp, .hpp)
- **Only allowed in**: .github/copilot-instructions.md (internal guidance only)

**Rationale:**
- Professional appearance for production library
- Accessibility (screen readers, plain text compatibility)
- Consistency with technical documentation standards
- Avoid visual clutter in code examples and console output
- No emojis in std::cout/std::cerr output

**Instead of emojis, use clear text:**
```markdown
// DON'T: // ✅ This works
// DO:    // VALID: This works

// DON'T: // ⚠️ Be careful here
// DO:    // WARNING: Be careful here

// DON'T: // ❌ Not supported
// DO:    // ERROR: Not supported

// DON'T: std::cout << "✅ Done!\n";
// DO:    std::cout << "Done!\n";

// DON'T: std::cout << "🚗 Running...\n";
// DO:    std::cout << "Running...\n";
```

### Documentation Structure

**Active Documentation** (`docs/`):
- `README.md` - Comprehensive overview, current state (Phase 6.10), and roadmap
- `USER_GUIDE.md` - Comprehensive framework documentation (Sections 1-8 complete)
- `KNOWN_ISSUES.md` - Active issues, limitations, and workarounds
- `work/ARCHITECTURE_ANALYSIS.md` - Detailed Phase 5 design (multi-I/O modules)
- `work/IO_SYNC_STRATEGY.md` - Multi-input synchronization strategy (Phase 6)
- `work/RACK_ANALYSIS.md` - RACK-style getData mechanism design
- `work/SERTIAL_RINGBUFFER_REQUEST.md` - RingBuffer requirements for buffered inputs
- `work/FIXES_APPLIED.md` - Historical record of bug fixes and solutions
- `archive/` - Archived historical documentation from earlier phases

**When to update documentation:**
- Major architectural changes → Update `ARCHITECTURE_ANALYSIS.md`
- Bug fixes with non-obvious causes → Add to `FIXES_APPLIED.md`
- Phase completion → Update `docs/README.md` current state section
- Design decisions with trade-offs → Add to `ARCHITECTURE_ANALYSIS.md`
- Template metaprogramming insights → Document in relevant analysis file
- **New issues discovered → Add to `KNOWN_ISSUES.md` with status, symptoms, workarounds**
- **Issues resolved → Update status in `KNOWN_ISSUES.md` and document solution**

**Documentation Style**:
- `docs/README.md` - Structured reference with examples, accessible to new users
- `docs/work/` - Developer-focused, explain the "why" not just "what"
- Keep examples up-to-date with latest API changes
- Archive obsolete docs to `docs/archive/` - don't delete history

### Code Documentation
Use Doxygen-style comments for public APIs:

```cpp
/**
 * @brief Send a message to a destination mailbox
 * 
 * This is a compile-time type-safe send operation. The message type
 * must be registered in the mailbox's registry.
 * 
 * @tparam T Message payload type (must be registered)
 * @param message Message to send
 * @param dest_mailbox Destination mailbox ID
 * @return Success or error
 * 
 * @note Real-time safe if T's serialization is real-time safe
 */
template<typename T>
    requires is_registered<T>
auto send(T& message, uint32_t dest_mailbox) -> MailboxResult<void>;
```

## Common Patterns

### Address Calculation
```cpp
uint32_t base = calculate_base_address(system_id, instance_id);
uint32_t cmd_mbx  = base + 0;   // Command mailbox
uint32_t work_mbx = base + 16;  // Subscription protocol
uint32_t data_mbx = base + 32;  // Data streaming
```

### Visitor Pattern for Type Dispatch
```cpp
mailbox.receive_any([](auto&& received_msg) {
    using MsgType = std::decay_t<decltype(received_msg.message)>;
    
    if constexpr (std::is_same_v<MsgType, TemperatureData>) {
        handle_temperature(received_msg.message);
    } else if constexpr (std::is_same_v<MsgType, CommandData>) {
        handle_command(received_msg.message);
    }
});
```

## SeRTial Integration

**SeRTial** is our serialization library (similar to flatbuffers but simpler):

```cpp
// Message wrapper (header + payload)
template<typename T>
struct TimsMessage {
    TimsHeader header;  // Timestamp, seq_number, msg_type, etc.
    T payload;          // User data
    
    using payload_type = T;  // Type introspection
};

// Serialization (compile-time size calculation)
auto result = sertial::Message<TimsMessage<T>>::serialize(msg);
// result.view() returns std::span<std::byte>

// Deserialization (zero-copy when possible)
auto result = sertial::Message<TimsMessage<T>>::deserialize(buffer);
// result.value() returns TimsMessage<T>

// RingBuffer (Phase 6) - zero allocation circular buffer
sertial::RingBuffer<TimsMessage<T>, 100> history;
history.push_back(msg);  // Overwrites oldest when full
// Serialization: Only current size() elements, not full capacity
```

## Design Principles

### Compile-Time Guarantee
If it can be validated at compile time, it MUST be validated at compile time:

```cpp
// VALID: Compile-time check
template<typename T>
auto send_message(const T& msg) {
    static_assert(is_registered<T>, "Type must be registered in message registry");
    static_assert(sizeof(T) < MAX_MESSAGE_SIZE,
                  "Message size exceeds maximum - use bounded containers");
    // ...
}

// ERROR: Runtime check for compile-time property
if (!is_registered(msg)) {
    throw std::runtime_error("Type not registered");
}
```

### Zero-Allocation Mandate
NO heap allocations in hot paths (periodic_loop, continuous_loop, process functions):

```cpp
// VALID: Stack-based, bounded containers
template<typename T>
void process_data(const T& input) {
    sertial::fixed_vector<T, 100> buffer;  // Stack-allocated
    std::array<float, 50> results;          // Fixed size
    // Process without allocations...
}

// ERROR: Dynamic allocation in hot path
void process_data(const T& input) {
    std::vector<T> buffer;        // Heap allocation!
    buffer.push_back(input);      // May reallocate!
}
```

## When Suggesting Code

**DO:**
- Use templates with concepts for type constraints
- Prefer `constexpr` over runtime checks
- Use `std::array` and `sertial::fixed_vector` for collections
- Use `std::atomic` for thread-safe counters
- Document real-time safety in comments
- Suggest compile-time validation via `static_assert`
- Hide complexity behind simple APIs - users should only interact with their message types and basic module interfaces
- Use elaborate template metaprogramming internally (SFINAE, concepts, variadic templates) to compute everything at compile time
- Make the simple case trivial - sensible defaults, minimal required configuration

**DON'T:**
- Suggest `std::vector` or `std::string` in hot paths
- Use exceptions in real-time code
- Suggest dynamic polymorphism (virtual functions) without justification
  - Exception: Helper base classes with specialization for conditional APIs (see ContinuousProcessorBase)
- Ignore const-correctness
- Use `auto` when type clarity is important
- Expose template complexity to end users - keep metaprogramming machinery internal
- Require users to understand reflection, type dispatch, or ID calculation mechanisms
- Add configuration parameters that users must understand - compute automatically when possible

## Future Considerations

### Completed Features
- Phase 5.3: Multi-output with type-specific filtering
- Phase 6.9: Multi-input with synchronized getData (RACK-style)
- Phase 6.10: Timestamp metadata accessors

### Planned Features (Phase 7+)
- Optional secondary inputs (getData failure handling)
- Input buffering strategies (sliding window, latest-only)
- RingBuffer integration for message history
- ROS 2 adapter (rclcpp-commrat) - separate repository
- DDS compatibility layer
- Performance profiling tools
- Static analysis for real-time safety

### Technical Debt
- Command dispatch (on_command overloads) needs better ergonomics
- Multi-input getData synchronization has timing issues (see KNOWN_ISSUES.md #1)
- Type-based metadata accessors incomplete (index-based works, type-based needs full tuple unpacking)

## Questions to Ask Yourself

Before suggesting code:
1. Is this allocation-free? (No new/malloc/vector in hot paths)
2. Can this be computed at compile time? (Use constexpr/consteval)
3. Does this respect the 3-mailbox architecture?
4. Is the type constraint clear and enforced? (Concepts/static_assert)
5. Is error handling deterministic? (std::optional, no exceptions in hot paths)
6. Would this cause unnecessary copies? (Use std::span for views)
7. Is the complexity hidden from users? (Simple API, complex internals)
8. Does this maintain real-time guarantees? (Bounded execution time)
9. Are timestamps managed via TimsHeader only? (No payload timestamp fields)
10. Can users access metadata if needed? (get_input_metadata API)
11. **Is this a known issue? (Check docs/KNOWN_ISSUES.md before implementing workarounds)**

## Summary

CommRaT is a **compile-time**, **real-time**, **type-safe** messaging framework. When writing code:
- Think templates, not runtime dispatch
- Think fixed-size, not dynamic allocation
- Think blocking receives, not polling
- Think separate mailboxes, not multiplexing
- Think compile-time validation, not runtime errors

**Mantra**: If it can be checked at compile time, it should be checked at compile time.
