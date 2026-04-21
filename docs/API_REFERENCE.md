# CommRaT API Reference

**Version**: 2.1.0
**Last Updated**: April 13, 2026

Accurate API reference derived from header files. For Doxygen docs, run `make docs`.

---

## Table of Contents

1. [Application Template (CommRaT)](#application-template)
2. [Module2 Base Class](#module2-base-class)
3. [I/O Specification Tags](#io-specification-tags)
4. [Synced Wrapper](#synced-wrapper)
5. [ModuleOutput](#moduleoutput)
6. [ContinuousInput](#continuousinput)
7. [SyncedInputImpl](#syncedinputimpl)
8. [Messages and Serialization](#messages-and-serialization)
9. [Message Definitions (MessageDefinition, Message::Data, etc.)](#message-definitions)
10. [Message Registry](#message-registry)
11. [Module Configuration](#module-configuration)
12. [Duration](#duration)
13. [Timestamp / Time](#timestamp--time)
14. [Threading Abstractions](#threading-abstractions)
15. [Platform Selection](#platform-selection)
16. [Introspection](#introspection)

---

## Application Template

**Header:** `<commrat/commrat.hpp>`

```cpp
template<typename... MessageDefs>
class CommRaT : public MessageRegistry<MessageDefs..., SubscribeRequest, UnsubscribeRequest>;
```

Automatically includes system messages (`SubscribeRequest`, `UnsubscribeRequest` and their replies).

### Type Aliases

| Alias | Type |
|-------|------|
| `Registry` | `MessageRegistry<MessageDefs..., SubscribeRequest, UnsubscribeRequest>` |
| `UserRegistry` | `MessageRegistry<MessageDefs...>` (user types only) |
| `payload_types` | `typename Registry::PayloadTypes` (tuple of all payload types) |
| `Module2<IOSpecs...>` | `commrat::Module2<CommRaT, IOSpecs...>` |
| `Mailbox<PayloadT>` | `commrat::Mailbox<MessageDefs..., SubscribeRequest, UnsubscribeRequest>` |
| `Introspection` | `IntrospectionHelper<CommRaT>` |

### Inherited from Registry

```cpp
template<typename T> static constexpr bool is_registered;
template<typename T> static constexpr uint32_t get_message_id();
template<typename T> static auto serialize(TimsMessage<T>& message);
template<typename T> static auto deserialize(std::span<const std::byte> data);
template<typename Visitor> static bool visit(uint32_t msg_id, std::span<const std::byte> data, Visitor&& v);
template<typename Callback> static bool dispatch(uint32_t msg_id, std::span<const std::byte> data, Callback&& cb);
static constexpr size_t max_message_size;
```

### System Nested Struct

```cpp
struct System {
    using PayloadTypes = std::tuple<SubscribeRequestPayload, SubscribeReplyPayload,
                                    UnsubscribeRequestPayload, UnsubscribeReplyPayload>;
    template<typename T> static constexpr uint32_t get_message_id();
    using WorkMailbox = MailboxFor<Registry>;  // Unrestricted mailbox for subscription protocol
};
```

### Usage

```cpp
using MyApp = commrat::CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<PressureData>,
    Message::Command<ResetCmd>
>;

class Sensor : public MyApp::Module2<Output<SensorData>, Period<Milliseconds(10)>> {
    // ...
};
```

---

## Module2 Base Class

**Header:** `<commrat/module2.hpp>`

```cpp
template<typename Registry, typename... IOSpecs>
class Module2;
```

When used via `MyApp::Module2<IOSpecs...>`, `Registry` is bound to `CommRaT`.

### Template Parameters

- `Registry` -- Message registry (bound automatically via `CommRaT::Module2`)
- `IOSpecs...` -- I/O specifications: `Output<T>`, `Input<T>`, `SyncedInput<T>`, `Period<D>`

### Public Type Aliases

```cpp
using OutputData = typename IO::Meta::SingleOutputType;  // Payload type if exactly 1 output, else void
using InputData  = typename IO::Meta::SingleInputType;   // Payload type if exactly 1 input, else void
```

### Constructor / Destructor

```cpp
explicit Module2(const ModuleConfig& config);
virtual ~Module2();  // Calls stop(), joins command threads, stops work mailbox
```

### Public Lifecycle Methods

```cpp
void start();  // Start mailboxes, subscribe inputs, launch data + command threads
void stop();   // Set stop flag, unsubscribe inputs, stop outputs, join threads
```

### Protected Lifecycle Hooks

```cpp
virtual void on_start();  // Called at end of start() -- user override
virtual void on_stop();   // Called at beginning of stop() -- user override
```

### Pure Virtual -- process()

Signature is auto-determined from `IOSpecs`:

```cpp
// No inputs (timer-driven or loop-driven):
void process(O1& out1, O2& out2, ...) override;

// With continuous input:
void process(const I1& in1, O1& out1, ...) override;

// With continuous + synced inputs:
void process(const I1& in1, Synced<I2> synced_in, ..., O1& out1, ...) override;
```

### Protected Metadata Accessors

```cpp
template<size_t InputIndex> uint64_t get_input_timestamp() const;
template<size_t InputIndex> bool has_new_data() const;
template<size_t InputIndex> bool is_input_valid() const;
```

All require `InputIndex < IO::Meta::num_inputs`.

### Protected Member

```cpp
ModuleConfig config_;
```

### Threading Model

- **1 data thread** -- runs `process()` in a loop (timer/input/loop driven)
- **N command threads** -- one per output, blocking receive on CMD mailbox
- **Work mailbox** -- no dedicated thread, used for outbound sends

---

## I/O Specification Tags

**Header:** `<commrat/module/io/io_spec.hpp>`

### Output\<T\>

```cpp
template<typename T>
struct Output {
    using Type = T;
    static constexpr bool is_output = true;
};
```

Declares a module output of type `T`. Multiple `Output<T>` allowed per module.

### Input\<T\>

```cpp
template<typename T>
struct Input {
    using Type = T;
    static constexpr bool is_input = true;
    static constexpr bool is_continuous = true;
};
```

Continuous push-model input. At most one per module. Mutually exclusive with `Period<>`.

### SyncedInput\<T\>

```cpp
template<typename T>
struct SyncedInput {
    using Type = T;
    static constexpr bool is_input = true;
    static constexpr bool is_synced = true;
};
```

Pull-model secondary input. Uses `get_data(timestamp)` for synchronization. Multiple allowed.

### Period\<DefaultPeriod\>

```cpp
template<auto DefaultPeriod>
struct Period {
    static constexpr auto default_period = DefaultPeriod;
    static constexpr bool is_periodic = true;
};
```

Timer-driven execution at fixed period. Mutually exclusive with `Input<>`.

### Execution Mode Inference

| Condition | Mode |
|-----------|------|
| `Input<T>` present | Input-driven (blocking receive) |
| `Period<D>` present, no `Input<T>` | Timer-driven (periodic sleep) |
| Neither | Loop-driven (max throughput) |

Constraints enforced at compile time:
- At most one `Input<T>`
- At most one `Period<D>`
- `Input<T>` and `Period<D>` are mutually exclusive

### Type Traits

```cpp
template<typename T> inline constexpr bool is_output_v;
template<typename T> inline constexpr bool is_input_v;
template<typename T> inline constexpr bool is_synced_input_v;
template<typename T> inline constexpr bool is_period_v;
template<typename T> inline constexpr bool is_continuous_input_v;
template<typename T> inline constexpr bool is_synced_input_instance_v;
template<typename T> inline constexpr bool is_module_output_v;
```

### BuildIOTuple\<Registry, IOSpecs...\>

Compile-time builder that converts I/O specs to a tuple of instances.

```cpp
template<typename Registry, typename... IOSpecs>
struct BuildIOTuple {
    using type = /* std::tuple<ModuleOutput<...>, ContinuousInput<...>, ...> */;
    static constexpr size_t num_outputs;
    static constexpr size_t num_continuous_inputs;
    static constexpr size_t num_synced_inputs;
    static constexpr bool is_input_driven;
    static constexpr bool is_timer_driven;
    static constexpr bool is_loop_driven;

    struct Meta {
        static constexpr size_t num_outputs;
        static constexpr size_t num_inputs;         // continuous + synced
        static constexpr bool has_inputs;
        static constexpr bool has_outputs;
        static constexpr bool is_input_driven;
        static constexpr bool is_timer_driven;
        static constexpr bool is_loop_driven;
        static constexpr size_t primary_input_index;
        static constexpr bool has_primary_input;
        static constexpr auto period;               // Milliseconds (timer-driven only)

        using OutputTypes;      // std::tuple<T1, T2, ...>
        using InputTypes;       // std::tuple<T1, T2, ...>
        using InputWrappers;    // Wrapper types for ProcessorBase
        using SingleOutputType; // T if exactly 1 output, else void
        using SingleInputType;  // T if exactly 1 input, else void
    };

    static constexpr auto output_indices();         // std::array mapping logical -> tuple indices
    static constexpr auto input_indices();

    template<size_t OutputIndex, typename IOTupleType>
    static constexpr auto& get_output(IOTupleType&& tuple);

    template<size_t InputIndex, typename IOTupleType>
    static constexpr auto& get_input(IOTupleType&& tuple);
};
```

---

## Synced Wrapper

**Header:** `<commrat/module/io/synced.hpp>`

```cpp
template<typename T>
class Synced;
```

Zero-copy wrapper for synchronized secondary input data. Holds `const T*` with validity/freshness metadata.

### States

- **Fresh**: `get_data()` found data matching requested timestamp exactly
- **Stale**: Valid data, but not an exact timestamp match
- **Invalid**: No data available

### Public Type Alias

```cpp
using value_type = T;
```

### Constructors

```cpp
constexpr Synced() noexcept;                             // Invalid state
constexpr Synced(const T& data, bool is_fresh) noexcept; // From reference
```

### Validity Checks (const)

```cpp
constexpr explicit operator bool() const noexcept;  // true if FRESH
constexpr bool is_fresh() const noexcept;            // true if valid AND fresh
constexpr bool has_stale() const noexcept;           // true if valid but NOT fresh
constexpr bool is_valid() const noexcept;            // true if any valid data
```

### Data Access (const, zero-copy)

```cpp
constexpr const T& value() const noexcept;                        // Fresh only (asserts)
constexpr const T& stale() const noexcept;                        // Any valid (asserts)
constexpr const T& value_or(const T& default_value) const noexcept;  // Fresh or default
constexpr const T& stale_or(const T& default_value) const noexcept;  // Valid or default
constexpr const T& operator*() const noexcept;                    // Alias for stale()
constexpr const T* operator->() const noexcept;                   // Member access (asserts valid)
```

### Modification (non-const -- internal use by SyncedInput)

```cpp
constexpr Synced& operator=(const T& data) noexcept;  // Set fresh data
constexpr void mark_stale() noexcept;                  // Keep data, mark not fresh
constexpr void reset() noexcept;                       // Clear to invalid
```

---

## ModuleOutput

**Header:** `<commrat/module/io/output/module_output.hpp>`

```cpp
template<typename CommratApp, typename T, std::size_t SLOTS = 100>
class ModuleOutput;
```

Output with timestamped ring buffer. Provides publish, get_data, workspace, and subscription management.

### Public Type Aliases

```cpp
using Type = T;
using ConfigType = OutputConfig;
using DataMessage = Message::Data<T>;
using message_def_type = DataMessage;
using CmdMailbox = TypedMailbox<CommratApp, SubscribeRequestPayload, SubscribeReplyPayload,
                                UnsubscribeRequestPayload, UnsubscribeReplyPayload,
                                GetDataRequestPayload<T>, GetDataReplyPayload<T>,
                                GetNextDataRequestPayload<T>, GetNextDataReplyPayload<T>>;
using PublishMailbox = TypedMailbox<CommratApp, T>;
```

### Constructors

```cpp
ModuleOutput();  // Default (uninitialized, must call initialize())
ModuleOutput(SystemId system_id, InstanceId instance_id,
             Duration default_tolerance = Milliseconds(50));
```

### Lifecycle

```cpp
void initialize(uint8_t sys_id, uint8_t inst_id, Duration tolerance);  // Allocation phase
void start();  // Activate mailboxes
void stop();   // Deactivate mailboxes
```

### Publishing

```cpp
void publish(T&& data, Timestamp timestamp);       // Move-publish to all subscribers
void publish(const T& data, Timestamp timestamp);   // Copy-publish
T& get_workspace();                                  // Zero-copy workspace buffer
void publish_workspace(Timestamp timestamp);         // Move workspace into buffer + send
```

### Data Queries

```cpp
const TimsMessage<T>* get_data(uint64_t timestamp,
                                Duration tolerance = Duration::milliseconds(-1),
                                InterpolationMode mode = InterpolationMode::NEAREST) const;
std::pair<uint64_t, uint64_t> get_timestamp_range() const;
std::size_t buffer_size() const;
void clear_buffer();
```

### Mailbox Access

```cpp
CmdMailbox& get_cmd_mailbox();
uint32_t get_cmd_address() const;
```

### Command Handlers

```cpp
auto handle_subscribe_request(const SubscribeRequestPayload& req) -> SubscribeReplyPayload;
auto handle_unsubscribe_request(const UnsubscribeRequestPayload& req) -> UnsubscribeReplyPayload;
auto handle_get_data_request(const GetDataRequestPayload<T>& req) -> GetDataReplyPayload<T>;
auto handle_get_next_data_request(const GetNextDataRequestPayload<T>& req) -> GetNextDataReplyPayload<T>;
```

### Constants

```cpp
static constexpr std::size_t kMaxSubscribers = 16;
```

---

## ContinuousInput

**Header:** `<commrat/module/io/input/continuous_input.hpp>`

```cpp
template<typename Registry, typename OutputType>
class ContinuousInput : public CmdInput<Registry, OutputType>;
```

Push-model input. Receives continuous data stream via subscription to a producer.

### Public Type Aliases

```cpp
using DataMessage = Message::Data<OutputType>;
using DataMailbox = TypedMailbox<Registry, OutputType>;
```

### Constructors

```cpp
ContinuousInput();  // Default (uninitialized)
ContinuousInput(MailboxFor<Registry>& work_mbx, MailboxFor<Registry>& data_mbx,
                uint8_t producer_system_id, uint8_t producer_instance_id,
                Milliseconds requested_period = Milliseconds::zero(),
                Milliseconds poll_timeout = Milliseconds(100),
                Milliseconds cmd_timeout = Milliseconds(1000));
```

### Initialization / Lifecycle

```cpp
void initialize(typename Registry::System::WorkMailbox& work_mbx,
                const MailboxConfig& data_mbx_config,
                uint8_t producer_system_id, uint8_t producer_instance_id,
                Milliseconds requested_period = Milliseconds::zero(),
                Milliseconds poll_timeout = Milliseconds(100),
                Milliseconds cmd_timeout = Milliseconds(1000));
void start();  // Activate DATA mailbox
void stop();   // Deactivate DATA mailbox
```

### Subscription Protocol

```cpp
bool subscribe(Milliseconds& actual_period, Milliseconds timeout = Milliseconds::zero());
bool unsubscribe(Milliseconds timeout = Milliseconds::zero());
```

### Data Access

```cpp
bool poll_data();                            // Blocking receive into internal buffer (zero-copy)
const OutputType& get_payload() const;       // Last received payload
const TimsHeader& get_header() const;        // Last received header
uint64_t get_timestamp() const;              // Header timestamp (nanoseconds)
uint32_t get_sequence_number() const;
uint32_t get_message_type() const;
```

### Status

```cpp
bool has_data() const;           // True if last poll_data() succeeded
bool is_valid() const;           // Alias for has_data()
bool is_fresh() const;           // True if valid (continuous is always fresh when valid)
bool is_subscribed() const;
Milliseconds get_actual_period() const;
```

---

## SyncedInputImpl

**Header:** `<commrat/module/io/input/synced_input.hpp>`

```cpp
template<typename Registry, typename OutputType>
class SyncedInputImpl : public CmdInput<Registry, OutputType>;
```

Pull-model input. Retrieves data via RPC `get_data(timestamp)` to producer's CMD mailbox.

### Public Type Alias

```cpp
using DataMessage = Message::Data<OutputType>;
```

### Constructors

```cpp
SyncedInputImpl();  // Default (uninitialized)
SyncedInputImpl(MailboxFor<Registry>& work_mbx,
                uint8_t producer_system_id, uint8_t producer_instance_id,
                Milliseconds tolerance = Milliseconds(50),
                InterpolationMode interpolation = InterpolationMode::NEAREST,
                Milliseconds cmd_timeout = Milliseconds(100));
```

### Initialization

```cpp
void initialize(typename Registry::System::WorkMailbox& work_mbx,
                uint8_t producer_system_id, uint8_t producer_instance_id,
                Milliseconds tolerance = Milliseconds(50),
                InterpolationMode interpolation = InterpolationMode::NEAREST,
                Milliseconds cmd_timeout = Milliseconds(100));
```

### Data Retrieval (RPC)

```cpp
bool get_data(const Timestamp& timestamp, const TimsMessage<OutputType>*& msg,
              Milliseconds timeout = Milliseconds(0));
bool get_next_data(const TimsMessage<OutputType>*& msg,
                   Milliseconds timeout = Milliseconds(0));
```

### Data Access

```cpp
Synced<OutputType> get_payload() const;     // Returns Synced wrapper with validity/freshness
const TimsHeader& get_header() const;
uint64_t get_timestamp() const;
uint32_t get_sequence_number() const;
uint32_t get_message_id() const;
```

### Status

```cpp
bool is_valid() const;   // True if last get_data/get_next_data found data
bool is_fresh() const;   // True if data was an exact timestamp match
```

---

## Messages and Serialization

**Header:** `<commrat/messages.hpp>`

### TimsHeader

```cpp
struct TimsHeader {
    uint32_t msg_type;      // Message type ID
    uint32_t msg_size;      // Set by serialization
    uint64_t timestamp;     // Set by send()
    uint32_t seq_number;    // Set by send()
    uint32_t dest;          // Destination mailbox address
    uint32_t src;           // Source mailbox address (for replies)
    uint32_t flags;
};
```

### TimsMessage\<PayloadT\>

```cpp
template<typename PayloadT>
struct TimsMessage {
    TimsHeader header;
    PayloadT payload;
    using payload_type = PayloadT;
};
```

Aggregate type (no constructors). Use designated initializers.

### Type Traits

```cpp
template<typename T> inline constexpr bool is_commrat_message_v;  // true for TimsMessage<P>
template<typename T> using message_payload_t;                     // Extract PayloadT from TimsMessage<P>
```

### Serialization Functions

```cpp
template<typename T> auto serialize(T& message);                            // Requires is_commrat_message_v<T>
template<typename T> auto deserialize(std::span<const std::byte> data);
template<typename T> auto deserialize(const uint8_t* data, size_t size);    // TIMS compatibility
```

### Compile-Time Size Utilities

```cpp
template<typename T> inline constexpr size_t max_message_buffer_size_v;
template<typename T> inline constexpr size_t packed_message_size_v;
template<typename T> inline constexpr bool message_has_padding_v;
```

---

## Message Definitions

**Header:** `<commrat/messaging/message_id.hpp>`, `<commrat/messaging/message_helpers.hpp>`

### MessageDefinition

```cpp
template<
    typename PayloadT,
    MessagePrefix Prefix_ = MessagePrefix::UserDefined,
    auto SubPrefix_ = UserSubPrefix::Data,
    uint16_t ID_ = 0,           // 0 = auto-assign
    typename ReplyT = void      // void = no reply
>
struct MessageDefinition;
```

#### Static Members

```cpp
static constexpr MessagePrefix prefix;
static constexpr uint8_t subprefix;
static constexpr uint16_t local_id;          // 0 = auto-assigned at registry construction
static constexpr bool needs_auto_id;         // true if local_id == 0
static constexpr bool is_request;            // true if ReplyT != void
static constexpr bool is_reply;              // true if derived as reply (negative ID)
static constexpr bool has_reply;             // true if ReplyT != void
static constexpr uint16_t request_id;        // For replies: the request's ID
```

#### Type Aliases

```cpp
using Payload = PayloadT;
using ReplyPayload = ReplyT;
using ReplyMessageDef = /* auto-generated MessageDefinition with negated ID, or void */;
```

### Message ID Structure

Format: `0xPSMM` where P=prefix, S=subprefix, MM=message ID (2 bytes).

```cpp
constexpr uint32_t make_message_id(uint8_t prefix, uint8_t subprefix, uint16_t id);
constexpr uint32_t system_message_id(SystemSubPrefix subprefix, uint16_t id);
constexpr uint32_t user_message_id(UserSubPrefix subprefix, uint16_t id);
```

### Enums

```cpp
enum class MessagePrefix : uint8_t { System = 0x00, UserDefined = 0x01 };
enum class SystemSubPrefix : uint8_t { Subscription = 0x00, Control = 0x01, Reserved = 0xFF };
enum class UserSubPrefix : uint8_t { Data = 0x00, Commands = 0x01, Events = 0x02,
                                      GetData = 0x03, GetNextData = 0x04, Custom = 0x05 };
```

### Constants

```cpp
static constexpr uint16_t MAX_MESSAGE_ID = 0x7FFF;  // Sign bit reserved for reply IDs
```

### Convenience Aliases (Message:: namespace)

```cpp
namespace Message {
    // Data<T> -- UserDefined::Data, auto-ID. Includes GetData protocol support.
    template<typename T, MessagePrefix Prefix = UserDefined, uint16_t LocalID = 0>
    using Data = DataMessageDef<T, Prefix, LocalID>;

    // Command<T, ReplyT> -- UserDefined::Commands, auto-ID. Optional reply type.
    template<typename T, typename ReplyT = void, MessagePrefix Prefix = UserDefined, uint16_t LocalID = 0>
    using Command = MessageDefinition<T, Prefix, UserSubPrefix::Commands, LocalID, ReplyT>;

    // Event<T> -- UserDefined::Events, auto-ID.
    template<typename T, MessagePrefix Prefix = UserDefined, uint16_t LocalID = 0>
    using Event = MessageDefinition<T, Prefix, UserSubPrefix::Events, LocalID>;

    // DataWith<PayloadT>::Commands<CmdDefs...> -- Data with associated commands.
    template<typename PayloadT>
    struct DataWith {
        template<typename... CommandTypes>
        using Commands = DataWithCommands<PayloadT, CommandTypes...>;
    };
}
```

`Message::Data<T>` additionally provides:

```cpp
using GetDataRequestDef;
using GetDataReplyDef;
using GetNextDataRequestDef;
using GetNextDataReplyDef;
```

---

## Message Registry

**Header:** `<commrat/messaging/message_registry.hpp>`

```cpp
template<typename... MessageDefs>
class MessageRegistry;
```

Compile-time registry with auto-ID assignment, reply expansion, GetData expansion, and collision detection.

### Public Type Aliases

```cpp
using MessageDefsTuple = /* std::tuple of all processed MessageDefinitions (after expansion) */;
using PayloadTypes = /* std::tuple of all payload types */;
template<uint32_t ID> using PayloadTypeFor = /* payload type for given message ID */;
```

### Public Constants

```cpp
static constexpr size_t num_types;          // Number of user-provided MessageDefs
static constexpr size_t max_message_size;   // Max serialized size across all types
template<uint32_t ID> static constexpr bool has_message_id;
```

### Public Static Methods

```cpp
static constexpr size_t size();                        // Actual count after expansion

template<typename T> static constexpr bool is_registered;
template<typename T> static constexpr uint32_t get_message_id();  // requires is_registered<T>

template<typename... SpecificTypes>
static constexpr size_t max_size_for_types();          // Max size for subset of types

template<typename T> static auto serialize(T& message);                     // Payload type
template<typename PayloadT> static auto serialize(TimsMessage<PayloadT>&);  // Full message
template<typename T> static auto deserialize(std::span<const std::byte>);   // Payload or TimsMessage

template<typename Visitor>
static bool visit(uint32_t msg_id, std::span<const std::byte> data, Visitor&& visitor);

template<typename Callback>
static bool dispatch(uint32_t msg_id, std::span<const std::byte> data, Callback&& callback);

static constexpr size_t max_buffer_size();
```

---

## Module Configuration

**Header:** `<commrat/module/module_config.hpp>`

### ModuleConfig

```cpp
struct ModuleConfig {
    std::string name;

    OutputConfig outputs = SimpleOutputConfig{.system_id = 0, .instance_id = 0};
    InputConfig inputs = NoInputConfig{};

    std::optional<std::chrono::milliseconds> period{std::chrono::milliseconds{100}};
    size_t message_slots{10};
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};

    rfl::DefaultVal<uint32_t> cmd_message_slots = DEFAULT_CMD_SLOTS;   // 10
    rfl::DefaultVal<uint32_t> data_message_slots = DEFAULT_DATA_SLOTS; // 50
};
```

### Output Configuration Accessors

```cpp
uint8_t system_id() const;              // NoOutput or SimpleOutput
uint8_t instance_id() const;            // NoOutput or SimpleOutput
uint8_t system_id(size_t index) const;  // MultiOutput
uint8_t instance_id(size_t index) const;// MultiOutput
bool has_no_output() const;
bool has_simple_output() const;
bool has_multi_output_config() const;
```

### Input Configuration Accessors

```cpp
uint8_t source_system_id() const;                // SingleInput
uint8_t source_instance_id() const;              // SingleInput
const std::vector<MultiInputConfig::InputSource>& input_sources() const;  // MultiInput
Duration sync_tolerance() const;                                          // MultiInput
size_t history_buffer_size() const;                                       // MultiInput
uint8_t input_system_id(size_t index) const;                              // MultiInput
uint8_t input_instance_id(size_t index) const;                            // MultiInput
bool has_no_input() const;
bool has_single_input() const;
bool has_multi_input_config() const;
```

### Output Config Variants

```cpp
struct NoOutputConfig     { uint8_t system_id{0}; uint8_t instance_id{0}; };
struct SimpleOutputConfig { uint8_t system_id{0}; uint8_t instance_id{0}; };
struct MultiOutputConfig  {
    struct OutputAddress { uint8_t system_id{0}; uint8_t instance_id{0}; };
    std::vector<OutputAddress> addresses;
};
using OutputConfig = rfl::TaggedUnion<"output_type", NoOutputConfig, SimpleOutputConfig, MultiOutputConfig>;
```

### Input Config Variants

```cpp
struct NoInputConfig {};
struct SingleInputConfig { uint8_t source_system_id{0}; uint8_t source_instance_id{0}; };
struct MultiInputConfig {
    struct InputSource {
        uint8_t system_id{0};
        uint8_t instance_id{0};
        bool is_primary{false};
        mutable size_t input_index{0};
    };
    std::vector<InputSource> sources;
    size_t history_buffer_size{100};
    std::chrono::milliseconds sync_tolerance{50};
};
using InputConfig = rfl::TaggedUnion<"input_type", NoInputConfig, SingleInputConfig, MultiInputConfig>;
```

---

## Duration

**Header:** `<commrat/platform/duration.hpp>`

Fixed-precision duration type backed by nanoseconds. Structural type (public `int64_t ns_` member) for C++20 NTTP compatibility with `Period<Milliseconds(100)>`.

Replaces the old `using Milliseconds = std::chrono::milliseconds` type aliases.
`Milliseconds()`, `Seconds()`, etc. are now **constexpr free functions** returning `Duration`.

### Duration Class

```cpp
class Duration {
public:
    int64_t ns_ = 0;  // Public for structural type / NTTP

    constexpr Duration() noexcept = default;
    constexpr explicit Duration(int64_t nanoseconds) noexcept;

    // Named factory methods
    static constexpr Duration nanoseconds(int64_t v) noexcept;
    static constexpr Duration microseconds(int64_t v) noexcept;
    static constexpr Duration milliseconds(int64_t v) noexcept;
    static constexpr Duration seconds(int64_t v) noexcept;
    static constexpr Duration minutes(int64_t v) noexcept;
    static constexpr Duration hours(int64_t v) noexcept;
    static constexpr Duration zero() noexcept;

    // Accessors
    constexpr int64_t count_ns() const noexcept;
    constexpr int64_t count_us() const noexcept;
    constexpr int64_t count_ms() const noexcept;
    constexpr int64_t count_s() const noexcept;

    // Predicates
    constexpr bool is_zero() const noexcept;
    constexpr bool is_negative() const noexcept;
    constexpr bool is_positive() const noexcept;

    // Arithmetic: +, -, unary -, +=, -=, *, /, %
    // Comparison: C++20 three-way (<=>) and ==

    // Chrono interop
    constexpr std::chrono::nanoseconds to_chrono_ns() const noexcept;
    constexpr std::chrono::milliseconds to_chrono_ms() const noexcept;
    template<typename Rep, typename Period>
    static constexpr Duration from_chrono(std::chrono::duration<Rep, Period> d) noexcept;

    // POSIX interop
    constexpr struct timespec to_timespec() const noexcept;
    static constexpr Duration from_timespec(const struct timespec& ts) noexcept;
};
```

### Free Function Constructors

```cpp
constexpr Duration Nanoseconds(int64_t v) noexcept;
constexpr Duration Microseconds(int64_t v) noexcept;
constexpr Duration Milliseconds(int64_t v) noexcept;
constexpr Duration Seconds(int64_t v) noexcept;
constexpr Duration Minutes(int64_t v) noexcept;
constexpr Duration Hours(int64_t v) noexcept;
```

These are functions, not types. Use `Duration` whenever you need a type name:

```cpp
// Type for variables/parameters/members:
Duration timeout = Milliseconds(100);

// Template NTTP (structural type):
class MySensor : public MyApp::Module2<Output<Data>, Period<Milliseconds(100)>> { ... };
```

### User-Defined Literals

```cpp
namespace commrat::literals {
    constexpr Duration operator""_ns(unsigned long long v) noexcept;
    constexpr Duration operator""_us(unsigned long long v) noexcept;
    constexpr Duration operator""_ms(unsigned long long v) noexcept;
    constexpr Duration operator""_s(unsigned long long v) noexcept;
}
```

Usage:
```cpp
using namespace commrat::literals;
auto timeout = 100_ms;
auto delay = 50_us;
auto period = 1_s;
```

---

## Timestamp / Time

**Header:** `<commrat/platform/timestamp.hpp>`

Timestamp and time utilities. Backend selected at compile time (std:: or EVL).

### Timestamp Type

```cpp
using Timestamp = uint64_t;  // Nanoseconds since epoch
```

### Time Class

```cpp
class Time {
public:
    enum class ClockSource { SYSTEM_CLOCK, STEADY_CLOCK, HIGH_RES_CLOCK,
                             REALTIME_CLOCK, MONOTONIC_CLOCK };

    // Current time
    static Timestamp now() noexcept;
    static Timestamp get_timestamp(ClockSource source = ClockSource::STEADY_CLOCK) noexcept;
    static void set_clock_source(ClockSource source) noexcept;

    // Conversions
    static constexpr Timestamp to_nanoseconds(Duration duration) noexcept;
    static constexpr Duration from_nanoseconds(Timestamp ns) noexcept;
    static constexpr Timestamp milliseconds_to_ns(uint64_t ms) noexcept;
    static constexpr uint64_t ns_to_milliseconds(Timestamp ns) noexcept;

    // Comparison
    static constexpr Timestamp diff(Timestamp t1, Timestamp t2) noexcept;
    static constexpr bool is_within_tolerance(Timestamp ts, Timestamp target,
                                              Timestamp tol_ns) noexcept;

    // Sleep
    static void sleep(Duration duration) noexcept;
    static void sleep_ns(Timestamp ns) noexcept;
    static void sleep_until(Timestamp target) noexcept;
    static void yield() noexcept;
};
```

### Usage

```cpp
Timestamp ts = Time::now();
Time::sleep(Milliseconds(100));
Time::sleep(Seconds(1));
Time::sleep_until(ts + Time::to_nanoseconds(Milliseconds(500)));
Time::yield();
```

---

## Threading Abstractions

**Header:** `<commrat/platform/threading.hpp>`

Thread and synchronization primitives. Backend selected at compile time (std:: or EVL).

All CommRaT code uses these abstractions instead of `std::thread`, `std::mutex`, etc.
This enables swapping to a hard real-time backend (libevl/Xenomai 4) without changing application code.

### Enums

```cpp
enum class ThreadPriority   { IDLE = 0, LOW = 10, NORMAL = 50, HIGH = 75, REALTIME = 99 };
enum class SchedulingPolicy { NORMAL, FIFO, ROUND_ROBIN, DEADLINE };
enum class CvStatus         { NO_TIMEOUT, TIMEOUT };
```

### ThreadConfig

```cpp
struct ThreadConfig {
    std::string name{"unnamed"};
    ThreadPriority priority = ThreadPriority::NORMAL;
    SchedulingPolicy policy = SchedulingPolicy::NORMAL;
    int cpu_affinity = -1;   // -1 = no affinity
    size_t stack_size = 0;   // 0 = default
};
```

### Thread

```cpp
class Thread {
public:
    Thread();                                                    // Default (no thread)
    template<typename Func> explicit Thread(Func&& func);        // Start immediately
    template<typename Func> Thread(const ThreadConfig& config, Func&& func);
    explicit Thread(const ThreadConfig& config);                 // Config only, start() later
    ~Thread();                                                   // Joins if joinable

    template<typename Func> void start(Func&& func);
    void join();
    void detach();
    bool joinable() const noexcept;
    auto native_handle();
    std::thread::id get_id() const noexcept;
    const ThreadConfig& config() const noexcept;
};
```

### Synchronization

```cpp
class Mutex {
public:
    void lock();
    bool try_lock();
    void unlock();
};

class SharedMutex {
public:
    void lock();              // Exclusive (write)
    void lock_shared();       // Shared (read)
    bool try_lock();
    bool try_lock_shared();
    void unlock();
    void unlock_shared();
};

class ConditionVariable {
public:
    void notify_one() noexcept;
    void notify_all() noexcept;
    void wait(UniqueLock& lock);
    template<typename Predicate>
    void wait(UniqueLock& lock, Predicate pred);
    CvStatus wait_for(UniqueLock& lock, Duration timeout);
    template<typename Predicate>
    bool wait_for(UniqueLock& lock, Duration timeout, Predicate pred);
};
```

### Lock Type Aliases

```cpp
using Lock             = std::lock_guard<Mutex>;
using UniqueLock       = std::unique_lock<Mutex>;
using SharedLock       = std::shared_lock<SharedMutex>;
using UniqueLockShared = std::unique_lock<SharedMutex>;
```

### Convenience Macros

```cpp
Synchronized(mutex) { /* exclusive critical section */ }
ReadLocked(mutex)    { /* shared read section */ }
WriteLocked(mutex)   { /* exclusive write section */ }
```

---

## Platform Selection

**Header:** `<commrat/platform/platform.hpp>`

Backend is selected via the `COMMRAT_PLATFORM` CMake cache variable:

```bash
cmake -B build                          # default: STD
cmake -B build -DCOMMRAT_PLATFORM=EVL   # hard real-time
```

CMake propagates the selection as a compile definition:

| `COMMRAT_PLATFORM` | Compile definition | Backend |
|---|---|---|
| `STD` (default) | `COMMRAT_PLATFORM_STD` | `std::thread`, `std::mutex`, `std::chrono` |
| `EVL` | `COMMRAT_PLATFORM_EVL` | libevl / Xenomai 4, out-of-band scheduling |

Defining both is a compile error. If neither is defined (e.g. manual build), defaults to `COMMRAT_PLATFORM_STD`.

> **EVL status**: `evl/threading_impl.hpp` and `evl/timestamp_impl.hpp` contain `#error` stubs.
> `-DCOMMRAT_PLATFORM=EVL` does not yet compile. Tracked in CI `build-evl-compile` job.

### Feature Detection Macros

```cpp
#ifdef COMMRAT_HAS_OOB          // EVL out-of-band execution available
#ifdef COMMRAT_HAS_PI_MUTEX     // Priority-inheritance mutexes available
#ifdef COMMRAT_HAS_SLEEP_UNTIL  // Hardware-precise sleep_until available
```

### Backend File Structure

```
include/commrat/platform/
    duration.hpp              # Duration type (shared, no backend dependency)
    platform.hpp              # Backend selection macros
    threading.hpp             # Common types + backend dispatch
    timestamp.hpp             # Timestamp typedef + backend dispatch
    std/
        threading_impl.hpp    # std:: backend (Thread, Mutex, SharedMutex, CV)
        timestamp_impl.hpp    # std:: backend (Time class)
    evl/
        threading_impl.hpp    # EVL backend (skeleton)
        timestamp_impl.hpp    # EVL backend (skeleton)
```

---

## Introspection

**Header:** `<commrat/introspection/introspection_helper.hpp>`

```cpp
using Introspection = MyApp::Introspection;  // IntrospectionHelper<CommRaT>
```

### Methods

```cpp
template<typename T, typename Writer = rfl::json::Writer>
static std::string export_as();

template<typename Writer = rfl::json::Writer>
static std::string export_all();

template<typename Writer = rfl::json::Writer>
static void write_to_file(const std::string& filename);
```

---

## See Also

- [User Guide](USER_GUIDE.md)
- [Getting Started](GETTING_STARTED.md)
- [Architecture](ARCHITECTURE.md)
- [Examples](../examples/)
- [Doxygen Docs](api/html/index.html) (after `make docs`)
