# CommRaT Architecture

This document describes the architectural decisions, design principles, and internal implementation details of the CommRaT framework.

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [System Architecture Overview](#system-architecture-overview)
3. [Mailbox System](#mailbox-system)
4. [Module Architecture](#module-architecture)
5. [Threading Model](#threading-model)
6. [Subscription Protocol](#subscription-protocol)
7. [Timestamp Management](#timestamp-management)
8. [Compile-Time Type System](#compile-time-type-system)
9. [Memory Management](#memory-management)
10. [Error Handling](#error-handling)

---

## Design Philosophy

CommRaT is built on three core principles:

### 1. Compile-Time Everything

**If it can be validated at compile time, it MUST be validated at compile time.**

```cpp
// Message IDs computed at compile time
constexpr uint32_t id = MyApp::get_message_id<TemperatureData>();

// Type mismatches caught at compile time
class BadModule : public MyApp::Module<
    Output<DataA>,
    Input<DataB>  // ERROR: DataB not in registry - won't compile
> {};

// Registry lookups resolved at compile time
static_assert(MyApp::is_registered<SensorData>, "Type not registered");
```

**Benefits:**
- Zero runtime overhead for type dispatch
- Impossible to send unregistered types
- Message ID collisions detected at compile time
- No reflection or RTTI needed

### 2. Zero Runtime Overhead

**Real-time systems cannot tolerate unpredictable performance.**

```cpp
// FORBIDDEN in hot paths:
new/delete, malloc/free           // Heap allocation
std::vector::push_back()          // May reallocate
std::string operations            // Dynamic allocation
throw exceptions                  // Unpredictable timing
std::cout in loops                // Blocking I/O

// REQUIRED instead:
std::array<T, N>                  // Stack allocation
sertial::fixed_vector<T, N>       // Bounded capacity
sertial::RingBuffer<T, N>         // Circular buffer
std::optional<T>                  // Error handling
constexpr / consteval             // Compile-time computation
```

**Guarantees:**
- No dynamic allocation in process() functions
- No virtual function calls in hot paths (except process() itself)
- No mutex contention in message processing
- Deterministic execution time

### 3. Simple User API

**Complex machinery hidden behind clean interfaces.**

Users interact with:
- Message types (plain POD structs)
- `Module<OutputSpec, InputSpec>` base class
- Simple `process()` override
- Configuration structs

Users never see:
- Template metaprogramming (SFINAE, concepts, variadic templates)
- Message ID calculation algorithms
- Mailbox address computation
- Subscription protocol details
- Type dispatch mechanisms

---

## System Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                      CommRaT<Types...>                      │
│  (Application-level registry + Module/Mailbox factories)   │
└─────────────────────────────────────────────────────────────┘
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
         ┌──────────────────┐  ┌──────────────────┐
         │  Module Instance │  │  Module Instance │
         │   (Producer)     │  │   (Consumer)     │
         └──────────────────┘  └──────────────────┘
                    │                   │
        ┌───────────┼───────────┐      │
        ▼           ▼           ▼      ▼
    ┌─────┐    ┌─────┐    ┌─────┐  ┌─────┐
    │ CMD │    │WORK │    │ PUB │  │DATA │
    │ mbx │    │ mbx │    │ mbx │  │ mbx │
    └─────┘    └─────┘    └─────┘  └─────┘
        │           │           │       │
        └───────────┴───────────┴───────┘
                    ▼
            ┌───────────────┐
            │ TiMS (RACK)   │
            │ IPC Backend   │
            └───────────────┘
```

### Layered Architecture

**Layer 1: User Code**
- Message type definitions (POD structs)
- Module implementations (override process())
- Configuration and initialization

**Layer 2: CommRaT Framework**
- Module base class with I/O specifications
- MailboxSet per output type
- Subscription protocol automation
- Timestamp management and synchronization

**Layer 3: Type System**
- MessageRegistry (compile-time ID mapping)
- Type traits and concepts
- SFINAE-based processor selection

**Layer 4: TiMS Wrapper**
- C++ RAII wrappers around TiMS C API
- Mailbox abstraction with type safety
- Serialization integration (SeRTial)

**Layer 5: TiMS (RACK)**
- Socket-based IPC
- Message routing
- Blocking receive primitives

---

## Mailbox System

### Mailbox Addressing

Each module has a **base address** calculated from system_id and instance_id:

```cpp
uint32_t base = calculate_base_address(system_id, instance_id);
// base = (system_id << 8) | instance_id
// Range: 0x0000 to 0xFFFF (65536 unique addresses)
```

### Phase 5.3: MailboxSet per Output Type

**Problem:** Multi-output modules need independent subscriptions per output type.

**Solution:** Each output gets its own set of mailboxes:

```cpp
// Module with 2 outputs: TemperatureData, PressureData
class SensorModule : public MyApp::Module<
    Outputs<TemperatureData, PressureData>,
    PeriodicInput
> {};

// Mailbox layout (base = 0x1001):
CMD_0:  0x1001 + 0*16 + 0  = 0x1001  // Temp commands
WORK_0: 0x1001 + 0*16 + 4  = 0x1005  // Temp subscriptions
PUB_0:  0x1001 + 0*16 + 8  = 0x1009  // Temp publish control

CMD_1:  0x1001 + 1*16 + 0  = 0x1011  // Pressure commands
WORK_1: 0x1001 + 1*16 + 4  = 0x1015  // Pressure subscriptions
PUB_1:  0x1001 + 1*16 + 8  = 0x1019  // Pressure publish control

DATA:   0x1001 + 12        = 0x100D  // Shared input mailbox
```

**Formula:**
```cpp
uint32_t cmd_mbx  = base + output_index * 16 + 0;
uint32_t work_mbx = base + output_index * 16 + 4;
uint32_t pub_mbx  = base + output_index * 16 + 8;
uint32_t data_mbx = base + 12;  // Fixed offset, shared
```

### Three-Mailbox Protocol (per output)

**CMD (Command Mailbox)**
- User commands (reset, calibrate, etc.)
- Module lifecycle control
- Administrative messages
- Handled by: `command_loop()` thread

**WORK (Subscription Protocol)**
- Subscription requests from consumers
- Acknowledgments from producer
- Subscription lifecycle management
- Handled by: `work_loop()` thread

**PUB (Publish Control)**
- Future use: publish rate control, filtering
- Currently unused but reserved in address space
- Handled by: (reserved)

**DATA (Input Mailbox)**
- Incoming data messages from subscribed producers
- Shared by all inputs (one DATA mailbox per module)
- Handles all subscribed message types
- Handled by: `data_thread_` thread

### Type-Specific Subscription Filtering

Consumers subscribe to a specific output type from multi-output producers:

```cpp
// Producer configuration (has 2 outputs)
ModuleConfig producer_config{
    .system_id = 10,
    .instance_id = 1,
    // ... outputs: TemperatureData, PressureData
};

// Consumer configuration (wants only TemperatureData)
ModuleConfig temp_consumer_config{
    .name = "TempMonitor",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = MyApp::get_message_id<TemperatureData>()
};

// Result: Consumer subscribes to WORK_0, receives only TemperatureData on its DATA mailbox
```

---

## Module Architecture

### Module Base Class Hierarchy

```cpp
CommRaT<Types...>::Module<OutputSpec, InputSpec, Commands...>
    │
    ├─► MailboxSetManager (per output type)
    │   ├─► CMD mailbox
    │   ├─► WORK mailbox
    │   └─► PUB mailbox (reserved)
    │
    ├─► DATA mailbox (shared for all inputs)
    │
    ├─► ProcessorBase (SFINAE-selected based on I/O spec)
    │   └─► Provides appropriate virtual process(...) signature
    │
    └─► Threads:
        ├─► command_loop()  (CMD mailbox)
        ├─► work_loop()     (WORK mailbox, per output)
        └─► data_thread_    (DATA mailbox or timer)
```

### Processor Selection (SFINAE)

The correct `process()` signature is selected at compile time based on I/O specification:

```cpp
// PeriodicInput + Output<T> → void process(T& output)
class ContinuousProcessorBase<void, T> {
    virtual void process(T& output) = 0;
};

// Input<U> + Output<T> → void process(const U& input, T& output)
class ContinuousProcessorBase<U, T> {
    virtual void process(const U& input, T& output) = 0;
};

// PeriodicInput + Outputs<T, U> → void process(T& out1, U& out2)
class MultiOutputProcessorBase<std::tuple<T, U>, void> {
    virtual void process(T& out1, U& out2) = 0;
};

// Inputs<U, V> + Output<T> → void process(const U& in1, const V& in2, T& output)
class MultiInputProcessorBase<std::tuple<U, V>, T, 2> {
    virtual void process(const U& in1, const V& in2, T& output) = 0;
};
```

User's module inherits from multiple bases, but **only one provides a process() signature** (others are empty).

---

## Threading Model

### Three-Thread Architecture

Each module runs **three concurrent threads**:

**1. Command Thread (`command_loop()`)**
```cpp
void command_loop() {
    while (running_) {
        auto result = mailbox_set_[0].cmd_mailbox.receive_any(visitor);
        // Blocking receive - 0% CPU when idle
        // Visitor dispatches to on_command<T>(const T&)
    }
}
```
- Handles user commands
- Low frequency (typically)
- Runs for each output mailbox set

**2. Subscription Thread (`work_loop()`)**
```cpp
void work_loop(size_t output_index) {
    while (running_) {
        auto result = mailbox_set_[output_index].work_mailbox.receive<SubscriptionRequest>();
        // Consumer wants to subscribe
        handle_subscription(result->message, output_index);
    }
}
```
- Handles subscription protocol
- One thread per output type
- Automatic acknowledgment to consumer

**3. Data Thread (`data_thread_()`)**
```cpp
// Continuous input mode
void data_thread_() {
    while (running_) {
        auto result = data_mailbox_.receive<InputType>();
        // Blocking receive
        process_and_publish(result->message);
    }
}

// Periodic mode
void data_thread_() {
    while (running_) {
        Time::sleep(config_.period);
        process_and_publish();
    }
}

// Loop mode
void data_thread_() {
    while (running_) {
        process_and_publish();  // No sleep, maximum throughput
    }
}
```
- Drives process() execution
- Mode depends on InputSpec

### Zero CPU When Idle

All threads use **blocking receives** on TiMS mailboxes:

```cpp
tims_recvmsg_timed(mbx, buffer, size, timeout=0);  // Block forever
```

**Result:** Module consumes 0% CPU when no messages arrive. No polling, no spin loops.

---

## Subscription Protocol

### Automatic Subscription Flow

When a consumer module starts with `Input<T>`:

```
┌──────────┐                          ┌──────────┐
│ Consumer │                          │ Producer │
│  (Input) │                          │ (Output) │
└──────────┘                          └──────────┘
      │                                     │
      │  1. SubscriptionRequest            │
      │  (to WORK mailbox)                 │
      ├────────────────────────────────────>│
      │                                     │
      │  Contains:                          │
      │  - consumer_data_mailbox            │
      │  - requested_message_type_id        │
      │                                     │
      │                                     │ work_loop() receives
      │                                     │ Validates type
      │                                     │ Adds to subscribers_
      │                                     │
      │  2. SubscriptionAck                 │
      │  (to consumer's DATA mailbox)       │
      │<────────────────────────────────────┤
      │                                     │
      │  Contains:                          │
      │  - success status                   │
      │  - producer info                    │
      │                                     │
      │                                     │
      │  3. Data Messages                   │
      │  (to consumer's DATA mailbox)       │
      │<════════════════════════════════════┤
      │<════════════════════════════════════┤
      │<════════════════════════════════════┤
```

### Subscription Message Types

```cpp
struct SubscriptionRequest {
    uint32_t consumer_data_mailbox;      // Where to send data
    uint32_t requested_message_type_id;  // Which output type to subscribe to
    uint8_t consumer_system_id;
    uint8_t consumer_instance_id;
};

struct SubscriptionAck {
    bool success;
    uint32_t producer_system_id;
    uint32_t producer_instance_id;
    uint32_t message_type_id;
};
```

### Subscriber Tracking

Each `MailboxSet` maintains a list of subscribers per output type:

```cpp
struct SubscriberInfo {
    uint32_t data_mailbox;       // Where to send messages
    uint32_t message_type_id;    // Which type they want
    uint8_t system_id;
    uint8_t instance_id;
};

std::array<
    sertial::fixed_vector<SubscriberInfo, MAX_SUBSCRIBERS>,
    OutputCount
> subscribers_;
```

When publishing, iterate subscribers and send to each DATA mailbox.

---

## Timestamp Management

### Single Source of Truth: TimsHeader.timestamp

**Rule:** ALL timestamps come from `TimsHeader.timestamp`. Payload structs MUST NOT have timestamp fields.

```cpp
// CORRECT: No timestamp in payload
struct SensorData {
    float value;
    int sensor_id;
};

// WRONG: Timestamp in payload creates confusion
struct BadSensorData {
    uint64_t timestamp;  // DON'T DO THIS
    float value;
};
```

### Automatic Timestamp Assignment

**PeriodicInput:** Timestamp assigned at generation
```cpp
void periodic_publish() {
    TimsHeader header;
    header.timestamp = Time::now();  // Current time
    T output{};
    process(output);
    send_with_header(header, output);
}
```

**Input<T>:** Timestamp propagated from input
```cpp
void continuous_publish(const ReceivedMessage<InputType>& received) {
    TimsHeader header = received.header;  // Copy input timestamp
    U output{};
    process(received.message, output);
    send_with_header(header, output);
}
```

**Inputs<T, U, ...>:** Timestamp synchronized to primary input
```cpp
void multi_input_publish(const ReceivedMessage<PrimaryType>& primary) {
    TimsHeader header = primary.header;  // Primary timestamp
    
    // Secondary inputs synchronized via getData(primary.header.timestamp)
    auto secondary = historical_mailbox_.getData<SecondaryType>(
        primary.header.timestamp,
        config_.sync_tolerance_ns
    );
    
    process(primary.message, secondary, output);
    send_with_header(header, output);
}
```

### Phase 6: Multi-Input Synchronization (RACK-style getData)

**Problem:** Fusing sensors with different rates (e.g., 100Hz IMU + 10Hz GPS).

**Solution:** Historical buffering with timestamp-based lookup:

```cpp
class HistoricalMailbox<T> {
    sertial::RingBuffer<TimsMessage<T>, CAPACITY> history_;
    
    std::optional<T> getData(uint64_t target_timestamp, uint64_t tolerance);
};
```

**Algorithm:**
1. Primary input blocks on receive() (drives execution)
2. Secondary inputs use `getData(primary_timestamp, tolerance)`
3. Returns closest message within tolerance window
4. If no match, returns `std::nullopt`

**Metadata tracking:**
```cpp
struct InputMetadata<T> {
    uint64_t timestamp;        // From TimsHeader
    uint32_t sequence_number;
    uint32_t message_id;
    bool is_new_data;          // True if fresh, false if reused
    bool is_valid;             // True if getData succeeded
};
```

---

## Compile-Time Type System

### Message Registry

The MessageRegistry maps types to IDs at compile time:

```cpp
template<typename... MessageTypes>
class MessageRegistry {
    // Compile-time ID calculation using type hashing
    template<typename T>
    static constexpr uint32_t get_message_id() {
        return detail::hash_type_name<T>();
    }
    
    // Collision detection via static_assert
    static_assert(all_ids_unique(), "Message ID collision detected");
};
```

### Type-to-Index Mapping

Find index of type in parameter pack:

```cpp
template<typename T, typename... Ts>
struct type_index;

// Base case: Found at index 0
template<typename T, typename... Ts>
struct type_index<T, T, Ts...> {
    static constexpr size_t value = 0;
};

// Recursive case: Check next type
template<typename T, typename U, typename... Ts>
struct type_index<T, U, Ts...> {
    static constexpr size_t value = 1 + type_index<T, Ts...>::value;
};
```

### Concepts for Type Constraints

```cpp
template<typename T>
concept Registered = requires {
    { MyApp::get_message_id<T>() } -> std::convertible_to<uint32_t>;
};

template<typename T>
concept SerTialSerializable = requires(T t) {
    { sertial::Message<T>::serialize(t) };
};

template<typename T>
concept ValidMessageType = Registered<T> && SerTialSerializable<T>;
```

---

## Memory Management

### Stack Allocation Only

**Hot paths use only stack-allocated containers:**

```cpp
// Process function (called frequently)
void process(const SensorData& input, FilteredData& output) {
    std::array<float, 10> window;           // Stack
    sertial::fixed_vector<float, 50> buf;   // Stack with capacity
    
    // Process without allocations...
    output = compute(input, window, buf);
}
```

### Cold Path Allocations

**Startup and configuration may allocate:**

```cpp
// Module constructor (called once)
Module(const ModuleConfig& config) {
    subscribers_.reserve(MAX_SUBSCRIBERS);  // Allocation OK here
    mailbox_sets_ = /* ... */;              // Allocation OK here
}
```

### Zero-Copy Message Passing

TiMS uses shared memory regions for large messages. CommRaT preserves this:

```cpp
auto result = mailbox.receive<LargeData>();
// result.message is a view into shared memory
// No copy until user explicitly copies
process(result->message);  // Pass by const reference
```

---

## Error Handling

### No Exceptions in Real-Time Paths

```cpp
// WRONG: Exception in hot path
void process(const T& input, U& output) {
    if (invalid(input)) {
        throw std::runtime_error("Invalid input");  // Unpredictable timing!
    }
}

// CORRECT: Return std::optional or error code
std::optional<U> validate_and_process(const T& input) {
    if (invalid(input)) {
        return std::nullopt;
    }
    return compute(input);
}
```

### MailboxResult<T>

TiMS operations return `std::optional`:

```cpp
template<typename T>
using MailboxResult = std::optional<T>;

auto result = mailbox.receive<SensorData>();
if (result) {
    process(result->message);
} else {
    // Receive failed (timeout, mailbox closed, etc.)
}
```

### Compile-Time Validation Preferred

Push errors to compile time whenever possible:

```cpp
// Runtime error (BAD)
if (!is_registered(msg)) {
    return error("Type not registered");
}

// Compile-time error (GOOD)
template<typename T>
    requires is_registered<T>
auto send(const T& msg) { /* ... */ }
```

---

## Performance Characteristics

### Latency

- **Message send:** ~5-10 µs (TiMS + serialization)
- **Blocking receive:** Immediate wakeup (<1 µs kernel scheduling)
- **Type dispatch:** 0 ns (compile-time resolved)
- **Subscription overhead:** One-time setup (~100 µs)

### Memory Footprint

- **Per module:** ~16 KB (mailboxes + thread stacks)
- **Per message:** Header (64 bytes) + payload (user-defined)
- **Historical buffer:** `sizeof(TimsMessage<T>) * CAPACITY`

### Scalability

- **Max modules:** 65536 (16-bit address space)
- **Max message types:** 2^32 (uint32_t message_id)
- **Max outputs per module:** Limited by address space (typically 10-20)
- **Max subscribers per output:** Configurable (default 50)

---

## Trade-Offs and Design Decisions

### Why MailboxSet per Output?

**Alternative considered:** Single WORK mailbox, demux by message type.

**Rejected because:**
- Requires runtime type dispatch in hot path
- Complicates subscription acknowledgment
- No independent rate control per output type

**Chosen approach:**
- Independent mailboxes = independent subscription management
- Clean separation at address level
- Slight address space cost (3 mailboxes per output vs. 1)

### Why Blocking Receives?

**Alternative considered:** Polling (epoll, select, busy-wait).

**Rejected because:**
- Polling wastes CPU even when idle
- Busy-wait impossible in real-time systems
- epoll/select adds complexity and latency

**Chosen approach:**
- Kernel-level blocking (futex-based)
- 0% CPU when idle
- Immediate wakeup on message arrival

### Why TimsHeader.timestamp Only?

**Alternative considered:** Allow payload timestamps.

**Rejected because:**
- Ambiguity: Which timestamp is "correct"?
- Synchronization complexity with multiple timestamps
- Users might forget to set payload timestamp

**Chosen approach:**
- Single source of truth in header
- Framework manages timestamp automatically
- Metadata accessors expose timestamp when needed

---

## Future Architecture Directions

### Phase 7+: Planned Features

1. **Optional Inputs:** Secondary inputs that don't block execution if unavailable
2. **Buffering Strategies:** Sliding window, latest-only, custom policies
3. **ROS 2 Adapter:** Separate repository (rclcpp-commrat)
4. **DDS Compatibility:** Abstract transport layer
5. **Platform Abstraction:** libevl support for true real-time Linux

### Open Questions

See [ROADMAP.md](../ROADMAP.md) for detailed discussion of:

- Input synchronization policy (getData failure modes)
- Output publishing order (sequential vs. parallel)
- Command reply mechanism (one-way vs. request-response)
- Error recovery strategies (restart, degrade, failover)

---

## References

- [API Reference](API_REFERENCE.md) - Complete API documentation
- [User Guide](USER_GUIDE.md) - Framework usage guide
- [Getting Started](GETTING_STARTED.md) - First application tutorial
- [RACK Project](https://github.com/smolorz/RACK) - TiMS messaging system
- [SeRTial](https://github.com/mattih11/SeRTial) - Serialization library
