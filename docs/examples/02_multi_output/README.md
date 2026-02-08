# Example 02: Multi-Output Modules

This example demonstrates **multi-output producers** with **type-specific message delivery**. A weather station produces both temperature and pressure data, and two separate monitors each receive only their subscribed message type.

## What You'll Learn

1. **Multi-output producers**: Using `Outputs<T, U>` to generate multiple message types
2. **Type-specific delivery**: How subscribers receive only their expected message type
3. **Source configuration**: Properly configuring consumers for multi-output producers
4. **Type filtering**: How CommRaT filters messages based on subscriber address

## Architecture

```
                         ┌─────────────────┐
                         │ WeatherStation  │
                         │   (Producer)    │
                         │  system_id=10   │
                         └────────┬────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │                           │
           TemperatureData               PressureData
                    │                           │
                    ▼                           ▼
        ┌───────────────────┐       ┌──────────────────┐
        │ TemperatureMonitor│       │ PressureMonitor  │
        │   (Consumer)      │       │   (Consumer)     │
        │   system_id=20    │       │   system_id=21   │
        └───────────────────┘       └──────────────────┘
```

**Key insight:** The weather station produces both types every 100ms, but each consumer receives only the messages matching their subscribed type. This is achieved through **address-based type filtering**.

## Message Types

### TemperatureData (Primary Output Type)

```cpp
struct TemperatureData {
    uint32_t station_id;
    float temperature_c;
    float humidity_percent;
};
```

### PressureData (Secondary Output Type)

```cpp
struct PressureData {
    uint32_t station_id;
    float pressure_hpa;
    float altitude_m;
};
```

**Important:** The order of types in `Outputs<T, U>` matters:
- **First type** (`TemperatureData`) = **primary output type**
- Producer's base address encodes the primary type
- Consumers subscribing to secondary types must specify `source_primary_output_type_id`

## Multi-Output Producer

```cpp
class WeatherStation : public WeatherApp::Module<
    commrat::Outputs<TemperatureData, PressureData>,  // Multi-output!
    commrat::PeriodicInput
> {
protected:
    // Multi-output process signature:
    // - void return type (outputs passed by reference)
    // - One reference parameter per output type
    void process(TemperatureData& temp_out, PressureData& pressure_out) override {
        // Generate temperature data
        temp_out = TemperatureData{
            .station_id = station_id_,
            .temperature_c = generate_temperature(),
            .humidity_percent = generate_humidity()
        };
        
        // Generate pressure data
        pressure_out = PressureData{
            .station_id = station_id_,
            .pressure_hpa = generate_pressure(),
            .altitude_m = generate_altitude()
        };
    }
};
```

**Key points:**
- `Outputs<T, U>` declares multiple output types
- `process(T& out1, U& out2)` fills outputs by reference
- Called every `config_.period` (100ms = 10Hz)
- Module automatically wraps and sends both outputs

## Type-Specific Consumers

### Temperature Consumer (Primary Type)

```cpp
class TemperatureMonitor : public WeatherApp::Module<
    commrat::Output<TemperatureData>,  // Pass-through
    commrat::Input<TemperatureData>
> {
protected:
    TemperatureData process_continuous(const TemperatureData& input) override {
        std::cout << "[TempMonitor] " << input.temperature_c << "°C\n";
        return input;  // Pass-through
    }
};

// Configuration
commrat::ModuleConfig temp_monitor_config{
    .name = "TempMonitor",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,    // WeatherStation
    .source_instance_id = 1
    // No source_primary_output_type_id needed - TemperatureData is primary!
};
```

**Why no `source_primary_output_type_id`?**
- Consumer expects `TemperatureData` (primary type)
- Producer's base address encodes `TemperatureData`
- Address calculation works automatically

### Pressure Consumer (Secondary Type)

```cpp
class PressureMonitor : public WeatherApp::Module<
    commrat::Output<PressureData>,
    commrat::Input<PressureData>
> {
protected:
    PressureData process_continuous(const PressureData& input) override {
        std::cout << "[PressureMonitor] " << input.pressure_hpa << " hPa\n";
        return input;
    }
};

// Configuration
commrat::ModuleConfig pressure_monitor_config{
    .name = "PressureMonitor",
    .system_id = 21,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = WeatherApp::get_message_id<TemperatureData>()  // KEY!
};
```

**Why `source_primary_output_type_id` required?**
- Consumer expects `PressureData` (secondary type)
- But producer's base address uses `TemperatureData` (primary)
- Without this field, consumer calculates wrong producer address!
- `source_primary_output_type_id` tells consumer where to find producer

## How Type Filtering Works

### Address Encoding

```cpp
// Producer's base address (system_id=10, instance_id=1)
uint16_t temp_type_id = get_message_id<TemperatureData>() & 0xFFFF;  // Lower 16 bits
uint32_t producer_base = (temp_type_id << 16) | (10 << 8) | 1;

// WORK mailbox: producer_base + 16 (subscription protocol)
// This is where both consumers send SubscribeRequest
```

### Type-Specific Delivery

```cpp
// When producer publishes, it checks each subscriber:
for (uint32_t subscriber_addr : subscribers_) {
    // Extract expected type from subscriber's base address
    uint16_t subscriber_type = (subscriber_addr >> 16) & 0xFFFF;
    
    // Send only matching outputs
    if (subscriber_type == temp_type_id) {
        send(temp_data, subscriber_addr + 32);  // DATA mailbox
    }
    if (subscriber_type == pressure_type_id) {
        send(pressure_data, subscriber_addr + 32);  // DATA mailbox
    }
}
```

**Result:** Each consumer receives only messages matching their expected type!

## Build and Run

### Build

```bash
mkdir build && cd build
cmake ..
make
```

### Run

```bash
./multi_output_example
```

### Expected Output

```
=== CommRaT Example 02: Multi-Output Modules ===

Creating modules...
[WeatherStation] Initialized station_id=1
[TempMonitor] Initialized
[PressureMonitor] Initialized

Starting producer...
[TiMS] Creating mailbox 133633 with max_msg_size=36 bytes
[TiMS] Creating mailbox 133649 with max_msg_size=40 bytes
[WeatherStation] work_loop started, listening on WORK mailbox 133649
[WeatherStation] command_loop started
[WeatherStation] Starting periodic_loop thread...
[WeatherStation] periodic_loop started, period=100ms
[WeatherStation] Generated: Temp=19.06°C, Pressure=1008.09 hPa

Starting consumers...
[TiMS] Creating mailbox 136193 with max_msg_size=36 bytes
[TiMS] Creating mailbox 136209 with max_msg_size=40 bytes
[TiMS] Creating mailbox 136225 with max_msg_size=36 bytes
[TempMonitor] work_loop started, listening on WORK mailbox 136209
[TempMonitor] Sending SubscribeRequest[0] to source WORK mailbox 133649
[TempMonitor] SubscribeRequest[0] sent successfully
[TempMonitor] Starting continuous_loop thread...
[WeatherStation] Handling SubscribeRequest
[WeatherStation] Added subscriber base=136193, will send to DATA mailbox=136225
[TempMonitor] continuous_loop started, waiting for data...
[TempMonitor] Handling SubscribeReply
[TempMonitor] SubscribeReply received: SUCCESS, actual_period_ms=100

[TiMS] Creating mailbox 70913 with max_msg_size=36 bytes
[TiMS] Creating mailbox 70929 with max_msg_size=40 bytes
[PressureMonitor] work_loop started, listening on WORK mailbox 70929
[PressureMonitor] Sending SubscribeRequest[0] to source WORK mailbox 133649
[PressureMonitor] Starting continuous_loop thread...
[WeatherStation] Handling SubscribeRequest
[WeatherStation] Added subscriber base=70913, will send to DATA mailbox=70945
[PressureMonitor] Handling SubscribeReply
[PressureMonitor] SubscribeReply received: SUCCESS, actual_period_ms=100
[PressureMonitor] continuous_loop started, waiting for data...

🌡️  Running weather monitoring... (Press Ctrl+C to stop)

[WeatherStation] Generated: Temp=27.66°C, Pressure=1003.39 hPa
[TempMonitor] #1 Station 1: 27.66°C, 46.32% humidity ⚠ HIGH TEMP
[PressureMonitor] #1 Station 1: 1003.39 hPa, alt=361.80m

[WeatherStation] Generated: Temp=27.30°C, Pressure=991.09 hPa
[TempMonitor] #2 Station 1: 27.30°C, 34.98% humidity ⚠ HIGH TEMP
[PressureMonitor] #2 Station 1: 991.09 hPa, alt=146.38m

[WeatherStation] Generated: Temp=22.96°C, Pressure=982.58 hPa
[TempMonitor] #3 Station 1: 22.96°C, 49.93% humidity
[PressureMonitor] #3 Station 1: 982.58 hPa, alt=244.45m 🌧 LOW PRESSURE

[WeatherStation] Generated: Temp=22.61°C, Pressure=1010.88 hPa
[TempMonitor] #4 Station 1: 22.61°C, 40.76% humidity
[PressureMonitor] #4 Station 1: 1010.88 hPa, alt=405.51m ☀ HIGH PRESSURE

[WeatherStation] Generated: Temp=26.35°C, Pressure=989.38 hPa
[TempMonitor] #5 Station 1: 26.35°C, 38.18% humidity ⚠ HIGH TEMP
[PressureMonitor] #5 Station 1: 989.38 hPa, alt=92.49m 🌧 LOW PRESSURE

... (continues until Ctrl+C)

[SIGNAL] Ctrl+C received, shutting down...

🛑 Shutting down...
✅ Done!
```

## Key Observations

1. **Both messages generated**: Weather station produces both temperature and pressure at 10Hz
2. **Separate subscribers**: Two independent subscription handshakes occur
3. **Type-specific delivery**: Each monitor receives only their subscribed type
   - `TempMonitor` receives `TemperatureData` only
   - `PressureMonitor` receives `PressureData` only
4. **Message filtering**: Happens automatically based on subscriber address encoding
5. **Clean shutdown**: Both consumers unsubscribe, producer stops cleanly

## Common Mistakes

### ❌ Forgetting source_primary_output_type_id

```cpp
// WRONG: Consumer expects secondary type but doesn't specify primary
commrat::ModuleConfig pressure_monitor_config{
    .system_id = 21,
    .source_system_id = 10,
    .source_instance_id = 1
    // Missing: .source_primary_output_type_id = ...
};
// Result: Consumer calculates WRONG producer address!
// Subscription will FAIL (producer not found)
```

### ❌ Wrong type order in Outputs<>

```cpp
// If you swap the order:
using WeatherApp = CommRaT<
    Message::Data<PressureData>,     // Now primary!
    Message::Data<TemperatureData>   // Now secondary
>;

// Then TemperatureMonitor needs source_primary_output_type_id:
commrat::ModuleConfig temp_monitor_config{
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = WeatherApp::get_message_id<PressureData>()  // Changed!
};
```

### ❌ Using single Output<T> return type

```cpp
// WRONG: Multi-output requires void return type
class WeatherStation : public WeatherApp::Module<
    Outputs<TemperatureData, PressureData>,
    PeriodicInput
> {
protected:
    // ERROR: Can't return single value for multi-output!
    TemperatureData process() override {  // ❌ Wrong signature
        return TemperatureData{...};
    }
};

// RIGHT: Use void with reference parameters
void process(TemperatureData& t, PressureData& p) override {  // ✅ Correct
    t = TemperatureData{...};
    p = PressureData{...};
}
```

## Next Steps

- **Example 03**: Multi-input fusion with synchronized getData
- **USER_GUIDE Section 6**: Deep dive into subscription protocol
- Try adding a third output type (e.g., `WindData`)
- Experiment with different update rates for each consumer

## Summary

**Multi-output modules enable:**
- ✅ Single producer generating multiple message types
- ✅ Efficient type-specific delivery (no unnecessary messages)
- ✅ Clean separation of concerns (each consumer handles one type)
- ✅ Compile-time type safety (wrong types caught at compile time)

**Key configuration:**
- Primary type consumers: No special config needed
- Secondary type consumers: Must specify `source_primary_output_type_id`
