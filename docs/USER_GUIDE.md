# CommRaT User Guide

**CommRaT** (Communication Runtime) is a C++20 real-time messaging framework that provides type-safe, compile-time validated message passing for deterministic systems. Built on top of TiMS (TIMS Interprocess Message System), it enables you to build robust distributed applications with guaranteed real-time performance.

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
11. [Best Practices](#11-best-practices)
12. [Troubleshooting](#12-troubleshooting)

---

## 1. Introduction

### What is CommRaT?

CommRaT is a messaging framework designed for **real-time embedded systems** where deterministic behavior, low latency, and type safety are critical. Unlike traditional publish-subscribe systems (ROS, DDS, MQTT), CommRaT provides:

- **Compile-time validation**: Message types, IDs, and registries validated at compile time
- **Zero runtime overhead**: No dynamic allocation, no runtime type checking
- **Deterministic execution**: Bounded execution time, no blocking I/O in hot paths
- **Real-time safe**: Lock-free where possible, priority inheritance mutexes
- **Type safety**: C++20 concepts enforce correct API usage

### Key Features

**1. Compile-Time Message Registry**
```cpp
// Define your application with all message types
using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<FilteredData>,
    Message::Command<ResetCmd>
>;
// Registry validates uniqueness, calculates IDs, generates serialization
```

**2. Three-Mailbox Architecture**
Every module has three separate mailboxes with distinct purposes:
- **CMD Mailbox**: User commands and control messages
- **WORK Mailbox**: Subscription protocol (SubscribeRequest/Reply)
- **DATA Mailbox**: High-frequency data streams (ContinuousInput)

This separation prevents interference between command, control, and data flows.

**3. Blocking Receives with Zero CPU Usage**
```cpp
// No polling, no timeouts, 0% CPU when idle
void command_loop() {
    while (running_) {
        cmd_mailbox_.receive_any([this](auto&& msg) {
            handle_command(msg);
        });  // Blocks until message arrives
    }
}
```

**4. Multi-Input Sensor Fusion**
```cpp
class SensorFusion : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>,  // Multiple inputs
    PrimaryInput<IMUData>                  // Primary drives execution
> {
protected:
    FusedData process_multi_input(
        const IMUData& imu,      // Received (blocking)
        const GPSData& gps,      // Fetched via getData
        const LidarData& lidar   // Fetched via getData
    ) override {
        return fuse_sensors(imu, gps, lidar);
    }
};
```

### When to Use CommRaT

**CommRaT is ideal for:**
- Real-time control systems (robotics, autonomous vehicles)
- Sensor fusion and data processing pipelines
- Embedded systems with hard timing constraints
- Applications requiring deterministic message delivery
- Systems where type safety prevents catastrophic errors

**CommRaT may NOT be ideal for:**
- Web services or microservices (use gRPC, REST)
- Data analytics pipelines (use Kafka, RabbitMQ)
- Systems without real-time requirements (use ROS 2, ZeroMQ)
- Applications requiring dynamic discovery (CommRaT uses static configuration)

### Comparison with Other Frameworks

| Feature | CommRaT | ROS 2 | DDS | ZeroMQ |
|---------|---------|-------|-----|--------|
| **Real-time guarantees** | Yes (compile-time) | Partial | Yes | No |
| **Zero dynamic allocation** | Yes | No | No | No |
| **Compile-time validation** | Yes | No | No | No |
| **Type safety** | Strong (C++20 concepts) | Moderate | Weak | None |
| **Learning curve** | Moderate | Steep | Steep | Easy |
| **Deployment complexity** | Low (static config) | High (discovery) | High | Moderate |
| **Performance** | High (zero-copy) | Moderate | High | Moderate |

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Application                          │
│  using MyApp = CommRaT<MessageA, MessageB, CommandC>;      │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
   ┌────▼─────┐         ┌────▼─────┐         ┌────▼─────┐
   │ Producer │         │ Consumer │         │  Filter  │
   │  Module  │         │  Module  │         │  Module  │
   └──────────┘         └──────────┘         └──────────┘
        │                     ▲                     ▲
        │ Publish             │ Subscribe           │
        │                     │                     │
   ┌────▼─────────────────────┴─────────────────────┴─────┐
   │              TiMS (Message Passing Layer)            │
   │  - 3 mailboxes per module (CMD/WORK/DATA)           │
   │  - Blocking receives (0% CPU idle)                   │
   │  - Zero-copy when possible                           │
   └──────────────────────────────────────────────────────┘
```

### What You'll Learn

This guide will take you from **zero to productive** with CommRaT:

1. **Sections 1-3**: Understand core concepts and create your first working system
2. **Sections 4-6**: Master different module types and message flow patterns
3. **Sections 7-9**: Learn advanced features (multi-input, timestamps, commands)
4. **Sections 10-12**: Deploy, optimize, and troubleshoot real systems

By the end, you'll be able to design and implement real-time messaging systems with confidence.

---

## 2. Core Concepts

Understanding CommRaT requires familiarity with five fundamental concepts: **Messages**, **Modules**, **Mailboxes**, **Registries**, and the **Subscription Protocol**.

### 2.1 Messages

Messages are the **data structures** you send between modules. Every message is a plain C++ struct (POD type) with no virtual functions, pointers, or dynamic allocation.

**Basic Message Structure:**
```cpp
// User-defined data structure
struct TemperatureData {
    uint64_t timestamp;       // Nanoseconds since epoch
    uint32_t sensor_id;       // Which sensor produced this
    float temperature_c;      // Temperature in Celsius
    float confidence;         // Confidence level (0.0-1.0)
};
```

**Message Wrapper:**
CommRaT automatically wraps your data in a `TimsMessage<T>` that includes metadata:

```cpp
template<typename T>
struct TimsMessage {
    TimsHeader header;  // timestamp, sequence_number, message_id
    T payload;          // Your data
};
```

**Key Properties:**
- **Serializable**: Must be compatible with SeRTial serialization (POD types work automatically)
- **Fixed-size or bounded**: No `std::string`, `std::vector` (use `sertial::fixed_vector<T, N>` instead)
- **Real-time safe**: No heap allocations when copying or serializing

### 2.2 Application Definition

The `CommRaT<...>` template defines your entire application's message types:

```cpp
using MyApp = CommRaT<
    Message::Data<TemperatureData>,    // Data message
    Message::Data<FilteredData>,       // Another data message
    Message::Command<ResetCmd>         // Command message
>;
```

**What This Does:**
1. **Validates uniqueness**: Compile error if duplicate message types
2. **Assigns message IDs**: Each type gets unique 32-bit ID
3. **Generates serialization**: Automatic serialization/deserialization
4. **Creates type registry**: Compile-time lookup of types by ID

**Message ID Format:**
```
[prefix:8][subprefix:8][local_id:16]
 └─ 0x01 = UserDefined
          └─ 0x00 = Data
                   └─ Auto-assigned unique ID
```

### 2.3 Modules

A **Module** is a processing unit that receives messages, processes them, and optionally produces output. Every module runs in its own thread(s) and has its own mailboxes.

**Module Signature:**
```cpp
class MyModule : public MyApp::Module<
    OutputSpec,    // What this module produces
    InputSpec,     // What this module consumes
    ...Commands    // What commands it handles
> {
protected:
    // Override process() or process_continuous() to implement logic
};
```

**Module Anatomy:**
```cpp
class TemperatureSensor : public MyApp::Module<
    Output<TemperatureData>,  // Produces temperature readings
    PeriodicInput             // Generates data every config_.period
> {
protected:
    TemperatureData process() override {
        // Called every 10ms (if config_.period = 10ms)
        float temp = read_sensor();  // Your hardware interface
        return TemperatureData{
            .timestamp = Time::now(),
            .sensor_id = sensor_id_,
            .temperature_c = temp,
            .confidence = 0.95
        };
    }
    
private:
    uint32_t sensor_id_;
};
```

**Module Lifecycle:**
1. **Construction**: Module initialized, mailboxes created
2. **start()**: Spawns threads (command_loop, work_loop, data_thread)
3. **Running**: Processes messages, publishes outputs
4. **stop()**: Signals threads to exit
5. **Destruction**: Threads joined, mailboxes cleaned up

### 2.4 Three-Mailbox Architecture

Each module has **three separate mailboxes** to prevent interference:

```
Module Address Space:
┌─────────────────────────────────────────┐
│ Base Address = [type_id:16][sys:8][inst:8] │
├─────────────────────────────────────────┤
│ CMD  Mailbox (base + 0)                 │  ← User commands
│  - Commands from other modules          │
│  - Control messages                     │
├─────────────────────────────────────────┤
│ WORK Mailbox (base + 16)                │  ← Subscription protocol
│  - SubscribeRequest from consumers      │
│  - SubscribeReply confirmations         │
│  - UnsubscribeRequest on shutdown       │
├─────────────────────────────────────────┤
│ DATA Mailbox (base + 32)                │  ← High-frequency data
│  - ContinuousInput messages             │
│  - Published data from producers        │
└─────────────────────────────────────────┘
```

**Why Three Mailboxes?**
- **Separation of concerns**: Commands don't interfere with data streams
- **Priority handling**: Can prioritize WORK over DATA
- **Blocking receives**: Each mailbox can block independently (0% CPU when idle)

### 2.5 Message Registry

The `MessageRegistry<...>` (internal to `CommRaT<...>`) is a **compile-time map** from message types to IDs:

```cpp
// Compile-time operations (zero runtime cost)
constexpr uint32_t msg_id = MyApp::get_message_id<TemperatureData>();

// Type-safe serialization (picks correct serializer at compile time)
auto serialized = MyApp::serialize(temp_data);

// Type-safe deserialization (validates type at compile time)
auto result = MyApp::deserialize<TemperatureData>(buffer);
```

**Registry Features:**
- **Compile-time validation**: All types checked at compile time
- **Unique ID assignment**: Guaranteed no collisions
- **Type safety**: Can't deserialize wrong type
- **Zero overhead**: No runtime type lookups

### 2.6 Subscription Protocol

CommRaT uses an **explicit subscription protocol** (not automatic discovery like ROS):

**Step 1: Consumer Sends SubscribeRequest**
```cpp
// Consumer (FilterModule) wants TemperatureData from Producer (SensorModule)
SubscribeRequest req{
    .subscriber_base_address = my_base_address,
    .message_type_id = MyApp::get_message_id<TemperatureData>()
};
work_mailbox_.send(req, producer_work_mailbox);
```

**Step 2: Producer Adds Subscriber**
```cpp
// Producer receives SubscribeRequest on WORK mailbox
void handle_subscribe_request(const SubscribeRequest& req) {
    subscribers_.push_back(req.subscriber_base_address + 32);  // DATA mailbox
    
    // Send acknowledgment
    SubscribeReply reply{ .success = true };
    work_mailbox_.send(reply, req.subscriber_base_address + 16);  // WORK mailbox
}
```

**Step 3: Producer Publishes to Subscribers**
```cpp
// Every time process() returns data
void publish(const TemperatureData& data) {
    for (uint32_t subscriber_data_mbx : subscribers_) {
        data_mailbox_.send(data, subscriber_data_mbx);
    }
}
```

**Step 4: Consumer Receives on DATA Mailbox**
```cpp
// Consumer's data_thread blocked on receive
void continuous_loop() {
    while (running_) {
        data_mailbox_.receive_any([this](auto&& msg) {
            process_continuous(msg.message);
        });  // Blocks until message arrives
    }
}
```

### 2.7 Compile-Time Guarantees

CommRaT validates many properties at **compile time**, catching errors before runtime:

```cpp
// COMPILE ERROR: Type not in registry
MyApp::serialize(UnregisteredType{});
// error: no matching function for template 'serialize'

// COMPILE ERROR: Wrong output type
class BadModule : public MyApp::Module<Output<WrongType>, PeriodicInput> {
    TemperatureData process() override { ... }  // Mismatch!
};
// error: invalid covariant return type

// COMPILE ERROR: Duplicate message types
using BadApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<TemperatureData>  // Duplicate!
>;
// static_assert failure: Duplicate message type detected
```

### 2.8 Real-Time Safety

CommRaT enforces real-time safety through **design constraints**:

**FORBIDDEN in process() functions:**
```cpp
TemperatureData process() override {
    // ERROR: Dynamic allocation
    std::vector<float> readings;  // May allocate
    readings.push_back(temp);     // May reallocate
    
    // ERROR: Blocking I/O
    std::cout << "Temperature: " << temp << "\n";  // Blocks
    
    // ERROR: Unbounded execution
    while (sensor_ready()) { /* ... */ }  // Non-deterministic
    
    // ERROR: Locks that may block
    std::lock_guard<std::mutex> lock(mtx_);  // May wait
}
```

**ALLOWED in process() functions:**
```cpp
TemperatureData process() override {
    // VALID: Fixed-size stack allocation
    std::array<float, 10> readings;
    sertial::fixed_vector<float, 100> buffer;  // Bounded capacity
    
    // VALID: Compile-time operations
    constexpr int scale = 100;
    
    // VALID: Lock-free operations
    uint64_t count = counter_.fetch_add(1, std::memory_order_relaxed);
    
    // VALID: Deterministic computation
    float filtered = alpha_ * temp + (1 - alpha_) * prev_temp_;
    
    return TemperatureData{ /* ... */ };
}
```

### Key Takeaways

1. **Messages** are plain POD structs, wrapped in `TimsMessage<T>` with metadata
2. **CommRaT<...>** defines your application and validates all message types at compile time
3. **Modules** are processing units with 3 mailboxes (CMD/WORK/DATA)
4. **Subscription** is explicit: Consumer requests, producer acknowledges, then publishes
5. **Compile-time validation** catches type errors before runtime
6. **Real-time safety** requires no dynamic allocation, no blocking I/O in process()

---

## 3. Your First Module

Let's build a complete working system: a temperature sensor that publishes data, and a monitor that receives and displays it. This tutorial will take about **10 minutes** and requires only basic C++ knowledge.

### 3.1 Project Setup

**Step 1: Create project directory**
```bash
mkdir my_first_commrat
cd my_first_commrat
```

**Step 2: Create CMakeLists.txt**
```cmake
cmake_minimum_required(VERSION 3.20)
project(MyFirstCommRaT CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find CommRaT (adjust path to your installation)
find_package(CommRaT REQUIRED)

add_executable(temperature_system
    temperature_system.cpp
)

target_link_libraries(temperature_system
    CommRaT::commrat
    tims
    pthread
)
```

### 3.2 Define Your Message

Create `temperature_system.cpp`:

```cpp
#include <commrat/commrat.hpp>
#include <iostream>

// Step 1: Define your data structure (plain POD type)
struct TemperatureReading {
    uint64_t timestamp;       // When this reading was taken (ns)
    uint32_t sensor_id;       // Which sensor (1, 2, 3, ...)
    float temperature_c;      // Temperature in Celsius
    float humidity_percent;   // Relative humidity (0-100%)
};

// Step 2: Define your application with all message types
using TempApp = CommRaT<
    Message::Data<TemperatureReading>  // Register our message type
>;

// Application is now ready! TempApp provides:
//   - TempApp::Module<...> for creating modules
//   - TempApp::serialize/deserialize for messages
//   - TempApp::get_message_id<T>() for type IDs
```

**What just happened?**
- You defined a **plain C++ struct** with your sensor data
- You created a **CommRaT application** that knows about your message type
- The compiler **validated** your types and **assigned a unique ID** to TemperatureReading
- All serialization code was **generated automatically**

### 3.3 Create a Producer Module

A producer generates data periodically:

```cpp
// Step 3: Create a producer module
class TemperatureSensor : public TempApp::Module<
    Output<TemperatureReading>,  // This module outputs TemperatureReading
    PeriodicInput                // Runs periodically (no input data)
> {
public:
    TemperatureSensor(const ModuleConfig& config, uint32_t sensor_id)
        : Module(config)
        , sensor_id_(sensor_id)
    {
        std::cout << "[Sensor] Initialized sensor_id=" << sensor_id_ << "\n";
    }

protected:
    // This function is called every config_.period (e.g., every 100ms)
    TemperatureReading process() override {
        // Simulate sensor reading
        float temp = 20.0f + (rand() % 100) / 10.0f;  // 20-30°C
        float humidity = 40.0f + (rand() % 200) / 10.0f;  // 40-60%
        
        TemperatureReading reading{
            .timestamp = Time::now(),
            .sensor_id = sensor_id_,
            .temperature_c = temp,
            .humidity_percent = humidity
        };
        
        std::cout << "[Sensor] Generated: " << temp << "°C, "
                  << humidity << "% humidity\n";
        
        return reading;  // Automatically published to all subscribers
    }

private:
    uint32_t sensor_id_;
};
```

**Key points:**
- Inherits from `TempApp::Module<Output<...>, PeriodicInput>`
- Overrides `process()` which is called automatically every `config_.period`
- Returns the data - **no explicit publish call needed**
- Must use `override` keyword (process is virtual)

### 3.4 Create a Consumer Module

A consumer receives and processes data:

```cpp
// Step 4: Create a consumer module
class TemperatureMonitor : public TempApp::Module<
    Output<void>,                     // No output (monitor only)
    Input<TemperatureReading>         // Receives TemperatureReading
> {
public:
    TemperatureMonitor(const ModuleConfig& config)
        : Module(config)
        , count_(0)
    {
        std::cout << "[Monitor] Initialized\n";
    }

protected:
    // Called for each received TemperatureReading
    void process_continuous(const TemperatureReading& reading) override {
        count_++;
        
        std::cout << "[Monitor] #" << count_ 
                  << " Sensor " << reading.sensor_id
                  << ": " << reading.temperature_c << "°C, "
                  << reading.humidity_percent << "% humidity\n";
        
        // Check for alerts
        if (reading.temperature_c > 28.0f) {
            std::cout << "  WARNING: High temperature!\n";
        }
    }

private:
    uint32_t count_;
};
```

**Key points:**
- Inherits from `TempApp::Module<Output<void>, Input<TemperatureReading>>`
- Overrides `process_continuous(const TemperatureReading&)` 
- Called automatically for **each received message**
- Blocks efficiently (0% CPU when no messages)

### 3.5 Wire It Together

Now create the main function to configure and run both modules:

```cpp
int main() {
    std::cout << "=== CommRaT Temperature System ===\n\n";
    
    // Initialize TiMS messaging layer
    if (!tims::init()) {
        std::cerr << "Failed to initialize TiMS\n";
        return 1;
    }
    
    // Step 5: Configure the sensor (producer)
    ModuleConfig sensor_config{
        .name = "TempSensor",
        .system_id = 10,           // Unique system ID
        .instance_id = 1,          // Instance within system
        .period = Milliseconds(100) // Generate reading every 100ms (10Hz)
    };
    
    // Step 6: Configure the monitor (consumer)
    ModuleConfig monitor_config{
        .name = "TempMonitor",
        .system_id = 20,           // Different system ID
        .instance_id = 1,
        .source_system_id = 10,    // Subscribe to system 10 (sensor)
        .source_instance_id = 1    // Instance 1
    };
    
    // Step 7: Create and start both modules
    TemperatureSensor sensor(sensor_config, 1);
    TemperatureMonitor monitor(monitor_config);
    
    sensor.start();   // Spawns threads, begins generating
    monitor.start();  // Spawns threads, subscribes to sensor
    
    // Let it run for 5 seconds
    std::cout << "\nRunning for 5 seconds...\n\n";
    Time::sleep(Seconds(5));
    
    // Step 8: Clean shutdown
    std::cout << "\nShutting down...\n";
    sensor.stop();
    monitor.stop();
    
    tims::cleanup();
    std::cout << "Done!\n";
    return 0;
}
```

### 3.6 Build and Run

**Build:**
```bash
mkdir build && cd build
cmake ..
make
```

**Run:**
```bash
./temperature_system
```

**Expected Output:**
```
=== CommRaT Temperature System ===

[Sensor] Initialized sensor_id=1
[Monitor] Initialized

Running for 5 seconds...

[Sensor] Generated: 23.4°C, 52.1% humidity
[Monitor] #1 Sensor 1: 23.4°C, 52.1% humidity
[Sensor] Generated: 27.8°C, 48.3% humidity
[Monitor] #2 Sensor 1: 27.8°C, 48.3% humidity
[Sensor] Generated: 29.1°C, 55.7% humidity
[Monitor] #3 Sensor 1: 29.1°C, 55.7% humidity
  WARNING: High temperature!
[Sensor] Generated: 21.5°C, 43.9% humidity
[Monitor] #4 Sensor 1: 21.5°C, 43.9% humidity
...

Shutting down...
Done!
```

### 3.7 What Happened Under the Hood

Let's trace the message flow:

**1. Initialization (0-10ms):**
```
main() → tims::init()
      → TemperatureSensor constructor
          → Creates 3 mailboxes (CMD, WORK, DATA)
          → Base address: [TemperatureReading_ID:16][10:8][1:8]
      → TemperatureMonitor constructor
          → Creates 3 mailboxes (CMD, WORK, DATA)
```

**2. Module Start (10-20ms):**
```
sensor.start()
    → Spawns command_loop() thread (handles commands)
    → Spawns work_loop() thread (handles subscriptions)
    → Spawns data_thread_ (calls process() every 100ms)

monitor.start()
    → Spawns command_loop() thread
    → Spawns work_loop() thread
    → Spawns data_thread_ (calls continuous_loop)
    → Sends SubscribeRequest to sensor's WORK mailbox
```

**3. Subscription (20-30ms):**
```
Sensor's work_loop receives SubscribeRequest
    → Adds monitor's DATA mailbox to subscribers_ list
    → Sends SubscribeReply to monitor's WORK mailbox

Monitor's work_loop receives SubscribeReply
    → Subscription confirmed
    → Begins blocking on DATA mailbox
```

**4. Data Flow (every 100ms):**
```
Sensor's data_thread (timer fires every 100ms)
    → Calls process()
    → User code generates TemperatureReading
    → Module automatically publishes to all subscribers
        → Serializes TemperatureReading
        → Sends to monitor's DATA mailbox

Monitor's continuous_loop (blocked on receive)
    → Receives message on DATA mailbox
    → Deserializes TemperatureReading
    → Calls process_continuous(reading)
    → User code displays data
    → Blocks again waiting for next message
```

**5. Shutdown (5000ms):**
```
main() calls sensor.stop()
    → Sets running_ = false
    → Threads detect flag and exit
    → Joins all threads

main() calls monitor.stop()
    → Sends UnsubscribeRequest to sensor
    → Exits threads
```

### 3.8 Common First-Time Mistakes

**Mistake 1: Forgetting `override` keyword**
```cpp
// ERROR: Won't compile
TemperatureReading process() {  // Missing override
    return reading;
}
```
**Fix:** Always use `override` for process methods (they're virtual).

**Mistake 2: Wrong output type**
```cpp
// ERROR: Module says Output<TemperatureReading> but returns wrong type
class BadSensor : public TempApp::Module<Output<TemperatureReading>, PeriodicInput> {
    OtherData process() override {  // Type mismatch!
        return OtherData{};
    }
};
```
**Fix:** Return type must match `Output<T>` specification.

**Mistake 3: Mismatched system IDs**
```cpp
ModuleConfig monitor_config{
    .system_id = 20,
    .source_system_id = 99,  // ERROR: Sensor is system 10, not 99!
    .source_instance_id = 1
};
```
**Fix:** `source_system_id` must match producer's `system_id`.

**Mistake 4: Blocking in process()**
```cpp
TemperatureReading process() override {
    std::this_thread::sleep_for(std::chrono::seconds(1));  // ERROR: Blocks!
    return reading;
}
```
**Fix:** Never block in process() - use CommRaT's timing primitives or configure `period`.

### 3.9 Extending Your System

**Add another sensor:**
```cpp
ModuleConfig sensor2_config{
    .name = "TempSensor2",
    .system_id = 11,       // Different system ID
    .instance_id = 1,
    .period = Milliseconds(100)
};
TemperatureSensor sensor2(sensor2_config, 2);  // sensor_id=2
sensor2.start();
```

**Monitor both sensors:**
```cpp
// Monitor can subscribe to multiple producers by starting multiple instances
ModuleConfig monitor2_config{
    .name = "TempMonitor2",
    .system_id = 21,
    .source_system_id = 11,  // Subscribe to sensor2
    .source_instance_id = 1
};
TemperatureMonitor monitor2(monitor2_config);
monitor2.start();
```

**Add a filter module:**
```cpp
class TemperatureFilter : public TempApp::Module<
    Output<TemperatureReading>,      // Outputs filtered data
    Input<TemperatureReading>        // Inputs raw data
> {
    TemperatureReading process_continuous(const TemperatureReading& raw) override {
        // Apply exponential moving average
        filtered_temp_ = 0.7f * filtered_temp_ + 0.3f * raw.temperature_c;
        
        return TemperatureReading{
            .timestamp = Time::now(),
            .sensor_id = raw.sensor_id,
            .temperature_c = filtered_temp_,
            .humidity_percent = raw.humidity_percent
        };
    }
    
private:
    float filtered_temp_ = 20.0f;
};
```

### Key Takeaways

1. **Messages** are plain POD structs - define your data structure naturally
2. **CommRaT<...>** registers all types and generates serialization automatically
3. **Producer modules** use `PeriodicInput` and override `process()`
4. **Consumer modules** use `Input<T>` and override `process_continuous(const T&)`
5. **Configuration** uses system_id/instance_id for addressing and source IDs for subscription
6. **Subscription** happens automatically in `start()` based on configuration
7. **Shutdown** is clean - just call `stop()` on all modules

**Next:** Section 4 explores all module types (Periodic, Continuous, Loop) and processing modes.

---

## 4. Module Types and Processing Modes

CommRaT modules come in different flavors based on **how they process data**. Understanding these patterns helps you choose the right architecture for your application.

### 4.1 The Three Processing Modes

Every module has an **InputSpec** that determines its processing behavior:

| InputSpec | When process() Called | Use Case | Example |
|-----------|----------------------|----------|---------|
| **PeriodicInput** | Timer fires (every `period`) | Data generation, periodic sampling | Sensor reading, heartbeat |
| **Input<T>** (ContinuousInput) | Message received | Data transformation, filtering | Signal processing, fusion |
| **LoopInput** | As fast as possible | Maximum throughput | High-speed data forwarding |

### 4.2 PeriodicInput: Timer-Driven Processing

**When to use:** Generate data at fixed intervals, periodic tasks.

**Signature:**
```cpp
class MyModule : public MyApp::Module<OutputSpec, PeriodicInput> {
protected:
    OutputType process() override {
        // Called every config_.period (e.g., 100ms)
        return OutputType{ /* ... */ };
    }
};
```

**Configuration:**
```cpp
ModuleConfig config{
    .name = "PeriodicModule",
    .system_id = 10,
    .instance_id = 1,
    .period = Milliseconds(100)  // REQUIRED for PeriodicInput
};
```

**Example: Heartbeat Generator**
```cpp
struct HeartbeatMsg {
    uint64_t timestamp;
    uint32_t sequence_number;
    uint32_t process_id;
};

class Heartbeat : public MyApp::Module<Output<HeartbeatMsg>, PeriodicInput> {
public:
    Heartbeat(const ModuleConfig& config)
        : Module(config), seq_(0) {}

protected:
    HeartbeatMsg process() override {
        return HeartbeatMsg{
            .timestamp = Time::now(),
            .sequence_number = seq_++,
            .process_id = getpid()
        };
    }

private:
    uint32_t seq_;
};

// Usage
ModuleConfig config{
    .period = Seconds(1)  // Heartbeat every second
};
Heartbeat hb(config);
hb.start();
```

**Characteristics:**
- **Deterministic timing**: Process runs at exact intervals (handled by CommRaT's scheduler)
- **No blocking**: If process() takes longer than period, warning logged
- **CPU efficient**: Thread sleeps between periods (0% CPU when idle)
- **Timestamp assignment**: `TimsHeader.timestamp = Time::now()` at generation moment

### 4.3 Input<T>: Event-Driven Processing (ContinuousInput)

**When to use:** React to incoming messages, transform data.

**Signature:**
```cpp
class MyModule : public MyApp::Module<OutputSpec, Input<InputType>> {
protected:
    OutputType process_continuous(const InputType& input) override {
        // Called for EACH received message
        return OutputType{ /* transform input */ };
    }
};
```

**Configuration:**
```cpp
ModuleConfig config{
    .name = "ContinuousModule",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,     // Producer's system ID
    .source_instance_id = 1,    // Producer's instance ID
    .period = Duration(0)       // Ignored for ContinuousInput
};
```

**Example: Temperature Filter**
```cpp
struct RawTemperature {
    uint64_t timestamp;
    float value_c;
};

struct FilteredTemperature {
    uint64_t timestamp;
    float value_c;
    float confidence;
};

class TemperatureFilter : public MyApp::Module<
    Output<FilteredTemperature>,
    Input<RawTemperature>
> {
public:
    TemperatureFilter(const ModuleConfig& config)
        : Module(config), alpha_(0.3f), prev_temp_(20.0f) {}

protected:
    FilteredTemperature process_continuous(const RawTemperature& raw) override {
        // Exponential moving average
        float filtered = alpha_ * raw.value_c + (1 - alpha_) * prev_temp_;
        prev_temp_ = filtered;
        
        // Calculate confidence based on rate of change
        float confidence = 1.0f - std::min(std::abs(filtered - raw.value_c) / 10.0f, 1.0f);
        
        return FilteredTemperature{
            .timestamp = Time::now(),
            .value_c = filtered,
            .confidence = confidence
        };
    }

private:
    float alpha_;
    float prev_temp_;
};
```

**Characteristics:**
- **Event-driven**: Executes only when data arrives (0% CPU when no messages)
- **Blocking receive**: Thread blocks efficiently until message available
- **Guaranteed delivery**: Every published message triggers process_continuous
- **Timestamp propagation**: Output timestamp = input.header.timestamp (exact propagation)
- **Order preserved**: Messages processed in arrival order

### 4.4 LoopInput: Maximum Throughput Processing

**When to use:** Process data as fast as possible, no timing constraints.

**Signature:**
```cpp
class MyModule : public MyApp::Module<OutputSpec, LoopInput> {
protected:
    OutputType process() override {
        // Called repeatedly as fast as possible
        return OutputType{ /* ... */ };
    }
};
```

**Configuration:**
```cpp
ModuleConfig config{
    .name = "LoopModule",
    .system_id = 30,
    .instance_id = 1,
    .period = Duration(0)  // Ignored for LoopInput
};
```

**Example: Data Generator**
```cpp
struct DataPacket {
    uint64_t timestamp;
    uint64_t packet_id;
    std::array<float, 100> samples;
};

class HighSpeedGenerator : public MyApp::Module<Output<DataPacket>, LoopInput> {
public:
    HighSpeedGenerator(const ModuleConfig& config)
        : Module(config), packet_id_(0) {}

protected:
    DataPacket process() override {
        DataPacket packet{
            .timestamp = Time::now(),
            .packet_id = packet_id_++
        };
        
        // Generate synthetic waveform
        for (size_t i = 0; i < 100; ++i) {
            packet.samples[i] = std::sin(2 * M_PI * i / 100.0);
        }
        
        return packet;
    }

private:
    uint64_t packet_id_;
};
```

**Characteristics:**
- **Maximum throughput**: No artificial delays
- **High CPU usage**: Runs continuously (100% CPU on one core)
- **Non-deterministic timing**: Rate depends on processing speed
- **Use with caution**: Can starve other processes

**WARNING:** LoopInput should be used sparingly. Most applications should use PeriodicInput or ContinuousInput for predictable behavior and efficient CPU usage.

### 4.5 Output Specifications

Modules can produce zero, one, or multiple outputs:

#### No Output (Monitor/Sink)
```cpp
class DataLogger : public MyApp::Module<Output<void>, Input<SensorData>> {
protected:
    void process_continuous(const SensorData& data) override {
        log_to_file(data);
        // No return value - this is a sink
    }
};
```

#### Single Output (Most Common)
```cpp
class Filter : public MyApp::Module<Output<FilteredData>, Input<RawData>> {
protected:
    FilteredData process_continuous(const RawData& raw) override {
        return apply_filter(raw);
    }
};
```

#### Multiple Outputs (Phase 5.3+)
```cpp
class SignalSplitter : public MyApp::Module<
    Outputs<DataA, DataB>,     // Multiple outputs
    Input<CombinedData>
> {
protected:
    void process_continuous(const CombinedData& input, DataA& out1, DataB& out2) override {
        // Fill both outputs by reference
        out1 = extract_channel_a(input);
        out2 = extract_channel_b(input);
    }
};
```

### 4.6 InputSpec vs ProcessingMode: Understanding the Difference

**Common confusion:** "What's the difference between InputSpec and ProcessingMode?"

**InputSpec** (compile-time): Specifies **what data comes in**
- `PeriodicInput` - No input data (self-generating)
- `Input<T>` - One input type
- `Inputs<T, U, V>` - Multiple input types

**ProcessingMode** (runtime): Specifies **when to process**
- `Periodic` - Timer-driven (for PeriodicInput)
- `Continuous` - Event-driven (for Input<T>)
- `Loop` - As-fast-as-possible (for LoopInput)

The InputSpec **determines** the ProcessingMode automatically:

```cpp
// InputSpec → ProcessingMode mapping
PeriodicInput → ProcessingMode::Periodic
Input<T>      → ProcessingMode::Continuous
LoopInput     → ProcessingMode::Loop
```

**You don't specify ProcessingMode directly** - it's inferred from InputSpec.

### 4.7 Combining Inputs and Outputs

Valid combinations:

```cpp
// Generator: No input, has output
class Generator : public MyApp::Module<Output<Data>, PeriodicInput>;

// Transformer: Input → Output
class Transformer : public MyApp::Module<Output<DataB>, Input<DataA>>;

// Sink: Input → No output
class Sink : public MyApp::Module<Output<void>, Input<Data>>;

// Multi-output transformer
class Splitter : public MyApp::Module<Outputs<DataA, DataB>, Input<Combined>>;

// Multi-input fusion (Phase 6.9+)
class Fusion : public MyApp::Module<Output<Fused>, Inputs<DataA, DataB, DataC>>;
```

### 4.8 Choosing the Right Module Type

**Decision tree:**

```
Does your module generate data from scratch?
├─ YES → Use PeriodicInput
│         └─ Set .period to desired rate
│
└─ NO → Does it receive messages?
        ├─ YES → Need maximum throughput?
        │        ├─ NO → Use Input<T> (ContinuousInput)
        │        │       └─ Subscribe with source_system_id
        │        │
        │        └─ YES → Use LoopInput (rarely needed)
        │                └─ Monitor CPU usage carefully
        │
        └─ NO → Invalid (module must do something)
```

**Examples by use case:**

| Use Case | Module Type | Rationale |
|----------|-------------|-----------|
| Read sensor every 10ms | PeriodicInput | Fixed sampling rate |
| Filter incoming data | Input<T> | React to each message |
| Log messages to disk | Input<T> | Process each message |
| Generate test patterns | PeriodicInput | Controlled generation rate |
| Fuse IMU + GPS | Inputs<IMU, GPS> | Multiple synchronized inputs |
| Stress test system | LoopInput | Maximum load generation |

### 4.9 Performance Characteristics

**PeriodicInput:**
- **Latency**: Fixed (= period)
- **Throughput**: 1 / period messages/sec
- **CPU Usage**: Low (sleeps between periods)
- **Jitter**: Low (timer-driven)

**Input<T> (ContinuousInput):**
- **Latency**: Minimal (processes immediately on arrival)
- **Throughput**: Depends on publisher rate
- **CPU Usage**: 0% when idle, scales with message rate
- **Jitter**: Minimal (event-driven)

**LoopInput:**
- **Latency**: Minimal (no delays)
- **Throughput**: Maximum (limited by CPU)
- **CPU Usage**: 100% (continuous loop)
- **Jitter**: High (depends on system load)

### 4.10 Real-World Example: Sensor Fusion Pipeline

Let's combine multiple module types in a realistic system:

```cpp
// 1. Periodic sensor reading (PeriodicInput)
class IMUSensor : public MyApp::Module<Output<IMUData>, PeriodicInput> {
protected:
    IMUData process() override {
        return read_imu_hardware();  // Every 10ms
    }
};

// 2. Event-driven filtering (ContinuousInput)
class IMUFilter : public MyApp::Module<Output<FilteredIMU>, Input<IMUData>> {
protected:
    FilteredIMU process_continuous(const IMUData& raw) override {
        return kalman_filter_.update(raw);
    }
private:
    KalmanFilter kalman_filter_;
};

// 3. Multi-input fusion (ContinuousInput with multiple inputs)
class PoseFusion : public MyApp::Module<
    Output<PoseEstimate>,
    Inputs<FilteredIMU, GPSData>,
    PrimaryInput<FilteredIMU>  // IMU drives execution
> {
protected:
    PoseEstimate process_multi_input(
        const FilteredIMU& imu,
        const GPSData& gps
    ) override {
        return fuse_sensors(imu, gps);
    }
};

// 4. Sink for logging (ContinuousInput, no output)
class PoseLogger : public MyApp::Module<Output<void>, Input<PoseEstimate>> {
protected:
    void process_continuous(const PoseEstimate& pose) override {
        write_to_log(pose);
    }
};
```

**Pipeline:**
```
IMUSensor (10ms) → IMUFilter → PoseFusion ← GPSData
                                    ↓
                               PoseLogger
```

### Key Takeaways

1. **PeriodicInput**: Timer-driven, for data generation at fixed rates
2. **Input<T>**: Event-driven, for message processing and transformation
3. **LoopInput**: Maximum throughput, use sparingly (high CPU usage)
4. **InputSpec determines ProcessingMode** automatically - you don't specify both
5. **Combine module types** to build complex pipelines
6. **Choose based on requirements**: Timing constraints → Periodic, React to events → Continuous

**Next:** Section 5 dives deep into I/O specifications for advanced patterns.

---
