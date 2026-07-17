# CommRaT Architecture

Internal architecture reference for the CommRaT (Communication Runtime) framework.
C++20 real-time messaging built on TiMS with compile-time type safety.

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [System Overview](#system-overview)
3. [Mailbox System](#mailbox-system)
4. [Module Architecture](#module-architecture)
5. [Threading Model](#threading-model)
6. [Subscription Protocol](#subscription-protocol)
7. [Timestamp Management](#timestamp-management)
8. [Message ID System](#message-id-system)
9. [Platform Abstraction Layer](#platform-abstraction-layer)
10. [Memory and Error Handling](#memory-and-error-handling)
11. [Design Decisions](#design-decisions)

---

## Design Philosophy

Three core principles:

**Compile-time everything** -- If it can be validated at compile time, it must be.
Message IDs, type safety, and registry lookups are all resolved at compile time.
No reflection, no RTTI, no runtime type dispatch.

**Zero runtime overhead** -- No dynamic allocation in `process()` hot paths.
Use `std::array`, `sertial::fixed_vector`, `sertial::RingBuffer`. No exceptions.
All blocking receives are kernel-level (0% CPU when idle).

**Simple user API** -- Users see message POD structs, `Module2` base class, and `process()`.
All metaprogramming (SFINAE, concepts, variadic templates, address calculation) is hidden.

---

## System Overview

```
+----------------------------------------------------------+
|                    CommRaT<Types...>                      |
|        (Message registry + Module2/Mailbox types)        |
+----------------------------------------------------------+
                            |
                  +---------+----------+
                  v                    v
       +-----------------+  +-----------------+
       | Module Instance  |  | Module Instance  |
       |   (Producer)     |  |   (Consumer)     |
       +-----------------+  +-----------------+
              |                      |
    +---------+------+               |
    v         v      v               v
 +-----+  +-----+  +-----+      +-----+
 | CMD |  |WORK |  | CMD |      |DATA |
 | mbx |  | mbx |  | mbx |      | mbx |
 |(out0)|  |(send)| |(out1)|      |(in0)|
 +-----+  +-----+  +-----+      +-----+
    |         |        |             |
    +---------+--------+-------------+
                  v
          +--------------+
          | TiMS (RACK)  |
          | IPC Backend  |
          +--------------+
```

### Layers

1. **User code** -- Message POD structs, Module2 subclass, `process()` override
2. **CommRaT framework** -- Module2 base, I/O specs, subscription automation
3. **Type system** -- MessageRegistry, compile-time ID mapping, SFINAE processor selection
4. **TiMS wrapper** -- RAII mailbox wrappers, SeRTial serialization
5. **TiMS (RACK)** -- Socket IPC, message routing, blocking receive primitives

---

## Mailbox System

### Three Mailbox Types

Each module uses three kinds of mailboxes (no PUBLISH mailbox):

| Mailbox | Scope | Purpose | Thread |
|---------|-------|---------|--------|
| **CMD** | Per-output | Receive commands, subscription requests | Dedicated blocking thread |
| **WORK** | Per-module | Send-only: subscribe to producers, publish to subscribers | No thread (sends from data thread) |
| **DATA** | Per-input | Receive continuous data streams | Implicit in ContinuousInput |

### Address Encoding

Format: `[type_id:8][system_id:8][instance_id:8][mailbox_index:8]`

```cpp
// From include/commrat/module/helpers/address_helpers.hpp
constexpr uint32_t encode_address(uint8_t type_id, uint8_t system_id,
                                   uint8_t instance_id, uint8_t mailbox_index);

// Mailbox index constants
constexpr uint8_t CMD_MBX_BASE  = 0;   // CMD at index 0 (per output)
constexpr uint8_t WORK_MBX_BASE = 1;   // WORK at index 1
constexpr uint8_t DATA_MBX_BASE = 3;   // DATA starts at index 3 (per input)
```

**type_id** is derived from the module's primary output message ID (low 8 bits).
For multi-output modules, the first output type determines `type_id`.

**Example** (single-output module, system_id=10, instance_id=1, type_id=0x42):

```
CMD:  0x420A0100   (type=0x42, sys=0x0A, inst=0x01, idx=0x00)
WORK: 0x420A0101   (type=0x42, sys=0x0A, inst=0x01, idx=0x01)
DATA: 0x420A0103   (type=0x42, sys=0x0A, inst=0x01, idx=0x03)
```

For multi-output modules, each output gets its own CMD mailbox at index 0..N-1.
DATA mailboxes start at `get_data_mbx_base(num_outputs)`.

---

## Module Architecture

### Module2 Base Class

Defined in `include/commrat/module2.hpp`.

```cpp
template<typename Registry, typename... IOSpecs>
class Module2;
```

Users access it through the application alias:

```cpp
using MyApp = CommRaT<Message::Data<TempData>, Message::Data<FilteredData>>;

// Timer-driven producer
class Sensor : public MyApp::Module2<Output<TempData>, Period<Milliseconds(100)>> {
    void process(TempData& output) override { /* ... */ }
};

// Input-driven consumer
class Filter : public MyApp::Module2<Output<FilteredData>, Input<TempData>> {
    void process(const TempData& input, FilteredData& output) override { /* ... */ }
};

// Multi-input fusion
class Fusion : public MyApp::Module2<
    Output<FusedData>, Input<IMUData>, SyncedInput<GPSData>
> {
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& out) override {
        if (gps) {
            out = fuse(imu, gps.value());
        } else {
            out = dead_reckon(imu);
        }
    }
};

// Loop mode (no Input, no Period)
class Spinner : public MyApp::Module2<Output<StatusData>> {
    void process(StatusData& output) override { /* ... */ }
};
```

I/O specs defined in `include/commrat/module/io/io_spec.hpp`:

- `Output<T>` -- one or more output types
- `Input<T>` -- primary continuous input (at most one, drives execution)
- `SyncedInput<T>` -- secondary pull-based input (timestamp-synchronized)
- `Period<Duration>` -- timer-driven execution (mutually exclusive with `Input<T>`)

### Execution Modes (Auto-Inferred)

| Has Input? | Has Period? | Mode |
|---|---|---|
| Yes | No | **Input-driven**: blocks on DATA mailbox receive |
| No | Yes | **Timer-driven**: sleeps for period between calls |
| No | No | **Loop-driven**: calls process() continuously |

### Process Signature Selection

The I/O spec tuple determines the `process()` signature at compile time:

```
Output<A>, Period<D>                -> void process(A& out)
Output<A>, Input<B>                 -> void process(const B& in, A& out)
Output<A>, Output<B>, Period<D>     -> void process(A& out1, B& out2)
Output<A>, Input<B>, SyncedInput<C> -> void process(const B& in, Synced<C> synced, A& out)
```

Inputs come first (const ref for primary, `Synced<T>` for synced), outputs last (mutable ref).

### Synced<T> Wrapper

Defined in `include/commrat/module/io/synced.hpp`.
Zero-copy wrapper for synchronized secondary input data with explicit freshness handling:

```cpp
template<typename T>
class Synced {
    const T* data_;
    bool is_valid_, is_fresh_;
public:
    explicit operator bool() const;      // True if fresh
    const T& value() const;              // Fresh only (asserts)
    const T& stale() const;              // Any valid (asserts)
    const T& value_or(const T&) const;   // Fresh or default
    const T& stale_or(const T&) const;   // Valid or default
    bool is_fresh() const;
    bool is_valid() const;
};
```

### Internal Structure

```
Module2<Registry, IOSpecs...>
  |-- IOHandler          (structured I/O access: outputs(), inputs(), publish, fetch)
  |-- CommandHandler     (system + user command dispatch per output)
  |-- SelectProcessorBase (SFINAE picks correct process() signature)
  |
  |-- ModuleOutput (per output, owns CMD mailbox + subscriber list + history buffer)
  |-- ContinuousInput (per Input<T>, owns DATA mailbox)
  |-- SyncedInput (per SyncedInput<T>, uses get_data RPC via WORK)
  |-- WorkMailbox (single, send-only)
  |
  |-- data_thread_         (1 thread: runs process())
  |-- command_threads_[]   (N threads: one per output CMD mailbox)
```

Key source files:
- `module/io/output/module_output.hpp` -- Output with CMD mailbox, subscribers, history
- `module/io/input/continuous_input.hpp` -- Primary input with DATA mailbox
- `module/io/input/synced_input.hpp` -- Secondary input via get_data
- `module/io/input/cmd_input.hpp` -- CMD mailbox for request/reply RPCs

---

## Threading Model

Each module runs **1 + N** threads (N = number of outputs):

**1 data_thread** -- Runs `process()` based on execution mode:
- Input-driven: blocks on `ContinuousInput::receive()`, calls process, publishes
- Timer-driven: sleeps for period via `Time::sleep()`, calls process, publishes
- Loop-driven: calls process, publishes, repeats immediately

**N command_threads** -- One per output. Each blocks on its CMD mailbox:
- Receives `SubscribeRequest`, `UnsubscribeRequest`, `GetDataRequest`, `GetNextDataRequest`
- Dispatches system commands automatically
- Dispatches user commands via `on_command<OutputIndex>(payload, reply)`
- 0% CPU when no commands arrive

No work_thread -- WORK mailbox is send-only (used from data_thread or startup).

All threading primitives (`Thread`, `Mutex`, `SharedMutex`, `ConditionVariable`) are
platform-abstracted. The active backend is selected at compile time via CMake.
See [Platform Abstraction Layer](#platform-abstraction-layer) for details.

---

## Subscription Protocol

### Flow

```
Consumer                                Producer
   |                                       |
   |  1. SubscribeRequest                 |
   |  (via WORK -> Producer's CMD mbx)    |
   |-------------------------------------->|
   |                                       | command_thread receives
   |                                       | validates, adds subscriber
   |  2. SubscribeReply                   |
   |  (Producer -> Consumer's WORK mbx)   |
   |<--------------------------------------|
   |                                       |
   |  3. Data (continuous)                |
   |  (Producer WORK -> Consumer DATA)    |
   |<======================================|
   |<======================================|
```

### Message Types

Defined in `include/commrat/messaging/system/subscription_messages.hpp`:

```cpp
struct SubscribeRequestPayload {
    uint32_t subscriber_addr{0};       // Consumer's DATA mailbox address
    int64_t requested_period_ms{0};    // Desired update period (0 = max rate)
};

struct SubscribeReplyPayload {
    int64_t actual_period_ms{0};
    bool success{false};
    uint32_t error_code{0};            // 0=ok, 1=max_subscribers, 2=other
};

struct UnsubscribeRequestPayload {
    uint32_t subscriber_addr{0};
};

struct UnsubscribeReplyPayload {
    bool success{true};
};
```

These use `MessageDefinition` with request/reply pairing:

```cpp
using SubscribeRequest = MessageDefinition<
    SubscribeRequestPayload, MessagePrefix::System,
    SystemSubPrefix::Subscription, 0x0001, SubscribeReplyPayload>;
using SubscribeReply = typename SubscribeRequest::ReplyMessageDef;
```

### Data Requests (Get Synchronization)

Defined in `include/commrat/messaging/system/data_request_messages.hpp`.
Used internally by `SyncedInput` for timestamp-synchronized pulls:

```cpp
template<typename T>
struct GetDataRequestPayload {
    uint64_t target_timestamp{0};
    uint64_t tolerance_ns{0};
    uint8_t interpolation_mode{0};     // NEAREST, LINEAR, PREVIOUS, NEXT
};

template<typename T>
struct GetDataReplyPayload {
    T data;
    bool found{false};
    uint64_t data_timestamp{0};
    int64_t timestamp_delta_ns{0};
};
```

Consumer sends `GetDataRequest` to producer's CMD mailbox. Producer searches history
buffer and replies with closest match. One-shot operation (no subscription created).

---

## Timestamp Management

**Single source of truth: `TimsHeader.timestamp`**. Payload structs must NOT have timestamp fields.

Automatic assignment based on execution mode:
- **Timer-driven**: `timestamp = Time::now()` at each period
- **Input-driven**: `timestamp = input.header.timestamp` (propagated)
- **Multi-input**: `timestamp = primary_input.header.timestamp` (sync point)

Abstractions from `include/commrat/platform/`:

```cpp
#include <commrat/platform/timestamp.hpp>
#include <commrat/platform/duration.hpp>

Timestamp ts = Time::now();           // Platform-selected clock
Duration timeout = Milliseconds(100); // Milliseconds() returns Duration
Time::sleep(Milliseconds(10));        // Platform-selected sleep
```

`Duration` is a custom structural type (constexpr, NTTP-compatible) that replaces
raw `std::chrono` types. `Milliseconds()`, `Seconds()`, etc. are free functions
returning `Duration`, not type aliases. See [Platform Abstraction Layer](#platform-abstraction-layer).

---

## Message ID System

Message IDs are computed at compile time via prefix/subprefix/local_id:

```cpp
using TempMsg = MessageDefinition<
    TemperatureData,
    MessagePrefix::UserDefined,
    UserDefinedSubPrefix::Data,
    AUTO_ID>;    // Auto-assigned starting from 1
```

Key constants:
- `AUTO_ID = 0` -- marker for auto-assigned IDs
- `MAX_MESSAGE_ID = 0x7FFF` -- sign bit reserved for reply IDs
- Reply ID = `static_cast<uint16_t>(-request_id)` (e.g., request 0x0001 -> reply 0xFFFF)

System messages are auto-included in every registry (Subscribe, Unsubscribe, GetData, GetNextData).

Defined in `include/commrat/messaging/message_id.hpp`
and `include/commrat/messaging/message_registry.hpp`.

---

## Platform Abstraction Layer

CommRaT uses a compile-time platform abstraction to support multiple backends.
All threading, timing, and synchronization primitives are wrapped behind a unified
C++ API. Select the backend via the `COMMRAT_PLATFORM` CMake cache variable:

```bash
cmake --preset default   # default -- standard Linux
cmake --preset evl       # hard real-time -- requires libevl / Xenomai 4 kernel
```

| `COMMRAT_PLATFORM` | Compile definition | Backend | Use Case |
|---|---|---|---|
| `STD` (default) | `COMMRAT_PLATFORM_STD` | `std::thread`, `std::mutex`, `std::chrono` | Standard Linux, development, testing |
| `EVL` | `COMMRAT_PLATFORM_EVL` | libevl / Xenomai 4 | Hard real-time, out-of-band scheduling |

The EVL backend requires `libevl` and `evl/thread.h` to be present on the build host.
CMake will locate them automatically via `find_library` / `find_path` and report an
error if they are missing.

> **Status**: EVL backend headers exist but contain `#error` stubs. `-DCOMMRAT_PLATFORM=EVL`
> will not yet link successfully. Tracked in the CI `build-evl-compile` job (`continue-on-error`).

### Abstraction Mapping

| CommRaT Type | std:: Backend | EVL Backend |
|---|---|---|
| `Thread` | `std::thread` | `pthread_create` + `evl_attach_self()` |
| `Mutex` | `std::mutex` | `evl_mutex` (priority inheritance) |
| `SharedMutex` | `std::shared_mutex` | `evl_rwlock` |
| `ConditionVariable` | `std::condition_variable` | `evl_event` |
| `Time::now()` | `steady_clock::now()` | `evl_read_clock(EVL_CLOCK_MONOTONIC)` |
| `Time::sleep()` | `this_thread::sleep_for` | `evl_usleep()` / `evl_sleep_until()` |

### Duration Type

`Duration` is a custom structural type storing nanoseconds as `int64_t`. It replaces
raw `std::chrono` duration types throughout the framework. Key properties:

- **Constexpr**: All operations are compile-time evaluable
- **NTTP-compatible**: Can be used as a non-type template parameter (e.g., `Period<Milliseconds(100)>`)
- **Arithmetic**: Supports `+`, `-`, `*`, `/`, comparisons
- **Conversion**: `to_chrono()` converts to `std::chrono::nanoseconds` for interop

Free function constructors: `Nanoseconds()`, `Microseconds()`, `Milliseconds()`,
`Seconds()`, `Minutes()`, `Hours()`. These are functions returning `Duration`,
not type aliases.

### File Structure

```
include/commrat/platform/
  duration.hpp              # Duration type (platform-independent)
  platform.hpp              # Backend detection macros
  threading.hpp             # Thread, Mutex, Lock type aliases
  timestamp.hpp             # Time, Timestamp, clock selection
  std/
    threading_impl.hpp      # std:: backend implementations
    timestamp_impl.hpp      # std::chrono backend
  evl/
    threading_impl.hpp      # libevl backend (skeleton)
    timestamp_impl.hpp      # EVL clock backend (skeleton)
```

Config structs (e.g., `ModuleConfig`) retain `std::chrono::milliseconds` fields
for rfl serialization compatibility. Accessor methods convert to `Duration` at
the API boundary.

---

## Memory and Error Handling

**Hot paths**: Stack-only. `std::array<T,N>`, `sertial::fixed_vector<T,N>`, `sertial::RingBuffer<T,N>`.
No `new`, `malloc`, `std::vector::push_back`, `std::string`, or exceptions.

**Cold paths** (startup/config): Heap allocation permitted.

**Error handling**: `std::optional` or error codes. Never exceptions in real-time paths.
Compile-time validation via `static_assert` and concepts preferred over runtime checks.

---

## Deployment

### commrat_module() CMake macro

Each module binary is declared with `commrat_module()` instead of `add_executable()`. At build time it generates a cmake-managed `<ModuleClass>.module.json` descriptor file alongside the binary:

```cmake
# CMakeLists.txt
commrat_module(my_sensor
    SOURCES  my_sensor.cpp
    MODULE_CLASS  MySensorModule)
```

Generated file (`MySensorModule.module.json`):
```json
{ "module_class": "MySensorModule", "binary": "/path/to/build/my_sensor" }
```

### ProcessLauncher

`ProcessLauncher` reads an `AppDescription` JSON, discovers `*.module.json` descriptors from `dirname(argv[0])`, writes per-module `ModuleConfig` JSON to `/tmp`, and fork/execs each binary as a separate process:

```cpp
// my_launcher.cpp
#include <commrat/launcher/process_launcher.hpp>
int main(int argc, char** argv) {
    return commrat::ProcessLauncher::main(argc, argv);
}
```

`AppDescription` format (`app.json`):
```json
{
  "app_name": "MySensorSystem",
  "modules": [
    { "name": "Sensor_1", "module_class": "MySensorModule",
      "outputs": [{"type_name": "SensorData", "system_id": 10, "instance_id": 1}],
      "inputs": [], "period_ms": 100 },
    { "name": "Filter_1", "module_class": "FilterModule",
      "outputs": [{"type_name": "FilteredData", "system_id": 20, "instance_id": 1}],
      "inputs": [{"type_name": "SensorData", "source_system_id": 10,
                   "source_instance_id": 1, "synced": false}] }
  ]
}
```

Producers are spawned first with a 200 ms stagger so their TiMS mailboxes are ready before consumers subscribe. On `stop()` / SIGINT: SIGTERM to all children, 3 s grace period, SIGKILL fallback.

CTest integration via `--duration-ms N`: the launcher exits 0 after N milliseconds.

---

## Design Decisions

### Why CMD Per Output (not shared)?

Each output independently manages its own subscribers and command handlers.
Independent CMD mailboxes avoid runtime type dispatch and allow per-output rate control.

### Why No PUBLISH Mailbox?

Publishing goes through the WORK mailbox (send-only). A separate PUBLISH mailbox
added addressing complexity with no benefit -- the WORK mailbox already handles
outbound sends without a dedicated thread.

### Why Blocking Receives?

Polling wastes CPU. Busy-wait is incompatible with real-time. Kernel-level blocking
(futex-based via TiMS) gives 0% CPU when idle with immediate wakeup on message arrival.

### Why TimsHeader.timestamp Only?

Single source of truth eliminates ambiguity. The framework assigns timestamps
automatically based on execution mode. Users access metadata via `get_input_timestamp<N>()`.

---

## References

- [API Reference](API_REFERENCE.md)
- [User Guide](USER_GUIDE.md)
- [Getting Started](GETTING_STARTED.md)
- [RACK Project](https://github.com/smolorz/RACK) -- TiMS messaging system
- [SeRTial](https://github.com/mattih11/SeRTial) -- Serialization library
