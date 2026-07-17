# GitHub Copilot Instructions for CommRaT

## Project Overview

**CommRaT** (Communication Runtime) is a C++20 real-time messaging framework built on TiMS (TIMS Interprocess Message System). Provides type-safe, compile-time message passing with zero runtime overhead.

**Current Status**: Build modernized, warning-free, CI pipeline in place. CMakePresets and Dev Container configured. EVL platform backend headers exist; implementation pending.  
**Next**: EVL backend implementation (`evl/threading_impl.hpp`, `evl/timestamp_impl.hpp`), input buffering, command ergonomics

### Core Philosophy
- **Compile-time everything**: Message IDs, type safety computed at compile time
- **Zero-copy where possible**: Direct memory access, no allocations
- **Real-time safe**: No dynamic allocation in hot paths
- **Simple user API**: Users only see message types and Module interface - metaprogramming hidden

## Architecture

### 3-Mailbox System
Each module has three mailboxes with distinct roles:

```cpp
CMD  mailbox: base_address + 0   // Per-output: Command/request handling (blocking receive)
WORK mailbox: base_address + 16  // Per-module: Outbound messages only (no thread)
DATA mailbox: base_address + 32  // Per-input: Continuous data streams (blocking receive)
```

**CMD Mailbox (Per-Output)**:
- **Purpose**: Receive and handle requests/commands for specific output
- **Messages**: SubscribeRequest, UnsubscribeRequest, GetDataRequest, GetNextDataRequest, user commands
- **Threading**: One dedicated blocking receive thread per output (N threads total)
- **Replies**: Sent via same CMD mailbox (request/reply pattern)
- **Address**: Varies per output (different type_id in address calculation)

**WORK Mailbox (Per-Module)**:
- **Purpose**: Send-only mailbox for module-initiated communication
- **Messages**: Subscribe to producers, control messages, publish output data to subscribers
- **Threading**: NO dedicated thread - sends from main/data thread
- **Usage**: Module uses this to communicate with other modules without blocking
- **Address**: Single mailbox per module instance

**DATA Mailbox (Per-Input)**:
- **Purpose**: Receive continuous data streams from producers
- **Messages**: Published output data (TemperatureData, SensorData, etc.)
- **Threading**: Implicit in ContinuousInput (blocking receive on primary input)
- **Flow**: Producer publishes via WORK → Consumer receives via DATA
- **Address**: Varies per input type

**Threading Model (N outputs, M inputs)**:
- **1 data_thread**: Runs process() based on execution mode (timer, input-driven, loop)
- **N command_threads**: One per output, blocking receive on CMD mailbox (0% CPU when idle)
- **M input threads**: Implicit in input handlers (ContinuousInput blocks on DATA mailbox)
- **Total**: N + 1 explicit threads (+ M implicit in inputs)

**Message Flow Patterns**:
- **Subscription Protocol**:
  1. Consumer sends SubscribeRequest to Producer's CMD mailbox (via WORK)
  2. Producer's command_thread receives and processes request
  3. Producer replies SubscribeReply via CMD mailbox
  4. Producer adds consumer to subscriber list
  5. Producer publishes data via WORK → Consumer's DATA mailbox

- **Data Request (Get Synchronization)**:
  1. Consumer sends GetDataRequest to Producer's CMD mailbox
  2. Producer's command_thread searches history for matching timestamp
  3. Producer replies GetDataReply with matched data via CMD mailbox
  4. One-shot operation (no subscription created)

- **Module Control**:
  1. Module sends control messages via WORK mailbox
  2. Target module receives on CMD mailbox
  3. Target processes and optionally replies via CMD

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

### Platform Abstraction Layer (libevl / Xenomai 4)

CommRaT supports two platform backends selected at compile time via the `COMMRAT_PLATFORM` CMake cache variable:

```bash
cmake --preset default    # STD platform — builds and runs natively on any Linux host
cmake --preset evl        # EVL compile-check — requires libevl (in ratos-dev-image)
cmake --preset evl-cross  # EVL cross-compile for QEMU — requires RaTOS SDK sourced
```

- `COMMRAT_PLATFORM=STD` (default): Standard Linux (`std::thread`, `std::mutex`, `std::chrono`)
- `COMMRAT_PLATFORM=EVL`: Hard real-time via libevl/Xenomai 4 (out-of-band scheduling)

The CMake option propagates `COMMRAT_PLATFORM_STD` or `COMMRAT_PLATFORM_EVL` as compile definitions and auto-discovers `libevl` + `evl/thread.h` when EVL is selected.

> **EVL backend status**: Headers exist in `include/commrat/platform/evl/` but contain `#error` stubs. `-DCOMMRAT_PLATFORM=EVL` does not yet compile. Tracked via `build-evl-compile` CI job (`continue-on-error: true`).

### EVL Cross-Compilation with the RaTOS SDK

The recommended iterative development loop for EVL work is:

```
cmake --preset evl-cross → cmake --build → evl-dev.sh --cross --run <binary>
```

**RaTOS SDK** is a Yocto-generated cross-compilation toolchain targeting the same
Debian Trixie amd64 sysroot that runs inside QEMU. It includes:

| Package | Provides |
|---|---|
| `linux-headers-xenomai-4` | EVL kernel headers (`<evl/thread.h>` etc.) |
| `libevl` | `libevl.so` / `libevl.a` + headers |
| `rack-dev`, `libreflect-cpp-dev`, SeRTial | All CommRaT dependencies |

**SDK setup (one-time):**
```bash
# Download ratos-dev-sdk-container-amd64.sh from a CommRaT or RaTOS GitHub release
chmod +x ratos-dev-sdk-container-amd64.sh
./ratos-dev-sdk-container-amd64.sh          # installs to /opt/ratos-sdk by default
source /opt/ratos-sdk/environment-setup-*   # sets CC, CXX, OECORE_NATIVE_SYSROOT, etc.
# Or set EVL_SDK_DIR in .commrat.env.local to change the prefix
```

**Cross-compile CommRaT:**
```bash
# Source the SDK first (sets OECORE_NATIVE_SYSROOT used by the preset)
source /opt/ratos-sdk/environment-setup-*

# Configure and build (evl-cross preset uses OEToolchainConfig.cmake automatically)
cmake --preset evl-cross
cmake --build --preset evl-cross --parallel $(nproc)
# Output: build/evl-cross/
```

**The `evl-cross` CMake preset** (`CMakePresets.json`) sets:
```cmake
COMMRAT_PLATFORM = EVL
CMAKE_TOOLCHAIN_FILE = $OECORE_NATIVE_SYSROOT/usr/share/cmake/OEToolchainConfig.cmake
```

**Deploy and run in QEMU with evl-dev.sh:**
```bash
# Cross-compile + deploy + open shell:
scripts/evl-dev.sh --cross --shell

# Cross-compile + deploy + run tests:
scripts/evl-dev.sh --cross --test

# Cross-compile + deploy + run specific binary:
scripts/evl-dev.sh --cross evl-cross --run build/evl-cross/test/test_io_spec

# Already deployed — just run a binary interactively:
scripts/evl-dev.sh --run /root/commrat/examples/example_commands

# Build inside the QEMU guest instead (slower, but works without the SDK):
scripts/evl-dev.sh --build --test
```

**CI cross-compilation** (when EVL backends are implemented):
The `test-evl-runtime` CI job currently builds inside the QEMU guest (STD platform, no SDK required). Once the EVL backends are done, change it to cross-compile inside the `ratos-dev-image` container (which has the SDK pre-installed) and deploy only binaries to the guest.

**EVL Execution Model:**
- EVL threads are regular POSIX threads that attach to the EVL core via `evl_attach_self()`
- Once attached, threads can run **out-of-band (OOB)** with ultra-low latency guarantees
- **Any glibc/kernel syscall from OOB context automatically demotes to in-band** (loses RT)
- `malloc`, `std::cout`, `throw`, `new/delete`, `pthread_mutex_lock` are ALL forbidden in OOB context
- Only `libevl` functions + `memcpy`/`strcpy`/`std::atomic` are safe in OOB context

**CommRaT Abstraction to EVL Mapping:**

| CommRaT | std:: backend | EVL backend |
|---|---|---|
| `Thread` | `std::thread` | `pthread_create` + `evl_attach_self()` |
| `Mutex` | `std::mutex` | `evl_mutex` (PI by default) |
| `SharedMutex` | `std::shared_mutex` | `evl_rwlock` (no PI, writer-biased) |
| `ConditionVariable` | `std::condition_variable` | `evl_event` (paired with `evl_mutex`) |
| `Time::now()` | `steady_clock::now()` | `evl_read_clock(EVL_CLOCK_MONOTONIC)` |
| `Time::sleep()` | `this_thread::sleep_for` | `evl_usleep()` / `evl_sleep_until()` |
| `ThreadPriority` | `pthread_setschedparam` | `evl_set_schedattr()` with `SCHED_FIFO` |

**When implementing platform code:**
- Detailed EVL API reference: `docs/work/EVL_API_REFERENCE.md`
- Platform design doc: `docs/work/PLATFORM_ABSTRACTION_LAYER.md`
- Header structure: `include/commrat/platform/{std,evl}/` backends selected via `platform.hpp`
- EVL health monitoring: Enable `EVL_T_WOSS` to detect accidental in-band switches in RT loops
- Periodic loops: Use `evl_sleep_until()` with absolute time for jitter-free timing

### Message Definitions

Messages use **Message::** namespace for clean syntax:

```cpp
// Message structure (plain POD, sertial-serializable)
struct TemperatureData {
    uint32_t sensor_id;
    float temperature_c;
    float confidence;
};

// Registry entry (compile-time ID calculation)
using TempMsg = Message::Data<TemperatureData>;
```

### Message ID System

**Auto-Increment ID Assignment**:
- `AUTO_ID = 0` - Marker for auto-assigned IDs (0 = -0, perfect for request/reply)
- IDs auto-assigned starting from 1 within each prefix/subprefix combination
- `MAX_MESSAGE_ID = 0x7FFF` - Maximum ID for non-reply messages (sign bit reserved)

**Request/Reply Protocol**:
```cpp
// Request message with reply
using MyRequest = MessageDefinition<
    RequestPayload,
    MessagePrefix::System,
    SystemSubPrefix::Subscription,
    0x0010,                // Request ID
    ReplyPayload           // Reply type (automatically gets ID = -0x0010 = 0xFFF0)
>;

// Reply automatically created
using MyReply = typename MyRequest::ReplyMessageDef;

// Validation
static_assert(MyRequest::has_reply);
static_assert(MyRequest::is_request);
static_assert(!MyReply::is_request);
static_assert(MyReply::local_id == static_cast<uint16_t>(-0x0010));  // 0xFFF0
```

**Allowed Reply Categories**:
- `System::Control` - Control commands with replies
- `System::Subscription` - Subscription protocol (Subscribe, Unsubscribe, GetData)
- `UserDefined::Commands` - User command messages with replies

**System Messages** (auto-included in registry):
```cpp
SubscribeRequest/Reply         // Continuous data subscription
UnsubscribeRequest/Reply       // Stop subscription
GetDataRequest/Reply           // Timestamp-synchronized data fetch (CMD mailbox)
GetNextDataRequest/Reply       // One-shot data fetch (CMD mailbox)
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

// 2. CREATE MODULES (Zero-Copy Workspace API)
// Timer-driven producer
class SensorModule : public MyApp::Module2<Output<TemperatureData>, Period<Milliseconds(10)>> {
protected:
    void process(TemperatureData& output) override {
        // Write directly to output workspace (zero-copy)
        output.sensor_id = id_;
        output.temperature_c = read_sensor();
    }
};

// Input-driven consumer
class FilterModule : public MyApp::Module2<Output<FilteredData>, Input<TemperatureData>> {
protected:
    void process(const TemperatureData& input, FilteredData& output) override {
        // Input is const ref (zero-copy), output is mutable ref (workspace)
        output.value = apply_filter(input);
    }
};

// Multi-output
class MultiSensor : public MyApp::Module2<Output<DataA>, Output<DataB>, Period<Milliseconds(10)>> {
protected:
    void process(DataA& out1, DataB& out2) override {
        out1 = read_sensor_a();
        out2 = read_sensor_b();
    }
};

// Multi-input (first is primary, rest use get_data synchronization)
class SensorFusion : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,              // Primary (drives execution)
    SyncedInput<GPSData>         // Secondary (synchronized)
> {
protected:
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) override {
        if (gps) output = fuse_sensors(imu, gps.value());
        else output = dead_reckoning(imu);
    }
};
```

**Key Points:**
- Use `MyApp::Module2<IOSpecs...>` with I/O tuple architecture
- Output specs: `Output<T>` (one or more)
- Input specs: `Input<T>` (primary, continuous), `SyncedInput<T>` (secondary, pull-based)
- Execution modes: `Period<Duration>` (timer) or auto-inferred (input-driven or loop)
- Process signature: `void process(const Input&..., Output&...) override` (zero-copy)
- Multi-output: Each output is separate `Output<T>` parameter
- Multi-input: First `Input<T>` is primary, `SyncedInput<T>` types use `Synced<T>`

**I/O Metadata Access**:
```cpp
class MySensor : public MyApp::Module2<Output<Data>, Period<...>> {
    // All metadata in clean IO::Meta struct
    using IO = typename IOBuilder::Meta;
    
    // Access metadata
    static_assert(IO::num_outputs == 1);
    static_assert(IO::is_timer_driven);
    
    // Type extraction
    using DataType = typename IO::SingleOutputType;
    
    // Primary input (multi-input sync)
    if constexpr (IO::has_primary_input) {
        auto primary_ts = get_input<IO::primary_input_index>();
    }
};
```

### Multi-Input Synchronization

```cpp
class SensorFusion : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,          // Primary (continuous)
    SyncedInput<GPSData>     // Secondary (pull-based)
> {
protected:
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) override {
        // Synced<T> forces explicit fresh/stale/invalid handling
        
        // Strict check - fresh data only
        if (gps) {
            output = fuse_sensors(imu, gps.value());
        } else {
            output = dead_reckoning(imu);  // GPS not fresh
        }
        
        // Alternative: Use stale data if available
        if (gps.is_valid()) {
            output = fuse_sensors(imu, gps.stale());  // Fresh or stale
        }
        
        // Or use value_or/stale_or helpers
        GPSData data = gps.value_or(default_gps);  // Fresh or default
        GPSData data = gps.stale_or(default_gps);  // Valid or default
        
        // Access metadata
        bool gps_fresh = gps.is_fresh();
        bool gps_stale = gps.has_stale();
        bool gps_valid = gps.is_valid();
    }
};
```

**How it works:**
1. Primary `Input<T>` blocks on receive() - drives execution
2. Secondary `SyncedInput<T>` uses get_data(primary_timestamp) automatically
3. Process receives `Synced<T>` for synced inputs (zero-copy wrapper)
4. `Synced<T>` API forces explicit fresh/stale/invalid handling:
   - `operator bool()` - strict (fresh only)
   - `value()` - fresh data (asserts if stale/invalid)
   - `stale()` - any valid data (asserts if invalid)
   - `value_or()` / `stale_or()` - safe defaults

### Synced<T> Wrapper (Zero-Copy Data Access)

**Purpose**: Provide zero-copy access to synchronized secondary input data with explicit freshness/staleness/validity handling.

**Design**:
```cpp
template<typename T>
class Synced {
    const T* data_;        // Zero-copy pointer (not owned)
    bool is_valid_;        // True if data pointer is valid
    bool is_fresh_;        // True if data is fresh (exact timestamp match)
    
public:
    // User API (const access)
    explicit operator bool() const { return is_fresh_; }  // STRICT: fresh only!
    bool is_fresh() const;      // True if exact timestamp match
    bool has_stale() const;     // True if valid but not fresh
    bool is_valid() const;      // True if any data available
    
    const T& value() const;     // Fresh data (asserts if not fresh)
    const T& stale() const;     // Any valid data (asserts if invalid)
    const T& operator*() const; // Alias for stale() (most permissive)
    
    const T& value_or(const T& default_value) const;  // Fresh or default
    const T& stale_or(const T& default_value) const;  // Valid or default
    
    // Internal API (for SyncedInput only - not for users)
    Synced& operator=(const T&);  // Set fresh data
    void mark_stale();            // Mark data as stale (keep pointer)
    void reset();                 // Invalidate (clear pointer)
};
```

**Usage Patterns**:
```cpp
void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) {
    // Pattern 1: Strict freshness check
    if (gps) {
        output = fuse_sensors(imu, gps.value());
    } else {
        output = dead_reckoning(imu);
    }
    
    // Pattern 2: Accept stale data
    if (gps.is_valid()) {
        output = fuse_sensors(imu, gps.stale());
    }
    
    // Pattern 3: Use helpers for defaults
    output = fuse_sensors(imu, gps.value_or(default_gps));  // Fresh or default
    output = fuse_sensors(imu, gps.stale_or(default_gps));  // Valid or default
}
```

**Benefits**:
- Zero-copy (const T* internally, no copying)
- Forces explicit handling of fresh/stale/invalid states
- Type-safe (no raw pointers exposed to users)
- Ergonomic (const reference parameter, not optional<reference_wrapper>)

### Data Request Messages (CMD Mailbox)

**GetDataRequest** - Timestamp-synchronized data retrieval:
```cpp
struct GetDataRequestPayload {
    uint64_t target_timestamp;      // Target timestamp in nanoseconds
    uint64_t tolerance_ns;          // Max time difference allowed
    uint8_t interpolation_mode;     // NEAREST, LINEAR, etc.
};

template<typename T>
struct GetDataReplyPayload {
    TimsMessage<T> message;         // The matched message
    bool found;                     // True if match found within tolerance
    bool is_fresh;                  // True if exact timestamp match
    int64_t timestamp_delta_ns;     // Actual time difference
};
```

**GetNextDataRequest** - One-shot data fetch:
```cpp
struct GetNextDataRequestPayload {
    // Empty - just request next available
};

template<typename T>
struct GetNextDataReplyPayload {
    TimsMessage<T> message;         // Next available message
    bool found;                     // True if data available
};
```

**Usage**: SyncedInput uses these internally for multi-input synchronization. Replies go to CMD mailbox (not WORK) since they may be large.

### Timestamp Management

**Single Source of Truth**: `TimsHeader.timestamp` ONLY - no payload timestamp fields!

**Automatic Timestamp Assignment:**
- `Period<>` (timer-driven): `timestamp = Time::now()` at generation
- `Input<T>` (input-driven): `timestamp = input.header.timestamp` (propagation)
- `Multi-input`: `timestamp = primary_input.header.timestamp` (sync point)

**Accessing Metadata:**

```cpp
class FilterModule : public MyApp::Module2<Output<FilteredData>, Input<SensorData>> {
protected:
    void process(const SensorData& input, FilteredData& output) override {
        // Index-based metadata access
        uint64_t ts = get_input_timestamp<0>();
        bool fresh = has_new_data<0>();
        bool valid = is_input_valid<0>();
        
        output = apply_filter(input);
    }
};
```

### Command Dispatch Pattern (CMD Mailbox)

**Per-Output CMD Mailboxes**: Each output has its own CMD mailbox (different type_id in address). Commands received via blocking receive in dedicated command thread.

**Clean Visitor Pattern** using registry data directly (NO duplication):

```cpp
// Command thread for specific output (one thread per output)
template<size_t OutputIndex>
void command_loop_impl() {
    auto& output = get_output<OutputIndex>();
    
    // Get command types from registry (single source of truth!)
    using SystemCommands = typename Registry::subscription_messages_t<>;  // From registry
    using UserCommands = ExtractUserCommands_t<OutputMessageDef>;        // From DataWithCommands
    
    while (!should_stop_) {
        cmd_mailbox.receive_any([&](auto&& received_msg) {
            using CmdType = typename std::decay_t<decltype(received_msg)>::payload_type;
            
            // System command check (simple tuple membership)
            if constexpr (is_in_tuple_v<CmdType, SystemCommands>) {
                handle_system_command(output, received_msg);
                // Send reply via CMD mailbox
            } 
            // User command check (from DataWithCommands)
            else if constexpr (is_in_tuple_v<CmdType, UserCommands>) {
                typename CmdType::Reply reply;
                on_command<OutputIndex>(received_msg.payload, reply);
                // Send reply via CMD mailbox
            } 
            // Unknown command
            else {
                // Send UnknownCommandReply via CMD mailbox
            }
        });
    }
}
```

**User Command Implementation**:
```cpp
class MySensor : public MyApp::Module2<Output<SensorData>> {
protected:
    void process(SensorData& output) override {
        output = read_sensor();
    }
    
    // Optional: Implement command handlers for output 0
    template<size_t OutputIndex>
    void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
        apply_calibration(cmd.offset);
        reply.success = true;
    }
};
```

**Key Points**:
- System commands from `Registry::subscription_messages_t<>` (single source of truth)
- User commands from `DataWithCommands<Payload, Cmd1, ...>` (no duplication)
- Simple `is_in_tuple_v<Cmd, Tuple>` check for dispatch
- CMD mailbox address varies per output (different type_id)
- All receives blocking (0% CPU when idle)

### Structured I/O Access

**Named access** to inputs and outputs (alternative to index-based):

```cpp
class MultiInputModule : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,
    SyncedInput<GPSData>
> {
protected:
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) override {
        // Named struct access (IDE autocomplete friendly)
        auto ins = inputs();          // Returns rfl::NamedTuple
        // ins.imu_data, ins.gps_data  (field names from payload types)
        
        auto outs = outputs();
        // outs.fused_data
        
        // Both named and index access work:
        // rfl::get<0>(ins) == ins.imu_data
        
        output = fuse_sensors(imu, gps.value_or(default_gps));
    }
};
```

**Benefits**: Auto-complete, self-documenting, zero-copy (references only)

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
├── .commrat.env                           # Non-secret config defaults (RATOS_RELEASE_TAG pin, QEMU settings, EVL_SDK_DIR)
├── CMakePresets.json                      # Build presets: default, debug, evl, evl-cross
├── CMakeLists.txt                         # Root build (COMMRAT_PLATFORM option, find_package, container targets)
├── .devcontainer/
│   └── devcontainer.json                  # VS Code Dev Container (ratos-dev-image, cmake-tools)
├── include/commrat/
│   ├── module2.hpp                        # Module base class (I/O tuple architecture)
│   ├── module/io/
│   │   ├── io_spec.hpp                    # I/O specification (Output, Input, Period, SyncedInput)
│   │   ├── synced.hpp                     # Synced<T> wrapper (zero-copy with validity/freshness)
│   │   ├── output_infrastructure.hpp      # Structured output access helpers
│   │   ├── input_infrastructure.hpp       # Structured input access helpers
│   │   ├── output/module_output.hpp       # Output with workspace API
│   │   └── input/
│   │       ├── continuous_input.hpp       # Primary input (blocking receive)
│   │       └── synced_input.hpp          # Secondary input (get_data)
│   ├── mailbox/typed_mailbox.hpp          # Type-restricted mailboxes
│   ├── messaging/
│   │   ├── message_registry.hpp           # Compile-time message registry
│   │   └── system/
│   │       ├── subscription_messages.hpp  # Subscribe/Unsubscribe protocol
│   │       └── data_request_messages.hpp  # GetData/GetNextData protocol
│   └── platform/
│       ├── platform.hpp                   # Backend selection macros
│       ├── threading.hpp                  # Thread, Mutex abstractions
│       ├── timestamp.hpp                  # Time, Duration, Timestamp
│       ├── std/                           # std:: backend (fully operational)
│       └── evl/                           # EVL backend (headers present, #error stubs)
├── examples/
│   ├── CMakeLists.txt                     # Example targets
│   └── *.cpp                              # Working examples
├── test/
│   ├── CMakeLists.txt                     # Test targets + add_test()
│   └── *.cpp                              # CTest suite
├── scripts/
│   └── evl-dev.sh                         # All EVL dev: --cross, --build, --test, --run, --shell; artifact cache in .evl-cache/
├── .github/workflows/
│   ├── doxygen.yml                        # Doxygen → GitHub Pages
│   └── ci.yml                             # 3-job CI: std build, EVL compile-check, EVL QEMU runtime
├── docs/
│   ├── ARCHITECTURE.md                    # System architecture
│   ├── API_REFERENCE.md                   # API documentation
│   ├── GETTING_STARTED.md                 # First-time setup
│   ├── USER_GUIDE.md                      # Comprehensive guide
│   ├── KNOWN_ISSUES.md                    # Active issues
│   ├── ROADMAP.md                         # Feature roadmap and status
│   ├── internal/                          # Design decisions, phase history
│   ├── work/                              # Active design docs
│   └── archive/                           # Archived docs
└── src/
    └── tims_wrapper.cpp                   # TiMS IPC wrapper
```

**External dependencies** (installed system-wide, not submodules):
- **RACK** — TiMS IPC library (`find_package(RACK REQUIRED)` → `RACK::rack`) — will be replaced by CoreRaT
- **SeRTial** — zero-allocation serialization (`find_package(SeRTial REQUIRED)` → `SeRTial::sertial`)
- **reflectcpp** — reflection, found transitively via SeRTial
- **libevl** — Xenomai 4 RT primitives (only needed when `COMMRAT_PLATFORM=EVL`) — will move to CoreRaT
- **CoreRaT** *(planned)* — shared foundation library: platform abstraction, IPC (TiMS + EVL-native), messaging system. Will replace `platform/`, `messaging/`, `mailbox/`, and `tims_wrapper.cpp` in CommRaT. See `docs/work/CORERAT_DESIGN.md`.

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
- `docs/KNOWN_ISSUES.md` - Active issues and workarounds
- `docs/ARCHITECTURE.md` - System architecture overview
- `docs/API_REFERENCE.md` - API documentation
- `docs/work/ZERO_COPY_ARCHITECTURE.md` - Zero-copy workspace design
- `docs/archive/` - Archived design documents (historical reference)

**When to update documentation:**
- Major architectural changes → Update `docs/ARCHITECTURE.md`
- Bug fixes with non-obvious causes → Add to `docs/internal/phase_history/FIXES_APPLIED.md`
- Phase completion → Update `docs/README.md` current state section
- Design decisions with trade-offs → Add to `docs/internal/design_decisions/`
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
uint32_t cmd_mbx  = base + 0;   // Per-output: Command/request receive (with thread)
uint32_t work_mbx = base + 16;  // Per-module: Outbound send-only (no thread)
uint32_t data_mbx = base + 32;  // Per-input: Data stream receive (implicit thread)
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
void process(const T& input) {
    sertial::fixed_vector<T, 100> buffer;  // Stack-allocated
    std::array<float, 50> results;          // Fixed size
    // Process without allocations...
}

// ERROR: Dynamic allocation in hot path
void process(const T& input) {
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
- Zero-copy workspace API (OutputBuffer, ModuleOutput)
- Structured I/O access (outputs()/inputs() with named fields)
- I/O tuple architecture (Module2 with BuildIOTuple)
- Multi-output support (separate Output<T> for each)
- Multi-input foundation (Input<T> + SyncedInput<T>)
- Synced<T> wrapper (zero-copy with fresh/stale/invalid handling)
- Message ID system (AUTO_ID=0, MAX_MESSAGE_ID=0x7FFF validation)
- Request/reply protocol (automatic reply message generation)
- System messages (Subscribe, Unsubscribe, GetData, GetNextData)
- **GetData message auto-registration** (UserDefined::GetData/GetNextData subprefixes, same local_id as data message)
- **Consumer-side get_data** (SyncedInput with get_data() and get_next_data() RPCs)
- **Producer-side get_data handling** (ModuleOutput CMD mailbox handlers for GetDataRequest/GetNextDataRequest)
- **Mailbox ownership and lifecycle** (ContinuousInput owns TypedMailbox<Registry, T>, two-phase init/start)
- **Mailbox addressing correctness** (WORK uses primary output type_id, DATA addresses per-input)
- **Input-driven skip logic** (data_loop skips process() when primary input times out, prevents zero timestamps)
- **Multi-output module support** (initialize_input handles MultiOutputConfig, instance_id indexed access)
- **Subscription protocol integration** (Subscribe/Unsubscribe/GetData dispatch wired in command_loop_impl, end-to-end proven)
- **User command reply mechanism** (on_command reply sent via CMD mailbox, matching system command pattern)
- **Dead code removal** (legacy subscription.hpp removed, RT violations eliminated)
- **Modern CMake build system** (find_package for RACK/SeRTial/Threads, examples/test subdirectories, COMMRAT_BUILD_EXAMPLES/TESTS options, warning-free)
- **COMMRAT_PLATFORM CMake option** (`STD`/`EVL`, auto-discovers libevl, propagates compile definitions, CMake-validated)
- **CI pipeline** (3-job GitHub Actions: `build-std` in container, `build-evl-compile` with continue-on-error, `test-evl-runtime` via `dawidd6/action-download-artifact` + QEMU; public image, only `RATOS_RELEASE_TOKEN` secret needed)
- **CMakePresets** (`default`/`debug`/`evl` presets; used by CI, local builds, VS Code cmake-tools, CLion)
- **Dev Container** (`.devcontainer/devcontainer.json` — zero-setup IDE using ratos-dev-image with all deps)
- **`.commrat.env` config** (single source of truth for non-secret values; `.commrat.env.local` for machine overrides)
- **Process launcher system**: `commrat_module()` CMake macro wraps `add_executable` and emits `<ClassName>.module.json` descriptors via `file(GENERATE)`. `ProcessLauncher` discovers descriptors from `dirname(argv[0])`, fork/execs each module binary with a temp `ModuleConfig` JSON, SIGTERM on stop with SIGKILL fallback. `MyApp::Launcher` (in-process variant) forward-declared in `commrat.hpp`; requires `#include <commrat/launcher/launcher.hpp>`. Both read `AppDescription` JSON with `modules[].module_class`, `outputs`, `inputs`, `period_ms`. `--duration-ms N` for timed exit (CTest). See `include/commrat/launcher/` and `docs/work/LAUNCHER_DESIGN.md`.

### In Progress
- EVL runtime tests passing (3 failing: test_3input_fusion, test_address_collisions, test_timestamp_logic — root cause is tims_recvmsg_timed() demoting EVL threads in-band; fixed by CoreRaT EVL IPC backend)
- Input buffering strategies
- Command ergonomics improvements

### Planned
- **CoreRaT migration** (phased; see `docs/work/CORERAT_DESIGN.md`):
  - Phase 1: extract platform + messaging to CoreRaT; delete `include/commrat/platform/`, `include/commrat/messaging/`, `include/commrat/messages.hpp`
  - Phase 2: move IPC (TiMS backend) to CoreRaT; delete `tims_wrapper.cpp`, `mailbox/mailbox.hpp`, `mailbox/typed_mailbox.hpp`
  - Phase 3: EVL IPC backend in CoreRaT (OOB-safe ring buffer); removes in-band demotion on EVL guest
  - After Phase 3: CommRaT CMakeLists replaces ~60 lines of platform/RACK/EVL detection with `find_package(CoreRaT REQUIRED)`
- System lifecycle commands (on/off/reset)
- Parameter system (typed params with get/set/list/save)
- ROS 2 adapter (separate repository)
- Performance profiling tools

### CoreRaT Agent Boundary
CoreRaT is a **separate project with its own agent**. Do not re-implement anything that has moved to CoreRaT. When CommRaT's cleanup phases are executed:
- Delete the files listed in `docs/work/CORERAT_DESIGN.md` Section 10
- Replace removed CMake logic with `find_package(CoreRaT REQUIRED)` + `target_link_libraries(commrat PUBLIC CoreRaT::corerat)`
- Update includes from `commrat/platform/` → `corerat/platform/` etc.
- Do not modify CoreRaT source — file issues or PRs against the CoreRaT repo instead

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
