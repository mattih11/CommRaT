# Multi-Input Synchronization Strategy

**Date**: February 6, 2026  
**Status**: Design Phase  
**Related**: ARCHITECTURE_ANALYSIS.md Phase 5

---

## Overview

This document defines the synchronization strategy for modules with multiple continuous inputs. Instead of asynchronous processing where inputs arrive independently, we implement a **primary-input-driven synchronization** mechanism similar to RACK's approach.

## Core Principle

**One Primary Input + Synchronized Secondary Inputs**

When a module has multiple continuous inputs:
1. **One input is designated as PRIMARY** (the driving input)
2. Module blocks waiting for PRIMARY input to arrive
3. Upon receiving PRIMARY, module immediately fetches **time-synchronized** data for all SECONDARY inputs
4. All inputs are processed together with temporal alignment

This ensures:
- **Temporal consistency**: All inputs represent the same time instant
- **Deterministic execution**: Processing always triggered by primary input
- **Simple threading model**: Single receive loop, no complex synchronization

---

## Design Decision: Single Continuous Input per Module

### Current Constraint (Phase 5 Initial Implementation)

**For Phase 5, we restrict modules to ONE continuous input.**

Rationale:
1. **Simplicity First**: Get single-input working perfectly before adding complexity
2. **Backward Compatibility**: All existing code uses single continuous input
3. **Clear Semantics**: One input = one data mailbox = one subscription

### Future Extension (Phase 6+)

Multi-input will be supported via **synchronized getData pattern**:

```cpp
// Phase 6 syntax (future):
class FusionModule : public Module<Registry,
                                   Output<FusedData>,
                                   Inputs<IMUData, GPSData, LidarData>,  // Multiple inputs
                                   PrimaryInput<IMUData>> {  // Designate primary
protected:
    FusedData process(const IMUData& imu,      // PRIMARY - drives execution
                      const GPSData& gps,       // SECONDARY - fetched at imu.timestamp
                      const LidarData& lidar)   // SECONDARY - fetched at imu.timestamp
        override {
        // All data is time-aligned to imu.timestamp
        return ekf_.update(imu, gps, lidar);
    }
};
```

---

## Synchronization Mechanism: getData(timestamp)

### Concept

Each producer module maintains a **timestamped ring buffer** of recent messages:

```
Producer Side:
┌─────────────────────────────────────────┐
│  Producer Module                        │
│                                         │
│  publish(data) ──> Ring Buffer:        │
│    [t0]: data_0                         │
│    [t1]: data_1  ← Oldest              │
│    [t2]: data_2                         │
│    [t3]: data_3                         │
│    [t4]: data_4  ← Newest              │
│                                         │
│  Subscribers receive t4 via push       │
│  getData(t2) returns interpolated      │
└─────────────────────────────────────────┘
```

Consumer can:
1. **Receive live** (blocking): Get latest published message (push)
2. **Get historical** (non-blocking): Request specific timestamp (pull)

### Ring Buffer Requirements

**Per-Output-Type Ring Buffer:**
- Fixed size (e.g., 100 messages = ~1 second at 100Hz)
- Thread-safe (producer writes, multiple consumers read)
- Sorted by timestamp
- Evicts oldest when full

**Operations:**
```cpp
class HistoricalMailbox {
    // Push new message (producer side)
    void publish(const TimsMessage<T>& msg);
    
    // Blocking receive latest (consumer side - primary input)
    MailboxResult<TimsMessage<T>> receive();
    
    // Non-blocking fetch at timestamp (consumer side - secondary inputs)
    std::optional<TimsMessage<T>> getData(uint64_t timestamp, 
                                          InterpolationMode mode = NEAREST);
};

enum InterpolationMode {
    NEAREST,      // Return closest message by timestamp
    BEFORE,       // Return message at or before timestamp
    AFTER,        // Return message at or after timestamp
    INTERPOLATE   // Linear interpolation (if payload supports it)
};
```

---

## Module Execution Flow with Multi-Input

### Single Primary Input (Current - Phase 5)

```
Module with Input<IMUData>:

┌──────────────────────────────────────┐
│  continuous_loop()                   │
│                                      │
│  while(running) {                    │
│    auto imu = data_mbx_.receive();  │  ← Blocking
│    auto out = process(imu.message); │
│    publish(out);                     │
│  }                                   │
└──────────────────────────────────────┘
```

### Multiple Inputs with Sync (Future - Phase 6)

```
Module with Inputs<IMUData, GPSData, LidarData> + PrimaryInput<IMUData>:

┌────────────────────────────────────────────────────────────┐
│  multi_input_loop()                                        │
│                                                            │
│  while(running) {                                          │
│    // 1. Block on PRIMARY input                           │
│    auto imu = input_mailbox<0>().receive();  ← IMU (PRIMARY)
│                                                            │
│    // 2. Fetch SECONDARY inputs at same timestamp         │
│    auto gps = input_mailbox<1>().getData(imu.timestamp);  │
│    auto lidar = input_mailbox<2>().getData(imu.timestamp);│
│                                                            │
│    // 3. Process synchronized inputs                      │
│    if (gps && lidar) {  // Check all secondaries present  │
│      auto out = process(imu.message,                      │
│                        gps->message,                      │
│                        lidar->message);                   │
│      publish(out);                                        │
│    } else {                                               │
│      log_warning("Secondary input missing");             │
│    }                                                       │
│  }                                                         │
└────────────────────────────────────────────────────────────┘
```

---

## Implementation Requirements

### Phase 5 (Current): Single Input Only

**1. Module Template Constraints**
```cpp
// In registry_module.hpp
template<typename UserRegistry,
         typename OutputSpec,
         typename InputSpec,
         typename... CommandTypes>
class Module {
    // Compile-time validation
    static_assert(
        !is_multi_input_v<InputSpec>,
        "Phase 5: Multiple inputs not yet supported. "
        "Use Input<T> or ContinuousInput<T> for single input."
    );
    
    // ... rest of implementation
};
```

**2. Documentation Updates**
- Update GETTING_STARTED.md: Clarify single input limitation
- Update examples: Show only single-input patterns
- Add note: "Multi-input coming in Phase 6"

### Phase 6 (Future): Multi-Input Support

**1. PrimaryInput Tag Type**
```cpp
// In io_spec.hpp

/**
 * @brief Designates the primary input for multi-input modules
 * 
 * When using Inputs<T1, T2, T3>, exactly one must be marked as primary.
 * The primary input drives execution; secondary inputs are fetched
 * at the primary's timestamp for synchronization.
 * 
 * @tparam T The payload type of the primary input (must be in Inputs<...>)
 */
template<typename T>
struct PrimaryInput {
    using PayloadType = T;
};
```

**2. Historical Mailbox Implementation**
```cpp
// New file: include/commrat/historical_mailbox.hpp

template<typename Registry>
class HistoricalMailbox : public RegistryMailbox<Registry> {
public:
    // Inherited: blocking receive() for primary input
    
    // New: non-blocking timestamped fetch for secondary inputs
    template<typename T>
    std::optional<TimsMessage<T>> getData(
        uint64_t timestamp,
        std::chrono::milliseconds tolerance = std::chrono::milliseconds(50),
        InterpolationMode mode = InterpolationMode::NEAREST
    );
    
private:
    // Ring buffer per message type
    template<typename T>
    struct RingBuffer {
        static constexpr size_t CAPACITY = 100;
        
        std::array<TimsMessage<T>, CAPACITY> buffer_;
        size_t head_{0};
        size_t tail_{0};
        size_t count_{0};
        mutable std::shared_mutex mutex_;
        
        void push(const TimsMessage<T>& msg);
        std::optional<TimsMessage<T>> get_at(uint64_t ts, InterpolationMode mode);
    };
    
    // Tuple of ring buffers (one per message type in registry)
    std::tuple<RingBuffer</* message types */>...> ring_buffers_;
};
```

**3. Multi-Input Module Base Class**
```cpp
// In registry_module.hpp (Phase 6 addition)

template<typename UserRegistry,
         typename OutputSpec,
         typename InputSpec,  // Must be Inputs<T1, T2, ...>
         typename PrimaryInputSpec,  // Must be PrimaryInput<Ti> where Ti in Inputs
         typename... CommandTypes>
class MultiInputModule : public Module<...> {
    static_assert(is_multi_input_v<InputSpec>,
                  "MultiInputModule requires Inputs<Ts...>");
    
    static_assert(/* primary is one of the inputs */,
                  "PrimaryInput type must be one of the Inputs<...> types");
    
protected:
    // Virtual process with all inputs
    virtual OutputType process(const InputTypes&... inputs) = 0;
    
private:
    void multi_input_loop() {
        while (running_) {
            // 1. Receive primary (blocking)
            auto primary = receive_primary_input();
            
            // 2. Fetch secondaries at primary timestamp
            auto secondaries = fetch_secondary_inputs(primary.timestamp);
            
            // 3. Process if all present
            if (all_present(secondaries)) {
                auto output = process_with_expanded_inputs(primary, secondaries);
                publish(output);
            } else {
                handle_missing_inputs(secondaries);
            }
        }
    }
    
    // Tuple of historical mailboxes (one per input type)
    std::tuple<HistoricalMailbox<T1>, 
               HistoricalMailbox<T2>, 
               ...> input_mailboxes_;
};
```

**4. Compile-Time Primary Input Validation**
```cpp
// Helper to extract index of primary input
template<typename PrimaryT, typename InputsTuple>
struct PrimaryInputIndex;

template<typename PrimaryT, typename... Ts>
struct PrimaryInputIndex<PrimaryT, std::tuple<Ts...>> {
    static constexpr size_t value = /* index of PrimaryT in Ts... */;
    static_assert(value < sizeof...(Ts), 
                  "PrimaryInput type not found in Inputs<...>");
};

// Usage in module:
static constexpr size_t PRIMARY_IDX = 
    PrimaryInputIndex<typename PrimaryInputSpec::PayloadType,
                     typename InputSpec::PayloadTypes>::value;
```

---

## Configuration Changes

### Current ModuleConfig (Phase 5)

```cpp
struct ModuleConfig {
    std::string name;
    uint8_t system_id{0};
    uint8_t instance_id{0};
    std::chrono::milliseconds period{100};
    size_t message_slots{10};
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    
    // For ContinuousInput (single source)
    std::optional<uint8_t> source_system_id;
    std::optional<uint8_t> source_instance_id;
};
```

### Future MultiInputConfig (Phase 6)

```cpp
struct InputSource {
    uint8_t system_id;
    uint8_t instance_id;
    std::chrono::milliseconds requested_period{0};
    bool is_primary{false};  // Mark primary input
};

struct MultiInputModuleConfig {
    std::string name;
    uint8_t system_id{0};
    uint8_t instance_id{0};
    size_t message_slots{10};
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    
    // Multiple input sources
    std::vector<InputSource> input_sources;
    
    // Ring buffer configuration
    size_t history_buffer_size{100};  // Messages to keep per input
    std::chrono::milliseconds sync_tolerance{50};  // Max timestamp deviation
};
```

---

## Example Usage (Phase 6 Preview)

### Sensor Fusion with Synchronized Inputs

```cpp
// Message definitions
struct IMUData { uint64_t timestamp; float ax, ay, az, gx, gy, gz; };
struct GPSData { uint64_t timestamp; double lat, lon, alt; };
struct LidarData { uint64_t timestamp; std::array<float, 360> ranges; };
struct FusedPose { uint64_t timestamp; double x, y, z, roll, pitch, yaw; };

using FusionApp = Registry<
    Message::Data<IMUData>,
    Message::Data<GPSData>,
    Message::Data<LidarData>,
    Message::Data<FusedPose>
>;

// Fusion module - IMU drives execution (fastest sensor)
class SensorFusion : public FusionApp::Module<
    Output<FusedPose>,
    Inputs<IMUData, GPSData, LidarData>,
    PrimaryInput<IMUData>  // IMU is primary - runs at 100Hz
> {
protected:
    FusedPose process(const IMUData& imu,      // Always fresh (received)
                      const GPSData& gps,       // Fetched at imu.timestamp
                      const LidarData& lidar)   // Fetched at imu.timestamp
        override {
        
        // All inputs time-aligned to imu.timestamp
        ekf_.predict(imu);
        ekf_.update_gps(gps);
        ekf_.update_lidar(lidar);
        
        return FusedPose{
            .timestamp = imu.timestamp,
            .x = ekf_.state.x,
            .y = ekf_.state.y,
            // ...
        };
    }
    
private:
    ExtendedKalmanFilter ekf_;
};

// Configuration
int main() {
    MultiInputModuleConfig config{
        .name = "SensorFusion",
        .system_id = 1,
        .instance_id = 0,
        .input_sources = {
            {.system_id = 0, .instance_id = 0, .is_primary = true},   // IMU (100Hz)
            {.system_id = 0, .instance_id = 1, .is_primary = false},  // GPS (10Hz)
            {.system_id = 0, .instance_id = 2, .is_primary = false}   // Lidar (20Hz)
        },
        .history_buffer_size = 100,    // 1 second history at 100Hz
        .sync_tolerance = std::chrono::milliseconds(50)  // ±50ms sync window
    };
    
    SensorFusion fusion(config);
    fusion.start();
    
    // Module behavior:
    // - Blocks on IMU (100Hz primary)
    // - When IMU arrives at t=1.234s:
    //   * Fetches GPS at t=1.234s (likely gets t=1.230s, 10Hz rate)
    //   * Fetches Lidar at t=1.234s (likely gets t=1.230s, 20Hz rate)
    //   * Processes all three together
    //   * Publishes FusedPose at t=1.234s
}
```

---

## Advantages of This Approach

### 1. **Temporal Consistency**
- All inputs represent (approximately) the same time instant
- No risk of processing IMU from t=1.0 with GPS from t=2.0
- Critical for sensor fusion, control loops, etc.

### 2. **Deterministic Execution**
- Module always runs at primary input rate
- Predictable latency and throughput
- Easy to reason about timing behavior

### 3. **Efficient Threading**
- Single receive loop (primary input)
- No complex multi-threaded synchronization
- Secondary fetches are non-blocking (quick lookups)

### 4. **Backward Compatible**
- Phase 5: Single input works as before
- Phase 6: Multi-input is opt-in extension
- No breaking changes to existing code

### 5. **RACK Similarity**
- Familiar pattern for RACK users
- Same `getData(timestamp)` semantics
- Easy migration from RACK to CommRaT

---

## Disadvantages & Mitigations

### Disadvantage 1: Memory Overhead

**Issue**: Ring buffer per input type increases memory usage.

**Mitigation**:
- Configurable buffer size (default 100 messages)
- At 100Hz with 100-message buffer = 1 second history (reasonable)
- Can be reduced for slower sensors or increased for faster

**Example**:
```cpp
IMU: 100 messages × 64 bytes = 6.4 KB
GPS: 100 messages × 48 bytes = 4.8 KB
Lidar: 100 messages × 1440 bytes = 140 KB
Total: ~150 KB per multi-input module (acceptable)
```

### Disadvantage 2: Timestamp Skew

**Issue**: Secondary inputs may not have exact timestamp match.

**Mitigation**:
- Configurable tolerance window (e.g., ±50ms)
- Interpolation modes (NEAREST, BEFORE, AFTER, INTERPOLATE)
- Module can detect and handle missing/stale data

**Example**:
```cpp
// IMU arrives at t=1.2345s
auto gps = gps_mailbox.getData(1.2345s, tolerance=50ms);
// Returns GPS at t=1.230s (15ms old, within tolerance)

if (!gps) {
    // GPS too old or missing - skip this iteration
    return std::nullopt;
}
```

### Disadvantage 3: Complexity

**Issue**: More complex than simple async processing.

**Mitigation**:
- Phase 5: Start with single input (simple)
- Phase 6: Add multi-input only when needed
- Clear documentation and examples
- Template machinery hides complexity from users

---

## Implementation Phases

### Phase 5 (Current - Weeks 1-6)

**Goal**: Single input working perfectly

- [x] Phase 5.1: I/O specification types (DONE)
- [ ] Phase 5.2: Module refactoring for single I/O
- [ ] Phase 5.3: Process signature generation
- [ ] Phase 5.4: Testing and examples
- [ ] Phase 5.5: Add static_assert for multi-input rejection

**Deliverable**: Modules with `Output<T>`, `Outputs<Ts...>`, `Input<T>` work perfectly. Multi-input blocked with clear error message.

### Phase 6 (Future - Weeks 7-12)

**Goal**: Multi-input with synchronization

- [ ] Phase 6.1: PrimaryInput<T> tag type
- [ ] Phase 6.2: HistoricalMailbox with ring buffer
- [ ] Phase 6.3: getData(timestamp) implementation
- [ ] Phase 6.4: MultiInputModule base class
- [ ] Phase 6.5: Compile-time primary validation
- [ ] Phase 6.6: Multi-input fusion example
- [ ] Phase 6.7: Interpolation modes
- [ ] Phase 6.8: Performance benchmarking

**Deliverable**: Modules with `Inputs<T1, T2, T3>` + `PrimaryInput<Ti>` work with time-synchronized processing.

---

## Open Questions

### Q1: What if primary input stops publishing?

**Issue**: Module blocks forever if primary producer fails.

**Options**:
- **A**: Timeout on receive (e.g., 1 second) - log warning and continue
- **B**: Heartbeat monitoring - detect stale producer
- **C**: Fallback to secondary input as primary

**Recommendation**: Option A with configurable timeout.

### Q2: What if secondary input is faster than primary?

**Issue**: GPS at 100Hz, IMU at 10Hz - ring buffer fills quickly.

**Options**:
- **A**: Larger buffer (store 10 seconds instead of 1)
- **B**: Downsample secondary (keep every 10th message)
- **C**: Swap primary/secondary (make GPS primary)

**Recommendation**: Option C - always make fastest sensor primary.

### Q3: What about timestamp clock skew?

**Issue**: IMU and GPS use different clocks, timestamps don't align.

**Options**:
- **A**: Require all sensors use same clock (NTP, PTP)
- **B**: Timestamp synchronization/calibration module
- **C**: Relative timestamps (offset from start)

**Recommendation**: Option A initially, Option B for advanced use cases.

### Q4: Should we support NO primary (all equal)?

**Issue**: Some applications want asynchronous processing.

**Options**:
- **A**: Force primary designation (current design)
- **B**: Allow `NoPrimary` - process on any input arrival

**Recommendation**: Option A for Phase 6, Option B for Phase 7 if needed.

---

## Conclusion

The **primary-input-driven synchronization** strategy provides:

✅ **Temporal consistency** - All inputs time-aligned  
✅ **Deterministic execution** - Predictable timing  
✅ **Simple threading** - Single receive loop  
✅ **RACK compatibility** - Familiar pattern  
✅ **Incremental adoption** - Phase 5 single input, Phase 6 multi-input

**Next Steps**:
1. Complete Phase 5 with single-input support
2. Add `static_assert` blocking multi-input with helpful message
3. Document limitation in GETTING_STARTED.md
4. Begin Phase 6 design and prototyping when Phase 5 is stable

**Target Timeline**:
- Phase 5 complete: March 15, 2026
- Phase 6 complete: May 15, 2026

---

## References

- **RACK getData()**: Similar historical data fetch mechanism
- **ARCHITECTURE_ANALYSIS.md**: Overall Phase 5-7 roadmap
- **io_spec.hpp**: I/O specification types (Phase 5.1)
- **registry_module.hpp**: Module base class implementation

