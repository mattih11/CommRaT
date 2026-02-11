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

**2. MailboxSet Architecture**

CommRaT uses a **MailboxSet per output type** design:

**Each output type gets 3 mailboxes:**
- **CMD Mailbox** (base+0): Commands for this output type
- **WORK Mailbox** (base+16): Subscription protocol for this output type
- **PUBLISH Mailbox** (base+32): Publishes this output type to subscribers

**Plus a shared DATA mailbox for all inputs:**
- **DATA Mailbox** (base+48): Receives input data (shared across output types)

**Single-Output Module Example:**
```cpp
class Sensor : public Module<Output<TempData>, PeriodicInput>
```
Has 1 MailboxSet = 3 mailboxes (CMD, WORK, PUBLISH)
Total: 3 mailboxes

**Multi-Output Module Example:**
```cpp  
class Fusion : public Module<Outputs<Raw, Filtered, Diag>, Input<SensorData>>
```
Has 3 MailboxSets × 3 = 9 mailboxes + 1 DATA = 10 total mailboxes
- Each output type (Raw, Filtered, Diag) has its own CMD, WORK, PUBLISH
- All share one DATA mailbox for receiving SensorData
- Subscribers choose which output type to subscribe to

This design allows **independent subscription per output type** - a subscriber can request only FilteredData without receiving Raw or Diag.

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
    void process(
        const IMUData& imu,      // Received (blocking)
        const GPSData& gps,      // Fetched via getData
        const LidarData& lidar,  // Fetched via getData
        FusedData& output        // Output written here
    ) override {
        output = fuse_sensors(imu, gps, lidar);
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
    // Override process() to implement logic
};
```

**Module Anatomy:**
```cpp
class TemperatureSensor : public MyApp::Module<
    Output<TemperatureData>,  // Produces temperature readings
    PeriodicInput             // Generates data every config_.period
> {
protected:
    void process(TemperatureData& output) override {
        // Called every 10ms (if config_.period = 10ms)
        float temp = read_sensor();  // Your hardware interface
        output = TemperatureData{
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
    subscribers_.push_back(req.subscriber_base_address + 48);  // DATA mailbox
    
    // Send acknowledgment
    SubscribeReply reply{ .success = true };
    work_mailbox_.send(reply, req.subscriber_base_address + 16);  // WORK mailbox
}
```

**Step 3: Producer Publishes to Subscribers**
```cpp
// Every time process() generates output
void publish(const TemperatureData& data) {
    for (uint32_t subscriber_data_mbx : subscribers_) {
        publish_mailbox_.send(data, subscriber_data_mbx);  // Send from PUBLISH mailbox
    }
}
```

**Step 4: Consumer Receives on DATA Mailbox (base+48)**
```cpp
// Consumer's data_thread blocked on receive
void continuous_loop() {
    while (running_) {
        data_mailbox_.receive_any([this](auto&& msg) {
            process(msg.message);
        });  // Blocks until message arrives on DATA mailbox
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
    void process(TemperatureData& output) override { ... }  // Mismatch!
};
// error: type constraint violation

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
void process(TemperatureData& output) override {
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
void process(TemperatureData& output) override {
    // VALID: Fixed-size stack allocation
    std::array<float, 10> readings;
    sertial::fixed_vector<float, 100> buffer;  // Bounded capacity
    
    // VALID: Compile-time operations
    constexpr int scale = 100;
    
    // VALID: Lock-free operations
    uint64_t count = counter_.fetch_add(1, std::memory_order_relaxed);
    
    // VALID: Deterministic computation
    float filtered = alpha_ * temp + (1 - alpha_) * prev_temp_;
    
    output = TemperatureData{ /* ... */ };
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
#include <csignal>
#include <atomic>

// Signal handler for clean shutdown
std::atomic<bool> shutdown_requested{false};
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        shutdown_requested.store(true);
    }
}

// Step 1: Define your data structure (plain POD type)
struct TemperatureReading {
    uint64_t timestamp;       // When this reading was taken (ns)
    uint32_t sensor_id;       // Which sensor (1, 2, 3, ...)
    float temperature_c;      // Temperature in Celsius
    float humidity_percent;   // Relative humidity (0-100%)
};

// Step 2: Define your application with all message types
using TempApp = commrat::CommRaT<
    commrat::Message::Data<TemperatureReading>  // Register our message type
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
    commrat::Output<TemperatureReading>,  // This module outputs TemperatureReading
    commrat::PeriodicInput                // Runs periodically (no input data)
> {
public:
    TemperatureSensor(const commrat::ModuleConfig& config, uint32_t sensor_id)
        : Module(config)
        , sensor_id_(sensor_id)
    {
        std::cout << "[Sensor] Initialized sensor_id=" << sensor_id_ << "\n";
    }

protected:
    // This function is called every config_.period (e.g., every 100ms)
    void process(TemperatureReading& output) override {
        // Simulate sensor reading
        float temp = 20.0f + (rand() % 100) / 10.0f;  // 20-30°C
        float humidity = 40.0f + (rand() % 200) / 10.0f;  // 40-60%
        
        output = TemperatureReading{
            .timestamp = commrat::Time::now(),
            .sensor_id = sensor_id_,
            .temperature_c = temp,
            .humidity_percent = humidity
        };
        
        std::cout << "[Sensor] Generated: " << temp << "°C, "
                  << humidity << "% humidity\n";
    }

private:
    uint32_t sensor_id_;
};
```

**Key points:**
- Inherits from `TempApp::Module<Output<...>, PeriodicInput>`
- Overrides `process(OutputData& output)` which is called automatically every `config_.period`
- Writes to output parameter - **no explicit publish call needed**
- Must use `override` keyword (process is virtual)

### 3.4 Create a Consumer Module

A consumer receives and processes data:

```cpp
// Step 4: Create a consumer module
class TemperatureMonitor : public TempApp::Module<
    commrat::Output<TemperatureReading>,     // Pass through output
    commrat::Input<TemperatureReading>        // Receives TemperatureReading
> {
public:
    TemperatureMonitor(const commrat::ModuleConfig& config)
        : Module(config)
        , count_(0)
    {
        std::cout << "[Monitor] Initialized\n";
    }

protected:
    // Called for each received TemperatureReading
    void process(const TemperatureReading& input, TemperatureReading& output) override {
        count_++;
        
        std::cout << "[Monitor] #" << count_ 
                  << " Sensor " << reading.sensor_id
                  << ": " << reading.temperature_c << "°C, "
                  << reading.humidity_percent << "% humidity\n";
        
        // Check for alerts
        if (reading.temperature_c > 28.0f) {
            std::cout << "  WARNING: High temperature!\n";
        }
        
        return reading;  // Pass through
    }

private:
    uint32_t count_;
};
```

**Key points:**
- Inherits from `TempApp::Module<Output<TemperatureReading>, Input<TemperatureReading>>`
- Overrides `process_continuous(const TemperatureReading&)` 
- Called automatically for **each received message**
- Blocks efficiently (0% CPU when no messages)

### 3.5 Wire It Together

Now create the main function to configure and run both modules:

```cpp
int main() {
    // Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "=== CommRaT Temperature System ===\n\n";
    
    // Step 5: Configure the sensor (producer)
    commrat::ModuleConfig sensor_config{
        .name = "TempSensor",
        .system_id = 10,           // Unique system ID
        .instance_id = 1,          // Instance within system
        .period = commrat::Milliseconds(100) // Generate reading every 100ms (10Hz)
    };
    
    // Step 6: Configure the monitor (consumer)
    commrat::ModuleConfig monitor_config{
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
    
    // Give producer time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    monitor.start();  // Spawns threads, subscribes to sensor
    
    // Run until signal or timeout
    std::cout << "\nRunning... (Press Ctrl+C to stop)\n\n";
    int seconds = 0;
    while (!shutdown_requested.load() && seconds < 5) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        seconds++;
    }
    
    // Step 8: Clean shutdown
    std::cout << "\nShutting down...\n";
    monitor.stop();
    sensor.stop();
    
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
    → Adds monitor's DATA mailbox (base+48) to subscribers_ list
    → Sends SubscribeReply to monitor's WORK mailbox (base+16)

Monitor's work_loop receives SubscribeReply
    → Subscription confirmed
    → Begins blocking on DATA mailbox (base+48)
```

**4. Data Flow (every 100ms):**
```
Sensor's data_thread (timer fires every 100ms)
    → Calls process()
    → User code generates TemperatureReading
    → Module automatically publishes to all subscribers
        → Serializes TemperatureReading
        → Sends from PUBLISH mailbox (base+32) to monitor's DATA mailbox (base+48)

Monitor's continuous_loop (blocked on receive)
    → Receives message on DATA mailbox (base+48)
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
void process(TemperatureReading& output) {  // Missing override
    output = reading;
}
```
**Fix:** Always use `override` for process methods (they're virtual).

**Mistake 2: Wrong output type**
```cpp
// ERROR: Module says Output<TemperatureReading> but returns wrong type
class BadSensor : public TempApp::Module<Output<TemperatureReading>, PeriodicInput> {
    void process(OtherData& output) override {  // Type mismatch!
        output = OtherData{};
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
void process(TemperatureReading& output) override {
    std::this_thread::sleep_for(std::chrono::seconds(1));  // ERROR: Blocks!
    output = reading;
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
    void process(const TemperatureReading& raw, TemperatureReading& output) override {
        // Apply exponential moving average
        filtered_temp_ = 0.7f * filtered_temp_ + 0.3f * raw.temperature_c;
        
        output = TemperatureReading{
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
3. **Producer modules** use `PeriodicInput` and override `process(OutputData& output)`
4. **Consumer modules** use `Input<T>` and override `process(const InputData& input, OutputData& output)`
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
| **Input<T>** | Message received | Data transformation, filtering | Signal processing, fusion |
| **LoopInput** | As fast as possible | Maximum throughput | High-speed data forwarding |

### 4.2 PeriodicInput: Timer-Driven Processing

**When to use:** Generate data at fixed intervals, periodic tasks.

**Signature:**
```cpp
class MyModule : public MyApp::Module<Output<OutputType>, PeriodicInput> {
protected:
    void process(OutputType& output) override {
        // Called every config_.period (e.g., 100ms)
        output = OutputType{ /* ... */ };
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
    void process(HeartbeatMsg& output) override {
        output = HeartbeatMsg{
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

### 4.3 Input<T>: Event-Driven Processing

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
    .period = Duration(0)       // Ignored for Input<T>
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
    void process(const RawTemperature& raw, FilteredTemperature& output) override {
        // Exponential moving average
        float filtered = alpha_ * raw.value_c + (1 - alpha_) * prev_temp_;
        prev_temp_ = filtered;
        
        // Calculate confidence based on rate of change
        float confidence = 1.0f - std::min(std::abs(filtered - raw.value_c) / 10.0f, 1.0f);
        
        output = FilteredTemperature{
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
- **Guaranteed delivery**: Every published message triggers process
- **Timestamp propagation**: Output timestamp = input.header.timestamp (exact propagation)
- **Order preserved**: Messages processed in arrival order

### 4.4 LoopInput: Maximum Throughput Processing

**When to use:** Process data as fast as possible, no timing constraints.

**Signature:**
```cpp
class MyModule : public MyApp::Module<Output<OutputType>, LoopInput> {
protected:
    void process(OutputType& output) override {
        // Called repeatedly as fast as possible
        output = OutputType{ /* ... */ };
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
    void process(DataPacket& output) override {
        DataPacket packet{
            .timestamp = Time::now(),
            .packet_id = packet_id_++
        };
        
        // Generate synthetic waveform
        for (size_t i = 0; i < 100; ++i) {
            packet.samples[i] = std::sin(2 * M_PI * i / 100.0);
        }
        
        output = packet;
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

**WARNING:** LoopInput should be used sparingly. Most applications should use PeriodicInput or Input<T> for predictable behavior and efficient CPU usage.

### 4.5 Output Specifications

Modules can produce zero, one, or multiple outputs:

#### No Output (Monitor/Sink)
```cpp
class DataLogger : public MyApp::Module<Output<void>, Input<SensorData>> {
protected:
    void process(const SensorData& data) override {
        log_to_file(data);
        // No return value - this is a sink
    }
};
```

#### Single Output (Most Common)
```cpp
class Filter : public MyApp::Module<Output<FilteredData>, Input<RawData>> {
protected:
    void process(const RawData& raw, FilteredData& output) override {
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
    void process(const CombinedData& input, DataA& out1, DataB& out2) override {
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
        │        ├─ NO → Use Input<T>
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

**Input<T>:****
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
    void process(IMUData& output) override {
        output = read_imu_hardware();  // Every 10ms
    }
};

// 2. Event-driven filtering (ContinuousInput)
class IMUFilter : public MyApp::Module<Output<FilteredIMU>, Input<IMUData>> {
protected:
    void process(const IMUData& raw, FilteredIMU& output) override {
        output = kalman_filter_.update(raw);
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
    void process(
        const FilteredIMU& imu,
        const GPSData& gps,
        PoseEstimate& output
    ) override {
        output = fuse_sensors(imu, gps);
    }
};

// 4. Sink for logging (ContinuousInput, no output)
class PoseLogger : public MyApp::Module<Output<void>, Input<PoseEstimate>> {
protected:
    void process(const PoseEstimate& pose) override {
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

## 5. I/O Specifications

CommRaT modules are defined by their **input and output specifications**. These determine what data a module produces, consumes, and how it interacts with other modules.

### 5.1 Output Specifications

- **Output<T>**: Module produces a single message type T.
- **Outputs<T, U, ...>**: Module produces multiple message types (multi-output).
- **Output<void>**: Module produces no output (sink/monitor).

**Examples:**
```cpp
class Producer : public MyApp::Module<Output<DataA>, PeriodicInput> { ... };
class Splitter : public MyApp::Module<Outputs<DataA, DataB>, Input<CombinedData>> { ... };
class Logger : public MyApp::Module<Output<void>, Input<DataA>> { ... };
```

### 5.2 Input Specifications

- **PeriodicInput**: No input, generates data periodically.
- **Input<T>**: Receives a single message type T (continuous input).
- **Inputs<T, U, ...>**: Receives multiple message types (multi-input fusion).
- **LoopInput**: No input, runs as fast as possible.

**Examples:**
```cpp
class Sensor : public MyApp::Module<Output<SensorData>, PeriodicInput> { ... };
class Filter : public MyApp::Module<Output<FilteredData>, Input<SensorData>> { ... };
class Fusion : public MyApp::Module<Output<FusedData>, Inputs<IMUData, GPSData>> { ... };
class StressTest : public MyApp::Module<Output<Data>, LoopInput> { ... };
```

### 5.3 Combining Inputs and Outputs

You can combine input and output specs for advanced patterns:
- **Generator**: Output<T>, PeriodicInput
- **Transformer**: Output<T>, Input<U>
- **Sink**: Output<void>, Input<T>
- **Multi-output**: Outputs<T, U>, Input<V>
- **Multi-input fusion**: Output<T>, Inputs<U, V, W>

**Example: Multi-output producer**
```cpp
class MultiProducer : public MyApp::Module<Outputs<DataA, DataB>, PeriodicInput> {
protected:
    void process(DataA& outA, DataB& outB) override {
        outA = generate_a();
        outB = generate_b();
    }
};
```

**Example: Multi-input fusion**
```cpp
class Fusion : public MyApp::Module<Output<FusedData>, Inputs<IMUData, GPSData>, PrimaryInput<IMUData>> {
protected:
    void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
        return fuse(imu, gps);
    }
};
```

### 5.4 Input Metadata and Accessors

For each input, CommRaT provides metadata accessors:
- `get_input_metadata<Index>()` - Get metadata for input at index
- `get_input_metadata<Type>()` - Get metadata for input of unique type
- `get_input_timestamp<Index>()` - Get timestamp for input
- `has_new_data<Index>()` - Check freshness
- `is_input_valid<Index>()` - Check validity

**Example:**
```cpp
uint64_t imu_ts = get_input_timestamp<IMUData>();
bool gps_fresh = has_new_data<1>();
```

### 5.5 Real-Time Safety in I/O Specs

- All input/output specs are validated at compile time
- No dynamic allocation in hot paths
- Type mismatches are compile errors
- Multi-output and multi-input patterns are deterministic

### Key Takeaways

1. **Output/Input specs** define module data flow
2. **Multi-output** and **multi-input** enable advanced pipelines
3. **Metadata accessors** provide timestamps, freshness, validity
4. **Compile-time validation** ensures correctness and real-time safety

---

## 6. Message Flow and Subscription

CommRaT uses a **3-mailbox architecture** with an **automatic subscription protocol** to enable efficient, deterministic message delivery between modules. This section explains how messages flow through the system and how modules discover and connect to each other.

### 6.1 The Three-Mailbox Architecture

Each module has **three separate mailboxes**, each serving a distinct purpose:

```cpp
// Base address format: [data_type_id_low16:16][system_id:8][instance_id:8]
// Example: system_id=10, instance_id=1 → base address varies by primary output type

CMD  mailbox:  base_address + 0    // User commands
WORK mailbox:  base_address + 16   // Subscription protocol
DATA mailbox:  base_address + 32   // Input data streams
```

**Why three mailboxes?**
- **Separation of concerns**: Commands, control, and data don't interfere
- **Real-time predictability**: High-priority data unaffected by control messages
- **Blocking efficiency**: Each mailbox can block independently (0% CPU when idle)

**Mailbox roles:**

| Mailbox | Purpose | Messages | Threading |
|---------|---------|----------|-----------|
| **CMD** | User commands | Custom command types | `command_loop()` thread |
| **WORK** | Subscription control | SubscribeRequest, SubscribeReply, UnsubscribeRequest | `work_loop()` thread |
| **DATA** | Data streaming | User data messages (from subscriptions) | Processed in `data_thread_` |

### 6.2 Hierarchical Addressing

CommRaT uses **hierarchical addressing** to uniquely identify modules and their mailboxes:

**Module identity:**
```cpp
commrat::ModuleConfig config{
    .name = "MySensor",
    .system_id = 10,      // Logical system (e.g., sensor subsystem)
    .instance_id = 1      // Instance within system (e.g., sensor #1)
};
```

**Address calculation:**
```cpp
// Base address incorporates primary output type ID (lower 16 bits)
// Example for Output<TemperatureData> with system_id=10, instance_id=1:
uint16_t type_id_low = get_message_id<TemperatureData>() & 0xFFFF;
uint32_t base = (type_id_low << 16) | (system_id << 8) | instance_id;
// base = (0xABCD << 16) | (10 << 8) | 1 = 0xABCD0A01

uint32_t cmd_mailbox  = base + 0;   // 0xABCD0A01
uint32_t work_mailbox = base + 16;  // 0xABCD0A11
uint32_t data_mailbox = base + 32;  // 0xABCD0A21
```

**Key insight:** The base address encodes the **primary output type**, enabling type-specific message delivery for multi-output producers.

### 6.3 The Subscription Protocol

When a consumer module wants to receive data from a producer, it automatically initiates a **4-step subscription handshake**:

```
Consumer                           Producer
   |                                  |
   |  1. SubscribeRequest             |
   |  (to producer's WORK mailbox)    |
   |--------------------------------->|
   |                                  | 2. Add subscriber
   |                                  |    to list
   |  3. SubscribeReply               |
   |  (to consumer's WORK mailbox)    |
   |<---------------------------------|
   |                                  |
   |  4. Data messages                |
   |  (to consumer's DATA mailbox)    |
   |<---------------------------------|
   |<---------------------------------|
   |<---------------------------------|
```

**Step 1: SubscribeRequest**

Consumer sends subscription request to producer's WORK mailbox:

```cpp
SubscribeRequestPayload request{
    .subscriber_mailbox_id = consumer_data_mailbox,  // Where to send data
    .requested_period_ms = 0  // 0 = as fast as possible
};
```

**Step 2: Producer Adds Subscriber**

Producer's `work_loop()` receives request and adds consumer to subscriber list:

```cpp
void handle_subscribe_request(const SubscribeRequestPayload& req) {
    // Add subscriber (thread-safe)
    add_subscriber(req.subscriber_mailbox_id);
    
    // Send acknowledgment
    SubscribeReplyPayload reply{
        .actual_period_ms = config_.period.count() / 1'000'000,  // Convert ns to ms
        .success = true,
        .error_code = 0
    };
    work_mailbox_.send(reply, /* consumer's WORK mailbox */);
}
```

**Step 3: SubscribeReply**

Consumer receives confirmation on its WORK mailbox:

```cpp
void handle_subscribe_reply(const SubscribeReplyPayload& reply) {
    if (reply.success) {
        std::cout << "Subscription confirmed! Period: " 
                  << reply.actual_period_ms << " ms\n";
    } else {
        std::cerr << "Subscription failed: error " << reply.error_code << "\n";
    }
}
```

**Step 4: Data Delivery**

Producer publishes messages to all subscribers' DATA mailboxes:

```cpp
// In producer's periodic_loop or after process_continuous:
for (uint32_t subscriber : subscribers_) {
    cmd_mailbox_.send(output_message, subscriber);  // Send to DATA mailbox
}
```

### 6.4 Automatic Subscription Setup

**The subscription happens automatically** when you call `module.start()` on a consumer module with `Input<T>` or `Inputs<T, U, V>`:

```cpp
// Configuration specifies source
commrat::ModuleConfig consumer_config{
    .name = "MyConsumer",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,     // Subscribe to system 10
    .source_instance_id = 1     // Instance 1
};

MyConsumer consumer(consumer_config);
consumer.start();  // Automatically sends SubscribeRequest to (10, 1)
```

**What happens in `start()`:**
1. Module spawns three threads: `command_loop()`, `work_loop()`, `data_thread_`
2. `data_thread_` calculates producer's WORK mailbox address
3. Sends `SubscribeRequest` to producer
4. Waits for `SubscribeReply` on WORK mailbox
5. Begins processing incoming data on DATA mailbox

### 6.5 Message Delivery Mechanisms

**Periodic Producer:**

```cpp
class Sensor : public MyApp::Module<Output<SensorData>, PeriodicInput> {
protected:
    void process(SensorData& output) override {
        output = read_sensor();  // Called every config_.period
    }
};
```

- `periodic_loop()` calls `process()` at fixed intervals
- Result wrapped in `TimsMessage<SensorData>` with `timestamp = Time::now()`
- Sent to all subscribers' DATA mailboxes

**Continuous Consumer:**

```cpp
class Filter : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {
protected:
    void process(const SensorData& input, FilteredData& output) override {
        output = apply_filter(input);  // Called for each message
    }
};
```

- `continuous_loop()` blocks on DATA mailbox `receive()`
- Receives `TimsMessage<SensorData>` from producer
- Unwraps payload, calls `process(payload, output)`
- Result wrapped with `timestamp = input.header.timestamp` (exact propagation)
- Sent to filter's subscribers

**Result:** Zero polling, zero CPU usage when idle, deterministic message delivery.

### 6.6 Unsubscription and Cleanup

On shutdown, consumers automatically unsubscribe:

```cpp
consumer.stop();  // Triggers unsubscribe protocol
```

**Unsubscribe steps:**
1. Consumer sends `UnsubscribeRequest` to producer's WORK mailbox
2. Producer removes consumer from subscriber list
3. Producer sends `UnsubscribeReply` to consumer's WORK mailbox
4. Consumer stops threads and closes mailboxes

**Clean shutdown pattern:**

```cpp
// ALWAYS stop in reverse order of start()
std::signal(SIGINT, signal_handler);  // Set up Ctrl+C handler

producer.start();
std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Initialization delay
consumer.start();

// Wait for shutdown signal
while (!shutdown_requested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Stop in reverse order
consumer.stop();   // Unsubscribe first
producer.stop();   // Then stop producer
```

### 6.7 Multi-Output Type-Specific Delivery

For multi-output producers, subscribers receive **only their subscribed type**:

```cpp
class MultiProducer : public MyApp::Module<Outputs<DataA, DataB>, PeriodicInput> {
protected:
    void process(DataA& outA, DataB& outB) override {
        outA = generate_a();
        outB = generate_b();
    }
};
```

**Type filtering:**
- Consumer's base address encodes expected message type (lower 16 bits)
- Producer extracts expected type from subscriber address
- Only sends outputs that match subscriber's expected type

**Example:**

```cpp
// Consumer A expects DataA
commrat::ModuleConfig consumerA_config{
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1
    // Base address will encode DataA's message_id
};

// Consumer B expects DataB (MUST specify source_primary_output_type_id)
commrat::ModuleConfig consumerB_config{
    .system_id = 21,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = MyApp::get_message_id<DataA>()  // Producer's primary type
    // Without this, consumerB would calculate wrong producer address!
};
```

**Why `source_primary_output_type_id`?**
- Multi-output producer's base address uses **first output type** (DataA)
- Consumer B wants DataB but must find producer at DataA's address
- `source_primary_output_type_id` tells consumer which address to use

### 6.8 Error Handling

**Subscription failures:**

```cpp
SubscribeReplyPayload reply{
    .actual_period_ms = 0,
    .success = false,
    .error_code = 1  // 1=max_subscribers, 2=other
};
```

**Common errors:**
- **Max subscribers reached**: Producer has limited subscriber capacity
- **Producer not running**: Consumer sends request before producer starts
- **Type mismatch**: Consumer expects wrong message type (caught at compile time)
- **Address collision**: Two modules with same system_id/instance_id

**Best practices:**
1. Start producers before consumers (with delay)
2. Check `SubscribeReply.success` in critical systems
3. Use unique system_id/instance_id combinations
4. Monitor mailbox errors via error callbacks

### 6.9 Message Flow Summary

**Producer → Consumer Data Flow:**

```
1. Producer generates data (periodic or continuous)
2. Wraps payload in TimsMessage with header (timestamp, seq_number)
3. Serializes using SeRTial (zero-copy when possible)
4. Sends to each subscriber's DATA mailbox (TiMS send)
5. Consumer's DATA mailbox receives (blocking)
6. Deserializes message (zero-copy when possible)
7. Unwraps payload from TimsMessage
8. Calls process(payload, output)
9. Result wrapped and published to consumer's subscribers
```

**Subscription Control Flow:**

```
1. Consumer.start() sends SubscribeRequest to producer's WORK mailbox
2. Producer.work_loop() receives request, adds subscriber
3. Producer sends SubscribeReply to consumer's WORK mailbox
4. Consumer.work_loop() receives reply, confirms subscription
5. On shutdown, consumer sends UnsubscribeRequest
6. Producer removes subscriber, sends UnsubscribeReply
```

### 6.10 Key Takeaways

1. **Three mailboxes** (CMD, WORK, DATA) separate concerns and enable real-time predictability
2. **Hierarchical addressing** (system_id, instance_id) uniquely identifies modules
3. **Automatic subscription** happens on `module.start()` for Input<T> modules
4. **Type-specific delivery** enables multi-output producers with type filtering
5. **Blocking receives** ensure 0% CPU when idle, deterministic message delivery
6. **Clean shutdown** requires reverse-order stop() and unsubscribe protocol

---

## 7. Multi-Input Synchronization

Many real-world applications require **sensor fusion** - combining data from multiple sources with different update rates to produce a unified output. CommRaT's multi-input pattern provides **time-synchronized** data fusion with automatic timestamp alignment.

### 7.1 The Challenge: Asynchronous Sensors

Consider a robotics system with three sensors:

- **IMU**: 100Hz (10ms period) - fast, drives robot control loop
- **GPS**: 5Hz (200ms period) - slow, provides absolute position
- **Lidar**: 20Hz (50ms period) - medium, provides obstacle detection

**Problem:** How do you fuse these asynchronous streams?

**Naive approach (broken):**
```cpp
// DON'T DO THIS - race conditions, temporal misalignment
class BadFusion : public MyApp::Module<Output<FusedData>, Input<IMUData>> {
    GPSData latest_gps_;     // Stale data, no synchronization
    LidarData latest_lidar_; // Stale data, no synchronization
    
    void process(const IMUData& imu, FusedData& output) override {
        // GPS and Lidar timestamps may be from completely different times!
        output = fuse(imu, latest_gps_, latest_lidar_);  // WRONG!
    }
};
```

**CommRaT solution:** Multi-input with automatic time synchronization.

### 7.2 Multi-Input Specification

Use `Inputs<T, U, V>` to declare multiple input types:

```cpp
class SensorFusion : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>,  // Three input types
    PrimaryInput<IMUData>                  // Primary input designation
> {
protected:
    void process(
        const IMUData& imu,      // PRIMARY - blocking receive
        const GPSData& gps,      // SECONDARY - time-synchronized getData
        const LidarData& lidar,  // SECONDARY - time-synchronized getData
        FusedData& output,       // OUTPUT reference - data will be published
    ) override {
        // All inputs guaranteed time-aligned to imu.header.timestamp!
        output = fuse_sensors(imu, gps, lidar);
    }
};
```

**Key concepts:**
- **Primary input** (IMUData): Drives execution rate via blocking receive
- **Secondary inputs** (GPSData, LidarData): Fetched via `getData(timestamp)` to match primary
- **Automatic synchronization**: Module ensures all inputs aligned to primary timestamp

### 7.3 Primary vs Secondary Inputs

**Primary Input:**
- **Blocking receive**: Module blocks until primary message arrives
- **Execution driver**: Sets the fusion rate (e.g., 100Hz for IMU)
- **Timestamp source**: All inputs synchronized to primary's timestamp
- **Designated by**: `PrimaryInput<IMUData>` template parameter

**Secondary Inputs:**
- **Non-blocking getData**: Fetches best-match message from history buffer
- **Time-aligned**: Retrieved based on primary input's timestamp
- **May be stale**: If no recent message within tolerance, returns older data
- **Freshness tracked**: `has_new_data<Index>()` indicates if data is fresh

**Why this design?**
- Fast sensors drive execution (low latency)
- Slow sensors don't block processing (deterministic timing)
- All data temporally coherent (no race conditions)

### 7.4 Configuration for Multi-Input Modules

Multi-input modules require **input_sources** and **sync_tolerance**:

```cpp
commrat::ModuleConfig fusion_config{
    .name = "SensorFusion",
    .system_id = 20,
    .instance_id = 1,
    .period = commrat::Milliseconds(10),  // Primary input rate (100Hz)
    .input_sources = {
        {10, 1},  // IMU sensor (system 10, instance 1) - PRIMARY
        {11, 1},  // GPS sensor (system 11, instance 1) - SECONDARY
        {12, 1}   // Lidar sensor (system 12, instance 1) - SECONDARY
    },
    .sync_tolerance = 50'000'000  // 50ms tolerance (nanoseconds)
};
```

**Configuration fields:**

| Field | Purpose | Example |
|-------|---------|---------|
| `input_sources` | System/instance IDs of input producers | `{{10,1}, {11,1}, {12,1}}` |
| `sync_tolerance` | Max timestamp difference for getData | `50'000'000` (50ms in ns) |
| `period` | Primary input's expected rate | `Milliseconds(10)` (100Hz) |

**Order matters:** First input_source is PRIMARY, rest are SECONDARY (unless PrimaryInput specified).

### 7.5 How getData Synchronization Works

**Under the hood:**

1. **HistoricalMailbox**: Each secondary input has a circular buffer (default: 100 messages)
2. **Automatic buffering**: Every received message stored with timestamp
3. **getData(timestamp, tolerance)**: Finds closest message within tolerance
4. **Best-match algorithm**: Returns message with smallest `|msg.timestamp - requested_timestamp|`

**Example timeline:**

```
Time (ms):  0    50   100  150  200  250  300
IMU:        ●----●----●----●----●----●----●    (100Hz primary)
GPS:        ●---------●---------●---------●    (5Hz secondary)
Lidar:      ●----●----●----●----●----●----●    (20Hz secondary, actually every 50ms)

At t=150ms (IMU arrives):
- Primary: receive() blocks, gets IMU@150ms
- GPS: getData(150ms, 50ms) → returns GPS@200ms (closest within tolerance)
- Lidar: getData(150ms, 50ms) → returns Lidar@150ms (exact match)
```

**Tolerance selection:**
- Too small: Secondary inputs often invalid (getData fails)
- Too large: Temporal misalignment (stale data accepted)
- **Rule of thumb**: 2-3x slowest sensor period (e.g., GPS@5Hz → 50ms tolerance)

### 7.6 Detecting Fresh vs Stale Data

Use metadata accessors to check secondary input freshness:

```cpp
void process(
    const IMUData& imu,
    const GPSData& gps,
    const LidarData& lidar,
    FusedData& output
) override {
    // Check if GPS is fresh (new message since last process_multi_input)
    if (has_new_data<1>()) {  // Index 1 = GPSData
        std::cout << "GPS updated!\n";
    } else {
        std::cout << "GPS stale (reusing old data)\n";
    }
    
    // Check if Lidar getData succeeded
    if (!is_input_valid<2>()) {  // Index 2 = LidarData
        std::cerr << "Lidar getData failed (no data within tolerance)\n";
        // Use fallback or skip Lidar fusion
    }
    
    // Get exact age of GPS data
    auto gps_meta = get_input_metadata<GPSData>();
    uint64_t age_ns = imu_meta.timestamp - gps_meta.timestamp;
    std::cout << "GPS age: " << age_ns / 1'000'000 << " ms\n";
    
    output = fuse_sensors(imu, gps, lidar);
}
```

**Metadata for multi-input:**

| Accessor | Returns | Use Case |
|----------|---------|----------|
| `get_input_metadata<Index>()` | Full metadata struct | Comprehensive input state |
| `get_input_timestamp<Index>()` | uint64_t timestamp | Calculate data age |
| `has_new_data<Index>()` | bool (true if fresh) | Detect sensor updates |
| `is_input_valid<Index>()` | bool (true if getData succeeded) | Handle optional inputs |

### 7.7 Complete Multi-Input Example

**Sensor fusion module:**

```cpp
#include <commrat/commrat.hpp>
#include <commrat/timestamp.hpp>
#include <iostream>

// Define message types (POD structures)
struct IMUData {
    uint64_t timestamp;  // NOTE: This is redundant! Use TimsHeader.timestamp instead
    double accel_x, accel_y, accel_z;
    double gyro_x, gyro_y, gyro_z;
};

struct GPSData {
    uint64_t timestamp;  // NOTE: This is redundant! Use TimsHeader.timestamp instead
    double latitude, longitude, altitude;
};

struct FusedPose {
    uint64_t timestamp;  // NOTE: This is redundant! Use TimsHeader.timestamp instead
    double x, y, z;
    double roll, pitch, yaw;
    float confidence;
};

// Application registry
using FusionApp = commrat::CommRaT<
    commrat::Message::Data<IMUData>,
    commrat::Message::Data<GPSData>,
    commrat::Message::Data<FusedPose>
>;

// Multi-input fusion module
class SensorFusion : public FusionApp::Module<
    commrat::Output<FusedPose>,
    commrat::Inputs<IMUData, GPSData>,
    commrat::PrimaryInput<IMUData>
> {
public:
    explicit SensorFusion(const commrat::ModuleConfig& config)
        : FusionApp::Module<commrat::Output<FusedPose>, commrat::Inputs<IMUData, GPSData>, commrat::PrimaryInput<IMUData>>(config) {
        std::cout << "[Fusion] Initialized\n";
    }

protected:
    void process(
        const IMUData& imu,
        const GPSData& gps,
        FusedPose& output
    ) override {
        // Check GPS freshness
        bool gps_fresh = has_new_data<1>();  // Index 1 = GPSData
        
        // Simple fusion algorithm (Kalman filter would go here)
        FusedPose pose{
            .timestamp = get_input_timestamp<0>(),  // IMU timestamp (primary)
            .x = gps.latitude * 111000.0,  // Rough lat→meters
            .y = gps.longitude * 111000.0,
            .z = gps.altitude,
            .roll = std::atan2(imu.accel_y, imu.accel_z),
            .pitch = std::atan2(-imu.accel_x, std::sqrt(imu.accel_y*imu.accel_y + imu.accel_z*imu.accel_z)),
            .yaw = 0.0,  // Would integrate gyro_z
            .confidence = gps_fresh ? 0.9f : 0.5f  // Lower confidence for stale GPS
        };
        
        std::cout << "[Fusion] Fused pose @ " << pose.timestamp / 1'000'000 << " ms"
                  << " (GPS " << (gps_fresh ? "FRESH" : "STALE") << ")\n";
        
        output = pose;
    }
};
```

**Configuration and main:**

```cpp
int main() {
    std::atomic<bool> shutdown{false};
    std::signal(SIGINT, [](int) { shutdown.store(true); });
    
    // IMU sensor (100Hz primary)
    commrat::ModuleConfig imu_config{
        .name = "IMU",
        .system_id = 10,
        .instance_id = 1,
        .period = commrat::Milliseconds(10)  // 100Hz
    };
    
    // GPS sensor (5Hz secondary)
    commrat::ModuleConfig gps_config{
        .name = "GPS",
        .system_id = 11,
        .instance_id = 1,
        .period = commrat::Milliseconds(200)  // 5Hz
    };
    
    // Fusion module (multi-input)
    commrat::ModuleConfig fusion_config{
        .name = "SensorFusion",
        .system_id = 20,
        .instance_id = 1,
        .period = commrat::Milliseconds(10),  // Match IMU rate
        .input_sources = {
            {10, 1},  // IMU (primary)
            {11, 1}   // GPS (secondary)
        },
        .sync_tolerance = 50'000'000  // 50ms
    };
    
    // Create modules
    IMUSensor imu(imu_config);
    GPSSensor gps(gps_config);
    SensorFusion fusion(fusion_config);
    
    // Start in order: producers first
    imu.start();
    gps.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fusion.start();
    
    // Run until Ctrl+C
    while (!shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop in reverse order
    fusion.stop();
    gps.stop();
    imu.stop();
    
    return 0;
}
```

### 7.8 Advanced Multi-Input Patterns

**Optional secondary inputs:**

```cpp
void process(
    const IMUData& imu,
    const GPSData& gps,
    const MagnetometerData& mag,
    FusedData& output
) override {
    // Use magnetometer only if available
    if (is_input_valid<2>()) {
        output = fuse_with_mag(imu, gps, mag);
    } else {
        output = fuse_without_mag(imu, gps);  // Graceful degradation
    }
}
```

**Adaptive tolerance:**

```cpp
void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
    auto gps_meta = get_input_metadata<GPSData>();
    uint64_t gps_age = get_input_timestamp<IMUData>() - gps_meta.timestamp;
    
    if (gps_age > 100'000'000) {  // > 100ms
        std::cerr << "WARNING: GPS very stale (" << gps_age / 1'000'000 << " ms)\n";
        // Reduce GPS weight in fusion
    }
    
    return weighted_fusion(imu, gps, calculate_weights(gps_age));
}
```

**Three or more inputs:**

```cpp
class MultiSensorFusion : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData, CameraData>,
    PrimaryInput<IMUData>
> {
protected:
    void process(
        const IMUData& imu,
        const GPSData& gps,
        const LidarData& lidar,
        const CameraData& camera,
        FusedData& output
    ) override {
        // All four inputs time-aligned to IMU timestamp
        return fuse_all(imu, gps, lidar, camera);
    }
};
```

### 7.9 Performance Considerations

**HistoricalMailbox overhead:**
- **Buffer size**: Default 100 messages per input (configurable)
- **Memory**: `sizeof(TimsMessage<T>) * 100 * num_secondary_inputs`
- **getData complexity**: O(log N) binary search in circular buffer
- **Real-time safe**: No dynamic allocation after initialization

**Best practices:**
1. **Choose primary wisely**: Use fastest sensor as primary (lowest latency)
2. **Minimize secondaries**: Each secondary input adds getData overhead
3. **Tune buffer size**: Match buffer to expected message rates
4. **Monitor freshness**: Log stale data warnings for debugging
5. **Handle failures**: Check `is_input_valid()` for optional inputs

### 7.10 Multi-Input vs Single-Input Patterns

**When to use multi-input:**
- Sensor fusion with different update rates
- Combining asynchronous data streams
- Time-critical applications requiring synchronization
- Kalman filtering, SLAM, state estimation

**When to use single-input:**
- Simple pipeline (A → B → C)
- Same update rate throughout
- Stateless processing (no fusion needed)
- Lowest latency required (no getData overhead)

**Hybrid pattern:**
```cpp
// Fast path: Single-input for low latency
class FastFilter : public MyApp::Module<Output<FilteredIMU>, Input<IMUData>> { ... };

// Fusion path: Multi-input for comprehensive state
class SlowFusion : public MyApp::Module<Output<FusedState>, Inputs<FilteredIMU, GPSData>> { ... };
```

### 7.11 Key Takeaways

1. **Inputs<T, U, V>** enables multi-input with automatic time synchronization
2. **PrimaryInput<T>** designates which input drives execution (blocking receive)
3. **Secondary inputs** use getData(timestamp, tolerance) for synchronization
4. **HistoricalMailbox** buffers recent messages for temporal queries
5. **Metadata accessors** (`has_new_data<>()`, `is_input_valid<>()`) track freshness and validity
6. **Tolerance tuning** balances data freshness vs synchronization success rate
7. **Real-time safe** with bounded execution time and no dynamic allocation

---

## 8. Timestamp Management

Timestamps are the **single source of truth** for message timing in CommRaT. Every message has exactly one timestamp stored in the **TimsHeader**, enabling precise time synchronization, latency measurement, and temporal reasoning. This section explains how timestamps work and how to use them effectively.

### 8.1 Single Source of Truth: TimsHeader.timestamp

**Core principle:** Every message has **exactly one timestamp** in its header, never in the payload.

```cpp
// CommRaT message structure
template<typename PayloadT>
struct TimsMessage {
    TimsHeader header;  // Contains timestamp (ns since epoch)
    PayloadT payload;   // YOUR data (no timestamp field needed!)
};

// TiMS header structure
struct TimsHeader {
    uint32_t msg_type;      // Message type ID
    uint32_t msg_size;      // Serialized size
    uint64_t timestamp;     // ← SINGLE SOURCE OF TRUTH (nanoseconds since epoch)
    uint32_t sequence_number; // Monotonically increasing counter
    uint32_t flags;         // Reserved
};
```

**Why header-only timestamps?**
- **No duplication**: One timestamp per message, no redundancy
- **Automatic management**: Module sets timestamp, not user code
- **Type agnostic**: Works for any payload type
- **Clean payloads**: User data structures remain simple POD types

**WRONG - Don't do this:**
```cpp
// ❌ DON'T include timestamp in payload!
struct SensorData {
    uint64_t timestamp;  // ← REDUNDANT! Use header.timestamp instead
    float temperature;
    float pressure;
};
```

**RIGHT - Clean payload:**
```cpp
// ✅ DO use clean payload (no timestamp field)
struct SensorData {
    float temperature;
    float pressure;
    // No timestamp - it's in TimsHeader!
};
```

### 8.2 Automatic Timestamp Assignment

CommRaT **automatically assigns timestamps** based on module type:

| Module Type | Timestamp Assignment | Meaning |
|-------------|---------------------|---------|
| **PeriodicInput** | `Time::now()` | Data generation moment |
| **ContinuousInput** | `input.header.timestamp` | Exact propagation from input |
| **Multi-input** | `primary.header.timestamp` | Synchronization point |
| **LoopInput** | `Time::now()` | Data generation moment |

**You never set timestamps manually** - the module does it automatically before sending.

**Example: Periodic producer**

```cpp
class Sensor : public MyApp::Module<Output<SensorData>, PeriodicInput> {
protected:
    void process(SensorData& output) override {
        // Just return payload - no timestamp needed!
        output = SensorData{
            .temperature = read_sensor(),
            .pressure = read_pressure()
        };
        // Module automatically wraps in TimsMessage<SensorData>
        // and sets header.timestamp = Time::now() before sending
    }
};
```

**Example: Continuous transformer**

```cpp
class Filter : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {
protected:
    void process(const SensorData& input, FilteredData& output) override {
        // Input has no timestamp field - it's in header!
        // Just return filtered payload
        output = FilteredData{
            .filtered_value = apply_filter(input.temperature)
        };
        // Module automatically wraps output and sets
        // header.timestamp = input.header.timestamp (exact propagation)
    }
};
```

**Example: Multi-input fusion**

```cpp
class Fusion : public MyApp::Module<Output<FusedData>, Inputs<IMUData, GPSData>, PrimaryInput<IMUData>> {
protected:
    void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
        // Both inputs have clean payloads (no timestamp fields)
        // Just return fused result
        output = FusedData{
            .position = fuse_position(imu, gps),
            .velocity = fuse_velocity(imu, gps)
        };
        // Module automatically sets
        // header.timestamp = primary_input.header.timestamp (IMU)
    }
};
```

### 8.3 Accessing Input Timestamps

Use **metadata accessors** to access input timestamps in your `process()` functions:

**Index-based access (always works):**

```cpp
void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
    // Get full metadata for each input
    auto imu_meta = get_input_metadata<0>();   // Index 0 = first input (IMUData)
    auto gps_meta = get_input_metadata<1>();   // Index 1 = second input (GPSData)
    
    // Access timestamps
    uint64_t imu_ts = imu_meta.timestamp;
    uint64_t gps_ts = gps_meta.timestamp;
    
    // Calculate GPS age
    uint64_t gps_age_ns = imu_ts - gps_ts;
    std::cout << "GPS age: " << gps_age_ns / 1'000'000 << " ms\n";
    
    output = fuse(imu, gps);
}
```

**Type-based access (when types unique):**

```cpp
void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
    // Cleaner syntax when input types are unique
    uint64_t imu_ts = get_input_timestamp<IMUData>();
    uint64_t gps_ts = get_input_timestamp<GPSData>();
    
    // Note: Compile error if duplicate types (e.g., Inputs<IMUData, IMUData>)
    
    output = fuse(imu, gps);
}
```

**Metadata structure:**

```cpp
template<typename T>
struct InputMetadata {
    uint64_t timestamp;       // From TimsHeader (nanoseconds since epoch)
    uint32_t sequence_number; // From TimsHeader (monotonically increasing)
    uint32_t message_id;      // From TimsHeader (message type ID)
    bool is_new_data;         // True if fresh, false if stale/reused
    bool is_valid;            // True if getData succeeded, false if failed
};
```

### 8.4 Timestamp Units and Conversions

CommRaT uses **nanoseconds since epoch** (UNIX timestamp in nanoseconds):

```cpp
// Current time
uint64_t now_ns = Time::now();  // Example: 1707398400000000000 (Feb 8, 2026)

// Convert to milliseconds
uint64_t now_ms = now_ns / 1'000'000;

// Convert to seconds
double now_s = now_ns / 1'000'000'000.0;

// Duration types (type-safe)
Duration ten_ms = Milliseconds(10);        // 10'000'000 nanoseconds
Duration one_sec = Seconds(1);             // 1'000'000'000 nanoseconds
Duration tolerance = Milliseconds(50);     // For getData sync_tolerance
```

**Common operations:**

```cpp
// Calculate message age
uint64_t age_ns = Time::now() - message.header.timestamp;
double age_ms = age_ns / 1'000'000.0;

// Calculate latency (end-to-end)
uint64_t generation_time = sensor_msg.header.timestamp;
uint64_t processing_time = Time::now();
uint64_t latency_ns = processing_time - generation_time;

// Check if message is recent
constexpr uint64_t MAX_AGE_NS = 100'000'000;  // 100ms
if ((Time::now() - msg.header.timestamp) > MAX_AGE_NS) {
    std::cerr << "Message too old!\n";
}
```

### 8.5 Monotonicity and Sequence Numbers

**Monotonicity guarantee:** Timestamps from a single producer are **monotonically increasing**.

```cpp
// Producer guarantees
message_1.header.timestamp <= message_2.header.timestamp  // Always true
message_1.header.seq_number <  message_2.header.seq_number  // Always true (strict)
```

**Sequence numbers:**
- Start at 0 for each module
- Increment by 1 for each sent message
- Enable message loss detection

**Detecting message loss:**

```cpp
class Monitor : public MyApp::Module<Output<void>, Input<SensorData>> {
    uint32_t last_seq_{0};
    
protected:
    void process(const SensorData& input, SensorData& output) override {
        auto meta = get_input_metadata<0>();
        
        // Check for dropped messages
        uint32_t expected_seq = last_seq_ + 1;
        if (meta.sequence_number != expected_seq) {
            uint32_t dropped = meta.sequence_number - expected_seq;
            std::cerr << "WARNING: Dropped " << dropped << " messages!\n";
        }
        
        last_seq_ = meta.sequence_number;
        output = input;  // Pass-through
    }
};
```

**Verifying monotonicity (debugging):**

```cpp
void process(const SensorData& input, FilteredData& output) override {
    static uint64_t last_ts = 0;
    
    uint64_t current_ts = get_input_timestamp<0>();
    
    if (current_ts < last_ts) {
        std::cerr << "ERROR: Non-monotonic timestamp! "
                  << current_ts << " < " << last_ts << "\n";
    }
    
    last_ts = current_ts;
    output = apply_filter(input);
}
```

### 8.6 Time Synchronization Across Modules

**Within single host:** Timestamps use monotonic clock (no drift, immune to NTP updates).

**Across hosts (future):** Time synchronization protocols (PTP, NTP) can be integrated via custom timestamp source.

**Current implementation:**

```cpp
// commrat/platform/timestamp.hpp
class Time {
public:
    static uint64_t now() {
        // Uses std::chrono::steady_clock (monotonic, no jumps)
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
    }
};
```

**For multi-host systems:**
- Use PTP hardware timestamping (IEEE 1588)
- Implement custom `Time::now()` using PTP clock
- Ensure all hosts synchronized to sub-microsecond accuracy

### 8.7 Debugging with Timestamps

**Logging message flow:**

```cpp
void process(const SensorData& input, FilteredData& output) override {
    uint64_t recv_time = Time::now();
    uint64_t msg_time = get_input_timestamp<0>();
    uint64_t latency_us = (recv_time - msg_time) / 1000;
    
    std::cout << "[Filter] Received message @ " << msg_time / 1'000'000 << " ms"
              << " (latency: " << latency_us << " µs)\n";
    
    output = apply_filter(input);
}
```

**Timestamp tracing (full pipeline):**

```cpp
// Sensor (t=0): Generates data
SensorData sensor_output = read_sensor();
// Module sets: header.timestamp = Time::now() = 1000000000

// Filter (t=1ms): Receives and processes
FilteredData filter_output = apply_filter(sensor_output);
// Module sets: header.timestamp = input.header.timestamp = 1000000000

// Monitor (t=2ms): Receives and logs
void process_continuous(const FilteredData& input) {
    uint64_t now = Time::now();  // 1002000000
    uint64_t origin = get_input_timestamp<0>();  // 1000000000
    uint64_t e2e_latency_us = (now - origin) / 1000;  // 2000 µs = 2 ms
    
    std::cout << "End-to-end latency: " << e2e_latency_us << " µs\n";
}
```

**Performance profiling:**

```cpp
class ProfilingFilter : public MyApp::Module<Output<FilteredData>, Input<SensorData>> {
    std::array<uint64_t, 1000> latencies_;
    size_t idx_{0};
    
protected:
    FilteredData process_continuous(const SensorData& input) override {
        uint64_t start = Time::now();
        
        FilteredData result = apply_filter(input);
        
        uint64_t end = Time::now();
        uint64_t processing_time_ns = end - start;
        
        latencies_[idx_++ % 1000] = processing_time_ns;
        
        if (idx_ % 1000 == 0) {
            uint64_t avg = std::accumulate(latencies_.begin(), latencies_.end(), 0ULL) / 1000;
            std::cout << "Avg processing time: " << avg / 1000 << " µs\n";
        }
        
        return result;
    }
};
```

### 8.8 Common Timestamp Pitfalls

**Pitfall 1: Payload timestamps**

```cpp
// ❌ WRONG - duplicated timestamp
struct SensorData {
    uint64_t timestamp;  // Don't do this!
    float temperature;
};

// ✅ RIGHT - clean payload
struct SensorData {
    float temperature;  // TimsHeader.timestamp is enough
};
```

**Pitfall 2: Manual timestamp setting**

```cpp
// TODO: manual timestamp setting should be provided, either as overload or member function
// WRONG - trying to set timestamp manually
void process(SensorData& output) override {
    SensorData data{.temperature = read_sensor()};
    data.timestamp = Time::now();  // NO! Payload has no timestamp field!
    output = data;
}

// RIGHT - automatic timestamp
void process(SensorData& output) override {
    return SensorData{.temperature = read_sensor()};
    // Module sets header.timestamp automatically
}
```

**Pitfall 3: Stale data without checking**

```cpp
// TODO - make synced inputs std::optional<const InType&>!
// RISKY - using GPS without freshness check
void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
    // GPS might be 500ms old!
    output = fuse(imu, gps);
}

// SAFE - check freshness
void process(const IMUData& imu, const GPSData& gps, FusedData& output) override {
    uint64_t gps_age = get_input_timestamp<0>() - get_input_timestamp<1>();
    if (gps_age > 100'000'000) {  // > 100ms
        std::cerr << "GPS stale: " << gps_age / 1'000'000 << " ms\n";
    }
    output = fuse(imu, gps);
}
```

**Pitfall 4: Timezone confusion**

```cpp
// WRONG - assuming local time
uint64_t ts_ns = get_input_timestamp<0>();
time_t ts_s = ts_ns / 1'000'000'000;
struct tm* local = localtime(&ts_s);  // Wrong! Not UTC-aligned

// RIGHT - use duration for intervals
uint64_t start_ts = message1.header.timestamp;
uint64_t end_ts = message2.header.timestamp;
uint64_t duration_ns = end_ts - start_ts;  // Correct interval
```

### 8.9 Best Practices

1. **Never add timestamp fields to payloads** - use `TimsHeader.timestamp` only
2. **Access via metadata accessors** - `get_input_timestamp<>()` or `get_input_metadata<>()`
3. **Check freshness for multi-input** - use `has_new_data<>()` and timestamp age
4. **Monitor sequence numbers** - detect message loss in critical systems
5. **Use nanosecond precision** - CommRaT timestamps are 64-bit nanoseconds
6. **Profile with timestamps** - measure processing latency and end-to-end delays
7. **Verify monotonicity** - add assertions in debug builds

### 8.10 Key Takeaways

1. **Single source of truth**: `TimsHeader.timestamp` is the ONLY timestamp (never in payload)
2. **Automatic assignment**: Module sets timestamp based on input type (periodic/continuous/multi)
3. **Metadata accessors**: `get_input_timestamp<>()`, `get_input_metadata<>()` for timestamp queries
4. **Nanosecond precision**: All timestamps in nanoseconds since epoch (uint64_t)
5. **Monotonicity**: Timestamps from single producer strictly increasing
6. **Sequence numbers**: Enable message loss detection
7. **Freshness tracking**: `has_new_data<>()` and timestamp age for multi-input

---

