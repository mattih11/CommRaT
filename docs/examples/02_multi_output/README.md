# Example 02: Multi-Output Modules

A weather station produces both temperature and pressure data. Two separate monitors each receive only their subscribed type.

## What You'll Learn

1. Multi-output producers using multiple `Output<T>` specs
2. Type-specific delivery via address-based filtering
3. Source configuration for secondary output types

## Architecture

```
                    WeatherStation
                     (Producer)
                      system=10
                    /            \
          TemperatureData    PressureData
                |                  |
                v                  v
        TempMonitor          PressureMonitor
         system=20              system=21
```

Each consumer receives only its subscribed type.

## Message Types

```cpp
struct TemperatureData {
    uint32_t station_id;
    float temperature_c;
    float humidity_percent;
};

struct PressureData {
    uint32_t station_id;
    float pressure_hpa;
    float altitude_m;
};
```

## Multi-Output Producer

```cpp
class WeatherStation : public WeatherApp::Module2<
    Output<TemperatureData>,   // Primary output
    Output<PressureData>,      // Secondary output
    Period<100>                // 10Hz
> {
protected:
    void process(TemperatureData& temp, PressureData& pressure) override {
        temp = TemperatureData{
            .station_id = station_id_,
            .temperature_c = generate_temperature(),
            .humidity_percent = generate_humidity()
        };
        pressure = PressureData{
            .station_id = station_id_,
            .pressure_hpa = generate_pressure(),
            .altitude_m = generate_altitude()
        };
    }
};
```

- Multiple `Output<T>` specs (not `Outputs<T,U>`)
- `process()` receives one reference per output type
- First `Output<T>` is the primary output type

## Type-Specific Consumers

### Temperature Consumer (Primary Type)

```cpp
class TemperatureMonitor : public WeatherApp::Module2<
    Output<TemperatureData>,
    Input<TemperatureData>
> { ... };

SimpleOutputConfig temp_config{
    .name = "TempMonitor",
    .system_id = 20,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1
    // No source_primary_output_type_id -- TemperatureData is the primary type
};
```

### Pressure Consumer (Secondary Type)

```cpp
class PressureMonitor : public WeatherApp::Module2<
    Output<PressureData>,
    Input<PressureData>
> { ... };

MultiOutputConfig pressure_config{
    .name = "PressureMonitor",
    .system_id = 21,
    .instance_id = 1,
    .source_system_id = 10,
    .source_instance_id = 1,
    .source_primary_output_type_id = WeatherApp::get_message_id<TemperatureData>()  // Required!
};
```

**Why `source_primary_output_type_id`?** The producer's base address encodes the primary type. Secondary-type consumers must specify the primary type to calculate the correct producer address.

## Build and Run

```bash
mkdir build && cd build
cmake .. && make
./multi_output_example
```

## Key Observations

1. Weather station produces both types at 10Hz
2. Each monitor receives only its subscribed type (address-based filtering)
3. Primary type consumers need no special config
4. Secondary type consumers must specify `source_primary_output_type_id`

## Next Steps

- **Example 03**: Multi-input sensor fusion with Synced<T>
