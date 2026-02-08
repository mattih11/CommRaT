# RACK getData Mechanism Analysis

**Date**: February 6, 2026  
**Purpose**: Understand RACK's historical data retrieval and apply lessons to CommRaT Phase 6  
**Related**: IO_SYNC_STRATEGY.md, ARCHITECTURE_ANALYSIS.md Phase 5-6

---

## Executive Summary

RACK implements **historical data retrieval** through:
1. **Ring buffer** in producer module (fixed-size circular buffer)
2. **MSG_GET_DATA command** - consumer requests data at specific timestamp
3. **Timestamp-based lookup** - finds closest data to requested time
4. **Synchronous reply** - producer sends data back on command mailbox

**Key Insight**: RACK doesn't need explicit ring buffer management in consumer - the **producer maintains history** and serves requests on-demand.

---

## RACK Architecture Overview

### Producer Side: RackDataModule

```
┌────────────────────────────────────────────────────────┐
│  RackDataModule (Producer)                             │
│                                                        │
│  dataBuffer[] = Ring buffer of recent messages        │
│    [0]: {pData, dataSize, timestamp}                  │
│    [1]: {pData, dataSize, timestamp}                  │
│    [2]: {pData, dataSize, timestamp}  ← index         │
│    ...                                                 │
│    [19]: {pData, dataSize, timestamp}                 │
│                                                        │
│  index: Current write position (newest)               │
│  globalDataCount: Total messages published            │
│  dataBufferMaxEntries: Ring buffer size (e.g., 20)    │
│                                                        │
│  moduleLoop() {                                       │
│    process();                                         │
│    putDataBufferWorkSpace(data, size);  ← Store      │
│      → index = (index + 1) % maxEntries              │
│      → Overwrites oldest when full                   │
│      → Sends to all continuous listeners             │
│  }                                                     │
│                                                        │
│  moduleCommand(MSG_GET_DATA) {                        │
│    timestamp = msg.recordingTime;                     │
│    idx = getDataBufferIndex(timestamp);  ← Lookup    │
│    sendDataReply(dataBuffer[idx]);                    │
│  }                                                     │
└────────────────────────────────────────────────────────┘
```

### Consumer Side: RackDataProxy

```
┌────────────────────────────────────────────────────────┐
│  RackDataProxy (Consumer)                              │
│                                                        │
│  getData(timestamp) {                                  │
│    send: MSG_GET_DATA {recordingTime = timestamp}     │
│    recv: MSG_DATA {data buffer}                       │
│    return: 0 on success, error code otherwise         │
│  }                                                     │
│                                                        │
│  getNextData() {                                       │
│    send: MSG_GET_NEXT_DATA                            │
│    recv: MSG_DATA {latest data}                       │
│  }                                                     │
│                                                        │
│  getContData(periodTime, dataMbx) {                    │
│    send: MSG_GET_CONT_DATA {periodTime, dataMbxAdr}   │
│    recv: MSG_CONT_DATA {actualPeriodTime}             │
│    → Producer adds to listener list                   │
│    → Producer pushes data to dataMbx periodically     │
│  }                                                     │
└────────────────────────────────────────────────────────┘
```

---

## Key Components

### 1. Ring Buffer Structure (Producer)

```cpp
// From rack_data_module.cpp
struct DataBufferEntry {
    void* pData;           // Pointer to message buffer
    uint32_t dataSize;     // Size of current message
};

DataBufferEntry* dataBuffer;  // Array[dataBufferMaxEntries]
uint32_t index;               // Current write position (newest)
uint32_t globalDataCount;     // Total messages published
uint32_t dataBufferMaxEntries; // Ring buffer capacity (e.g., 20)
```

**Behavior:**
- **Circular**: `index = (index + 1) % dataBufferMaxEntries`
- **Overwrites oldest**: When full, oldest entry is lost
- **No allocation**: Fixed-size array allocated at init
- **Thread-safe**: `bufferMtx` protects read/write

### 2. Timestamp Lookup Algorithm

```cpp
// From getDataBufferIndex(rack_time_t time)
int RackDataModule::getDataBufferIndex(rack_time_t time) {
    if (time == 0) {
        // Return newest (current index)
        return index;
    }
    
    // Find oldest still in buffer
    old_index = (index + 2) % dataBufferMaxEntries;
    old_rectime = getRecordingTime(dataBuffer[old_index].pData);
    
    // Validate time range
    if (time > new_rectime + 2 * period) {
        return -EINVAL;  // Too new
    }
    if (time < old_rectime) {
        return -EINVAL;  // Too old
    }
    
    // Linear search from oldest to newest
    // Find entry with minimum |timestamp - requested_time|
    minTimeDiff = RACK_TIME_MAX;
    while (n--) {
        timeDiff = abs(current_timestamp - time);
        if (timeDiff <= minTimeDiff) {
            minTimeDiff = timeDiff;
            best_index = current_index;
        } else {
            break;  // Passed minimum, stop
        }
        current_index = (current_index + 1) % maxEntries;
    }
    
    return best_index;
}
```

**Key Properties:**
- **Nearest neighbor**: Returns closest timestamp (before or after)
- **Early exit**: Stops when timeDiff starts increasing
- **Validates range**: Rejects timestamps outside buffer window
- **O(N) complexity**: Linear search (N = buffer size, typically 20)

### 3. Message Flow for getData

```
Consumer                     Producer
   │                            │
   │  MSG_GET_DATA              │
   │  {recordingTime = t}       │
   ├───────────────────────────>│
   │                            │  getDataBufferIndex(t)
   │                            │  → Find best match in ring buffer
   │                            │
   │       MSG_DATA             │
   │  {data at timestamp ≈ t}  │
   │<───────────────────────────┤
   │                            │
```

**Synchronous RPC:**
- Consumer **blocks** waiting for reply
- Timeout: 100ms default (`dataTimeout`)
- Reply sent to **consumer's workMbx** (not data mailbox)

### 4. Continuous Data vs. Historical Fetch

RACK supports **two modes**:

#### **Push Mode (Continuous Data)**
```cpp
// Consumer subscribes
proxy.getContData(periodTime, dataMbx);

// Producer behavior
putDataBufferWorkSpace(data) {
    // ... store in ring buffer ...
    
    // Push to all listeners
    for (listener in listeners) {
        if (should_send(listener)) {
            sendDataMsgReply(MSG_DATA, listener.dataMbx, data);
        }
    }
}
```

- **Automatic**: Producer pushes every N cycles
- **Efficient**: No per-message request overhead
- **Used for**: Primary input (high-rate, always needed)

#### **Pull Mode (Historical Fetch)**
```cpp
// Consumer pulls on-demand
proxy.getData(timestamp);

// Producer behavior
moduleCommand(MSG_GET_DATA) {
    timestamp = parse(msg);
    idx = getDataBufferIndex(timestamp);
    sendDataReply(dataBuffer[idx]);
}
```

- **On-demand**: Only fetches when needed
- **Synchronous**: Blocks until reply
- **Used for**: Secondary inputs (lower-rate, sync to primary)

---

## Application to CommRaT

### Current CommRaT (Phase 4)

```
Producer                     Consumer
   │                            │
   │  SubscribeRequest          │
   │<───────────────────────────┤
   │                            │
   │  SubscribeReply            │
   ├───────────────────────────>│
   │                            │
   │  ContinuousInput (push)    │
   ├───────────────────────────>│  Blocking receive
   │  ContinuousInput (push)    │
   ├───────────────────────────>│  Blocking receive
   │  ContinuousInput (push)    │
   ├───────────────────────────>│  Blocking receive
```

**Problem**: Only push mode, no historical fetch.

### Proposed CommRaT (Phase 6)

#### **Add GetDataRequest/Reply Messages**

```cpp
// In messages.hpp
struct GetDataRequest {
    uint64_t timestamp;          // Requested timestamp
    uint8_t source_system_id;    // Which producer
    uint8_t source_instance_id;
};

struct GetDataReply {
    uint64_t timestamp;          // Actual timestamp of returned data
    int32_t error_code;          // 0 = success, -EINVAL = out of range
    // Followed by: actual data message
};

using GetDataRequestMsg = Message::Command<GetDataRequest>;
using GetDataReplyMsg = Message::Reply<GetDataReply>;
```

#### **Add Ring Buffer to Module Base**

**Note**: Ring buffer implementation uses SeRTial's `RingBuffer<T, MaxSize>` for realtime safety.

```cpp
// In registry_module.hpp
#include <sertial/ring_buffer.hpp>
#include "commrat/threading.hpp"
#include "commrat/timestamp.hpp"

template<typename OutputDataT, size_t Capacity = 100>
class OutputRingBuffer {
public:
    OutputRingBuffer() = default;
    
    // Producer: store new message (realtime-safe)
    void push(const TimsMessage<OutputDataT>& msg) {
        WriteLocked(mutex_) {
            buffer_.push_back(msg);  // SeRTial RingBuffer::push_back (no allocation)
        }
    }
    
    // Consumer: fetch at timestamp (realtime-safe)
    std::optional<TimsMessage<OutputDataT>> get(Timestamp timestamp) {
        ReadLocked(mutex_) {
            if (buffer_.empty()) {
                return std::nullopt;
            }
            
            // Find closest timestamp (RACK-style linear search)
            size_t best_idx = buffer_.size() - 1;  // Start with newest
            Timestamp min_diff = UINT64_MAX;
            
            // Iterate from oldest to newest
            for (size_t i = 0; i < buffer_.size(); ++i) {
                Timestamp diff = Time::diff(buffer_[i].timestamp, timestamp);
                
                if (diff <= min_diff) {
                    min_diff = diff;
                    best_idx = i;
                } else {
                    break;  // Passed minimum (timestamps increasing)
                }
            }
            
            return buffer_[best_idx];
        }
    }
    
    // Get newest entry
    std::optional<TimsMessage<OutputDataT>> get_latest() {
        ReadLocked(mutex_) {
            if (buffer_.empty()) {
                return std::nullopt;
            }
            return buffer_.back();
        }
    }
    
    // Check if buffer has data
    bool empty() const {
        ReadLocked(mutex_) {
            return buffer_.empty();
        }
    }
    
    // Get current size
    size_t size() const {
        ReadLocked(mutex_) {
            return buffer_.size();
        }
    }
    
private:
    // SeRTial's realtime-safe ring buffer (fixed capacity, no allocation)
    sertial::RingBuffer<TimsMessage<OutputDataT>, Capacity> buffer_;
    mutable SharedMutex mutex_;  // R/W lock for concurrent access
};
```

#### **Module with Historical Data Support**

```cpp
// In registry_module.hpp
template<typename UserRegistry, typename OutputDataT, typename InputModeT>
class Module {
protected:
    // Existing publish() - also stores in ring buffer
    void publish(const OutputDataT& data) {
        TimsMessage<OutputDataT> msg = create_message(data);
        
        // Store in ring buffer for historical fetch
        if constexpr (supports_historical_fetch) {
            output_ring_buffer_.push(msg);
        }
        
        // Push to all subscribers (existing behavior)
        for (auto& subscriber : subscribers_) {
            send_to_subscriber(msg, subscriber);
        }
    }
    
private:
    // New: handle GetDataRequest on WORK mailbox
    void handle_get_data_request(const GetDataRequest& req) {
        auto result = output_ring_buffer_.get(req.timestamp);
        
        if (result) {
            // Send GetDataReply + data
            GetDataReply reply{
                .timestamp = result->timestamp,
                .error_code = 0
            };
            send_reply(reply, result->payload);
        } else {
            // Send error reply
            GetDataReply reply{
                .timestamp = 0,
                .error_code = -ENODATA
            };
            send_reply(reply);
        }
    }
    
    OutputRingBuffer<OutputDataT> output_ring_buffer_{100};
};
```

### Multi-Input Synchronization (Phase 6)

```cpp
// Sensor fusion example
class FusionModule : public Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>,
    PrimaryInput<IMUData>
> {
protected:
    FusedData process(const IMUData& imu,
                      const GPSData& gps,
                      const LidarData& lidar) override {
        // All time-aligned
        return fuse(imu, gps, lidar);
    }
    
private:
    void multi_input_loop() {
        while (running_) {
            // 1. Receive PRIMARY (blocking push)
            auto imu = primary_mailbox_.receive();
            
            // 2. Fetch SECONDARY (synchronous pull via GetDataRequest)
            auto gps = fetch_historical(gps_source_, imu.timestamp);
            auto lidar = fetch_historical(lidar_source_, imu.timestamp);
            
            // 3. Process synchronized
            if (gps && lidar) {
                auto output = process(imu.message, gps->message, lidar->message);
                publish(output);
            }
        }
    }
    
    // Helper: send GetDataRequest, block for reply
    std::optional<TimsMessage<T>> fetch_historical(Source source, uint64_t ts) {
        GetDataRequest req{
            .timestamp = ts,
            .source_system_id = source.system_id,
            .source_instance_id = source.instance_id
        };
        
        // Send to source's WORK mailbox
        work_mailbox_.send(req, source.work_mailbox_id);
        
        // Receive reply (blocking, 50ms timeout)
        auto reply = work_mailbox_.receive_timed<GetDataReply>(50ms);
        
        if (reply && reply->error_code == 0) {
            // Reply contains data
            return reply->data;
        }
        return std::nullopt;
    }
};
```

---

## Comparison: RACK vs. CommRaT

| Aspect | RACK | CommRaT (Proposed) |
|--------|------|-------------------|
| **Ring Buffer Location** | Producer only | Producer only |
| **Buffer Size** | Configurable (default 20) | Configurable (default 100) |
| **Lookup Algorithm** | Linear search O(N) | Linear search O(N) (same) |
| **Request Message** | MSG_GET_DATA | GetDataRequest |
| **Reply Message** | MSG_DATA | GetDataReply + data |
| **Mailbox Used** | Command mailbox | WORK mailbox |
| **Timeout** | 100ms default | 50ms default |
| **Continuous Push** | MSG_GET_CONT_DATA | SubscribeRequest (existing) |
| **Thread Safety** | Mutex | std::shared_mutex (R/W lock) |

---

## Implementation Plan for CommRaT Phase 6

### 6.1: Add Historical Data Messages

**Files**: `include/commrat/messages.hpp`

```cpp
struct GetDataRequest {
    uint64_t timestamp;
    uint8_t source_system_id;
    uint8_t source_instance_id;
};

struct GetDataReply {
    uint64_t actual_timestamp;
    int32_t error_code;  // 0 = OK, -ENODATA, -EINVAL
};

using GetDataRequestMsg = Message::Command<GetDataRequest>;
using GetDataReplyMsg = Message::Reply<GetDataReply>;
```

### 6.2: Implement OutputRingBuffer

**Dependency**: Use SeRTial's `RingBuffer<T, MaxSize>` for realtime-safe storage

**New File**: `include/commrat/ring_buffer.hpp`

```cpp
#include <sertial/ring_buffer.hpp>  // From SeRTial library

template<typename T, size_t Capacity = 100>
class OutputRingBuffer {
    // RACK-style ring buffer using SeRTial's realtime-safe container
    // - Fixed capacity (no allocation)
    // - Circular indexing
    // - Timestamp-based lookup
    // - Thread-safe (shared_mutex)
    
private:
    sertial::RingBuffer<TimsMessage<T>, Capacity> buffer_;
    // ... rest of implementation
};
```

### 6.3: Integrate into Module Base

**Files**: `include/commrat/registry_module.hpp`

```cpp
template<typename OutputSpec>
class Module {
    // Add ring buffer for each output type
    // Add handle_get_data_request() to work_loop()
    // Modify publish() to also store in ring buffer
};
```

### 6.4: Multi-Input Module Implementation

**Files**: `include/commrat/registry_module.hpp`

```cpp
template<typename Inputs, typename PrimaryInput>
class MultiInputModule : public Module {
    // Primary: blocking receive (push)
    // Secondary: synchronous fetch_historical (pull)
    // Process with all inputs time-aligned
};
```

---

## Advantages of RACK's Approach

✅ **Producer-side buffering**: Consumer doesn't need storage  
✅ **Simple protocol**: One request, one reply  
✅ **Bounded latency**: Fixed buffer size = predictable search time  
✅ **No memory growth**: Ring buffer never grows  
✅ **Works across processes**: Mailbox-based, no shared memory  

## Potential Improvements for CommRaT

### 1. Use R/W Lock Instead of Mutex

```cpp
// RACK uses mutex (exclusive lock for read and write)
mutex.lock();
data = buffer[index];
mutex.unlock();

// CommRaT can use shared_mutex (multiple readers, single writer)
std::shared_lock read_lock(mutex_);  // Multiple readers OK
data = buffer[index];

std::unique_lock write_lock(mutex_);  // Exclusive write
buffer[index] = new_data;
```

**Benefit**: Multiple consumers can fetch historical data simultaneously.

### 2. Binary Search If Timestamps Monotonic

```cpp
// RACK: Linear search O(N)
for (i = 0; i < buffer_size; i++) { /* check timestamp */ }

// CommRaT: Binary search O(log N) if timestamps are sorted
// (Only works if timestamps strictly increasing - not guaranteed with multi-source)
```

**Consideration**: Timestamps may not be strictly monotonic if sources have clock skew. Linear search is safer.

### 3. Configurable Interpolation Modes

```cpp
enum class FetchMode {
    NEAREST,       // RACK default - closest timestamp
    BEFORE,        // Latest data before requested time
    AFTER,         // Earliest data after requested time
    INTERPOLATE    // Linear interpolation (if payload supports it)
};

std::optional<T> get(uint64_t timestamp, FetchMode mode = NEAREST);
```

**Benefit**: More control over synchronization behavior.

### 4. Return Tolerance in Reply

```cpp
struct GetDataReply {
    uint64_t actual_timestamp;
    int32_t error_code;
    uint64_t time_difference;  // |actual - requested|
};
```

**Benefit**: Consumer can decide if tolerance is acceptable.

---

## Open Questions

### Q1: Should getData block or timeout?

**RACK**: Synchronous with timeout (100ms)  
**CommRaT Options**:
- A) Same as RACK - synchronous with timeout
- B) Async callback when data ready
- C) Non-blocking with `std::optional` return

**Recommendation**: Option A (RACK-style) for Phase 6 - simplest, proven design.

### Q2: What if producer is slower than primary consumer?

**Scenario**: IMU at 100Hz (primary), GPS at 10Hz (secondary)

**RACK Behavior**: 
- GPS ring buffer updates at 10Hz
- When IMU requests at 100Hz, gets same GPS data 10 times
- Consumer receives "stale" GPS (acceptable for sensor fusion)

**CommRaT**: Same behavior - fetch returns best available.

### Q3: Should ring buffer be per-output-type or per-module?

**Option A**: One ring buffer per module (all outputs share)  
**Option B**: One ring buffer per output type  

**Recommendation**: Option B - matches multi-output design where each output has independent subscribers.

---

## Real-World Example: scan2d_dyn_obj_recog

**RACK module that uses BOTH continuous data AND getData:**

```cpp
// Module has TWO inputs:
// 1. scan2d (PRIMARY) - continuous push
// 2. objRecog (SECONDARY) - historical fetch

int Scan2dDynObjRecog::moduleLoop(void) {
    // 1. RECEIVE PRIMARY INPUT (blocking - continuous data)
    ret = dataMbx.peekTimed(timeout, &msgInfo);
    scan2dIn = Scan2dData::parse(&msgInfo);
    
    // 2. FETCH SECONDARY INPUT at primary's timestamp
    ret = objRecog->getData(&objRecogMsg.data, 
                           sizeof(obj_recog_data_msg), 
                           scan2dIn->recordingTime);  // ← sync to scan2d time!
    
    // 3. PROCESS SYNCHRONIZED INPUTS
    classifyDynamic(scan2dOut, &objRecogMsg.data, vMin);
    
    // 4. PUBLISH OUTPUT
    putDataBufferWorkSpace(scan2dOut);
    return 0;
}
```

**This is EXACTLY the pattern we need for CommRaT Phase 6!**

**Key observations:**
- Primary input (scan2d): **Blocking receive** on dataMbx - pushed by producer
- Secondary input (objRecog): **Synchronous getData** - pulled at primary's timestamp
- Both inputs **time-aligned** via `scan2dIn->recordingTime`
- Simple, clean, works perfectly

**Setup (moduleOn):**
```cpp
int Scan2dDynObjRecog::moduleOn(void) {
    // Turn on both producers
    scan2d->on();
    objRecog->on();
    
    // Subscribe to PRIMARY input (continuous push)
    ret = scan2d->getContData(0, &dataMbx, &dataBufferPeriodTime);
    
    // No subscription needed for SECONDARY (pull on-demand)
    
    return RackDataModule::moduleOn();
}
```

**This validates:**
✅ Primary input: Continuous subscription (push)  
✅ Secondary input: No subscription, just getData when needed  
✅ Synchronization: Use primary's timestamp for secondary fetch  
✅ Simple threading: Single receive loop on primary  

---

## Summary

### RACK's getData = Producer-Side Ring Buffer + Request/Reply Protocol

**Producer Side:**
1. Maintain fixed-size ring buffer of recent messages
2. On MSG_GET_DATA, search buffer for closest timestamp
3. Reply with matching data or error

**Consumer Side:**
1. Send MSG_GET_DATA with requested timestamp
2. Block waiting for reply (timeout 100ms)
3. Receive data at ≈requested time

### CommRaT Phase 6 Should:

1. ✅ **Copy RACK's ring buffer design** - proven, simple, efficient
2. ✅ **Add GetDataRequest/Reply messages** - similar to RACK protocol
3. ✅ **Integrate into Module base** - automatic for all producers
4. ✅ **Use for multi-input sync** - primary push + secondary pull
5. ✅ **Keep buffer producer-side** - consumer stays lightweight

### What We DON'T Need:

**CommRaT Files to Modify (Phase 6):**
- `include/commrat/messages.hpp` - Add GetDataRequest/Reply
- `include/commrat/ring_buffer.hpp` - NEW - OutputRingBuffer (wraps SeRTial's RingBuffer)
- `include/commrat/registry_module.hpp` - Integrate ring buffer + multi-input

**SeRTial Dependency (Phase 5.5):**
- `SeRTial/include/sertial/ring_buffer.hpp` - NEW - Realtime-safe RingBuffer<T, MaxSize>
  - Fixed-capacity circular buffer (no dynamic allocation)
  - Stack-allocated or embedded storage
  - Full/overwrite behavior (like RACK)
  - Iterator support
  - Constexpr-friendly

### Next Steps:

1. **Phase 5 current priority**: Single-input modules with compile-time I/O specs
2. **Phase 6 preparation**: Design GetDataRequest messages and OutputRingBuffer
3. **Phase 6 implementation**: Multi-input modules with RACK-style synchronization

---

## Code References

**RACK Files Analyzed:**
- `rack/main/common/rack_proxy.cpp` - getData() implementation
- `rack/main/common/rack_data_module.cpp` - Ring buffer and lookup
- `rack/main/common/rack_mailbox.cpp` - Message send/receive

**CommRaT Files to Modify (Phase 6):**
- `include/commrat/messages.hpp` - Add GetDataRequest/Reply
- `include/commrat/ring_buffer.hpp` - NEW - OutputRingBuffer implementation
- `include/commrat/registry_module.hpp` - Integrate ring buffer + multi-input

---

## CommRaT Utility Classes (New)

To support RACK-style patterns and prepare for future realtime enhancements:

### 1. Timestamp Abstraction (`timestamp.hpp`)

**Purpose**: Unified timestamp interface, future-proof for realtime clocks

```cpp
using namespace commrat;

// Get current time
Timestamp now = Time::now();

// Time arithmetic
Timestamp timeout = now + Time::milliseconds_to_ns(100);

// Check tolerance
bool in_sync = Time::is_within_tolerance(ts1, ts2, 50_ms);

// Convert units
auto ms = Time::ns_to_milliseconds(timestamp);

// Future: Switch to CLOCK_MONOTONIC without changing code
Time::set_clock_source(Time::ClockSource::MONOTONIC_CLOCK);
```

**Benefits:**
- ✅ Consistent timestamp type across codebase
- ✅ Easy unit conversions
- ✅ Prepare for realtime clock sources (CLOCK_MONOTONIC, PTP)
- ✅ No code changes when switching clock sources

### 2. Threading Abstraction (`threading.hpp`)

**Purpose**: Thread and mutex wrappers, future-proof for realtime threads

```cpp
using namespace commrat;

// Create thread with priority
ThreadConfig config{
    .name = "data_loop",
    .priority = ThreadPriority::HIGH,
    .policy = SchedulingPolicy::FIFO,
    .cpu_affinity = 2  // Pin to CPU 2
};
Thread worker(config, []{
    // High-priority work
});

// Scoped locking
Mutex mtx;
Synchronized(mtx) {
    // Critical section
}

// Reader-writer lock (for ring buffers)
SharedMutex buffer_mtx;
ReadLocked(buffer_mtx) {
    // Multiple readers OK
    data = buffer[idx];
}
WriteLocked(buffer_mtx) {
    // Exclusive writer
    buffer[idx] = new_data;
}
```

**Benefits:**
- ✅ Named threads (easier debugging)
- ✅ Thread priorities and affinity
- ✅ Prepare for SCHED_FIFO realtime threads
- ✅ Reader-writer locks (better than mutex for ring buffers)
- ✅ Clean RAII-based locking

### 3. Integration with Module Base

```cpp
// Example: Use in Module class
class Module {
private:
    void data_thread_function() {
        while (running_) {
            // Get timestamp with unified API
            Timestamp now = Time::now();
            
            // Process with scoped locking
            WriteLocked(output_mutex_) {
                output_ring_buffer_.push(data);
            }
            
            // Sleep until next period
            Time::sleep(config_.period);
        }
    }
    
    // Thread with configuration
    Thread data_thread_{
        ThreadConfig{
            .name = "data_loop",
            .priority = ThreadPriority::REALTIME,
            .policy = SchedulingPolicy::FIFO
        },
        [this]{ data_thread_function(); }
    };
    
    SharedMutex output_mutex_;  // R/W lock for ring buffer
};
```

