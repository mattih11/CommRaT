# GitHub Copilot Instructions for CommRaT

## Project Overview

**CommRaT** (Communication Runtime) is a C++20 real-time messaging framework built on top of TiMS (TIMS Interprocess Message System). It provides a modern, type-safe, compile-time message passing system with zero runtime overhead.

**Current Status**: Phase 4 Complete (3-mailbox architecture, hierarchical addressing, clean examples)  
**Next Evolution**: Phase 5 - Multi-input/multi-output modules (see `docs/work/ARCHITECTURE_ANALYSIS.md`)

### Core Philosophy
- **Compile-time everything**: Message IDs, registries, and type safety computed at compile time
- **Zero-copy where possible**: Direct memory access, no unnecessary allocations
- **Real-time safe**: No dynamic allocation in hot paths, deterministic execution
- **Template metaprogramming**: Heavy use of C++20 concepts, SFINAE, and compile-time dispatch
- **Simple user-facing API**: Complex template logic should be invisible to users - they only see their message types and simple module interfaces. All the metaprogramming machinery (reflection, type dispatch, ID calculation) happens automatically at compile time behind the scenes

## Architecture

### 3-Mailbox System (RACK-style)
Each module has **three separate mailboxes** with hierarchical addressing:

```cpp
// Address format: [data_type_id:16][system_id:8][instance_id:8] + mailbox_offset
CMD  mailbox: base_address + 0   // User commands
WORK mailbox: base_address + 16  // Subscription protocol (SubscribeRequest/Reply)
DATA mailbox: base_address + 32  // Input data streams (ContinuousInput)
```

**Key principle**: Separate concerns → separate mailboxes → no interference between command, control, and data flows.

### Threading Model
- `command_loop()`: Handles user commands on CMD mailbox (blocking receive)
- `work_loop()`: Handles subscription protocol on WORK mailbox (blocking receive)
- `data_thread_`: Runs periodic/continuous/loop processing
- **All use blocking receives** - no polling, 0% CPU when idle

### Message Flow
1. **Subscription**: Consumer sends SubscribeRequest to producer's WORK mailbox
2. **Acknowledgment**: Producer adds subscriber, replies to consumer's WORK mailbox
3. **Data**: Producer publishes to subscriber's DATA mailbox
4. **Unsubscription**: Consumer sends UnsubscribeRequest on shutdown

## Code Style & Conventions

### Real-Time Constraints

**NEVER use in hot paths (periodic_loop, continuous_loop, process functions):**
```cpp
// ❌ FORBIDDEN in real-time code:
new / delete
malloc / free
std::vector::push_back() // May allocate
std::string operations   // May allocate
std::cout in loops       // Blocking I/O
std::lock_guard          // May block (use lock-free if needed)
throw exceptions         // Unpredictable timing
```

**DO use:**
```cpp
// ✅ REAL-TIME SAFE:
std::array<T, N>              // Fixed-size, stack-allocated
sertial::fixed_vector<T, N>   // Fixed capacity, no allocation
std::atomic<T>                // Lock-free operations
constexpr functions           // Compile-time computation
Templates with static_assert  // Compile-time validation
```

### Template Metaprogramming

CommRaT heavily uses **compile-time dispatch** via templates:

```cpp
// Use concepts for constraints
template<typename T>
concept IsCommRatMessage = requires(T msg) {
    { msg.timestamp } -> std::convertible_to<uint64_t>;
};

// SFINAE for conditional compilation
template<typename T>
static constexpr bool has_continuous_input = 
    std::is_same_v<InputModeT, ContinuousInput<T>>;

if constexpr (has_continuous_input) {
    // Compile-time branch - no runtime cost
}

// Variadic templates for message registries
template<typename... MessageDefs>
class MessageRegistry {
    static constexpr size_t num_types = sizeof...(MessageDefs);
};
```

### Message Definitions

Messages are defined with the **Message::** namespace for clean, simple syntax:

```cpp
// Message structure (plain POD, sertial-serializable)
// USER SEES: Just a simple struct
struct TemperatureData {
    uint64_t timestamp;
    uint32_t sensor_id;
    float temperature_c;
    float confidence;
};

// Registry entry (compile-time ID calculation)
// USER SEES: Simple, clean type alias
using TempMsg = Message::Data<TemperatureData>;

// Advanced usage (explicit prefix/ID if needed)
using CustomMsg = Message::Data<MyData, MessagePrefix::UserDefined, 42>;

// BEHIND THE SCENES: Template magic computes unique IDs, validates structure,
// calculates buffer sizes, generates serialization code - all at compile time
// Message ID format: [prefix:8][subprefix:8][local_id:16]
// Example: 0x01000002 = UserDefined (0x01) + Data (0x00) + ID (0x0002)
```

### Module Pattern

Modules are the core abstraction - derive from `Module<OutputData, InputMode, ...CommandTypes>`:

```cpp
// Producer (periodic publishing)
// USER SEES: Simple class with one process() method
class SensorModule : public Module<TemperatureData, PeriodicInput> {
protected:
    TemperatureData process() override {
        // Called every config_.period
        // MUST be real-time safe (no malloc/IO)
        return TemperatureData{...};
    }
};

// Consumer (continuous input processing)
// USER SEES: Simple class with one process_continuous() method
class FilterModule : public Module<TemperatureData, ContinuousInput<TemperatureData>> {
protected:
    TemperatureData process_continuous(const TemperatureData& input) override {
        // Called for each received message
        // MUST be real-time safe
        return filtered_data;
    }
};

// Command handler
// USER SEES: Simple overloaded methods for each command type
class CommandableModule : public Module<Data, PeriodicInput, CmdA, CmdB> {
protected:
    void on_command(const CmdA& cmd) override {
        // Handle CmdA (compile-time dispatch)
    }
    void on_command(const CmdB& cmd) override {
        // Handle CmdB (different overload)
    }
};

// BEHIND THE SCENES: Module base class creates 3 mailboxes, spawns threads,
// handles subscription protocol, dispatches commands/messages to correct handlers,
// manages memory buffers - all automatically based on template parameters
    }
};
```

## Directory Structure

```
CommRaT/
├── include/commrat/          # Public headers
│   ├── commrat.hpp          # Main include
│   ├── registry_module.hpp  # Module base class (3-mailbox)
│   ├── mailbox.hpp          # Payload-only mailbox wrapper
│   ├── message_registry.hpp # Compile-time registry
│   ├── messages.hpp         # Core message types
│   └── tims_wrapper.hpp     # TiMS C API wrapper
├── src/                     # Implementation files
│   ├── message_service.cpp  # Legacy (being phased out)
│   └── tims_wrapper.cpp     # TiMS initialization
├── examples/                # Usage examples
│   ├── continuous_input_example.cpp  # Producer→Consumer pattern
│   ├── clean_interface_example.cpp   # Simple API showcase
│   ├── command_example.cpp           # Variadic command handling
│   └── loop_mode_example.cpp         # LoopInput maximum throughput demo
├── test/                    # Test executables
├── docs/                    # Documentation
│   ├── README.md           # Documentation overview and quick reference
│   ├── work/               # Active development documentation
│   │   ├── ARCHITECTURE_ANALYSIS.md  # Phase 5 roadmap (multi-I/O)
│   │   └── FIXES_APPLIED.md          # Historical bug fixes
│   └── archive/            # Archived historical documentation
├── tims/                   # TiMS library (external dependency)
└── SeRTial/                # Serialization library (submodule)
```

## Documentation Strategy

### Documentation Structure

**Active Documentation** (`docs/`):
- `README.md` - Comprehensive overview, current state (Phase 4), and Phase 5 roadmap
- `work/ARCHITECTURE_ANALYSIS.md` - Detailed Phase 5 design (multi-I/O modules)
- `work/FIXES_APPLIED.md` - Historical record of bug fixes and solutions
- `archive/` - Archived historical documentation from earlier phases

**When to update documentation:**
- Major architectural changes → Update `ARCHITECTURE_ANALYSIS.md`
- Bug fixes with non-obvious causes → Add to `FIXES_APPLIED.md`
- Phase completion → Update `docs/README.md` current state section
- Design decisions with trade-offs → Add to `ARCHITECTURE_ANALYSIS.md`
- Template metaprogramming insights → Document in relevant analysis file

**Documentation Style**:
- `docs/README.md` - Structured reference with examples, accessible to new users
- `docs/work/` - Developer-focused, explain the "why" not just "what"
- Keep examples up-to-date with latest API changes
- Archive obsolete docs to `docs/archive/` - don't delete history

### Code Documentation
Use Doxygen-style comments for public APIs:

```cpp
/**
 * @brief Send a message to a destination mailbox
 * 
 * This is a compile-time type-safe send operation. The message type
 * must be registered in the mailbox's registry.
 * 
 * @tparam T Message payload type (must be registered)
 * @param message Message to send
 * @param dest_mailbox Destination mailbox ID
 * @return Success or error
 * 
 * @note Real-time safe if T's serialization is real-time safe
 */
template<typename T>
    requires is_registered<T>
auto send(T& message, uint32_t dest_mailbox) -> MailboxResult<void>;
```

## Common Patterns

### Compile-Time Registry Expansion
```cpp
// User defines messages with clean syntax
using MyRegistry = MessageRegistry<
    Message::Data<DataA>,
    Message::Data<DataB>
>;

// System automatically adds subscription messages
using FullRegistry = CombinedRegistry<
    SystemRegistry,  // SubscribeRequest, SubscribeReply, etc.
    MyRegistry
>;
```

### Address Calculation
```cpp
// Hierarchical addressing (compile-time friendly)
uint32_t base = calculate_base_address(system_id, instance_id);
// base = (data_type_id & 0xFFFF) | (system_id << 8) | instance_id

uint32_t cmd_mbx  = base + 0;   // Command mailbox
uint32_t work_mbx = base + 16;  // Subscription protocol
uint32_t data_mbx = base + 32;  // Data streaming
```

### Visitor Pattern for Type Dispatch
```cpp
// Runtime message type dispatch (efficient)
mailbox.receive_any([](auto&& received_msg) {
    using MsgType = std::decay_t<decltype(received_msg.message)>;
    
    if constexpr (std::is_same_v<MsgType, TemperatureData>) {
        handle_temperature(received_msg.message);
    } else if constexpr (std::is_same_v<MsgType, CommandData>) {
        handle_command(received_msg.message);
    }
});
```

## Testing Guidelines

### Unit Tests
Focus on:
- Compile-time correctness (static_assert)
- Message ID uniqueness
- Address collision detection
- Serialization round-trip

### Integration Tests
- Multi-module communication
- Subscription protocol correctness
- Message delivery guarantees
- Graceful shutdown

### Performance Tests
- Real-time constraints verification
- CPU usage when idle (should be ~0%)
- Message latency measurement
- Memory footprint validation

## Migration Notes

### Recent Changes (Phase 3 → Phase 4)
- **OLD**: Single mailbox with polling (100ms timeout)
- **NEW**: 3 separate mailboxes with blocking receives
- **Impact**: 
  - Config changed: `mailbox_id` → `system_id` + `instance_id`
  - Subscription uses WORK mailbox now
  - Data streams use DATA mailbox
  - Commands use CMD mailbox

### Backward Compatibility
Legacy examples may still use old single-mailbox API. When updating:
1. Change `ModuleConfig` to use `system_id`/`instance_id`
2. Update subscription to use source IDs, not mailbox ID
3. Test with 3-mailbox addressing scheme

## SeRTial Integration

**SeRTial** is our serialization library (similar to flatbuffers but simpler):

```cpp
// Message wrapper (header + payload)
template<typename T>
struct TimsMessage {
    TimsHeader header;  // Timestamp, seq_number, msg_type, etc.
    T payload;          // User data
    
    using payload_type = T;  // Type introspection
};

// Serialization (compile-time size calculation)
auto result = sertial::Message<TimsMessage<T>>::serialize(msg);
// result.view() returns std::span<std::byte>

// Deserialization (zero-copy when possible)
auto result = sertial::Message<TimsMessage<T>>::deserialize(buffer);
// result.value() returns TimsMessage<T>
```

## When Suggesting Code

**DO:**
- Use templates with concepts for type constraints
- Prefer `constexpr` over runtime checks
- Use `std::array` and `sertial::fixed_vector` for collections
- Use `std::atomic` for thread-safe counters
- Document real-time safety in comments
- Suggest compile-time validation via `static_assert`
- Hide complexity behind simple APIs - users should only interact with their message types and basic module interfaces
- Use elaborate template metaprogramming internally (SFINAE, concepts, variadic templates) to compute everything at compile time
- Make the simple case trivial - sensible defaults, minimal required configuration

**DON'T:**
- Suggest `std::vector` or `std::string` in hot paths
- Use exceptions in real-time code
- Suggest dynamic polymorphism (virtual functions) without justification
- Ignore const-correctness
- Use `auto` when type clarity is important
- Expose template complexity to end users - keep metaprogramming machinery internal
- Require users to understand reflection, type dispatch, or ID calculation mechanisms
- Add configuration parameters that users must understand - compute automatically when possible

## Error Handling

Prefer **std::expected-like** pattern (MailboxResult):

```cpp
// Return type that can be success or error
template<typename T>
class MailboxResult {
    std::optional<T> value_;
    std::optional<MailboxError> error_;
public:
    explicit operator bool() const { return value_.has_value(); }
    T& value() { return *value_; }
    MailboxError error() const { return *error_; }
};

// Usage
auto result = mailbox.send(msg, dest);
if (!result) {
    std::cerr << "Send failed: " << to_string(result.error()) << "\n";
    return;
}
```

## Future Considerations

### Planned Features
- ROS 2 adapter (rclcpp-commrat) - separate repository
- DDS compatibility layer
- Performance profiling tools
- Static analysis for real-time safety

### Technical Debt
- Command dispatch (on_command overloads) needs improvement
- Legacy MessageService being phased out
- Some examples need update to 3-mailbox system

## Questions to Ask Yourself

Before suggesting code:
1. Is this real-time safe? (No malloc, no blocking I/O)
2. Can this be computed at compile time?
3. Does this respect the 3-mailbox architecture?
4. Is the type constraint clear and enforced?
5. Would this cause unnecessary copies?
6. Is the error handling explicit and testable?

## Summary

CommRaT is a **compile-time**, **real-time**, **type-safe** messaging framework. When writing code:
- Think templates, not runtime dispatch
- Think fixed-size, not dynamic allocation
- Think blocking receives, not polling
- Think separate mailboxes, not multiplexing
- Think compile-time validation, not runtime errors

**Mantra**: If it can be checked at compile time, it should be checked at compile time.
