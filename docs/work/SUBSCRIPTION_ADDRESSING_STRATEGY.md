# Subscription Addressing Strategy

## Overview

CommRaT uses different addressing strategies for single-input vs multi-input modules. This document explains why and provides the architectural rationale.

## Key Issue

**Question**: Why do single-input and multi-input modules use different address calculation strategies for subscription?

## The Two Strategies

### Single-Input (Continuous Input)

**subscriber_base_addr**: Uses **OUTPUT type** (module's identity)

```cpp
// Filter module: Output=FilteredData (type_id=2)
subscriber_base_addr = calculate_base_address(system_id, instance_id);
// Result: base = (2 << 16) | (20 << 8) | 1 = 136193
// DATA mailbox = 136193 + 48 = 136241
```

**Rationale**:
- Single-input modules have ONE DATA mailbox that receives all input
- The mailbox address is based on the module's OUTPUT type (its identity)
- Simple addressing: one module identity → one DATA mailbox
- Producer sends to: `subscriber_base_addr + MailboxType::DATA`

### Multi-Input

**subscriber_base_addr**: Uses **INPUT type** at specific index

```cpp
// Fusion module: Inputs=<IMUData, GPSData, LidarData>
// For input[0] (IMUData, type_id=7):
subscriber_base_addr[0] = (7 << 16) | (system_id << 8) | instance_id;
// For input[1] (GPSData, type_id=8):
subscriber_base_addr[1] = (8 << 16) | (system_id << 8) | instance_id;
// For input[2] (LidarData, type_id=9):
subscriber_base_addr[2] = (9 << 16) | (system_id << 8) | instance_id;
```

**Rationale**:
- Multi-input modules have MULTIPLE DATA mailboxes (one per input type)
- Each mailbox is addressed by the INPUT type it receives
- Allows different producers to send to different mailboxes simultaneously
- Enables independent receive loops and historical buffering per input

## Why Different Strategies?

### Historical Design Evolution

1. **Phase 1-4**: All modules used OUTPUT type for addressing
   - Simple but limited - couldn't handle multiple inputs

2. **Phase 5-6**: Multi-input support added
   - Needed multiple mailboxes for different input types
   - INPUT-type-based addressing enables this

3. **Phase 7 (Current)**: Unified addressing with branching
   - Single-input: OUTPUT type (backward compatible, simple)
   - Multi-input: INPUT types (necessary for multiple mailboxes)

### Architectural Reasons

#### Single-Input: OUTPUT Type

**Advantages**:
- Module identity is clear: one OUTPUT type = one module
- Simple for producers: send to module's identity
- No ambiguity about which mailbox to use
- Works naturally with module lifecycle (CMD/WORK/PUBLISH/DATA all use same base)

**Use Cases**:
- Filters (SensorData → FilteredData)
- Transformers (RawImage → ProcessedImage)
- Controllers (Setpoint → ControlOutput)

#### Multi-Input: INPUT Types

**Advantages**:
- **Parallel reception**: Each input can receive independently
- **Type safety**: Producer sends IMUData to IMU-mailbox, GPSData to GPS-mailbox
- **Historical buffering**: Each input has its own HistoricalMailbox
- **getData synchronization**: Can query specific input's history by type
- **No multiplexing**: Inputs don't share a mailbox, no need to demux

**Use Cases**:
- Sensor fusion (IMU + GPS + Lidar → FusedState)
- Multi-modal processing (Camera + Radar → ObjectDetection)
- Data aggregation (Multiple sensors → CombinedOutput)

## Implementation Details

### Address Encoding

Both strategies use 32-bit address:
```
[31:16] type_id_low (16 bits)
[15:8]  system_id   (8 bits)
[7:0]   instance_id (8 bits)
```

**Single-Input**:
```cpp
type_id = Registry::get_message_id<OutputData>();  // Module's OUTPUT type
base = (type_id_low << 16) | (system_id << 8) | instance_id;
```

**Multi-Input**:
```cpp
type_id = Registry::get_message_id<InputTypeAtIndex>();  // Specific INPUT type
base = (type_id_low << 16) | (system_id << 8) | instance_id;
```

### Mailbox Creation

#### Single-Input Mailboxes

```cpp
// Filter module (system_id=20, instance_id=1, OUTPUT=FilteredData, type_id=2)
uint32_t base = (2 << 16) | (20 << 8) | 1;  // 136193
CMD:     base + 0  = 136193
WORK:    base + 16 = 136209
PUBLISH: base + 32 = 136225
DATA:    base + 48 = 136241  // Receives SensorData here
```

#### Multi-Input Mailboxes

```cpp
// Fusion module (system_id=30, instance_id=1)
// OUTPUT=FusedData (type_id=9), Inputs=<IMUData(7), GPSData(8), LidarData(9)>

// Command/Work/Publish use OUTPUT type:
uint32_t output_base = (9 << 16) | (30 << 8) | 1;  // 590593
CMD:     590593
WORK:    590609
PUBLISH: 590625

// DATA mailboxes use INPUT types:
DATA[0]: ((7 << 16) | (30 << 8) | 1) + 48  // IMUData
DATA[1]: ((8 << 16) | (30 << 8) | 1) + 48  // GPSData  
DATA[2]: ((9 << 16) | (30 << 8) | 1) + 48  // LidarData
```

## Trade-offs

### Single-Input (OUTPUT type)

**Pros**:
- Simpler addressing (one identity)
- Natural mailbox organization
- Backward compatible
- Easy to understand

**Cons**:
- Can't receive multiple input types simultaneously
- No per-input buffering
- No parallel input processing

### Multi-Input (INPUT types)

**Pros**:
- Parallel input reception
- Independent buffering per input
- Type-safe routing
- Supports complex fusion patterns

**Cons**:
- More complex addressing
- Multiple mailbox creation overhead
- Must track multiple subscriber addresses

## Why Not Unify?

**Question**: Why not use INPUT type for single-input modules too?

**Answer**: 
1. **Backward compatibility**: Existing single-input code works
2. **Simplicity**: Single-input doesn't need multiple mailboxes
3. **Performance**: One mailbox is cheaper than multiple
4. **API clarity**: Module identity = OUTPUT type is intuitive

Using OUTPUT type for single-input is semantically correct: "Send data to this module (identified by its output type)."

Using INPUT types for multi-input is functionally necessary: "Send IMU data to IMU-mailbox, GPS data to GPS-mailbox."

## Future Considerations

### Potential Unification

If we wanted to unify (use INPUT types for all), we would need:

1. **Single-input wrapping**: Wrap single INPUT type in tuple
2. **Address calculation**: Always use first INPUT type
3. **Mailbox creation**: Create DATA[0] even for single-input
4. **Migration**: Update all existing single-input modules

**Cost**: Migration effort, increased complexity
**Benefit**: Uniform addressing logic

**Decision**: Not worth it - current branching is clear and works.

### Alternative: Multi-Mailbox for Single-Input

Could create multiple DATA mailboxes for single-input (e.g., one per message variant). Would enable:
- Receiving different message types
- Priority lanes
- Type-specific buffering

But this adds complexity without clear use case. Current design is sufficient.

## Summary

**Single-Input**: Uses OUTPUT type for subscriber_base_addr
- Module identity = OUTPUT type
- One DATA mailbox receives all input
- Simple, backward compatible

**Multi-Input**: Uses INPUT types for subscriber_base_addr
- Each input has its own DATA mailbox
- Parallel reception, independent buffering
- Necessary for multi-input functionality

Both strategies are correct for their use case. The branching in `subscribe_to_source_impl()` is intentional and architecturally sound.

## Code Reference

See `include/commrat/module/services/subscription.hpp:271-298`

```cpp
if constexpr (has_multi_input) {
    // Use INPUT type at source_index
    source_data_type_id = get_input_type_id_at_index(source_index);
    uint16_t input_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
    subscriber_base_addr = (static_cast<uint32_t>(input_type_id_low) << 16) | 
                            (config_->system_id() << 8) | config_->instance_id();
} 
else if constexpr (has_continuous_input) {
    // Use OUTPUT type (module identity)
    subscriber_base_addr = calculate_base_address(config_->system_id(), config_->instance_id());
    source_data_type_id = Registry::template get_message_id<InputData>();
}
```

This is not a bug - it's a deliberate architectural choice supporting two different input patterns.
