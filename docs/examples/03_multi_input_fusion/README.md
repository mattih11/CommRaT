# Example 03: Multi-Input Fusion

✅ **STATUS**: Fully functional! This example demonstrates multi-input sensor fusion with time-synchronized getData. Fixed in commit 519469b (tolerance unit conversion bug).

This example demonstrates **multi-input sensor fusion** with **time-synchronized data** from multiple sources at different rates. An IMU sensor running at 100Hz is fused with GPS data at 10Hz using CommRaT's multi-input synchronization.

## What You'll Learn

1. **Multi-input processing**: Using `Inputs<T, U>` to receive multiple message types
2. **Primary input**: Designating which input drives execution with `PrimaryInput<T>`
3. **getData synchronization**: Fetching secondary inputs synchronized to primary timestamp
4. **Metadata accessors**: Checking freshness and validity of synchronized inputs
5. **Different update rates**: Handling sensors at 100Hz and 10Hz simultaneously

## Architecture

```
  ┌──────────┐                    ┌──────────┐
  │   IMU    │  100Hz (primary)   │   GPS    │  10Hz (secondary)
  │ Sensor   │────────┐            │ Sensor   │─────────┐
  └──────────┘        │            └──────────┘         │
                      ▼                                  ▼
                 IMUData                             GPSData
                      │                                  │
                      └────────────┬─────────────────────┘
                                   │
                                   ▼
                          ┌─────────────────┐
                          │  SensorFusion   │  100Hz
                          │  (Multi-Input)  │
                          └────────┬────────┘
                                   │
                                   ▼
                              FusedData
                                   │
                                   ▼
                          ┌─────────────────┐
                          │ FusionMonitor   │
                          └─────────────────┘
```

**Key points:**
- IMU runs at 100Hz (primary input - drives execution)
- GPS runs at 10Hz (secondary input - fetched via getData)
- Fusion outputs at 100Hz (same as primary rate)
- GPS data time-aligned to IMU timestamps within 50ms tolerance

## Message Types

### IMUData (High-Rate Primary)

```cpp
struct IMUData {
    float accel_x, accel_y, accel_z;      // m/s²
    float gyro_x, gyro_y, gyro_z;         // rad/s
};
```

### GPSData (Low-Rate Secondary)

```cpp
struct GPSData {
    double latitude, longitude;            // degrees
    float altitude;                        // meters
    float speed;                           // m/s
};
```

### FusedData (Output)

```cpp
struct FusedData {
    // Position from GPS
    double latitude, longitude;
    float altitude;
    
    // Motion from IMU + GPS
    float velocity_x, velocity_y, velocity_z;
    float acceleration_x, acceleration_y, acceleration_z;
    
    // Quality indicators
    bool gps_fresh;        // GPS data is recent
    uint32_t imu_count;    // Number of IMU samples processed
};
```

## Multi-Input Fusion Module

```cpp
class SensorFusion : public FusionApp::Module<
    commrat::Output<FusedData>,
    commrat::Inputs<IMUData, GPSData>,     // Multiple inputs
    commrat::PrimaryInput<IMUData>         // IMU drives execution
> {
protected:
    // Multi-input process signature: process(const T1&, const T2&, ...)
    FusedData process(const IMUData& imu, const GPSData& gps) override {
        // Access input metadata
        auto imu_meta = get_input_metadata<0>();  // Index-based
        auto gps_meta = get_input_metadata<1>();
        
        // Or type-based (when types unique)
        uint64_t imu_ts = get_input_timestamp<IMUData>();
        uint64_t gps_ts = get_input_timestamp<GPSData>();
        
        // Check GPS freshness
        bool gps_fresh = has_new_data<1>();       // New GPS data?
        bool gps_valid = is_input_valid<1>();     // getData succeeded?
        
        // Calculate GPS age
        uint64_t gps_age_ns = imu_ts - gps_ts;
        double gps_age_ms = gps_age_ns / 1'000'000.0;
        
        // Fuse data (simplified)
        return FusedData{
            .latitude = gps.latitude,
            .longitude = gps.longitude,
            .altitude = gps.altitude,
            .velocity_x = gps.speed * std::cos(0.1f),
            .velocity_y = gps.speed * std::sin(0.1f),
            .velocity_z = 0.0f,
            .acceleration_x = imu.accel_x,
            .acceleration_y = imu.accel_y,
            .acceleration_z = imu.accel_z - 9.81f,  // Remove gravity
            .gps_fresh = gps_fresh,
            .imu_count = imu_count_++
        };
    }
};
```

**Key features:**
- `Inputs<IMUData, GPSData>` declares multiple input types
- `PrimaryInput<IMUData>` specifies IMU drives execution
- `process(const IMUData&, const GPSData&)` receives both inputs
- Metadata accessors check freshness and validity
- GPS data automatically synchronized to IMU timestamp

## Configuration

### Multi-Input Module Config

```cpp
commrat::ModuleConfig fusion_config{
    .name = "SensorFusion",
    .system_id = 20,
    .instance_id = 1,
    .period = commrat::Milliseconds(10),  // 100Hz (IMU rate)
    .input_sources = {
        {10, 1},  // IMU sensor (primary - index 0)
        {11, 1}   // GPS sensor (secondary - index 1)
    },
    .sync_tolerance = commrat::Milliseconds(50)  // 50ms tolerance
};
```

**Critical fields:**
- `period`: Must match primary input rate (10ms = 100Hz)
- `input_sources`: List of (system_id, instance_id) for each input
  - **Order matters**: Index 0 = primary, index 1+ = secondary
- `sync_tolerance`: Maximum age for getData synchronization (50ms)

## How Multi-Input Synchronization Works

### Primary Input (Blocking)

```
1. Fusion blocks on IMU DATA mailbox
2. IMU message arrives (timestamp = T)
3. Fusion receives IMU data
```

### Secondary Input (getData)

```
4. Fusion calls getData<GPSData>(timestamp=T, tolerance=50ms)
5. HistoricalMailbox searches for GPS message where:
   |GPS.timestamp - T| < 50ms
6. Returns closest GPS message (or error if none within tolerance)
```

### Process Execution

```
7. If getData succeeds:
   - process(imu, gps) called with synchronized data
   - Output timestamp = IMU timestamp (primary)
8. If getData fails:
   - Last valid GPS data reused (if available)
   - is_new_data<1>() returns false (stale)
```

## Build and Run

### Build

```bash
mkdir build && cd build
cmake ..
make
```

### Run

```bash
./multi_input_fusion
```

### Expected Output

```
=== CommRaT Example 03: Multi-Input Fusion ===

Creating modules...
[IMU] Initialized (100Hz)
[GPS] Initialized (10Hz) at (37.7749, -122.419)
[Fusion] Initialized with sync_tolerance=100ms
[Monitor] Initialized

Starting sensors...
[IMU] Starting periodic_loop thread...
[IMU] periodic_loop started, period=10ms
[GPS] Starting periodic_loop thread...
[GPS] periodic_loop started, period=100ms

Starting fusion...
[SensorFusion] Sending SubscribeRequest[0] to IMU
[SensorFusion] Sending SubscribeRequest[1] to GPS
[SensorFusion] Starting multi_input_loop thread...
[SensorFusion] multi_input_loop started (2 inputs)

🚗 Running sensor fusion... (Press Ctrl+C to stop)

[Fusion] #100 | IMU: [-0.328401, -0.0130347, 9.75061] m/s² | GPS: (37.7751, -122.419) ✓fresh age=17.7405ms
[Monitor] Fused #100 | Pos: (37.7751, -122.419), alt=101.115m | Vel: [14.9251, 1.4975] m/s | GPS: ✓

[Fusion] #200 | IMU: [0.802381, 0.156789, 9.88897] m/s² | GPS: (37.7752, -122.419) ✓fresh age=36.313ms
[Monitor] Fused #200 | Pos: (37.7752, -122.419), alt=100.869m | Vel: [14.9251, 1.4975] m/s | GPS: ✓

[Fusion] #300 | IMU: [-1.03636, 0.059671, 9.82302] m/s² | GPS: (37.7753, -122.419) ✓fresh age=66.4988ms
[Monitor] Fused #300 | Pos: (37.7753, -122.419), alt=101.886m | Vel: [14.9251, 1.4975] m/s | GPS: ✓

... (continues until Ctrl+C)

[SIGNAL] Ctrl+C received, shutting down...
🛑 Shutting down...
✅ Done!
```

**Note**: GPS age varies (17ms, 36ms, 66ms, 87ms) due to asynchronous rates (100Hz IMU vs 10Hz GPS). All ages are well within the 100ms tolerance.

## Key Observations

1. **Rate mismatch handled**: 100Hz IMU + 10Hz GPS → 100Hz fusion output
2. **Synchronization working**: GPS data time-aligned to IMU timestamps within 100ms
3. **Freshness tracking**: System shows GPS age (17ms, 36ms, 66ms, 87ms) all within tolerance
4. **Successful getData**: Secondary input synchronization fully functional (fixed in commit 519469b)
5. **Metadata rich**: Timestamps, freshness flags, validity flags all accessible

## Common Patterns

### Checking Freshness

```cpp
if (!has_new_data<1>()) {
    std::cout << "GPS data is stale (reused from previous iteration)\n";
}
```

### Handling Stale Data

```cpp
if (!has_new_data<1>()) {
    // GPS stale - use with caution or skip fusion
    if (gps_age_ms > 200.0) {
        std::cerr << "GPS too old (" << gps_age_ms << " ms), skipping fusion\n";
        return last_valid_output_;  // Use cached output
    }
}
```

### Validity Checking

```cpp
if (!is_input_valid<1>()) {
    std::cerr << "GPS getData failed - no data within tolerance\n";
    // Handle missing secondary input
}
```

## Next Steps

- **USER_GUIDE Section 7**: Deep dive into multi-input synchronization
- **USER_GUIDE Section 8**: Timestamp management and metadata
- Try different sync_tolerance values (trade freshness vs success rate)
- Experiment with different sensor rates (e.g., 200Hz IMU, 5Hz GPS)

## Summary

**Multi-input fusion enables:**
- ✅ Synchronizing sensors at different rates
- ✅ Time-aligned data processing
- ✅ Freshness and validity tracking
- ✅ Graceful handling of rate mismatches
- ✅ Compile-time type safety for all inputs

**Key API:**
- `Inputs<T, U, V>` for multiple input types
- `PrimaryInput<T>` to designate execution driver
- `process(const T&, const U&, ...)` for synchronized processing
- `get_input_metadata<>()`, `has_new_data<>()`, `is_input_valid<>()` for metadata access
