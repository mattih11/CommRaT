# CommRaT User Guide

CommRaT (Communication Runtime) is a C++20 real-time messaging framework providing type-safe, compile-time validated message passing. Built on TiMS, it enables deterministic distributed applications with zero runtime overhead.

## Table of Contents

1. [Introduction](#1-introduction)
2. [Core Concepts](#2-core-concepts)
3. [Your First Module](#3-your-first-module)
4. [Module Types and Processing Modes](#4-module-types-and-processing-modes)
5. [I/O Specifications](#5-io-specifications)
6. [Message Flow and Subscription](#6-message-flow-and-subscription)
7. [Multi-Input Synchronization](#7-multi-input-synchronization)
8. [Timestamp Management](#8-timestamp-management)
9. [Command Handling](#9-command-handling)
10. [Configuration and Deployment](#10-configuration-and-deployment)
11. [Schema Introspection and Viewer](#11-schema-introspection-and-viewer)
12. [Best Practices](#12-best-practices)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. Introduction

### What is CommRaT?

CommRaT is a messaging framework for real-time embedded systems where deterministic behavior, low latency, and type safety are critical.

**Key properties:**

- **Compile-time validation** -- Message types, IDs, and registries checked at compile time
- **Zero runtime overhead** -- No dynamic allocation, no runtime type checking
- **Deterministic execution** -- Bounded execution time, blocking receives with 0% CPU idle
- **Type safety** -- C++20 concepts enforce correct API usage
- **Zero-copy where possible** -- Direct memory access, output workspace pattern

### When to Use CommRaT

**Ideal for:** Real-time control systems, sensor fusion, embedded systems with hard timing constraints, deterministic message delivery.

**Not ideal for:** Web services (use gRPC), data analytics (use Kafka), systems without RT requirements (use ROS 2).

### Architecture Overview

```
Application: using MyApp = CommRaT<Message::Data<A>, Message::Data<B>, ...>;

   Producer Module          Consumer Module         Filter Module
   [Module2<...>]           [Module2<...>]          [Module2<...>]
        |                        ^                       ^
        | Publish                | Subscribe             |
        v________________________|_______________________|
                    TiMS (Message Passing Layer)
          3 mailboxes per module (CMD / WORK / DATA)
```

---

## 2. Core Concepts

### 2.1 Messages

Messages are plain C++ structs (POD types) with no virtual functions, pointers, or dynamic allocation:

```cpp
struct TemperatureData {
    uint32_t sensor_id;
    float temperature_c;
    float confidence;
};
```

CommRaT wraps payloads in `TimsMessage<T>` automatically:

```cpp
template<typename T>
struct TimsMessage {
    TimsHeader header;  // timestamp, sequence_number, msg_type
    T payload;          // Your data
};
```

**Rules:** No `std::string`, no `std::vector` in payloads. Use `std::array` or `sertial::fixed_vector<T, N>` for bounded collections.

### 2.2 Application Definition

`CommRaT<...>` defines your application and validates all message types at compile time:

```cpp
using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<FilteredData>,
    Message::Command<ResetCmd>
>;
```

This validates uniqueness, assigns message IDs, and generates serialization code -- all at compile time.

### 2.3 Modules

A Module is a processing unit with its own thread(s) and mailboxes. Use `Module2<IOSpecs...>`:

```cpp
class Sensor : public MyApp::Module2<Output<TemperatureData>, Period<100>> {
protected:
    void process(TemperatureData& output) override {
        output.sensor_id = 1;
        output.temperature_c = read_sensor();
        output.confidence = 0.95f;
    }
};
```

**Module lifecycle:** Construction -> `start()` (spawns threads) -> Running -> `stop()` (joins threads) -> Destruction.

### 2.4 Three-Mailbox Architecture

Each module has three mailboxes with distinct roles:

```
Base Address = [type_id:16][sys:8][inst:8]

CMD  mailbox: base + 0    Per-output: Commands, subscribe/unsubscribe (blocking receive thread)
WORK mailbox: base + 16   Per-module: Send-only (publish to subscribers, subscribe to producers)
DATA mailbox: base + 32   Per-input: Data stream receive (blocking receive)
```

**Why three mailboxes:**
- Separation of concerns -- commands don't interfere with data streams
- Each mailbox blocks independently (0% CPU when idle)
- Real-time predictability -- data unaffected by control traffic

### 2.5 Threading Model

For a module with N outputs and M inputs:
- **1 data_thread** -- runs `process()` based on execution mode (timer, input-driven, or loop)
- **N command_threads** -- one per output, blocking receive on CMD mailbox
- **No WORK thread** -- WORK mailbox is send-only

Total: N + 1 threads per module.

All threading and timing primitives (`Thread`, `Mutex`, `Time::sleep()`, etc.) are
platform-abstracted. The backend is selected at compile time via CMake:
`COMMRAT_PLATFORM_STD` (default) or `COMMRAT_PLATFORM_EVL` (libevl/Xenomai 4).
See [Architecture: Platform Abstraction](ARCHITECTURE.md#platform-abstraction-layer).

### 2.6 Compile-Time Guarantees

```cpp
// COMPILE ERROR: Type not in registry
MyApp::serialize(UnregisteredType{});

// COMPILE ERROR: Duplicate message types
using BadApp = CommRaT<Message::Data<Foo>, Message::Data<Foo>>;
// static_assert failure: Duplicate message type detected
```

---

## 3. Your First Module

A minimal working system: a temperature sensor publishing data, and a filter receiving it. See GETTING_STARTED.md for full project setup.

### 3.1 Define Messages and Application

```cpp
#include <commrat/commrat.hpp>

struct TemperatureData {
    uint32_t sensor_id;
    float temperature_c;
    float humidity_percent;
};

using TempApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureData>
>;
```

### 3.2 Create a Producer (Timer-Driven)

```cpp
class TemperatureSensor : public TempApp::Module2<
    commrat::Output<TemperatureData>,
    commrat::Period<100>  // 100ms period = 10Hz
> {
public:
    explicit TemperatureSensor(const commrat::ModuleConfig& config)
        : TempApp::Module2<commrat::Output<TemperatureData>, commrat::Period<100>>(config) {}

protected:
    void process(TemperatureData& output) override {
        output.sensor_id = 1;
        output.temperature_c = 20.0f + (rand() % 100) / 10.0f;
        output.humidity_percent = 50.0f;
    }
};
```

### 3.3 Create a Consumer (Input-Driven)

```cpp
class TemperatureFilter : public TempApp::Module2<
    commrat::Output<TemperatureData>,
    commrat::Input<TemperatureData>
> {
public:
    explicit TemperatureFilter(const commrat::ModuleConfig& config)
        : TempApp::Module2<commrat::Output<TemperatureData>,
          commrat::Input<TemperatureData>>(config) {}

protected:
    void process(const TemperatureData& input, TemperatureData& output) override {
        filtered_ = 0.7f * filtered_ + 0.3f * input.temperature_c;
        output.sensor_id = input.sensor_id;
        output.temperature_c = filtered_;
        output.humidity_percent = input.humidity_percent;
    }

private:
    float filtered_ = 20.0f;
};
```

### 3.4 Wire It Together

```cpp
int main() {
    // Producer config
    commrat::ModuleConfig sensor_config{
        .name = "TempSensor",
        .outputs = commrat::SimpleOutputConfig{.system_id = 10, .instance_id = 1},
        .inputs = commrat::NoInputConfig{},
        .period = std::chrono::milliseconds(100)
    };

    // Consumer config (subscribes to producer via source IDs)
    commrat::ModuleConfig filter_config{
        .name = "TempFilter",
        .outputs = commrat::SimpleOutputConfig{.system_id = 20, .instance_id = 1},
        .inputs = commrat::SingleInputConfig{
            .source_system_id = 10,
            .source_instance_id = 1
        }
    };

    TemperatureSensor sensor(sensor_config);
    TemperatureFilter filter(filter_config);

    sensor.start();
    commrat::Time::sleep(commrat::Milliseconds(100));
    filter.start();

    // Run for 5 seconds
    commrat::Time::sleep(commrat::Seconds(5));

    // Stop in reverse order
    filter.stop();
    sensor.stop();
    return 0;
}
```

**What happens:** `sensor.start()` spawns threads and begins generating data every 100ms. `filter.start()` automatically subscribes to the sensor and blocks on its DATA mailbox. Each received message triggers `process()`.

---

## 4. Module Types and Processing Modes

### 4.1 Overview

The I/O specs determine the execution mode automatically:

| I/O Specs | Mode | When process() Called | Use Case |
|-----------|------|---------------------|----------|
| `Output<T>, Period<N>` | Timer-driven | Every N milliseconds | Sensor reading, heartbeat |
| `Output<T>, Input<U>` | Input-driven | Each received message | Filtering, transformation |
| `Output<T>` (no Period, no Input) | Loop | As fast as possible | High-speed generation |
| `Output<T1>, Output<T2>, Period<N>` | Timer, multi-output | Every N milliseconds | Multi-sensor producer |
| `Output<T>, Input<U>, SyncedInput<V>` | Input-driven, multi-input | Primary input arrives | Sensor fusion |

### 4.2 Timer-Driven (Period)

Generates data at fixed intervals. No input needed.

```cpp
class Sensor : public MyApp::Module2<Output<SensorData>, Period<Milliseconds(10)>> {
protected:
    void process(SensorData& output) override {
        output = read_sensor();  // Called every 10ms
    }
};
```

- Deterministic timing (CommRaT scheduler)
- 0% CPU between periods
- Timestamp: `TimsHeader.timestamp = Time::now()`

### 4.3 Input-Driven

Reacts to incoming messages. Blocks efficiently until data arrives.

```cpp
class Filter : public MyApp::Module2<Output<FilteredData>, Input<RawData>> {
protected:
    void process(const RawData& input, FilteredData& output) override {
        output.value = apply_filter(input);  // Called per received message
    }
};
```

- 0% CPU when idle
- Every published message triggers `process()`
- Timestamp: `TimsHeader.timestamp = input.header.timestamp` (propagated)

### 4.4 Loop Mode

Runs as fast as possible. No timing constraints. Omit both `Period` and `Input` specs.

```cpp
class FastCounter : public MyApp::Module2<Output<CounterData>> {
protected:
    void process(CounterData& output) override {
        output.value = counter_++;  // Runs continuously
    }

private:
    uint64_t counter_ = 0;
};
```

- 100% CPU on one core
- Use sparingly -- can starve other processes

### 4.5 Multi-Output

Separate `Output<T>` for each output type. Each gets its own CMD mailbox.

```cpp
class MultiSensor : public MyApp::Module2<
    Output<TemperatureData>,
    Output<PressureData>,
    Period<100>
> {
protected:
    void process(TemperatureData& temp, PressureData& pressure) override {
        temp.temperature_c = read_temp();
        pressure.pressure_pa = read_pressure();
    }
};
```

Subscribers choose which output type to receive. Each output can have independent subscribers.

### 4.6 Multi-Input with Synced

First `Input<T>` is primary (drives execution). `SyncedInput<T>` types are secondary (synchronized via `get_data`).

```cpp
class Fusion : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,              // Primary -- blocking receive
    SyncedInput<GPSData>         // Secondary -- get_data synchronized
> {
protected:
    void process(const IMUData& imu, const Synced<GPSData>& gps,
                 FusedData& output) override {
        if (gps) {
            output = fuse_sensors(imu, gps.value());  // Fresh GPS
        } else if (gps.is_valid()) {
            output = fuse_sensors(imu, gps.stale());   // Stale GPS
        } else {
            output = dead_reckoning(imu);               // No GPS
        }
    }
};
```

See Section 7 for full `Synced<T>` API.

### 4.7 Choosing the Right Mode

```
Generate data from scratch?
  YES -> Period<N> (timer-driven)
  NO  -> Receive messages?
           YES -> Need multi-source sync?
                    YES -> Input<T> + SyncedInput<U>
                    NO  -> Input<T>
           NO  -> Output<T> alone (loop mode, use sparingly)
```

---

## 5. I/O Specifications

### 5.1 Reference Table

| Spec | Purpose | Template Parameter |
|------|---------|--------------------|
| `Output<T>` | Declares an output type | Payload struct type |
| `Input<T>` | Primary input (blocking receive) | Input payload type |
| `SyncedInput<T>` | Secondary input (get_data sync) | Input payload type |
| `Period<N>` | Timer interval | Duration value (ms) or `Milliseconds(N)` |

### 5.2 Output Specs

One or more `Output<T>` specs per module. Each output gets its own CMD mailbox and subscriber list.

```cpp
// Single output
Module2<Output<DataA>, Period<100>>

// Multiple outputs -- each is separate
Module2<Output<DataA>, Output<DataB>, Period<100>>
```

There is no `Output<void>`. Every module must produce at least one output.

### 5.3 Input Specs

```cpp
// No input (timer-driven or loop)
Module2<Output<T>, Period<100>>        // Timer
Module2<Output<T>>                     // Loop

// Single input
Module2<Output<U>, Input<T>>           // Input-driven

// Multi-input (primary + synced secondaries)
Module2<Output<U>, Input<T>, SyncedInput<V>, SyncedInput<W>>
```

The first `Input<T>` is automatically the primary input. All `SyncedInput<T>` types use `get_data()` synchronized to the primary input's timestamp.

### 5.4 Period Spec

```cpp
Period<100>                  // 100ms (integer literal)
Period<Milliseconds(100)>    // Explicit duration type
Period<Seconds(1)>           // 1 second
```

### 5.5 Process Signatures

The `process()` signature is determined by the I/O specs:

| I/O Specs | Process Signature |
|-----------|-------------------|
| `Output<T>, Period<N>` | `void process(T& output)` |
| `Output<T>` (loop) | `void process(T& output)` |
| `Output<U>, Input<T>` | `void process(const T& input, U& output)` |
| `Output<A>, Output<B>, Period<N>` | `void process(A& out1, B& out2)` |
| `Output<U>, Input<T>, SyncedInput<V>` | `void process(const T& in, const Synced<V>& synced, U& out)` |

Inputs always come first (const refs), then synced inputs (const Synced refs), then outputs (mutable refs).

### 5.6 I/O Metadata Access

Inside `process()`, access metadata for inputs:

```cpp
uint64_t ts = get_input_timestamp<0>();   // Index-based
bool fresh = has_new_data<1>();           // Is synced input fresh?
bool valid = is_input_valid<1>();         // Did get_data succeed?
```

### 5.7 Structured I/O Access

Named access to inputs and outputs via `inputs()` and `outputs()`:

```cpp
auto ins = inputs();    // rfl::NamedTuple with auto-generated field names
auto outs = outputs();  // e.g., outs.temperature_data, ins.imu_data
```

---

## 6. Message Flow and Subscription

### 6.1 Subscription Protocol

CommRaT uses explicit subscription (not automatic discovery). When a consumer calls `start()`, it automatically subscribes to producers based on configuration:

```
Consumer                           Producer
   |                                  |
   |  1. SubscribeRequest             |
   |  (to producer's CMD mailbox)     |
   |--------------------------------->|
   |                                  | 2. Add subscriber
   |  3. SubscribeReply               |
   |<---------------------------------|
   |                                  |
   |  4. Data messages                |
   |  (to consumer's DATA mailbox)    |
   |<---------------------------------|
```

All subscription is automatic -- configured via `SingleInputConfig` source IDs.

### 6.2 Address Calculation

```cpp
// Base address encodes primary output type
uint16_t type_id_low = get_message_id<TemperatureData>() & 0xFFFF;
uint32_t base = (type_id_low << 16) | (system_id << 8) | instance_id;

uint32_t cmd_mailbox  = base + 0;    // Commands
uint32_t work_mailbox = base + 16;   // Send-only
uint32_t data_mailbox = base + 32;   // Data receive
```

### 6.3 Data Flow

**Producer:** `process()` writes to output workspace -> module publishes to all subscribers' DATA mailboxes via WORK mailbox.

**Consumer:** Blocks on DATA mailbox `receive()` -> deserializes -> calls `process(input, output)` -> publishes to consumer's subscribers.

### 6.4 Unsubscription

On `stop()`, consumers automatically send `UnsubscribeRequest` to producers. Always stop consumers before producers:

```cpp
consumer.stop();   // Unsubscribes first
producer.stop();   // Then stop producer
```

### 6.5 Multi-Output Type Filtering

For multi-output producers, subscribers receive only their subscribed type. The consumer's `Input<T>` type determines which output stream it subscribes to.

When subscribing to a multi-output producer's non-primary output, use `MultiOutputConfig` to specify the producer's primary output type for correct address resolution:

```cpp
commrat::ModuleConfig receiver_config{
    .name = "PressureRx",
    .outputs = commrat::SimpleOutputConfig{.system_id = 30, .instance_id = 1},
    .inputs = commrat::SingleInputConfig{
        .source_system_id = 10,
        .source_instance_id = 1
    }
};
```

---

## 7. Multi-Input Synchronization

### 7.1 The Problem

Sensors run at different rates (IMU at 100Hz, GPS at 5Hz). Naive approaches lead to temporal misalignment. CommRaT solves this with primary/secondary input synchronization.

### 7.2 Primary vs Secondary

- **Primary** (`Input<T>`): Blocking receive, drives execution rate
- **Secondary** (`SyncedInput<T>`): Fetched via `get_data(timestamp)` aligned to primary's timestamp

```cpp
class SensorFusion : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,              // Primary: 100Hz
    SyncedInput<GPSData>         // Secondary: 5Hz, time-synchronized
> {
protected:
    void process(const IMUData& imu, const Synced<GPSData>& gps,
                 FusedData& output) override {
        if (gps) {
            output = fuse_with_gps(imu, gps.value());
        } else if (gps.has_stale()) {
            output = fuse_with_gps(imu, gps.stale());
        } else {
            output = dead_reckoning(imu);
        }
    }
};
```

### 7.3 Synced<T> API

`Synced<T>` wraps a zero-copy const pointer with validity/freshness metadata:

| Method | Returns | Meaning |
|--------|---------|---------|
| `operator bool()` | `bool` | True if data is **fresh** (strict) |
| `is_fresh()` | `bool` | True if data is newly received |
| `has_stale()` | `bool` | True if valid but not fresh |
| `is_valid()` | `bool` | True if any data available (fresh or stale) |
| `value()` | `const T&` | Fresh data (asserts if not fresh) |
| `stale()` | `const T&` | Any valid data (asserts if invalid) |
| `*gps` | `const T&` | Alias for `stale()` (most permissive) |
| `value_or(default)` | `const T&` | Fresh data or default |
| `stale_or(default)` | `const T&` | Valid data or default |

**Usage patterns:**

```cpp
void process(const IMUData& imu, const Synced<GPSData>& gps, FusedData& out) override {
    // Pattern 1: Strict freshness
    if (gps) {
        out = fuse(imu, gps.value());
    }

    // Pattern 2: Accept stale data
    if (gps.is_valid()) {
        out = fuse(imu, gps.stale());
    }

    // Pattern 3: Default fallback
    out = fuse(imu, gps.stale_or(default_gps));
}
```

### 7.4 Configuration for Multi-Input

```cpp
commrat::ModuleConfig fusion_config{
    .name = "SensorFusion",
    .outputs = commrat::SimpleOutputConfig{.system_id = 20, .instance_id = 1},
    .inputs = commrat::MultiInputConfig{
        .sources = {
            {.system_id = 10, .instance_id = 1, .is_primary = true},   // IMU
            {.system_id = 11, .instance_id = 1, .is_primary = false}   // GPS
        },
        .history_buffer_size = 100,
        .sync_tolerance = std::chrono::milliseconds(50)
    }
};
```

**Tolerance tuning:** Too small -> secondary inputs often invalid. Too large -> stale data accepted. Rule of thumb: 2-3x the slowest sensor's period.

### 7.5 Multiple Secondary Inputs

```cpp
class MultiSensorFusion : public MyApp::Module2<
    Output<FusedData>,
    Input<IMUData>,
    SyncedInput<GPSData>,
    SyncedInput<LidarData>
> {
protected:
    void process(const IMUData& imu, const Synced<GPSData>& gps,
                 const Synced<LidarData>& lidar, FusedData& output) override {
        // Handle each secondary independently
        if (gps.is_valid() && lidar.is_valid()) {
            output = full_fusion(imu, gps.stale(), lidar.stale());
        } else if (gps.is_valid()) {
            output = gps_only_fusion(imu, gps.stale());
        } else {
            output = dead_reckoning(imu);
        }
    }
};
```

---

## 8. Timestamp Management

### 8.1 Single Source of Truth

Every message has exactly one timestamp in `TimsHeader.timestamp`. Never add timestamp fields to payloads.

```cpp
// WRONG: Timestamp in payload
struct SensorData {
    uint64_t timestamp;    // Redundant!
    float temperature;
};

// RIGHT: Clean payload
struct SensorData {
    float temperature;     // TimsHeader.timestamp handles timing
};
```

### 8.2 Automatic Assignment

CommRaT sets timestamps automatically based on module type:

| Module Type | Timestamp Assignment |
|-------------|---------------------|
| Timer-driven (`Period<N>`) | `Time::now()` at generation |
| Input-driven (`Input<T>`) | `input.header.timestamp` (propagated) |
| Multi-input | `primary_input.header.timestamp` |
| Loop mode | `Time::now()` at generation |

You never set timestamps manually -- the module handles it before publishing.

### 8.3 Accessing Timestamps

Use metadata accessors inside `process()`:

```cpp
void process(const IMUData& imu, const Synced<GPSData>& gps, FusedData& out) override {
    uint64_t imu_ts = get_input_timestamp<0>();    // Primary timestamp
    bool gps_fresh = has_new_data<1>();             // GPS freshness
    bool gps_ok = is_input_valid<1>();              // GPS validity
}
```

### 8.4 Units and Conversions

All timestamps are `uint64_t` nanoseconds since epoch.

```cpp
uint64_t now_ns = Time::now();
uint64_t now_ms = now_ns / 1'000'000;
double now_s = now_ns / 1'000'000'000.0;

// Duration types
Duration ten_ms = Milliseconds(10);
Duration one_sec = Seconds(1);
```

### 8.5 Monotonicity

Timestamps from a single producer are monotonically increasing. Sequence numbers (in `TimsHeader`) enable message loss detection:

```cpp
void process(const SensorData& input, SensorData& output) override {
    auto meta = get_input_metadata<0>();
    if (meta.sequence_number != last_seq_ + 1) {
        // Message loss detected
    }
    last_seq_ = meta.sequence_number;
    output = input;
}
```

---

## 9. Command Handling

### 9.1 DataWithCommands Pattern

Associate commands with an output's data type using `DataWithCommands`:

```cpp
// Define command payloads with nested Reply types
struct CalibrateCmd {
    float offset;
    struct Reply {
        bool success;
        float previous_offset;
    };
};

struct ResetCmd {
    bool hard_reset;
    struct Reply {
        bool success;
    };
};

// Associate commands with data type
using TempWithCommands = commrat::DataWithCommands<
    TemperatureData,
    CalibrateCmd,
    ResetCmd
>;

// Application definition
using MyApp = commrat::CommRaT<
    TempWithCommands,
    commrat::Message::Data<FilteredData>
>;
```

### 9.2 Implementing Command Handlers

Use `on_command<OutputIndex>()` templates in your module:

```cpp
class CommandableSensor : public MyApp::Module2<
    Output<TemperatureData>,
    Period<200>
> {
protected:
    void process(TemperatureData& output) override {
        output.temperature_c = read_sensor() + calibration_offset_;
    }

    template<size_t OutputIndex>
    void on_command(const CalibrateCmd& cmd, typename CalibrateCmd::Reply& reply) {
        reply.previous_offset = calibration_offset_;
        calibration_offset_ = cmd.offset;
        reply.success = true;
    }

    template<size_t OutputIndex>
    void on_command(const ResetCmd& cmd, typename ResetCmd::Reply& reply) {
        calibration_offset_ = 0.0f;
        reply.success = true;
    }

private:
    float calibration_offset_ = 0.0f;
};
```

### 9.3 Command Dispatch

Commands arrive on the output's CMD mailbox. The framework dispatches to the correct `on_command` handler based on message type. System commands (SubscribeRequest, UnsubscribeRequest, GetDataRequest) are handled automatically.

---

## 10. Configuration and Deployment

### 10.1 ModuleConfig

`ModuleConfig` uses tagged unions for output and input configuration:

```cpp
struct ModuleConfig {
    std::string name;
    OutputConfig outputs;         // SimpleOutputConfig | MultiOutputConfig | NoOutputConfig
    InputConfig inputs;           // NoInputConfig | SingleInputConfig | MultiInputConfig
    std::optional<std::chrono::milliseconds> period;
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    // ... mailbox slot counts
};
```

### 10.2 Output Config Types

**SimpleOutputConfig** -- All outputs share same addressing (most common):

```cpp
commrat::SimpleOutputConfig{.system_id = 10, .instance_id = 1}
```

**MultiOutputConfig** -- Per-output addressing (for multi-output modules):

```cpp
commrat::MultiOutputConfig{.addresses = {
    {.system_id = 10, .instance_id = 1},   // Output 0
    {.system_id = 10, .instance_id = 1}    // Output 1
}}
```

**NoOutputConfig** -- For monitor/logger modules (still needs addressing):

```cpp
commrat::NoOutputConfig{.system_id = 20, .instance_id = 1}
```

### 10.3 Input Config Types

**NoInputConfig** -- Timer-driven or loop mode:

```cpp
commrat::NoInputConfig{}
```

**SingleInputConfig** -- One source module:

```cpp
commrat::SingleInputConfig{
    .source_system_id = 10,
    .source_instance_id = 1
}
```

**MultiInputConfig** -- Multiple synchronized sources:

```cpp
commrat::MultiInputConfig{
    .sources = {
        {.system_id = 10, .instance_id = 1, .is_primary = true},
        {.system_id = 11, .instance_id = 1, .is_primary = false}
    },
    .history_buffer_size = 100,
    .sync_tolerance = std::chrono::milliseconds(50)
}
```

### 10.4 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyProject CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(CommRaT REQUIRED)

add_executable(my_system main.cpp)
target_link_libraries(my_system CommRaT::commrat tims pthread)
```

### 10.5 Startup and Shutdown Order

1. Start producers first, then consumers (with short delay)
2. Stop consumers first (unsubscribe), then producers

```cpp
producer.start();
std::this_thread::sleep_for(std::chrono::milliseconds(100));
consumer.start();

// ... run ...

consumer.stop();
producer.stop();
```

### 10.6 System ID Guidelines

| Range | Purpose |
|-------|---------|
| 1-49 | Sensor modules |
| 50-99 | Processing modules |
| 100-149 | Output/actuator modules |
| 200-254 | Test/diagnostic modules |

Use unique `(system_id, instance_id)` pairs. Multiple instances of the same module type use different `instance_id` values.

---

## 11. Schema Introspection and Viewer

CommRaT can export a machine-readable JSON schema describing every message
type registered in a `CommRaT<>` application.  The schema captures the
full binary layout of each wire message, plus CommRaT-specific metadata
(message ID, registry name, payload and wire type names, max message size).

### 11.1 C++ runtime export

```cpp
#include <commrat/introspection.hpp>

// Write schema JSON to file
MyApp::Introspection::write_to_file("my_app_schema.json");

// Or capture as a string (e.g. for a REST endpoint)
std::string json = MyApp::Introspection::export_all();
```

The output file contains a `CommRaTSchemaOutput` object:

```json
{
  "version": "1.0",
  "generated": "...",
  "messages": [
    {
      "layout": "<sertial StructLayout JSON>",
      "commrat": {
        "message_id": 16777217,
        "payload_type": "SensorData",
        "full_type": "commrat::corerat::WireMessage<SensorData>",
        "max_message_size": 32,
        "registry_name": "MyApp"
      }
    },
    ...
  ]
}
```

### 11.2 CMake schema generation (recommended)

`commrat_generate_schema()` creates a lightweight side-car executable and
attaches a `POST_BUILD` command so the JSON is regenerated automatically
whenever the registry header changes.

```cmake
find_package(CommRaT REQUIRED)   # makes commrat_generate_schema() available

commrat_generate_schema(
    TARGET      my_module
    APP_HEADER  "${CMAKE_SOURCE_DIR}/include/myapp/my_app.hpp"
    APP_TYPE    MyApp
    OUTPUT      "${CMAKE_BINARY_DIR}/my_app_schema.json"
)
```

| Parameter | Description |
|---|---|
| `TARGET` | Any existing CMake executable target that can see the app header |
| `APP_HEADER` | Path to the header that defines `APP_TYPE` (passed to `#include`) |
| `APP_TYPE` | Unqualified name of the `CommRaT<...>` alias |
| `OUTPUT` | Absolute path where the JSON file will be written |

The side-car target is named `<TARGET>_schema_gen`.  Build it explicitly:

```bash
cmake --build build/ --target my_module_schema_gen
```

### 11.3 Web viewer

Open `tools/commrat-inspect/viewer.html` in any browser and load a schema
JSON via the file picker, or pass a `?schema=` URL parameter:

```
viewer.html?schema=../../build/default/my_app_schema.json
```

#### Live viewer (GitHub Pages)

The CI workflow builds the `AllExamplesApp` schema and deploys it with
the viewer to GitHub Pages:

```
https://<org>.github.io/<repo>/commrat-inspect/
```

That URL auto-redirects to:

```
viewer.html?schema=all_examples_schema.json
```

#### Viewer features

- Memory layout map with byte-level visualization
- CommRaT metadata block: message ID (decimal + hex), payload type,
  wire type name, max message size, registry name
- Filter bar: search by type name or message ID
- "Hide system messages" checkbox (suppresses Subscribe/Unsubscribe/GetData)
- Compatible with plain sertial `SchemaOutput` files (no CommRaT block)

---

## 12. Best Practices

### DO

- **Use `Module2<>`** with I/O specs (`Output<T>`, `Input<T>`, `Period<N>`, `SyncedInput<T>`)
- **Keep payloads clean** -- no timestamp fields, no dynamic allocation
- **Use `override`** on all `process()` methods
- **Start producers before consumers** with a short delay
- **Stop in reverse order** of start
- **Use `std::array`** and `sertial::fixed_vector<T, N>` for bounded data
- **Use `constexpr` / `static_assert`** for compile-time validation
- **Use `Synced<T>`** API to handle fresh/stale/invalid correctly
- **Use CommRaT abstractions** for threading (`Thread`, `Mutex`, `Time::now()`)
- **Keep `process()` deterministic** -- bounded execution time

### DON'T

- **Don't use `std::vector` or `std::string`** in payloads or hot paths
- **Don't use `new` / `delete` / `malloc`** in `process()`
- **Don't use `std::cout`** in high-frequency `process()` loops (blocking I/O)
- **Don't throw exceptions** in real-time code paths
- **Don't block** in `process()` (no sleep, no mutex wait)
- **Don't add timestamp fields** to payloads -- use `TimsHeader.timestamp`
- **Don't use `std::thread` / `std::mutex`** directly -- use CommRaT wrappers
- **Don't poll** -- use blocking receives (0% CPU idle)

### Real-Time Safety Checklist

Before shipping `process()` code, verify:
1. No heap allocations (`new`, `malloc`, `vector::push_back`)
2. No blocking I/O (`cout`, file writes, network calls)
3. No unbounded loops
4. No exceptions
5. Bounded execution time
6. All containers fixed-size

---

## 13. Troubleshooting

### Common Issues

**Module not receiving data**
- Check `source_system_id` / `source_instance_id` match producer's `system_id` / `instance_id`
- Ensure producer starts before consumer
- Verify TiMS router is running (`corerat-router-tcp`)

**Address collision**
- Two modules with same `(type_id, system_id, instance_id)` will collide
- Use unique `system_id`/`instance_id` combinations
- Multi-output modules: each output type creates a separate address

**process() not called**
- Timer-driven: check `period` is set in config
- Input-driven: check subscription completed (producer running?)
- Loop mode: verify no `Period` or `Input` spec is accidentally included

**Compile error: type not registered**
- Add message type to `CommRaT<...>` application definition
- Ensure `Message::Data<T>` or `DataWithCommands<T, ...>` wraps the type

**Synced input always invalid**
- Check `sync_tolerance` is large enough (2-3x slowest sensor period)
- Verify `history_buffer_size` is adequate
- Ensure secondary producer is running and subscribed

**Subscription fails**
- Start producers before consumers
- Check `max_subscribers` limit on producer
- Verify address matching (system_id/instance_id)

**High latency**
- Avoid blocking I/O in `process()`
- Check for priority inversion (set `realtime = true` and appropriate `priority`)
- Profile `process()` duration -- should be less than period

### Debugging Tips

- Use `get_input_timestamp<N>()` to measure end-to-end latency
- Use `has_new_data<N>()` to detect stale secondary inputs
- Check sequence numbers for message loss
- Log sparingly -- use `fprintf` (thread-safe) instead of `std::cout`

### Error Reference

| Symptom | Cause | Fix |
|---------|-------|-----|
| 0 messages received | Wrong source IDs | Match system_id/instance_id |
| Compile error on `process()` | Wrong signature | Match I/O specs (see Section 5.5) |
| Assertion in `Synced::value()` | Data not fresh | Check `if (gps)` before `.value()` |
| Address collision at startup | Duplicate addresses | Use unique system_id/instance_id |
| 100% CPU | Loop mode or busy `process()` | Add `Period<N>` or optimize |
| Subscription timeout | Producer not started | Start producers first |
