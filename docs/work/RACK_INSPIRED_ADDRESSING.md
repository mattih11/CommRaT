# RACK-Inspired Addressing Architecture

## Current Problem

Address collisions occur because we use:
```
[type_id:16][system:8][instance:8] + offsets (0, 16, 32, 48, 64...)
```

This causes collisions when modules share system/instance but have different OUTPUT types.

## RACK Architecture Analysis

RACK uses a proven 32-bit addressing scheme:
```
[31:24] system_id   (8 bits = 256 systems)
[23:16] class_id    (8 bits = 256 module types/classes)
[15:8]  instance_id (8 bits = 256 instances per type)
[7:0]   local_id    (8 bits = 256 mailboxes per module)
```

**Key RACK Principles:**
1. **ONE OUTPUT per module** - module identity = cmdMbx address
2. **cmdMbx at local_id=0** - receives commands, subscriptions; sends data
3. **Input mailboxes** manually allocated at local_id=1,2,3...
4. **No separate WORK/PUBLISH mailboxes** - cmdMbx handles everything

## Proposed CommRaT Address Scheme

**Clean byte-aligned encoding (high to low hierarchy):**

```
[31:24] type_id       (8 bits = 256 types, expandable to 10 bits later)
[23:16] system_id     (8 bits = 256 systems)
[15:8]  instance_id   (8 bits = 256 instances)
[7:0]   mailbox_index (8 bits = 256 mailboxes per module, practically use 6 bits = 64)
```

**Encoding helpers (constexpr):**
```cpp
constexpr uint8_t TYPE_ID_SHIFT = 24;
constexpr uint8_t SYSTEM_ID_SHIFT = 16;
constexpr uint8_t INSTANCE_ID_SHIFT = 8;
constexpr uint8_t MAILBOX_INDEX_SHIFT = 0;

constexpr uint32_t TYPE_ID_MASK = 0xFF000000;
constexpr uint32_t SYSTEM_ID_MASK = 0x00FF0000;
constexpr uint32_t INSTANCE_ID_MASK = 0x0000FF00;
constexpr uint32_t MAILBOX_INDEX_MASK = 0x000000FF;
```

**Rationale:**
- Clean byte boundaries (easy debugging: 0xTTSSIIMM format)
- Matches RACK hierarchy: high-level (type/system) in high bits, local (mailbox) in low bits
- Type ID: 8 bits now (256 types), can expand to 10 bits later if needed
- Mailbox index: 8 bits max (256), but practically limit to 64 (6 bits used)
- Easy to extract fields with byte shifts

**Mailbox Index Allocation (RACK-style):**

**Per OUTPUT** (each output gets its own CMD mailbox):
```
mailbox_index = 0: CMD mailbox for output[0] (primary)
  - Receives: user commands, subscription requests for output[0]
  - Sends: subscription replies, published data for output[0]
  
mailbox_index = 1: CMD mailbox for output[1]  // if multi-output
  - Receives: subscription requests for output[1]
  - Sends: published data for output[1]
```

**Per INPUT** (sequential allocation after CMD mailboxes):
```
mailbox_index = num_outputs + 0: DATA mailbox for input[0]
mailbox_index = num_outputs + 1: DATA mailbox for input[1]
  - Each input receives data from its producer
```

**Constants:**
```cpp
constexpr uint8_t CMD_MBX_BASE = 0;  // CMD mailboxes start at index 0
constexpr uint8_t get_data_mbx_base(uint8_t num_outputs) {
    return num_outputs;  // DATA mailboxes start after all CMD mailboxes
}
```

**Example: Multi-output (2 outputs) + Multi-input (3 inputs):**
```
index=0: CMD for Output[0] (Temperature)
index=1: CMD for Output[1] (Humidity)
index=2: DATA for Input[0] (Sensor1)
index=3: DATA for Input[1] (Sensor2)
index=4: DATA for Input[2] (Sensor3)
Total: 5 mailboxes
```

**Example: Sensor Fusion Module**
- Output: FusedData (type_id=10)
- System=5, Instance=1
- Inputs: IMU, GPS, Lidar

```
Address encoding (0xTTSSIIMM format):
type_id = 10 (0x0A)
system_id = 5 (0x05)
instance_id = 1 (0x01)

Base (type/system/instance): 0x0A050100

CMD (index=0):  0x0A050100  // Commands, subscriptions, publishing
DATA[0] (IMU):  0x0A050101  // IMU input
DATA[1] (GPS):  0x0A050102  // GPS input  
DATA[2] (Lidar):0x0A050103  // Lidar input
```

**Example: Multi-Output Module (Temperature + Humidity)**
- Outputs: TemperatureData (type_id=5), HumidityData (type_id=6)
- System=10, Instance=2
- Uses primary output (Temperature) type_id for module addressing

```
Address encoding:
type_id = 5 (0x05)
system_id = 10 (0x0A)
instance_id = 2 (0x02)

Base: 0x050A0200

CMD[0] (Temp):     0x050A0200  // Temperature subscriptions/publishing
CMD[1] (Humidity): 0x050A0201  // Humidity subscriptions/publishing
```

**Example: Simple Filter Module**
- Output: FilteredData (type_id=2)
- System=20, Instance=1
- Input: SensorData

```
Address encoding: 0x02140100

CMD (index=0):  0x02140100  // Commands, subscriptions, publishing
DATA (index=1): 0x02140101  // Receives SensorData
```

## Benefits

1. **No Address Collisions**: Module identity = [type:10][sys:8][inst:8], mailbox distinguished by index
2. **Simplified Architecture**: Only CMD + DATA mailboxes (no WORK, no PUBLISH)
3. **RACK-Compatible**: Follows proven RACK pattern
4. **Efficient**: 64 mailboxes per module is plenty (most modules need <10)
5. **Type Safety**: Type ID in address enables routing validation

## Migration Path

### Changes Required:

1. **Address Encoding Constants** (module_config.hpp or new address_encoding.hpp):
   ```cpp
   // Address field shifts (high to low hierarchy)
   constexpr uint8_t TYPE_ID_SHIFT = 24;
   constexpr uint8_t SYSTEM_ID_SHIFT = 16;
   constexpr uint8_t INSTANCE_ID_SHIFT = 8;
   constexpr uint8_t MAILBOX_INDEX_SHIFT = 0;
   
   // Address field masks
   constexpr uint32_t TYPE_ID_MASK = 0xFF000000;
   constexpr uint32_t SYSTEM_ID_MASK = 0x00FF0000;
   constexpr uint32_t INSTANCE_ID_MASK = 0x0000FF00;
   constexpr uint32_t MAILBOX_INDEX_MASK = 0x000000FF;
   
   // Mailbox index constants
   constexpr uint8_t CMD_MBX_BASE = 0;
   constexpr uint8_t get_data_mbx_base(uint8_t num_outputs) {
       return num_outputs;
   }
   ```

2. **Address Encoding Functions** (address_helpers.hpp):
   - Replace: `(type << 16) | (sys << 8) | inst`
   - With: `(type << 24) | (sys << 16) | (inst << 8) | mbx_idx`
   - Add helper functions:
     ```cpp
     constexpr uint32_t encode_address(uint8_t type_id, uint8_t sys, uint8_t inst, uint8_t mbx_idx) {
         return (static_cast<uint32_t>(type_id) << TYPE_ID_SHIFT) |
                (static_cast<uint32_t>(sys) << SYSTEM_ID_SHIFT) |
                (static_cast<uint32_t>(inst) << INSTANCE_ID_SHIFT) |
                (static_cast<uint32_t>(mbx_idx) << MAILBOX_INDEX_SHIFT);
     }
     
     constexpr uint8_t extract_mailbox_index(uint32_t addr) {
         return static_cast<uint8_t>((addr & MAILBOX_INDEX_MASK) >> MAILBOX_INDEX_SHIFT);
     }
     
     constexpr uint8_t extract_type_id(uint32_t addr) {
         return static_cast<uint8_t>((addr & TYPE_ID_MASK) >> TYPE_ID_SHIFT);
     }
     
     constexpr uint8_t extract_system_id(uint32_t addr) {
         return static_cast<uint8_t>((addr & SYSTEM_ID_MASK) >> SYSTEM_ID_SHIFT);
     }
     
     constexpr uint8_t extract_instance_id(uint32_t addr) {
         return static_cast<uint8_t>((addr & INSTANCE_ID_MASK) >> INSTANCE_ID_SHIFT);
     }
     ```

3. **Mailbox Type Changes** (module_config.hpp):
   - Remove: `enum class MailboxType { CMD=0, WORK=16, PUBLISH=32, DATA=48 }`
   - Keep only mailbox index constants (CMD_MBX_BASE, etc.)
   - Multi-output: Each output gets sequential CMD mailbox (0, 1, 2...)
   - Multi-input: Each input gets sequential DATA mailbox after CMD mailboxes

4. **Subscription Protocol** (subscription.hpp):
   - Update `SubscribeRequestPayload`:
     ```cpp
     struct SubscribeRequestPayload {
         uint32_t subscriber_base_addr;  // Base address (type/system/instance only)
         uint8_t mailbox_index;          // Which mailbox to send data to
         int64_t requested_period_ms;
     };
     ```
   - Producer calculates target: `subscriber_base_addr | mailbox_index`

5. **Publishing** (publishing.hpp):
   - Use CMD mailbox (index=0 for single-output, index=output_idx for multi-output)
   - Remove PUBLISH mailbox concept
   - Send to: `(subscriber_base & ~MAILBOX_INDEX_MASK) | subscriber_mbx_idx`

6. **Multi-Input Infrastructure** (multi_input_infrastructure.hpp):
   - DATA mailboxes: `base_addr | (data_mbx_base + input_idx)`
   - Where `data_mbx_base = num_outputs` (after all CMD mailboxes)

7. **Multi-Output Manager** (multi_output_manager.hpp):
   - Each output uses CMD mailbox at: `base_addr | output_idx`
   - Subscriber storage includes mailbox_index
   - Type-based routing handled by subscriber lists per output

4. **Publishing** (publishing.hpp):
   - Use CMD mailbox (index=0) for sending data
   - Remove PUBLISH mailbox concept

5. **Multi-Input** (multi_input_infrastructure.hpp):
   - DATA mailboxes: `base | ((DATA_MBX_BASE + input_idx) << 16)`

6. **Multi-Output**:
   - Each output still uses same CMD mailbox (index=0)
   - Subscriber list per output maintained internally
   - Type-based routing handled at publish time

### Compatibility:

- **Backward compatible**: Old tests will fail initially, but architecture is clearer
- **Easier debugging**: Clear mailbox index in address
- **Better scalability**: 1024 types × 256 systems × 256 instances = 67M unique modules

## Summary

This RACK-inspired addressing:
- ✅ **Solves collision problem**: Module identity = [type][system][instance], mailbox distinguished by index
- ✅ **Simplifies architecture**: CMD + DATA mailboxes only (no WORK, no PUBLISH)
- ✅ **Follows proven RACK pattern**: One CMD per output, sequential mailbox allocation
- ✅ **Maintains type safety**: Type ID in address enables routing validation
- ✅ **Scales well**: 256 types × 256 systems × 256 instances = 16M unique modules
- ✅ **Easy debugging**: Clean byte format 0xTTSSIIMM (Type/System/Instance/Mailbox)
- ✅ **Expandable**: Can increase type_id to 10 bits later if needed (borrow 2 bits from mailbox_index)
- ✅ **Efficient**: 64 mailboxes per module sufficient for most use cases

**Future Expansion Option:**
If 256 types becomes limiting, can expand to 10-bit type_id:
```
[31:24] type_id[9:2]   (8 bits)
[23:16] system_id      (8 bits)
[15:8]  instance_id    (8 bits)
[7:6]   type_id[1:0]   (2 bits)
[5:0]   mailbox_index  (6 bits = 64 mailboxes)
```
This maintains backward compatibility with simple bit manipulation.
