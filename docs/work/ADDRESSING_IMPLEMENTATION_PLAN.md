# CommRaT Addressing Implementation Plan

## Overview

Implement RACK-inspired mailbox addressing to solve collision problems and simplify architecture.

## Address Encoding (Byte-Aligned)

```
[31:24] type_id       (8 bits = 256 types)
[23:16] system_id     (8 bits = 256 systems)  
[15:8]  instance_id   (8 bits = 256 instances)
[7:0]   mailbox_index (8 bits, practically 6 bits = 64 mailboxes)

Format: 0xTTSSIIMM (easy hex debugging)
```

**Constants (constexpr):**
```cpp
constexpr uint8_t TYPE_ID_SHIFT = 24;
constexpr uint8_t SYSTEM_ID_SHIFT = 16;
constexpr uint8_t INSTANCE_ID_SHIFT = 8;
constexpr uint8_t MAILBOX_INDEX_SHIFT = 0;

constexpr uint32_t TYPE_ID_MASK = 0xFF000000;
constexpr uint32_t SYSTEM_ID_MASK = 0x00FF0000;
constexpr uint32_t INSTANCE_ID_MASK = 0x0000FF00;
constexpr uint32_t MAILBOX_INDEX_MASK = 0x000000FF;

constexpr uint8_t CMD_MBX_BASE = 0;
```

## Mailbox Architecture (RACK-Style)

### Per Output
Each output gets a **CMD mailbox** for all communication:
- Receives: commands, subscription requests
- Sends: subscription replies, data to subscribers

### Per Input  
Each input gets a **DATA mailbox** for receiving data:
- Receives: input data from producer
- Sequential allocation after all CMD mailboxes

### Example Allocations

**Single-output, single-input (Filter):**
```
mailbox_index = 0: CMD  (commands, subscriptions, publishing)
mailbox_index = 1: DATA (receives input)
```

**Multi-output (2), no inputs:**
```
mailbox_index = 0: CMD for output[0]
mailbox_index = 1: CMD for output[1]
```

**Single-output, multi-input (3-input Fusion):**
```
mailbox_index = 0: CMD  (commands, subscriptions, publishing fusion data)
mailbox_index = 1: DATA (IMU input)
mailbox_index = 2: DATA (GPS input)
mailbox_index = 3: DATA (Lidar input)
```

**Multi-output (2), multi-input (2):**
```
mailbox_index = 0: CMD for output[0]
mailbox_index = 1: CMD for output[1]
mailbox_index = 2: DATA for input[0]
mailbox_index = 3: DATA for input[1]
```

## Mailbox Sizing Strategy (Compile-Time)

**Key Principle: Buffer sizing only for RECEIVE side, all sizes known at compile time**

### CMD Mailbox Sizing
**Receives:**
- User commands (from Module's `Commands<Cmd1, Cmd2, ...>` template parameter)
- System messages (SubscribeRequest, UnsubscribeRequest from SystemRegistry)

**Size calculation (compile-time):**
```cpp
constexpr size_t cmd_mailbox_size = std::max({
    sizeof(TimsMessage<SystemRegistry::SubscribeRequest>),
    sizeof(TimsMessage<SystemRegistry::UnsubscribeRequest>),
    sizeof(TimsMessage<Cmd1>),
    sizeof(TimsMessage<Cmd2>),
    // ... all command types from Module<..., Commands<Cmd1, Cmd2>>
});
```

**NOT** using `UserRegistry::max_message_size` - that includes ALL message types!
We only need max of: SystemRegistry messages + Module's specific command types.

**Slots:** Configurable per module (default: 10-20 for command buffering)

### DATA Mailbox Sizing (Per Input)
**Receives:**
- Specific input type data (from `Input<T>` or `Inputs<T1, T2, ...>`)

**Size calculation (compile-time):**
```cpp
template<typename InputType>
constexpr size_t data_mailbox_size = sizeof(TimsMessage<InputType>);
```

Each DATA mailbox sized exactly for its input type - **no waste!**

**Slots:** Configurable per module (default: 50-100 for historical buffering / getData synchronization)

**Memory Efficiency Example:**
```cpp
// Registry with different message sizes
struct SmallSensor { uint32_t value; };           // ~20 bytes with header
struct LargeSensor { float data[1000]; };         // ~4016 bytes with header
struct ResetCmd { };                              // ~16 bytes with header

// Module: Outputs<LargeSensor>, Inputs<SmallSensor>, Commands<ResetCmd>

CMD mailbox size = max(
    sizeof(SubscribeRequest),     // ~32 bytes
    sizeof(UnsubscribeRequest),   // ~24 bytes  
    sizeof(TimsMessage<ResetCmd>) // ~16 bytes
) = 32 bytes

DATA mailbox size = sizeof(TimsMessage<SmallSensor>) = 20 bytes

// Memory allocation:
CMD:  10 slots × 32 bytes = 320 bytes    ✅ Efficient!
DATA: 50 slots × 20 bytes = 1000 bytes   ✅ Efficient!

// If we used max_message_size (4016 bytes):
CMD:  10 slots × 4016 = 40 KB            ❌ Wasteful!
DATA: 50 slots × 4016 = 200 KB           ❌ Wasteful!
```

**Savings: 240 KB → 1.3 KB (185× reduction!)**

## Implementation Steps

### Step 1: Add Address Encoding (address_helpers.hpp)

```cpp
// Address encoding helpers
namespace addressing {

constexpr uint32_t encode_address(uint8_t type_id, uint8_t system_id, 
                                   uint8_t instance_id, uint8_t mailbox_index) {
    return (static_cast<uint32_t>(type_id) << TYPE_ID_SHIFT) |
           (static_cast<uint32_t>(system_id) << SYSTEM_ID_SHIFT) |
           (static_cast<uint32_t>(instance_id) << INSTANCE_ID_SHIFT) |
           (static_cast<uint32_t>(mailbox_index) << MAILBOX_INDEX_SHIFT);
}

constexpr uint32_t get_base_address(uint8_t type_id, uint8_t system_id, uint8_t instance_id) {
    return encode_address(type_id, system_id, instance_id, 0);
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

constexpr uint8_t get_data_mbx_base(uint8_t num_outputs) {
    return CMD_MBX_BASE + num_outputs;  // DATA mailboxes after CMD mailboxes
}

} // namespace addressing
```

### Step 2: Update module_config.hpp

**Remove:**
```cpp
enum class MailboxType : uint8_t {
    CMD = 0,
    WORK = 16,
    PUBLISH = 32,
    DATA = 48
};
```

**Add:**
```cpp
// Mailbox index allocation (RACK-style)
constexpr uint8_t CMD_MBX_BASE = 0;  // CMD mailboxes for outputs start at 0

// Default mailbox slot counts (configurable per module)
constexpr uint32_t DEFAULT_CMD_SLOTS = 10;   // Command buffering
constexpr uint32_t DEFAULT_DATA_SLOTS = 50;  // Historical buffering for getData

// In ModuleConfig struct, add:
struct ModuleConfig {
    // ... existing fields ...
    
    uint32_t cmd_message_slots = DEFAULT_CMD_SLOTS;   // CMD mailbox slots
    uint32_t data_message_slots = DEFAULT_DATA_SLOTS; // DATA mailbox slots (per input)
    
    // Note: message_size is NOW per-mailbox-type, calculated at compile time!
    // - CMD: max(SystemRegistry, Commands<...>)
    // - DATA: sizeof(InputType) for each input
};
```

### Step 3: Update SubscribeRequestPayload

```cpp
struct SubscribeRequestPayload {
    uint32_t subscriber_base_addr;  // Base address (type/system/instance, mailbox=0)
    uint8_t mailbox_index;          // Which mailbox to send data to
    int64_t requested_period_ms;
};
```

### Step 4: Update Multi-Input Infrastructure

```cpp
// In multi_input_infrastructure.hpp

// Compile-time size calculation for DATA mailbox
template<typename InputType>
static constexpr size_t get_data_mailbox_size() {
    return sizeof(TimsMessage<InputType>);
}

template<std::size_t Index>
auto create_historical_mailbox_for_input() {
    auto& module = static_cast<ModuleType&>(*this);
    using InputType = std::tuple_element_t<Index, InputTypesTuple>;
    
    // Calculate base address (type/system/instance)
    uint32_t base_addr = module.calculate_base_address();
    
    // DATA mailboxes start after all CMD mailboxes
    constexpr uint8_t num_outputs = std::tuple_size_v<OutputTypesTuple>;
    uint8_t data_mbx_index = addressing::get_data_mbx_base(num_outputs) + Index;
    
    // Full mailbox address
    uint32_t data_mailbox_id = base_addr | data_mbx_index;
    
    // Compile-time size for THIS specific input type
    constexpr size_t input_message_size = get_data_mailbox_size<InputType>();
    
    std::cout << "[" << module.config_.name << "] Creating DATA mailbox[" << Index 
              << "] at 0x" << std::hex << data_mailbox_id << std::dec
              << " (base=0x" << std::hex << base_addr << std::dec 
              << ", index=" << static_cast<int>(data_mbx_index)
              << ", size=" << input_message_size << " bytes)\n";
    
    MailboxConfig mbx_config{
        .mailbox_id = data_mailbox_id,
        .message_slots = module.config_.message_slots,  // Configurable
        .max_message_size = input_message_size,         // Compile-time, input-specific!
        .send_priority = static_cast<uint8_t>(module.config_.priority),
        .realtime = module.config_.realtime,
        .mailbox_name = module.config_.name + "_data_" + std::to_string(Index)
    };
    
    return HistoricalMailboxFor<InputType>(mbx_config, module.config_.sync_tolerance());
}
```

### Step 5: Update Multi-Output Manager

```cpp
// Compile-time CMD mailbox size calculation
// Max of: SystemRegistry messages + Module's Commands<...>
template<typename... CommandTypes>
static constexpr size_t calculate_cmd_mailbox_size() {
    return std::max({
        sizeof(TimsMessage<SystemRegistry::SubscribeRequest>),
        sizeof(TimsMessage<SystemRegistry::UnsubscribeRequest>),
        sizeof(TimsMessage<SystemRegistry::SubscribeReply>),
        sizeof(TimsMessage<SystemRegistry::UnsubscribeReply>),
        sizeof(TimsMessage<CommandTypes>)...  // User commands
    });
}

// In Module class or MailboxSet creation:
template<std::size_t OutputIndex>
void create_cmd_mailbox() {
    uint32_t base = calculate_base_address();
    uint32_t cmd_address = base | (CMD_MBX_BASE + OutputIndex);
    
    // Compile-time size: max of system messages + module's commands
    constexpr size_t cmd_size = calculate_cmd_mailbox_size<CommandTypes...>();
    
    MailboxConfig cmd_config{
        .mailbox_id = cmd_address,
        .message_slots = config_.cmd_message_slots,  // Configurable (default 10-20)
        .max_message_size = cmd_size,                // Compile-time!
        .send_priority = static_cast<uint8_t>(config_.priority),
        .realtime = config_.realtime,
        .mailbox_name = config_.name + "_cmd_" + std::to_string(OutputIndex)
    };
    
    std::cout << "[" << config_.name << "] Creating CMD mailbox[" << OutputIndex
              << "] at 0x" << std::hex << cmd_address << std::dec
              << " (size=" << cmd_size << " bytes)\n";
    
    // Create mailbox...
}

// Each output gets CMD mailbox at sequential index
template<std::size_t OutputIndex>
uint32_t get_cmd_mailbox_address() const {
    uint32_t base = calculate_base_address();
    return base | (CMD_MBX_BASE + OutputIndex);
}

// Store subscriber info with mailbox index
struct SubscriberInfo {
    uint32_t base_addr;      // Subscriber's base address
    uint8_t mailbox_index;   // Which mailbox to send to
};

std::vector<std::vector<SubscriberInfo>> output_subscribers_;
```

### Step 6: Update Subscription Service

```cpp
void subscribe_to_source_impl(uint8_t source_system_id, uint8_t source_instance_id,
                               size_t source_index) {
    // Calculate our base address (type/system/instance only)
    uint32_t subscriber_base = calculate_base_address(config_->system_id(), config_->instance_id());
    
    // Calculate which mailbox index for this input
    constexpr uint8_t num_outputs = /* ... */;
    uint8_t mailbox_index = addressing::get_data_mbx_base(num_outputs) + source_index;
    
    SubscribeRequestType request{
        .subscriber_base_addr = subscriber_base,
        .mailbox_index = mailbox_index,
        .requested_period_ms = config_->period.count()
    };
    
    // Send to source's CMD mailbox (index=0)
    uint32_t source_base = /* calculate source base */;
    uint32_t source_cmd_mbx = source_base | 0;  // CMD at index 0
    
    work_mbx.send(request, source_cmd_mbx);
}
```

### Step 7: Update Publishing Service

```cpp
// Use CMD mailbox for publishing (not separate PUBLISH mailbox)
template<typename T>
void publish_to_subscribers(T& data, size_t output_index = 0) {
    // Get CMD mailbox for this output
    auto& cmd_mbx = module_ptr_->template get_cmd_mailbox<output_index>();
    
    // Get subscribers for this output
    auto& subscribers = module_ptr_->get_output_subscribers(output_index);
    
    for (const auto& sub : subscribers) {
        // Calculate full subscriber address
        uint32_t dest_addr = sub.base_addr | sub.mailbox_index;
        
        auto result = cmd_mbx.send(data, dest_addr);
        if (!result) {
            std::cerr << "[" << module_name_ << "] Send failed to 0x" 
                      << std::hex << dest_addr << std::dec << "\n";
        }
    }
}
```

## Benefits

1. **No Collisions**: Module identity = [type][system][instance], mailbox = index
2. **Simple**: Only CMD + DATA mailboxes (no WORK, no PUBLISH)
3. **RACK-Compatible**: Follows proven pattern
4. **Debuggable**: 0xTTSSIIMM format easy to read in hex
5. **Scalable**: 256 types sufficient, expandable to 1024 if needed
6. **Efficient**: 64 mailboxes per module covers all realistic use cases
7. **Memory Efficient**: Per-mailbox-type sizing at compile time
   - CMD: Only max(SystemRegistry, module's Commands<...>)
   - DATA: Exact sizeof(InputType) per input
   - **Typical savings: 100-200× reduction vs max_message_size**
8. **Type Safe**: All message sizes validated at compile time
9. **Zero Runtime Overhead**: Address encoding and size calculation are constexpr

## Testing Strategy

1. Update address encoding tests
2. Test single-output, single-input (Filter)
3. Test multi-input (3-input Fusion)
4. Test multi-output (2 outputs)
5. Test combined multi-output + multi-input
6. Verify no address collisions with same system/instance, different types
7. Performance test: mailbox allocation overhead

## Rollback Plan

If issues arise, address encoding change is isolated to:
- address_helpers.hpp (encoding functions)
- Mailbox creation (infrastructure files)
- Subscription protocol (one struct field)

Can revert these changes independently.
