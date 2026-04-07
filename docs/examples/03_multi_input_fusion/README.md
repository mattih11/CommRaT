# Example 03: Multi-Input Fusion

Multi-input sensor fusion with time-synchronized get_data. An IMU sensor at 100Hz is fused with GPS data at 10Hz using CommRaT's multi-input synchronization.

## What You'll Learn

1. Multi-input processing using `Input<T>` and `SyncedInput<T>`
2. Primary input drives execution; secondary uses get_data synchronization
3. `Synced<T>` wrapper for fresh/stale/invalid handling
4. Handling sensors at different update rates

## Architecture

```
  IMU Sensor (100Hz)          GPS Sensor (10Hz)
       |                           |
   IMUData (primary)          GPSData (secondary)
       |                           |
       +----------+----------------+
                  |
           SensorFusion (100Hz)
                  |
             FusedData
                  |
           FusionMonitor
```

- IMU at 100Hz is the primary input (drives execution)
- GPS at 10Hz is the secondary input (time-synchronized via get_data)
- Fusion outputs at 100Hz (same as primary rate)

## Module Definition

```cpp
class SensorFusion : public FusionApp::Module2<
    Output<FusedData>,
    Input<IMUData>,          // Primary (continuous, drives execution)
    SyncedInput<GPSData>     // Secondary (get_data synchronized)
> {
protected:
    void process(const IMUData& imu, Synced<GPSData> gps, FusedData& output) override {
        // Synced<T> forces explicit freshness handling
        if (gps) {
            // Fresh GPS data (exact timestamp match)
            output = fuse_sensors(imu, gps.value());
        } else if (gps.is_valid()) {
            // Stale but usable GPS data
            output = fuse_sensors(imu, gps.stale());
        } else {
            // No GPS data available
            output = dead_reckoning(imu);
        }
    }
};
```

**Key points:**
- `Input<IMUData>` blocks on receive -- drives execution at 100Hz
- `SyncedInput<GPSData>` fetches GPS data synchronized to IMU timestamp
- `Synced<T>` provides `value()` (fresh), `stale()` (any valid), `value_or()`/`stale_or()` helpers

## How Multi-Input Synchronization Works

1. Fusion blocks on IMU DATA mailbox
2. IMU message arrives (timestamp = T)
3. Fusion calls get_data for GPS with timestamp T and tolerance
4. Returns closest GPS message within tolerance (or marks invalid)
5. `process()` called with both inputs

## Configuration

```cpp
SimpleOutputConfig imu_config{
    .name = "IMU", .system_id = 10, .instance_id = 1
};

SimpleOutputConfig gps_config{
    .name = "GPS", .system_id = 11, .instance_id = 1
};

SimpleOutputConfig fusion_config{
    .name = "SensorFusion",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,      // Primary input source (IMU)
    .source_instance_id = 1
};
// Secondary input sources configured internally via SyncedInput
```

## Build and Run

```bash
mkdir build && cd build
cmake .. && make
./multi_input_fusion
```

## Synced<T> API Summary

| Method | Returns | Condition |
|--------|---------|-----------|
| `operator bool()` | true | Fresh data only |
| `value()` | `const T&` | Asserts if not fresh |
| `stale()` | `const T&` | Asserts if invalid |
| `is_fresh()` | bool | Exact timestamp match |
| `is_valid()` | bool | Any data available |
| `value_or(default)` | `const T&` | Fresh or default |
| `stale_or(default)` | `const T&` | Valid or default |

## Next Steps

- [USER_GUIDE.md](../../USER_GUIDE.md) for comprehensive documentation
- Try 3+ input fusion (add LidarData as another SyncedInput)
- Experiment with different sync tolerance values
