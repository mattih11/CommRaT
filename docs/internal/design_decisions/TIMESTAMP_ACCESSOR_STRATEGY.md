# Timestamp Accessor API - Implementation Strategy (REVISED)

## Problem Statement

Modules need access to message metadata (timestamp, sequence number, type) from their inputs, but the current API only provides payloads:

```cpp
// Single input
virtual OutputData process_continuous(const InputData& input);

// Multi-input
virtual OutputData process_multi_input(const Input1&, const Input2&, const Input3&);
```

Users have no way to:
1. Access timestamps for timestamp verification
2. Detect which inputs have new data (multi-input sync)
3. Handle optional/missing secondary inputs
4. Determine data freshness

## Goal

Provide **index-based metadata access** for each input that:
- Works for both single-input and multi-input modules
- Returns structured metadata (not just timestamp)
- Indicates data freshness (new vs. reused)
- Handles optional secondary inputs (sync may fail)
- Zero overhead when unused

## Proposed Solution: Indexed Metadata Accessors ⭐

### API Design

```cpp
// Metadata structure for each input
template<typename T>
struct InputMetadata {
    using DataType = T;              // Compile-time type information
    uint64_t timestamp;              // Nanoseconds since epoch
    uint32_t sequence_number;        // Message sequence (detect drops)
    uint32_t message_id;             // Runtime type ID
    bool is_new_data;                // True if fresh, false if reused/stale
    
    // For multi-input: indicates if getData succeeded
    bool is_valid;                   // False if getData failed (out of tolerance)
};

// Protected accessor methods in Module base class
protected:
    // Single-input modules (index = 0 always)
    template<std::size_t Index = 0>
    const auto& get_input_metadata() const {
        if constexpr (Index == 0) {
            return input_metadata_[0];
        } else {
            static_assert(Index < num_inputs, "Input index out of range");
            return input_metadata_[Index];
        }
    }
    
    // Convenience: Get just timestamp
    template<std::size_t Index = 0>
    uint64_t get_input_timestamp() const {
        return get_input_metadata<Index>().timestamp;
    }
    
    // Convenience: Check if input is fresh
    template<std::size_t Index = 0>
    bool has_new_data() const {
        return get_input_metadata<Index>().is_new_data;
    }
```

### Usage Examples

**Single-Input Module:**
```cpp
class TimestampChecker : public MyApp::Module<NoOutput, Input<SensorData>> {
protected:
    void process_continuous(const SensorData& input) override {
        // Get metadata for the single input (index 0)
        auto meta = get_input_metadata<0>();
        
        assert(meta.timestamp > 0);
        assert(meta.is_new_data);  // Always true for continuous input
        
        std::cout << "Received at " << meta.timestamp << " ns\n";
    }
};
```

**Multi-Input Module:**
```cpp
class FusionModule : public MyApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>  // 3 inputs
> {
protected:
    FusedData process_multi_input(const IMUData& imu,
                                   const GPSData& gps,
                                   const LidarData& lidar) override {
        // Primary input (index 0) - always new
        auto imu_meta = get_input_metadata<0>();
        assert(imu_meta.is_new_data);
        assert(imu_meta.is_valid);
        
        // Secondary inputs (index 1, 2) - may be stale or missing
        auto gps_meta = get_input_metadata<1>();
        auto lidar_meta = get_input_metadata<2>();
        
        if (!gps_meta.is_valid) {
            std::cerr << "GPS getData failed (out of tolerance)\n";
            // Use stale GPS data or skip GPS fusion
        }
        
        if (!lidar_meta.is_new_data) {
            std::cout << "Reusing stale LiDAR data (age: " 
                      << (imu_meta.timestamp - lidar_meta.timestamp) << " ns)\n";
        }
        
        // Output timestamp = primary input timestamp
        return fuse(imu, gps, lidar);
    }
};
```

## Implementation Details

### 1. Storage in Module Base Class

```cpp
template<typename UserRegistry, typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
class Module {
private:
    // Compute number of inputs at compile time
    static constexpr std::size_t num_inputs = []() {
        if constexpr (HasContinuousInput<InputSpec_>) {
            return 1;
        } else if constexpr (HasMultiInput<InputSpec_>) {
            return std::tuple_size_v<typename InputSpec_::InputTypes>;
        } else {
            return 0;  // PeriodicInput/LoopInput have no inputs
        }
    }();
    
    // Fixed-size array for metadata (zero size if no inputs)
    std::array<InputMetadataStorage, num_inputs> input_metadata_;
    
    // Type-erased storage (since each input may be different type)
    struct InputMetadataStorage {
        uint64_t timestamp{0};
        uint32_t sequence_number{0};
        uint32_t message_id{0};
        bool is_new_data{false};
        bool is_valid{true};
        std::type_index type_info{typeid(void)};  // Runtime type info
    };
```

### 2. Update in continuous_loop()

```cpp
void continuous_loop() {
    while (running_) {
        auto result = data_mailbox_.template receive<InputData>();
        if (!result) continue;
        
        // CHANGE: Store metadata before processing
        input_metadata_[0] = {
            .timestamp = result->timestamp,
            .sequence_number = result->seq_number,
            .message_id = result->msg_type,
            .is_new_data = true,  // Always new for continuous input
            .is_valid = true,
            .type_info = typeid(InputData)
        };
        
        auto output = process_continuous_dispatch(result->message);
        // ... publish ...
    }
}
```

### 3. Update in multi_input_loop()

```cpp
void multi_input_loop() {
    while (running_) {
        // Get primary input (index 0)
        auto primary_result = data_mailbox_.template receive<PrimaryInputType>();
        if (!primary_result) continue;
        
        // CHANGE: Store primary metadata
        input_metadata_[0] = {
            .timestamp = primary_result->timestamp,
            .sequence_number = primary_result->seq_number,
            .message_id = primary_result->msg_type,
            .is_new_data = true,  // Primary always new
            .is_valid = true,
            .type_info = typeid(PrimaryInputType)
        };
        
        // Get secondary inputs via getData
        auto [secondary_tuple, sync_status] = gather_all_inputs(primary_result->timestamp);
        
        // CHANGE: Store secondary metadata with sync status
        std::apply([this](auto&... secondary_results) {
            std::size_t index = 1;
            ((update_secondary_metadata<index>(secondary_results, index++)), ...);
        }, secondary_tuple);
        
        // Process with all inputs
        auto output = process_multi_input_dispatch(primary_result->message, secondary_tuple);
        // ... publish ...
    }
}

template<std::size_t Index, typename T>
void update_secondary_metadata(const std::optional<ReceivedMessage<T>>& result, std::size_t& idx) {
    if (result) {
        input_metadata_[idx] = {
            .timestamp = result->timestamp,
            .sequence_number = result->seq_number,
            .message_id = result->msg_type,
            .is_new_data = result->is_fresh,  // Depends on tolerance check
            .is_valid = true,
            .type_info = typeid(T)
        };
    } else {
        // getData failed - mark as invalid
        input_metadata_[idx].is_valid = false;
        // Keep old timestamp/data for reference
    }
}
```

### 4. Public API in Module Base Class

```cpp
protected:
    // ========================================================================
    // Index-Based Access (Always Available)
    // ========================================================================
    
    /**
     * @brief Get metadata for input at compile-time index
     * 
     * @tparam Index Input index (0 for primary/single, 1+ for multi-input secondaries)
     * @return InputMetadata structure with timestamp, freshness, validity
     * 
     * @example Single-input:
     *   auto meta = get_input_metadata<0>();
     * 
     * @example Multi-input:
     *   auto imu_meta = get_input_metadata<0>();   // Primary
     *   auto gps_meta = get_input_metadata<1>();   // Secondary
     */
    template<std::size_t Index = 0>
    auto get_input_metadata() const {
        static_assert(Index < num_inputs, "Input index out of range");
        
        if constexpr (num_inputs == 0) {
            // PeriodicInput/LoopInput - no inputs
            static_assert(Index == 0 && false, "Module has no inputs");
        }
        
        const auto& storage = input_metadata_[Index];
        
        // Return typed metadata (reconstruct from storage)
        // Note: DataType is void here since we lost compile-time type
        return InputMetadata<void>{
            .timestamp = storage.timestamp,
            .sequence_number = storage.sequence_number,
            .message_id = storage.message_id,
            .is_new_data = storage.is_new_data,
            .is_valid = storage.is_valid
        };
    }
    
    /**
     * @brief Convenience: Get timestamp for input at index
     */
    template<std::size_t Index = 0>
    uint64_t get_input_timestamp() const {
        return get_input_metadata<Index>().timestamp;
    }
    
    /**
     * @brief Convenience: Check if input has new data
     * 
     * For continuous input: Always true
     * For multi-input: True if getData returned fresh data within tolerance
     */
    template<std::size_t Index = 0>
    bool has_new_data() const {
        return get_input_metadata<Index>().is_new_data;
    }
    
    /**
     * @brief Convenience: Check if input getData succeeded
     * 
     * For continuous input: Always true
     * For multi-input secondary: False if getData failed (out of tolerance)
     */
    template<std::size_t Index = 0>
    bool is_input_valid() const {
        return get_input_metadata<Index>().is_valid;
    }
    
    // ========================================================================
    // Type-Based Access (Only When Input Types Are Unique)
    // ========================================================================
    
private:
    // Helper: Find index of type T in input tuple
    template<typename T, typename... Types>
    static constexpr std::size_t find_type_index() {
        constexpr std::array<bool, sizeof...(Types)> matches = {
            std::is_same_v<T, Types>...
        };
        
        std::size_t index = 0;
        std::size_t count = 0;
        
        for (std::size_t i = 0; i < sizeof...(Types); ++i) {
            if (matches[i]) {
                index = i;
                count++;
            }
        }
        
        // Static assert if type appears multiple times
        static_assert(count <= 1, "Type appears multiple times in inputs - use index-based access");
        static_assert(count == 1, "Type not found in inputs");
        
        return index;
    }
    
protected:
    /**
     * @brief Get metadata for input by type (only if types are unique)
     * 
     * @tparam T Input data type
     * @return InputMetadata structure
     * 
     * @note Compilation error if:
     *   - Type T not in inputs
     *   - Type T appears multiple times (use index-based access instead)
     * 
     * @example Multi-input with unique types:
     *   auto imu_meta = get_input_metadata<IMUData>();
     *   auto gps_meta = get_input_metadata<GPSData>();
     * 
     * @example FAILS if duplicate types:
     *   Inputs<SensorData, SensorData>  // ERROR at compile time
     */
    template<typename T>
    auto get_input_metadata() const {
        if constexpr (HasMultiInput<InputSpec_>) {
            // Find index of type T in input tuple
            using InputTypes = typename InputSpec_::InputTypes;
            constexpr std::size_t index = []<typename... Types>(std::tuple<Types...>*) {
                return find_type_index<T, Types...>();
            }(static_cast<InputTypes*>(nullptr));
            
            return get_input_metadata<index>();
            
        } else if constexpr (HasContinuousInput<InputSpec_>) {
            // Single input - T must match InputData
            using InputData = typename InputSpec_::DataType;
            static_assert(std::is_same_v<T, InputData>, 
                          "Type mismatch: requested type not in inputs");
            return get_input_metadata<0>();
            
        } else {
            // PeriodicInput/LoopInput - no inputs
            static_assert(false, "Module has no inputs");
        }
    }
    
    /**
     * @brief Get timestamp for input by type
     */
    template<typename T>
    uint64_t get_input_timestamp() const {
        return get_input_metadata<T>().timestamp;
    }
    
    /**
     * @brief Check if input has new data (by type)
     */
    template<typename T>
    bool has_new_data() const {
        return get_input_metadata<T>().is_new_data;
    }
    
    /**
     * @brief Check if input is valid (by type)
     */
    template<typename T>
    bool is_input_valid() const {
        return get_input_metadata<T>().is_valid;
    }
```

## Key Design Decisions

### 1. Index-Based Access (Not "last")
 **Aligned with inputs**: `get_input_metadata<0>()` matches `input0`  
 **Multi-input friendly**: Each input has independent metadata  
 **Compile-time indexed**: Zero runtime cost for index calculation  

### 2. Structured Metadata (Not just timestamp)
 **Extensible**: Easy to add fields (sender_id, priority, etc.)  
 **Complete info**: Type, timestamp, sequence, freshness  
 **Type-safe**: Compile-time DataType for introspection  

### 3. is_new_data Flag
 **Continuous input**: Always `true` (fresh from mailbox)  
 **Multi-input primary**: Always `true` (triggers processing)  
 **Multi-input secondary**: Depends on getData tolerance check  
   - `true` if timestamp within tolerance → fresh data  
   - `false` if reusing stale data from history  

### 4. is_valid Flag
**Continuous input**: Always `true`  
**Multi-input primary**: Always `true`  
**Multi-input secondary**: `false` if getData failed  
   - No data in history within tolerance  
   - Input not yet received  
   - History buffer empty  

### 5. Optional Secondary Inputs

**Current behavior**: Multi-input always waits for ALL inputs before calling process  
**Proposed**: Make secondaries optional via `is_valid` check

```cpp
// User can handle missing inputs
FusedData process_multi_input(const IMUData& imu,
                               const GPSData& gps,
                               const LidarData& lidar) override {
    if (!is_input_valid<1>()) {
        // GPS getData failed - use IMU + LiDAR only
        return fuse_without_gps(imu, lidar);
    }
    
    return fuse_all(imu, gps, lidar);
}
```

**Alternative**: Make getData optional at type level (Phase 7)
```cpp
Inputs<IMUData, Optional<GPSData>, Optional<LidarData>>
// getData won't block if optional input missing
```

## Updated Test Implementation

```cpp
class TimestampChecker : public TestApp::Module<NoOutput, Input<FilteredData>> {
protected:
    void process_continuous(const FilteredData& input) override {
        // INDEX-BASED: Always works
        auto meta = get_input_metadata<0>();
        
        // TYPE-BASED: Cleaner when types unique
        auto meta2 = get_input_metadata<FilteredData>();
        
        // Both return same metadata
        assert(meta.timestamp == meta2.timestamp);
        
        // Verify timestamp
        assert(meta.timestamp > 0);
        assert(meta.is_new_data);  // Always true for continuous
        assert(meta.is_valid);     // Always true for continuous
        
        {
            Lock lock(timestamps_mutex);
            captured_timestamps.push_back(meta.timestamp);
            
            // Verify monotonic
            if (captured_timestamps.size() > 1) {
                assert(meta.timestamp > captured_timestamps[captured_timestamps.size() - 2]);
            }
        }
        
        messages_received++;
        std::cout << "[Checker] ts=" << meta.timestamp 
                  << " seq=" << meta.sequence_number 
                  << " new=" << meta.is_new_data << "\n";
    }
};

class FusionChecker : public TestApp::Module<
    Output<FusedData>,
    Inputs<IMUData, GPSData, LidarData>  // All types unique
> {
protected:
    FusedData process_multi_input(const IMUData& imu,
                                   const GPSData& gps,
                                   const LidarData& lidar) override {
        // ========================================================================
        // OPTION 1: Index-Based Access (Always Works)
        // ========================================================================
        auto imu_meta_idx = get_input_metadata<0>();    // Primary
        auto gps_meta_idx = get_input_metadata<1>();    // Secondary
        auto lidar_meta_idx = get_input_metadata<2>();  // Secondary
        
        // ========================================================================
        // OPTION 2: Type-Based Access (Cleaner When Types Unique)
        // ========================================================================
        auto imu_meta = get_input_metadata<IMUData>();
        auto gps_meta = get_input_metadata<GPSData>();
        auto lidar_meta = get_input_metadata<LidarData>();
        
        // Both approaches return same data
        assert(imu_meta.timestamp == imu_meta_idx.timestamp);
        
        // Verify primary is always fresh
        assert(imu_meta.is_new_data);
        assert(imu_meta.is_valid);
        
        // Check secondary freshness (type-based is cleaner!)
        if (!is_input_valid<GPSData>()) {
            std::cerr << "GPS getData failed\n";
        } else if (!has_new_data<GPSData>()) {
            uint64_t age_ns = imu_meta.timestamp - gps_meta.timestamp;
            std::cout << "GPS stale (age: " << age_ns / 1'000'000 << " ms)\n";
        }
        
        // Convenience methods work with both index and type
        assert(get_input_timestamp<0>() == get_input_timestamp<IMUData>());
        
        return FusedData{/* ... */};
    }
};

// Example where TYPE-BASED doesn't work (duplicate types)
class DuplicateInputModule : public TestApp::Module<
    Output<Result>,
    Inputs<SensorData, SensorData>  // Same type twice!
> {
protected:
    Result process_multi_input(const SensorData& sensor1,
                               const SensorData& sensor2) override {
        // MUST use index-based (type-based won't compile)
        auto meta1 = get_input_metadata<0>();  // Works
        auto meta2 = get_input_metadata<1>();  // Works
        
        // auto meta = get_input_metadata<SensorData>();  // Compile error!
        // Error: "Type appears multiple times in inputs - use index-based access"
        
        return Result{};
    }
};
```

## Implementation Phases

### Phase 1: Core Metadata Storage (1 hour)
1. Add `InputMetadataStorage` struct to Module
2. Add `input_metadata_` array (compile-time sized)
3. Add `num_inputs` constexpr calculation

### Phase 2: Update Loops (1 hour)
1. Update `continuous_loop()` to populate metadata[0]
2. Update `multi_input_loop()` to populate metadata[0] (primary)
3. Add `update_secondary_metadata()` for secondary inputs
4. Update `gather_all_inputs()` to return freshness info

### Phase 3: Public API (30 minutes)
1. Add `get_input_metadata<Index>()` template
2. Add `get_input_timestamp<Index>()` convenience
3. Add `has_new_data<Index>()` convenience
4. Add `is_input_valid<Index>()` convenience

### Phase 4: Test Implementation (1 hour)
1. Update `test_timestamp_logic.cpp` to use new API
2. Add multi-input metadata test
3. Add freshness/validity tests
4. Verify all assertions pass

**Total Time**: ~3.5 hours

## Alternative Names

- `get_input_metadata<I>()` ⭐ **CHOSEN** - Clear, descriptive
- `get_input_header<I>()` - Good alternative (matches TimsHeader)
- `get_input_info<I>()` - Shorter but less specific
- `input_meta<I>()` - Too terse
- `get_metadata<I>()` - Ambiguous (input vs output?)

## Future Extensions

### Phase 7: Optional Inputs
```cpp
// Type-level optional
Inputs<IMUData, Optional<GPSData>>

// getData won't block if no GPS data available
// is_input_valid<1>() returns false when missing
```

### Phase 8: Output Metadata Control
```cpp
// Allow user to set custom timestamp (LoopInput)
void process() override {
    set_output_timestamp(custom_timestamp);
    return SensorData{...};
}
```

### Phase 9: Sender Tracking
```cpp
struct InputMetadata {
    // ... existing fields ...
    uint32_t sender_system_id;
    uint32_t sender_instance_id;
};
```

## Migration Path

**100% Backward Compatible**: New API is purely additive  
**Opt-in**: Modules don't need to use metadata accessors  
**Zero Cost**: Metadata only populated when inputs exist  
**No Breaking Changes**: Existing modules compile unchanged  

---

**Document Status**: Revised Proposal  
**Phase**: 6.10 (Timestamp Architecture)  
**Changes from v1**: Index-based access, structured metadata, freshness/validity flags  
**Date**: 2026-02-08


## Proposed Solutions

### Option 1: Protected Accessor Method ⭐ **RECOMMENDED**

Add a protected method to the Module base class that derived classes can call:

```cpp
protected:
    /**
     * @brief Get timestamp of the last received input message
     * 
     * Only valid for continuous/multi-input modules. Returns 0 for
     * PeriodicInput/LoopInput which have no input messages.
     * 
     * @return Timestamp in nanoseconds since epoch
     */
    uint64_t get_input_timestamp() const {
        return last_received_timestamp_;
    }
```

#### Implementation Details

**1. Add member variable to Module base class** (`registry_module.hpp`):

```cpp
template<typename UserRegistry, typename OutputSpec_, typename InputSpec_, typename... CommandTypes>
class Module {
    // ... existing members ...
    
private:
    uint64_t last_received_timestamp_{0};  // Add this
```

**2. Update `continuous_loop()`** (line ~1237):

```cpp
void continuous_loop() {
    while (running_) {
        auto result = data_mailbox_.template receive<InputData>();
        if (!result) continue;
        
        // CHANGE 1: Store timestamp before processing
        last_received_timestamp_ = result->timestamp;
        
        // Process and publish
        auto output = process_continuous_dispatch(result->message);
        // ... existing publish logic ...
    }
}
```

**3. Update `multi_input_loop()`** (line ~1315):

```cpp
void multi_input_loop() {
    while (running_) {
        // ... existing getData logic ...
        
        if (primary_result) {
            // CHANGE 2: Store primary input timestamp
            last_received_timestamp_ = primary_result->timestamp;
            
            // ... existing processing ...
        }
    }
}
```

**4. Add protected accessor** (in Module public/protected section):

```cpp
protected:
    /**
     * @brief Get timestamp of last received input message
     * 
     * For ContinuousInput modules: timestamp of the message being processed
     * For Multi-input modules: timestamp of the primary input
     * For PeriodicInput/LoopInput: returns 0 (no input messages)
     * 
     * @return Timestamp in nanoseconds since epoch (UTC)
     * 
     * @note Only valid during process_continuous() execution.
     *       Value is updated before each call to process_continuous().
     * 
     * @example
     * FilteredData process_continuous(const SensorData& input) override {
     *     uint64_t input_ts = get_input_timestamp();
     *     assert(input_ts > 0);  // Verify auto-timestamping worked
     *     return apply_filter(input);
     * }
     */
    uint64_t get_input_timestamp() const {
        return last_received_timestamp_;
    }
```

#### Pros
Simple, clean API  
No signature changes  
Backward compatible (100%)  
Zero overhead when unused (inlined getter)  
Easy to understand and document  
✅ Matches C++ best practices (getter methods)  
✅ Only 8 bytes per Module instance  

#### Cons
❌ Extra member variable (8 bytes - negligible)  
❌ Only valid during `process_continuous()` execution  
❌ Cannot access historical timestamps (just last received)  

---

### Option 2: Thread-Local Storage

Use `thread_local` to store current message metadata:

```cpp
namespace commrat::detail {
    struct InputMetadata {
        uint64_t timestamp{0};
        uint32_t sequence_number{0};
        uint32_t message_id{0};
    };
    
    thread_local InputMetadata current_input_metadata;
}

// In continuous_loop:
detail::current_input_metadata = {
    .timestamp = result->timestamp,
    .sequence_number = result->seq_number,
    .message_id = result->msg_type
};
auto output = process_continuous_dispatch(result->message);

// User API:
uint64_t get_input_timestamp() {
    return detail::current_input_metadata.timestamp;
}
```

#### Pros
✅ No member variable overhead  
✅ Works across helper functions  
✅ Can store multiple metadata fields  

#### Cons
❌ Thread-local complexity  
❌ Harder to debug (implicit state)  
❌ Unusual pattern for C++ libraries  
❌ Not obvious when metadata is valid  

---

### Option 3: Optional Metadata Parameter

Add an overloaded `process_continuous()` with metadata:

```cpp
// Default: payload only (backward compatible)
virtual OutputData process_continuous(const InputData& input) {
    // Call metadata version with default metadata
    return process_continuous(input, MessageMetadata{});
}

// Opt-in: with metadata
virtual OutputData process_continuous(const InputData& input, 
                                      const MessageMetadata& meta) {
    // User can override this version to access metadata
    return OutputData{};
}

struct MessageMetadata {
    uint64_t timestamp;
    uint32_t sequence_number;
    uint32_t message_id;
};
```

#### Pros
✅ Explicit, clear API  
✅ Users opt-in by overriding metadata version  
✅ Can pass multiple metadata fields  

#### Cons
❌ Two virtual functions to maintain  
❌ Slight complexity in dispatch logic  
❌ Signature changes (though backward compatible)  
❌ More complex documentation  

---

### Option 4: Return ReceivedMessage Reference

Store the last `ReceivedMessage` and provide a reference:

```cpp
protected:
    template<typename T>
    const ReceivedMessage<T>& get_last_received_message() const {
        return *last_received_message_ptr_;
    }

private:
    // Use variant or type erasure to store pointer
    std::any last_received_message_ptr_;  // or void* with type tag
```

#### Pros
✅ Access to all message metadata  
✅ No data copying  

#### Cons
❌ Lifetime management complexity  
❌ Type safety issues (`std::any` or unsafe casts)  
❌ More complex implementation  
❌ Harder to make thread-safe  

---

## Recommendation: Option 1 ⭐

**Option 1 (Protected Accessor Method)** is the best choice because:

1. **Simplicity**: Single getter method, easy to understand
2. **Safety**: Compile-time type safety, no casts or type erasure
3. **Performance**: Zero overhead when unused (inlined)
4. **Compatibility**: 100% backward compatible
5. **Idioms**: Matches C++ best practices for accessor methods
6. **Extensibility**: Easy to add more accessors later:
   - `get_input_sequence_number()`
   - `get_input_message_id()`
   - `get_input_sender()`

## Implementation Plan

### Phase 1: Core Implementation (30 minutes)

**File**: `include/commrat/registry_module.hpp`

1. **Add member variable** (line ~460, private section):
   ```cpp
   uint64_t last_received_timestamp_{0};
   ```

2. **Update `continuous_loop()`** (line ~1237):
   ```cpp
   last_received_timestamp_ = result->timestamp;
   auto output = process_continuous_dispatch(result->message);
   ```

3. **Update `multi_input_loop()`** (line ~1315):
   ```cpp
   if (primary_result) {
       last_received_timestamp_ = primary_result->timestamp;
       // ... existing logic ...
   }
   ```

4. **Add protected accessor** (line ~550, protected section):
   ```cpp
   protected:
       uint64_t get_input_timestamp() const {
           return last_received_timestamp_;
       }
   ```

### Phase 2: Test Implementation (30 minutes)

**File**: `test/test_timestamp_logic.cpp`

Update `TimestampChecker::process_continuous()`:

```cpp
void process_continuous(const FilteredData& input) override {
    uint64_t timestamp = get_input_timestamp();
    
    {
        Lock lock(timestamps_mutex);
        captured_timestamps.push_back(timestamp);
        
        // Verify non-zero
        assert(timestamp > 0);
        
        // Verify monotonic
        if (captured_timestamps.size() > 1) {
            assert(timestamp > captured_timestamps[captured_timestamps.size() - 2]);
        }
    }
    
    messages_received++;
    std::cout << "[TimestampChecker] Received message, timestamp=" << timestamp << " ns\n";
}
```

Update test assertions:

```cpp
void test_periodic_timestamps() {
    // ... setup ...
    
    Time::sleep(Milliseconds(500));
    
    // Verify captured timestamps
    Lock lock(checker.timestamps_mutex);
    assert(checker.captured_timestamps.size() >= 3);
    
    // Verify spacing (~100ms ± 30ms)
    for (size_t i = 1; i < checker.captured_timestamps.size(); ++i) {
        uint64_t spacing = checker.captured_timestamps[i] - checker.captured_timestamps[i-1];
        assert(spacing > 70'000'000 && spacing < 130'000'000);
    }
}
```

### Phase 3: Documentation (15 minutes)

1. **Update `.github/copilot-instructions.md`**:
   - Add `get_input_timestamp()` to Module API section
   - Document usage patterns and limitations

2. **Create example** in `examples/`:
   ```cpp
   class TimestampLogger : public MyApp::Module<NoOutput, Input<SensorData>> {
   protected:
       void process_continuous(const SensorData& input) override {
           uint64_t ts = get_input_timestamp();
           std::cout << "Received at " << ts << " ns\n";
       }
   };
   ```

3. **Update `docs/README.md`**:
   - Add to Phase 6.10 features
   - Include timestamp accessor API

### Phase 4: Testing (30 minutes)

1. Run `test_timestamp_logic` - verify all assertions pass
2. Run `test_3input_fusion` - ensure no regression
3. Run `test_historical_mailbox` - ensure no regression
4. Performance check: Verify no overhead when accessor unused

**Total Estimated Time**: ~2 hours

## Alternative Names Considered

- `get_input_timestamp()` ⭐ **CHOSEN** - Most descriptive
- `get_last_received_timestamp()` - Too verbose
- `input_timestamp()` - Less clear it's a getter
- `last_timestamp()` - Ambiguous (input vs output?)
- `received_timestamp()` - Good alternative

## Future Extensions

Once the basic accessor is proven, we can add:

```cpp
protected:
    // Sequence number (for detecting dropped messages)
    uint32_t get_input_sequence_number() const;
    
    // Message type ID (for runtime type validation)
    uint32_t get_input_message_id() const;
    
    // Sender identification
    uint32_t get_input_sender_id() const;
    
    // Full metadata struct (advanced users)
    struct InputMetadata {
        uint64_t timestamp;
        uint32_t sequence_number;
        uint32_t message_id;
        uint32_t sender_id;
    };
    InputMetadata get_input_metadata() const;
```

## Testing Strategy

### Unit Tests (`test_timestamp_logic.cpp`)

1. **PeriodicInput Timestamps**:
   - ✅ Verify timestamps are non-zero
   - ✅ Verify monotonically increasing
   - ✅ Verify spacing matches period (±tolerance)

2. **ContinuousInput Propagation**:
   - ✅ Verify output timestamp == input timestamp (EXACT)
   - ✅ Verify through Propagator module chain

3. **Multi-input Synchronization**:
   - ✅ Verify output timestamp == primary input timestamp
   - ✅ Verify with multiple input sources

### Integration Tests

- ✅ `test_3input_fusion`: Implicit timestamp validation (already passing)
- ✅ `test_historical_mailbox`: Timestamp storage and retrieval
- ✅ All existing tests should pass without changes

## Risks & Mitigation

| Risk | Impact | Mitigation |
|------|--------|------------|
| Thread safety | Medium | `last_received_timestamp_` only accessed by data thread |
| Stale timestamps | Low | Only valid during `process_continuous()` - document clearly |
| Memory overhead | Low | 8 bytes per Module (negligible) |
| Performance | Low | Inlined getter, zero overhead when unused |

## Success Criteria

✅ `test_timestamp_logic` passes with full timestamp verification  
✅ All existing tests pass without modification  
✅ No measurable performance regression  
✅ Clear documentation for users  
✅ Zero breaking changes  

## Next Steps

1. **Approve this strategy** - Review and confirm approach
2. **Implement Phase 1** - Core accessor in `registry_module.hpp`
3. **Implement Phase 2** - Complete test verification logic
4. **Run tests** - Verify all assertions pass
5. **Commit Phase 6.10** - "Add timestamp accessor API for test verification"
6. **Phase 6 complete!** - Merge feature branch

---

**Document Status**: Proposal  
**Phase**: 6.10 (Timestamp Architecture)  
**Author**: GitHub Copilot  
**Date**: 2026-02-08  
