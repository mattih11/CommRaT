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
