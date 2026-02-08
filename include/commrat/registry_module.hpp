#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/mailbox/historical_mailbox.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/io_spec.hpp"
#include "commrat/platform/threading.hpp"
#include "commrat/platform/timestamp.hpp"
#include <sertial/sertial.hpp>
#include <atomic>
#include <vector>
#include <functional>
#include <optional>

namespace commrat {

// ============================================================================
// Input Mode Tags
// ============================================================================

/// Periodic execution - module runs on a timer, no input data
struct PeriodicInput {
    std::chrono::milliseconds period{100};
};

/// Free-running loop - module runs as fast as possible, no input data
struct LoopInput {};

/// Continuous input - module receives data of type T from subscribed source
template<typename T>
struct ContinuousInput {
    using InputData = T;
    std::chrono::milliseconds requested_period{0};  // 0 = as fast as source publishes
};

// ============================================================================
// Module Configuration
// ============================================================================

// Mailbox type identifiers (offsets from base address)
enum class MailboxType : uint8_t {
    CMD = 0,   // Command mailbox - receives imperative commands
    WORK = 16,  // Work mailbox - handles subscription protocol  
    DATA = 32   // Data mailbox - receives input data streams
};

// Phase 6.5: Multi-input source configuration
struct InputSource {
    uint8_t system_id{0};      // Source module's system identifier
    uint8_t instance_id{0};    // Source module's instance number
    bool is_primary{false};    // Is this the primary input? (drives execution)
    
    // For multi-output producers: primary output type ID for base address calculation
    std::optional<uint32_t> source_primary_output_type_id;
};

struct ModuleConfig {
    std::string name;
    uint8_t system_id{0};        // System/robot identifier
    uint8_t instance_id{0};      // Instance number for this module type
    std::chrono::milliseconds period{100};
    size_t message_slots{10};
    size_t max_subscribers{8};
    int priority{10};
    bool realtime{false};
    
    // ========================================================================
    // Single-Input Mode (backward compatible)
    // ========================================================================
    
    // For ContinuousInput mode: source module's system_id and instance_id
    std::optional<uint8_t> source_system_id;
    std::optional<uint8_t> source_instance_id;
    
    // For multi-output producers: the primary output type ID used for base address calculation
    // If not set, will use InputData type (works for single-output producers)
    std::optional<uint32_t> source_primary_output_type_id;
    
    // ========================================================================
    // Multi-Input Mode (Phase 6.5)
    // ========================================================================
    
    // Multiple input sources (one per type in Inputs<Ts...>)
    // Order must match order of types in Inputs<T1, T2, ...>
    // Exactly one source must have is_primary=true
    std::vector<InputSource> input_sources;
    
    // Per-input history buffer capacity (for getData synchronization)
    size_t history_buffer_size{100};
    
    // Default tolerance for getData() calls (milliseconds)
    std::chrono::milliseconds sync_tolerance{50};
};

// ============================================================================
// Phase 6.10: Automatic Timestamp Management
// ============================================================================

/**
 * @brief Phase 6.10: Timestamp Management Architecture
 * 
 * Timestamps are stored ONLY in TimsHeader.timestamp, never in payload.
 * Module automatically wraps payloads in TimsMessage and sets header.timestamp:
 * - PeriodicInput/LoopInput: timestamp = Time::now() (data generation time)
 * - Multi-input: timestamp = primary_input.header.timestamp (sync point)
 * - ContinuousInput: timestamp = input.header.timestamp (propagation)
 * 
 * Users never deal with timestamps - messages are clean data structures.
 */

// ============================================================================
// Helper Base Classes for Conditional Virtual Functions
// ============================================================================

// Base class providing process_continuous when InputData is not void
template<typename InputData_, typename OutputData_>
class ContinuousProcessorBase {
protected:
    virtual OutputData_ process_continuous(const InputData_& input) {
        std::cerr << "[Module] ERROR: process_continuous not overridden in derived class!\n";
        return OutputData_{};
    }
};

// Specialization for void InputData (no process_continuous function)
template<typename OutputData_>
class ContinuousProcessorBase<void, OutputData_> {
    // No process_continuous for periodic/loop modes
};

// ============================================================================
// Multi-Output Processor Base (Phase 5.3)
// ============================================================================

// Helper to generate multi-output process signature
template<typename OutputTypesTuple, typename InputData_>
class MultiOutputProcessorBase;

// Single output: OutputData process() or OutputData process_continuous(const InputData&)
template<typename T>
class MultiOutputProcessorBase<std::tuple<T>, void> {
    // Single output handled by normal process() - no additional signature
};

template<typename T, typename InputData_>
class MultiOutputProcessorBase<std::tuple<T>, InputData_> {
    // Single output with continuous input - handled by process_continuous()
};

// Multi-output without continuous input: void process(T1& out1, T2& out2, ...)
template<typename... Ts>
    requires (sizeof...(Ts) > 1)
class MultiOutputProcessorBase<std::tuple<Ts...>, void> {
public:
    // Public virtual function for polymorphic calls from Module
    virtual void process(Ts&... outputs) {
        std::cerr << "[Module] ERROR: Multi-output process(...) not overridden in derived class!\n";
        // Leave outputs as default-constructed
    }
};

// Multi-output with continuous input: void process_continuous(const InputData&, T1& out1, T2& out2, ...)
template<typename... Ts, typename InputData_>
    requires (sizeof...(Ts) > 1)
class MultiOutputProcessorBase<std::tuple<Ts...>, InputData_> {
public:
    // Public virtual function for polymorphic calls from Module
    virtual void process_continuous(const InputData_& input, Ts&... outputs) {
        std::cerr << "[Module] ERROR: Multi-output process_continuous(...) not overridden in derived class!\n";
        // Leave outputs as default-constructed
        (void)input;  // Suppress unused warning
    }
};

// ============================================================================
// Single-Output Processor Base (conditional process() function)
// ============================================================================

// Helper base class that provides virtual process() only for single-output modules
// For multi-output (OutputData = void), this base is empty
template<typename OutputData_>
class SingleOutputProcessorBase {
protected:
    // Single output: provide virtual process() returning OutputData
    virtual OutputData_ process() {
        return OutputData_{};
    }
};

// Specialization for void (multi-output): no process() function
template<>
class SingleOutputProcessorBase<void> {
    // Empty - multi-output modules use MultiOutputProcessorBase::process(Ts&...) instead
};

// ============================================================================
// Phase 6.7: Multi-Input Processor Base
// ============================================================================

// Helper base class that provides virtual multi-input process signatures
// Only enabled when InputCount > 1 (multi-input modules)

template<typename InputTypesTuple_, typename OutputData_, size_t InputCount_>
class MultiInputProcessorBase {
    // Primary template - should not be instantiated
    // Use specializations for InputCount == 0, 1, or > 1
};

// No multi-input (single input or no input): empty base
template<typename InputTypesTuple_, typename OutputData_>
class MultiInputProcessorBase<InputTypesTuple_, OutputData_, 0> {
    // Empty - not multi-input
};

template<typename InputTypesTuple_, typename OutputData_>
class MultiInputProcessorBase<InputTypesTuple_, OutputData_, 1> {
    // Empty - single input, not multi-input
};

// Multi-input with single output: OutputData process(const T1&, const T2&, ...)
template<typename... Ts, typename OutputData_>
    requires (sizeof...(Ts) > 1 && !std::is_void_v<OutputData_>)
class MultiInputProcessorBase<std::tuple<Ts...>, OutputData_, sizeof...(Ts)> {
public:
    virtual OutputData_ process(const Ts&... inputs) {
        std::cerr << "[Module] ERROR: Multi-input process(...) not overridden in derived class!\n";
        (void)std::make_tuple(inputs...);  // Suppress unused warnings
        return OutputData_{};
    }
};

// Multi-input with multi-output: void process(const T1&, const T2&, ..., O1& out1, O2& out2, ...)
template<typename... InputTs, typename... OutputTs>
    requires (sizeof...(InputTs) > 1 && sizeof...(OutputTs) > 1)
class MultiInputProcessorBase<std::tuple<InputTs...>, std::tuple<OutputTs...>, sizeof...(InputTs)> {
public:
    virtual void process(const InputTs&... inputs, OutputTs&... outputs) {
        std::cerr << "[Module] ERROR: Multi-input+multi-output process(...) not overridden!\n";
        (void)std::make_tuple(inputs...);  // Suppress unused warnings
    }
};

// ============================================================================
// Type Extraction Helpers
// ============================================================================

// Helper to extract InputData from InputSpec (outside Module class)
template<typename T>
struct ExtractInputPayload {
    using type = void;  // PeriodicInput, LoopInput
};

template<typename T>
struct ExtractInputPayload<Input<T>> {
    using type = T;
};

template<typename T>
struct ExtractInputPayload<ContinuousInput<T>> {
    using type = T;  // Legacy support
};

// Helper to extract input types tuple from InputSpec (outside Module)
template<typename T>
struct ExtractInputTypes {
    using type = std::tuple<>;  // Default: no inputs (PeriodicInput, LoopInput)
};

template<typename T>
struct ExtractInputTypes<Input<T>> {
    using type = std::tuple<T>;  // Single input
};

template<typename... Ts>
struct ExtractInputTypes<Inputs<Ts...>> {
    using type = std::tuple<Ts...>;  // Multi-input
};

// Helper to extract OutputData from OutputSpec (outside Module class)
template<typename T>
struct ExtractOutputPayload {
    using type = T;  // Raw type
};

template<typename T>
struct ExtractOutputPayload<Output<T>> {
    using type = T;
};

// Helper to get output types as tuple (outside Module class)
template<typename T>
struct OutputTypesTuple {
    using type = std::tuple<T>;  // Raw type
};

template<typename T>
struct OutputTypesTuple<Output<T>> {
    using type = std::tuple<T>;
};

template<typename... Ts>
struct OutputTypesTuple<Outputs<Ts...>> {
    using type = std::tuple<Ts...>;
};

// ============================================================================
// Registry-Based Module (New Clean Interface)
// ============================================================================

/**
 * @brief Modern C++20 module base using MessageRegistry
 * 
 * Users define their complete application message registry once, then Module
 * automatically includes subscription protocol messages and uses the registry.
 * 
 * @tparam UserRegistry The application's complete MessageRegistry
 * @tparam OutputSpec_ Output specification: Output<T>, Outputs<Ts...>, or raw type T (normalized to Output<T>)
 * @tparam InputSpec_ Input specification: Input<T>, PeriodicInput, LoopInput, ContinuousInput<T> (legacy)
 * @tparam CommandTypes Optional variadic command types this module handles
 * 
 * Example (new style with I/O specs):
 * @code
 * class SensorModule : public Module<Registry, Output<TempData>, PeriodicInput> {
 * protected:
 *     TempData process() override {
 *         return TempData{.temperature_celsius = read_sensor()};
 *     }
 * };
 * @endcode
 * 
 * Example (backward compatible - raw type):
 * @code
 * class SensorModule : public Module<Registry, TempData, PeriodicInput> {
 * protected:
 *     TempData process() override {
 *         return TempData{.temperature_celsius = read_sensor()};
 *     }
 * };
 * @endcode
 */
// Helper to resolve MultiInputProcessorBase template arguments
// (workaround for complex nested template parsing issues)
template<typename InputSpec_, typename OutputSpec_>
struct ResolveMultiInputBase {
    using NormalizedInput = typename NormalizeInput<InputSpec_>::Type;
    using InputTypesTuple = typename ExtractInputTypes<NormalizedInput>::type;
    using NormalizedOutput = typename NormalizeOutput<OutputSpec_>::Type;
    using OutputData = typename ExtractOutputPayload<NormalizedOutput>::type;
    static constexpr std::size_t InputCount = std::tuple_size_v<InputTypesTuple>;
    
    using type = MultiInputProcessorBase<InputTypesTuple, OutputData, InputCount>;
};

// Specialization for multi-output case (Outputs<Ts...>)
template<typename InputSpec_, typename... OutputTypes>
struct ResolveMultiInputBase<InputSpec_, Outputs<OutputTypes...>> {
    using NormalizedInput = typename NormalizeInput<InputSpec_>::Type;
    using InputTypesTuple = typename ExtractInputTypes<NormalizedInput>::type;
    using OutputData = std::tuple<OutputTypes...>;  // Multi-output uses tuple
    static constexpr std::size_t InputCount = std::tuple_size_v<InputTypesTuple>;
    
    using type = MultiInputProcessorBase<InputTypesTuple, OutputData, InputCount>;
};

template<typename UserRegistry,
         typename OutputSpec_,
         typename InputSpec_,
         typename... CommandTypes>
class Module 
    : public ContinuousProcessorBase<
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type,
        typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
      >
    , public MultiOutputProcessorBase<
        typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type,
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type
      >
    , public SingleOutputProcessorBase<
        typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
      >
    , public ResolveMultiInputBase<InputSpec_, OutputSpec_>::type
{
private:
    // Normalize OutputSpec: raw type T -> Output<T>
    using OutputSpec = NormalizeOutput_t<OutputSpec_>;
    
    // Normalize InputSpec: ContinuousInput<T> -> Input<T>, keep others as-is
    using InputSpec = NormalizeInput_t<InputSpec_>;
    
    // Phase 6.6: Multi-input Module integration in progress
    // Phase 6.5 complete: InputSource, ModuleConfig extended
    
    // Helper to extract InputData type from InputSpec (single input)
    template<typename T>
    struct ExtractInputData {
        using type = void;
    };
    
    template<typename T>
    struct ExtractInputData<Input<T>> {
        using type = T;
    };
    
    // Helper to extract input types tuple from InputSpec
    template<typename T>
    struct ExtractInputTypes {
        using type = std::tuple<>;  // No inputs
    };
    
    template<typename T>
    struct ExtractInputTypes<Input<T>> {
        using type = std::tuple<T>;  // Single input
    };
    
    template<typename... Ts>
    struct ExtractInputTypes<Inputs<Ts...>> {
        using type = std::tuple<Ts...>;  // Multi-input
    };
    
    using InputTypesTuple = typename ExtractInputTypes<InputSpec>::type;
    static constexpr size_t InputCount = std::tuple_size_v<InputTypesTuple>;
    static constexpr bool has_multi_input = InputCount > 1;
    
    // Helper to check if there's an explicit PrimaryInput in CommandTypes
    // (PrimaryInput is passed as a command type to Module<..., Inputs<>, PrimaryInput<T>>)
    template<typename...>
    struct HasPrimaryInputHelper : std::false_type {};
    
    template<typename T, typename... Rest>
    struct HasPrimaryInputHelper<PrimaryInput<T>, Rest...> : std::true_type {};
    
    template<typename First, typename... Rest>
    struct HasPrimaryInputHelper<First, Rest...> : HasPrimaryInputHelper<Rest...> {};
    
    static constexpr bool has_primary_input_spec = HasPrimaryInputHelper<CommandTypes...>::value;
    
    // Helper to extract primary payload type from CommandTypes
    template<typename...>
    struct ExtractPrimaryPayloadHelper {
        using type = void;
    };
    
    template<typename T, typename... Rest>
    struct ExtractPrimaryPayloadHelper<PrimaryInput<T>, Rest...> {
        using type = T;
    };
    
    template<typename First, typename... Rest>
    struct ExtractPrimaryPayloadHelper<First, Rest...> : ExtractPrimaryPayloadHelper<Rest...> {};
    
    using PrimaryPayloadType = typename ExtractPrimaryPayloadHelper<CommandTypes...>::type;
    
    // Compile-time validation: If PrimaryInput<T> specified, T must be in Inputs<...>
    // For multi-input modules with explicit PrimaryInput, validate the type is in the inputs list
    template<typename Dummy = void>
    struct ValidatePrimaryInputImpl {
        static constexpr bool check() {
            if constexpr (has_primary_input_spec && has_multi_input) {
                // This will trigger PrimaryInputIndex's static_assert if type not found
                return PrimaryInputIndex_v<PrimaryPayloadType, InputTypesTuple> >= 0;
            }
            return true;
        }
        static constexpr bool value = check();
    };
    static_assert(ValidatePrimaryInputImpl<>::value, 
                  "PrimaryInput validation failed - see error above for details");
    
    // Helper to extract OutputData type from OutputSpec
    template<typename T>
    struct ExtractOutputData {
        using type = T;  // Raw type passes through
    };
    
    template<typename T>
    struct ExtractOutputData<Output<T>> {
        using type = T;
    };
    
    template<typename... Ts>
    struct ExtractOutputData<Outputs<Ts...>> {
        using type = void;  // Multi-output: void process(T1& out1, T2& out2, ...)
    };
    
    // Helper to get output types as tuple
    template<typename T>
    struct OutputTypes {
        using type = std::tuple<T>;
    };
    
    template<typename T>
    struct OutputTypes<Output<T>> {
        using type = std::tuple<T>;
    };
    
    template<typename... Ts>
    struct OutputTypes<Outputs<Ts...>> {
        using type = std::tuple<Ts...>;
    };
    
    using OutputTypesTuple = typename OutputTypes<OutputSpec>::type;
    
public:
    using OutputData = typename ExtractOutputData<OutputSpec>::type;
    using InputData = typename ExtractInputData<InputSpec>::type;
    
    static constexpr bool has_continuous_input = HasContinuousInput<InputSpec>;
    static constexpr bool has_periodic_input = std::is_same_v<InputSpec, PeriodicInput>;
    static constexpr bool has_loop_input = std::is_same_v<InputSpec, LoopInput>;
    static constexpr bool has_multi_output = OutputCount_v<OutputSpec> > 1;
    
private:
    // Type aliases for mailboxes
    using CmdMailbox = RegistryMailbox<UserRegistry>;  // Command mailbox (user commands only)
    using WorkMailbox = RegistryMailbox<SystemRegistry>;  // Work mailbox (subscription protocol)
    using DataMailbox = RegistryMailbox<UserRegistry>;  // Data mailbox (input data)
    
    /**
     * @brief Calculate base mailbox address from output type, system_id, and instance_id
     * Format: [data_type_id_low16:16][system_id:8][instance_id:8]
     * Uses lower 16 bits of message ID to fit in uint32_t
     * 
     * For multi-output modules (OutputData = void), uses the first output type.
     */
    static constexpr uint32_t calculate_base_address(uint8_t system_id, uint8_t instance_id) {
        // Get message ID for output data type from registry
        // For multi-output, use first output type from the tuple
        using BaseType = std::conditional_t<
            std::is_void_v<OutputData>,
            std::tuple_element_t<0, OutputTypesTuple>,
            OutputData
        >;
        constexpr uint32_t data_type_id = UserRegistry::template get_message_id<BaseType>();
        // Use lower 16 bits of message ID to fit addressing in 32 bits
        constexpr uint16_t data_type_id_low = static_cast<uint16_t>(data_type_id & 0xFFFF);
        return (static_cast<uint32_t>(data_type_id_low) << 16) | (system_id << 8) | instance_id;
    }
    
    /**
     * @brief Get specific mailbox address
     */
    static constexpr uint32_t get_mailbox_address(uint8_t system_id, uint8_t instance_id, MailboxType type) {
        return calculate_base_address(system_id, instance_id) + static_cast<uint8_t>(type);
    }
    
    /**
     * @brief Extract message type ID from subscriber base address
     * Base address format: [data_type_id_low16:16][system_id:8][instance_id:8]
     */
    static constexpr uint16_t extract_message_type_from_address(uint32_t base_addr) {
        return static_cast<uint16_t>((base_addr >> 16) & 0xFFFF);
    }
    
protected:
    ModuleConfig config_;
    CmdMailbox cmd_mailbox_;    // base + 0: Receives user commands
    WorkMailbox work_mailbox_;  // base + 16: Handles subscription protocol
    
    // Phase 6.6: Multi-input support
    // Single-input mode (backward compatible)
    std::optional<DataMailbox> data_mailbox_;  // base + 32: Receives input data (only for single ContinuousInput)
    
    // Multi-input mode (Phase 6.6) - one HistoricalMailbox per input type
    // Tuple index corresponds to position in Inputs<T1, T2, ...>
    // Example: Inputs<IMU, GPS> → tuple<HistoricalMailbox<Registry, HistorySize, IMU>, ...>
    // Only populated if has_multi_input == true
    
    // Helper: Create HistoricalMailbox type for each input type in the tuple
    template<typename T>
    using HistoricalMailboxFor = HistoricalMailbox<UserRegistry, 100>; // TODO: Make history size configurable
    
    // Generate tuple of HistoricalMailbox types from InputTypesTuple
    template<typename Tuple>
    struct MakeHistoricalMailboxTuple;
    
    template<typename... Ts>
    struct MakeHistoricalMailboxTuple<std::tuple<Ts...>> {
        using type = std::tuple<HistoricalMailboxFor<Ts>...>;
    };
    
    using HistoricalMailboxTuple = typename MakeHistoricalMailboxTuple<InputTypesTuple>::type;
    std::optional<HistoricalMailboxTuple> input_mailboxes_;
    
    // Subscription tracking (Phase 6.6)
    struct SubscriptionState {
        bool subscribed{false};
        bool reply_received{false};
        uint32_t actual_period_ms{0};
    };
    std::vector<SubscriptionState> input_subscriptions_;  // One per input source
    std::mutex subscription_mutex_;
    
    std::atomic<bool> running_;
    
    // Module threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::optional<std::thread> work_thread_;
    std::vector<std::thread> secondary_input_threads_;  // Phase 6.9: Receive loops for secondary inputs
    
    // Subscriber management
    std::vector<uint32_t> subscribers_;
    std::mutex subscribers_mutex_;
    
    // ========================================================================
    // Phase 6.10: Input Metadata Storage
    // ========================================================================
    
    /**
     * @brief Storage for input message metadata
     * 
     * Stores metadata about received input messages, populated before process() calls.
     * Available to derived classes via get_input_metadata<Index>() accessors.
     */
    struct InputMetadataStorage {
        uint64_t timestamp{0};           // Message timestamp (from TimsHeader)
        uint32_t sequence_number{0};     // Message sequence number (from TimsHeader)
        uint32_t message_id{0};          // Message type ID (from TimsHeader)
        bool is_new_data{false};         // True if freshly received, false if stale/reused
        bool is_valid{false};            // True if getData succeeded, false if failed
    };
    
    // Calculate number of inputs at compile time
    static constexpr std::size_t num_inputs = InputCount_v<InputSpec>;
    
    // Fixed-size array for input metadata (zero-size when no inputs)
    std::array<InputMetadataStorage, (num_inputs > 0 ? num_inputs : 1)> input_metadata_;
    
    /**
     * @brief Update metadata for a specific input index
     * 
     * Helper method to populate metadata from received TimsMessage.
     * Called by loop functions before invoking process() methods.
     */
    template<typename T>
    void update_input_metadata(std::size_t index, const TimsMessage<T>& received, bool is_new) {
        if (index >= num_inputs) {
            std::cerr << "[Module] ERROR: Invalid metadata index " << index << "\n";
            return;
        }
        
        input_metadata_[index].timestamp = received.header.timestamp;
        input_metadata_[index].sequence_number = received.header.seq_number;
        input_metadata_[index].message_id = received.header.msg_type;
        input_metadata_[index].is_new_data = is_new;
        input_metadata_[index].is_valid = true;
    }
    
    /**
     * @brief Mark input metadata as invalid (getData failed)
     */
    void mark_input_invalid(std::size_t index) {
        if (index >= num_inputs) {
            return;
        }
        input_metadata_[index].is_valid = false;
        input_metadata_[index].is_new_data = false;
    }
    
public:
    explicit Module(const ModuleConfig& config)
        : config_(config)
        , cmd_mailbox_(createMailboxConfig(config, MailboxType::CMD))
        , work_mailbox_(createWorkMailboxConfig(config))
        , data_mailbox_(has_continuous_input && !has_multi_input ? 
            std::make_optional<DataMailbox>(createMailboxConfig(config, MailboxType::DATA)) : 
            std::nullopt)
        , running_(false)
    {
        subscribers_.reserve(config.max_subscribers);
        
        // Phase 6.6: Initialize multi-input mailboxes
        if constexpr (has_multi_input) {
            initialize_multi_input_mailboxes();
        }
    }
    
    virtual ~Module() {
        stop();
    }
    
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;
    
    // ========================================================================
    // Lifecycle Hooks (Override These)
    // ========================================================================
    
    virtual void on_init() {}
    virtual void on_start() {}
    virtual void on_stop() {}
    virtual void on_cleanup() {}
    
    // ========================================================================
    // Processing Hooks (Override Based on InputMode)
    // ========================================================================
    
    // Note: process() is provided by SingleOutputProcessorBase (empty for multi-output)

protected:
    // For ContinuousInput: process input and return output
    // Calls process_continuous which derived classes should hide/override
    template<typename T = InputData>
    OutputData process_continuous_dispatch(const T& input) {
        // This->process_continuous will resolve to derived class version via name hiding
        return this->process_continuous(input);
    }
    
    // Derived classes define this (non-template) for continuous input:
    //   OutputData process_continuous(const InputData& input) { ... }
    
    // ========================================================================
    // Phase 6.10: Input Metadata Accessors
    // ========================================================================
    
    /**
     * @brief Input metadata structure returned by accessor methods
     * 
     * Contains timestamp, sequence number, and freshness information about
     * a received input message. Available for modules with continuous inputs.
     * 
     * @tparam T The payload type of the input
     */
    template<typename T>
    struct InputMetadata {
        uint64_t timestamp;          ///< Message timestamp (from TimsHeader)
        uint32_t sequence_number;    ///< Message sequence number
        uint32_t message_id;         ///< Message type ID
        bool is_new_data;            ///< True if freshly received, false if stale/reused
        bool is_valid;               ///< True if getData succeeded, false if failed
        
        // Helper to get input type (for debugging/logging)
        static constexpr const char* type_name() { return typeid(T).name(); }
    };
    
    /**
     * @brief Get metadata for input by index (always available)
     * 
     * Returns metadata about the input at the specified index.
     * Index 0 is always the primary input (or only input for single-input modules).
     * 
     * @tparam Index The input index (0 for primary/single, 1+ for multi-input secondaries)
     * @return InputMetadata containing timestamp, sequence, freshness, validity
     * 
     * @example
     * // Single input module
     * auto meta = get_input_metadata<0>();
     * assert(meta.timestamp > 0);
     * 
     * // Multi-input module
     * auto imu_meta = get_input_metadata<0>();  // Primary
     * auto gps_meta = get_input_metadata<1>();  // Secondary
     * if (!gps_meta.is_new_data) {
     *     std::cout << "GPS data is stale\\n";
     * }
     */
    template<std::size_t Index>
    auto get_input_metadata() const {
        static_assert(Index < num_inputs, "Input index out of bounds");
        
        using InputType = std::conditional_t<
            (num_inputs == 1),
            InputData,  // Single input: use InputData directly
            std::tuple_element_t<Index, InputTypesTuple>  // Multi-input: extract from tuple
        >;
        
        const auto& storage = input_metadata_[Index];
        return InputMetadata<InputType>{
            .timestamp = storage.timestamp,
            .sequence_number = storage.sequence_number,
            .message_id = storage.message_id,
            .is_new_data = storage.is_new_data,
            .is_valid = storage.is_valid
        };
    }
    
    /**
     * @brief Get input timestamp by index (convenience method)
     * 
     * @tparam Index The input index
     * @return Timestamp from TimsHeader (nanoseconds since epoch)
     */
    template<std::size_t Index>
    uint64_t get_input_timestamp() const {
        static_assert(Index < num_inputs, "Input index out of bounds");
        return input_metadata_[Index].timestamp;
    }
    
    /**
     * @brief Check if input has new data by index
     * 
     * For multi-input modules, returns true if the input was freshly received
     * and not reused from history (timestamp matches primary).
     * For single-input modules, always returns true.
     * 
     * @tparam Index The input index
     * @return True if fresh data, false if stale
     */
    template<std::size_t Index>
    bool has_new_data() const {
        static_assert(Index < num_inputs, "Input index out of bounds");
        return input_metadata_[Index].is_new_data;
    }
    
    /**
     * @brief Check if input is valid by index
     * 
     * For multi-input modules, returns true if getData succeeded.
     * For single-input modules, always returns true (receive succeeded).
     * 
     * @tparam Index The input index
     * @return True if valid, false if getData failed
     */
    template<std::size_t Index>
    bool is_input_valid() const {
        static_assert(Index < num_inputs, "Input index out of bounds");
        return input_metadata_[Index].is_valid;
    }
    
    // ========================================================================
    // Phase 6.10: Type-Based Metadata Accessors
    // ========================================================================
    
    /**
     * @brief Find index of type T in input tuple
     * 
     * Compile-time helper to locate type in parameter pack.
     * Static asserts if type not found or appears multiple times.
     */
    template<typename T, typename... Types>
    static constexpr std::size_t find_type_index() {
        constexpr std::size_t count = ((std::is_same_v<T, Types> ? 1 : 0) + ...);
        static_assert(count > 0, "Type not found in inputs - check your input specification");
        static_assert(count == 1, "Type appears multiple times in inputs - use index-based access instead");
        
        // Find index where type matches
        std::size_t index = 0;
        bool found = false;
        ((std::is_same_v<T, Types> ? (found = true) : (found ? true : (++index, false))), ...);
        return index;
    }
    
    /**
     * @brief Get metadata for input by type (only when types unique)
     * 
     * Returns metadata about the input with the specified payload type.
     * Only works when all input types are unique (compile error otherwise).
     * 
     * @tparam T The payload type of the input
     * @return InputMetadata for the input of type T
     * 
     * @example
     * // Multi-input module with unique types
     * auto imu_meta = get_input_metadata<IMUData>();
     * auto gps_meta = get_input_metadata<GPSData>();
     * 
     * // Compile error if types duplicate:
     * // Inputs<SensorData, SensorData> - can't use type-based access
     */
    template<typename T>
    auto get_input_metadata() const
        requires (num_inputs > 1)  // Only for multi-input
    {
        constexpr std::size_t index = find_type_index<T, 
            typename std::tuple_element<0, InputTypesTuple>::type,
            typename std::tuple_element<1, InputTypesTuple>::type
            // TODO: Need to unpack full tuple, not just first 2 elements
        >();
        return get_input_metadata<index>();
    }
    
    /**
     * @brief Get input timestamp by type (convenience method)
     */
    template<typename T>
    uint64_t get_input_timestamp() const
        requires (num_inputs > 1)
    {
        constexpr std::size_t index = find_type_index<T,
            typename std::tuple_element<0, InputTypesTuple>::type,
            typename std::tuple_element<1, InputTypesTuple>::type
            // TODO: Need to unpack full tuple
        >();
        return get_input_timestamp<index>();
    }
    
    /**
     * @brief Check if input has new data by type
     */
    template<typename T>
    bool has_new_data() const
        requires (num_inputs > 1)
    {
        constexpr std::size_t index = find_type_index<T,
            typename std::tuple_element<0, InputTypesTuple>::type,
            typename std::tuple_element<1, InputTypesTuple>::type
        >();
        return has_new_data<index>();
    }
    
    /**
     * @brief Check if input is valid by type
     */
    template<typename T>
    bool is_input_valid() const
        requires (num_inputs > 1)
    {
        constexpr std::size_t index = find_type_index<T,
            typename std::tuple_element<0, InputTypesTuple>::type,
            typename std::tuple_element<1, InputTypesTuple>::type
        >();
        return is_input_valid<index>();
    }
    
public:
    // ========================================================================
    // Lifecycle Management
    // ========================================================================
    
    void start() {
        if (running_) {
            return;
        }
        
        on_init();
        
        // Start all 3 mailboxes
        auto cmd_result = cmd_mailbox_.start();
        if (!cmd_result) {
            throw std::runtime_error("Failed to start command mailbox");
        }
        
        auto work_result = work_mailbox_.start();
        if (!work_result) {
            throw std::runtime_error("Failed to start work mailbox");
        }
        
        // Only start data mailbox for single ContinuousInput modules
        if (data_mailbox_) {
            auto data_result = data_mailbox_->start();
            if (!data_result) {
                throw std::runtime_error("Failed to start data mailbox");
            }
        }
        
        // Phase 6.6: Start multi-input mailboxes
        if constexpr (has_multi_input) {
            start_input_mailboxes();
        }
        
        running_ = true;
        on_start();
        
        // Start work thread FIRST to handle subscriptions
        work_thread_ = std::thread(&Module::work_loop, this);
        
        // Start command thread for user commands
        command_thread_ = std::thread(&Module::command_loop, this);
        
        // Give threads time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Subscribe to source(s)
        if constexpr (has_multi_input) {
            // Phase 6.6: Multi-input subscription
            subscribe_to_all_sources();
        } else if constexpr (has_continuous_input) {
            // Single-input (backward compatible)
            if (config_.source_system_id && config_.source_instance_id) {
                subscribe_to_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        // Start data thread based on input mode
        if constexpr (has_periodic_input) {
            std::cout << "[" << config_.name << "] Starting periodic_loop thread...\n";
            data_thread_ = std::thread(&Module::periodic_loop, this);
        } else if constexpr (has_loop_input) {
            std::cout << "[" << config_.name << "] Starting free_loop thread...\n";
            data_thread_ = std::thread(&Module::free_loop, this);
        } else if constexpr (has_multi_input) {
            // Phase 6.6: Multi-input processing
            std::cout << "[" << config_.name << "] Starting multi_input_loop thread...\n";
            data_thread_ = std::thread(&Module::multi_input_loop, this);
            
            // Phase 6.9: Start secondary input receive threads
            // Primary input (index 0) is handled by multi_input_loop's blocking receive
            // Secondary inputs (indices 1, 2, ...) need background receive loops
            start_secondary_input_threads();
        } else if constexpr (has_continuous_input) {
            // Single continuous input (backward compatible)
            std::cout << "[" << config_.name << "] Starting continuous_loop thread...\n";
            data_thread_ = std::thread(&Module::continuous_loop, this);
        }
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        on_stop();
        
        // Unsubscribe from source(s)
        if constexpr (has_multi_input) {
            // Multi-input: Unsubscribe from all configured sources
            for (const auto& source : config_.input_sources) {
                unsubscribe_from_multi_input_source(source);
            }
        } else if constexpr (has_continuous_input) {
            // Single continuous input (legacy)
            if (config_.source_system_id && config_.source_instance_id) {
                unsubscribe_from_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        running_ = false;
        
        // Wait for threads to finish
        if (data_thread_ && data_thread_->joinable()) {
            data_thread_->join();
        }
        
        // Phase 6.9: Join secondary input threads
        for (auto& thread : secondary_input_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (work_thread_ && work_thread_->joinable()) {
            work_thread_->join();
        }
        if (command_thread_ && command_thread_->joinable()) {
            command_thread_->join();
        }
        
        // Stop all mailboxes
        cmd_mailbox_.stop();
        work_mailbox_.stop();
        if (data_mailbox_) {
            data_mailbox_->stop();
        }
        
        on_cleanup();
    }
    
    bool is_running() const { return running_; }
    
    // ========================================================================
    // Subscriber Management
    // ========================================================================
    
protected:
    void add_subscriber(uint32_t subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        
        // Check if already subscribed
        if (std::find(subscribers_.begin(), subscribers_.end(), subscriber_id) != subscribers_.end()) {
            return;
        }
        
        // Check capacity
        if (subscribers_.size() >= config_.max_subscribers) {
            throw std::runtime_error("Maximum subscribers reached");
        }
        
        subscribers_.push_back(subscriber_id);
    }
    
    void remove_subscriber(uint32_t subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        subscribers_.erase(
            std::remove(subscribers_.begin(), subscribers_.end(), subscriber_id),
        subscribers_.end()
    );
}

// Single-output publishing (only enabled when OutputData is not void)
template<typename T = OutputData>
    requires (!std::is_void_v<T>)
void publish_to_subscribers(T& data) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    for (uint32_t subscriber_base_addr : subscribers_) {
        // Send to subscriber's DATA mailbox (base + 32)
        uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
        auto result = cmd_mailbox_.send(data, subscriber_data_mbx);
        if (!result) {
            std::cout << "[" << config_.name << "] Send failed to subscriber " << subscriber_base_addr << "\n";
        }
    }
}

// Helper to send output only if type matches subscriber's expected type
template<typename OutputType>
void send_if_type_matches(uint16_t subscriber_type_id_low, OutputType& output, uint32_t dest_mailbox) {
    // Get the full message ID for this output type
    constexpr uint32_t output_msg_id = UserRegistry::template get_message_id<OutputType>();
    constexpr uint16_t output_type_id_low = static_cast<uint16_t>(output_msg_id & 0xFFFF);
    
    // Only send if types match
    if (output_type_id_low == subscriber_type_id_low) {
        auto result = cmd_mailbox_.send(output, dest_mailbox);
        if (!result) {
            std::cout << "[" << config_.name << "] Send failed to subscriber " 
                      << std::hex << dest_mailbox << std::dec << "\n";
        }
    }
}

// Multi-output publishing helper (publish each output in the tuple)
// Only sends outputs that match the subscriber's message type (extracted from base address)
template<typename... Ts, std::size_t... Is>
void publish_multi_outputs_impl(std::tuple<Ts...>& outputs, std::index_sequence<Is...>) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    for (uint32_t subscriber_base_addr : subscribers_) {
        uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
        
        // Extract the message type ID that this subscriber expects
        uint16_t subscriber_type_id_low = extract_message_type_from_address(subscriber_base_addr);
        
        // Send each output only if it matches the subscriber's type
        (void)std::initializer_list<int>{
            (send_if_type_matches<std::tuple_element_t<Is, std::tuple<Ts...>>>(
                subscriber_type_id_low,
                std::get<Is>(outputs),
                subscriber_data_mbx
            ), 0)...
        };
    }
}

template<typename... Ts>
void publish_multi_outputs(std::tuple<Ts...>& outputs) {
    publish_multi_outputs_impl(outputs, std::index_sequence_for<Ts...>{});
}

// Phase 6.10: TimsMessage-based publishing functions

// Helper function to create TimsMessage with header.timestamp set
template<typename T>
TimsMessage<T> create_tims_message(T&& payload, uint64_t timestamp_ns) {
    TimsMessage<T> msg{
        .header = {
            .msg_type = 0,     // serialize() will set this
            .msg_size = 0,     // serialize() will set this
            .timestamp = timestamp_ns,  // ONE SOURCE OF TRUTH
            .seq_number = 0,   // TiMS will set this
            .flags = 0
        },
        .payload = std::forward<T>(payload)
    };
    return msg;
}

// Publish TimsMessage<T> (for single output)
template<typename T>
void publish_tims_message(TimsMessage<T>& tims_msg) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    for (uint32_t subscriber_base_addr : subscribers_) {
        uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
        // Phase 6.10: Send with explicit timestamp from header
        auto result = cmd_mailbox_.send(tims_msg.payload, subscriber_data_mbx, tims_msg.header.timestamp);
        if (!result) {
            std::cout << "[" << config_.name << "] Send failed to subscriber " << subscriber_base_addr << "\n";
        }
    }
}

// Publish multi-outputs with timestamp (wraps each in TimsMessage)
template<typename... Ts>
void publish_multi_outputs_with_timestamp(std::tuple<Ts...>& outputs, uint64_t timestamp_ns) {
    // For multi-output, we still use the existing publish_multi_outputs
    // because each output goes through send_if_type_matches which wraps in TimsMessage
    // The timestamp is not used here yet - future enhancement could wrap each output
    publish_multi_outputs(outputs);
}

// ========================================================================
    // Subscription Protocol (ContinuousInput and Multi-Input)
    // ========================================================================
    
    // Phase 6.6: Multi-input subscription
    void subscribe_to_all_sources() {
        static_assert(has_continuous_input || has_multi_input, 
                      "subscribe_to_all_sources() only for continuous or multi-input modules");
        
        if constexpr (has_multi_input) {
            // Multi-input: subscribe to each source in input_sources
            if (config_.input_sources.empty()) {
                std::cerr << "[" << config_.name << "] ERROR: Multi-input module but input_sources is empty!\n";
                return;
            }
            
            std::lock_guard<std::mutex> lock(subscription_mutex_);
            input_subscriptions_.resize(config_.input_sources.size());
            
            for (size_t i = 0; i < config_.input_sources.size(); ++i) {
                const auto& source = config_.input_sources[i];
                subscribe_to_source_impl(source.system_id, source.instance_id, 
                                          source.source_primary_output_type_id, i);
            }
        } else if constexpr (has_continuous_input) {
            // Single-input: use legacy config fields
            if (config_.source_system_id && config_.source_instance_id) {
                input_subscriptions_.resize(1);
                subscribe_to_source_impl(*config_.source_system_id, *config_.source_instance_id,
                                          config_.source_primary_output_type_id, 0);
            }
        }
    }
    
    // Internal implementation: send SubscribeRequest to one source
    void subscribe_to_source_impl(uint8_t source_system_id, uint8_t source_instance_id,
                                   std::optional<uint32_t> source_primary_output_type_id,
                                   size_t source_index) {
        // For multi-input: Calculate base address using the INPUT TYPE we're subscribing to
        // This ensures producers send to the correct input-specific DATA mailbox
        uint32_t subscriber_base_addr;
        
        if constexpr (has_multi_input) {
            // Multi-input: use the input type ID at source_index for subscriber base address
            uint32_t input_type_id = get_input_type_id_at_index(source_index);
            uint16_t input_type_id_low = static_cast<uint16_t>(input_type_id & 0xFFFF);
            subscriber_base_addr = (static_cast<uint32_t>(input_type_id_low) << 16) | 
                                    (config_.system_id << 8) | config_.instance_id;
        } else {
            // Single-input: use output type for base address (backward compatible)
            subscriber_base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        }
        
        SubscribeRequestType request{
            .subscriber_mailbox_id = subscriber_base_addr,  // Input-type-specific base for multi-input
            .requested_period_ms = config_.period.count()
        };
        
        // Calculate source module's WORK mailbox address
        uint32_t source_data_type_id;
        
        if (source_primary_output_type_id) {
            // Multi-output producer: use the provided primary output type ID
            source_data_type_id = *source_primary_output_type_id;
        } else if constexpr (has_multi_input) {
            // Multi-input: use the input type at source_index
            source_data_type_id = get_input_type_id_at_index(source_index);
        } else if constexpr (has_continuous_input) {
            // Single-input: use InputData type
            source_data_type_id = UserRegistry::template get_message_id<InputData>();
        } else {
            static_assert(has_continuous_input || has_multi_input, "Invalid input configuration");
            return;
        }
        
        uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
        uint32_t source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | 
                                (source_system_id << 8) | source_instance_id;
        uint32_t source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        
        std::cout << "[" << config_.name << "] Sending SubscribeRequest[" << source_index 
                  << "] to source WORK mailbox " << source_work_mbx << "\n";
        
        // Retry a few times in case the producer's mailbox isn't ready yet
        int max_retries = 5;
        for (int i = 0; i < max_retries; ++i) {
            // Send subscribe request from work mailbox (SystemRegistry messages)
            auto result = work_mailbox_.send(request, source_work_mbx);
            if (result) {
                std::cout << "[" << config_.name << "] SubscribeRequest[" << source_index 
                          << "] sent successfully\n";
                // Mark subscription as sent (reply not yet received)
                if (source_index < input_subscriptions_.size()) {
                    input_subscriptions_[source_index].subscribed = true;
                    input_subscriptions_[source_index].reply_received = false;
                }
                return;
            }
            
            if (i < max_retries - 1) {
                std::cout << "[" << config_.name << "] Failed to send SubscribeRequest (attempt " 
                          << (i + 1) << "/" << max_retries << "), retrying...\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        
        std::cout << "[" << config_.name << "] Failed to send SubscribeRequest[" << source_index
                  << "] after " << max_retries << " attempts!\n";
    }
    
    // Helper: Get message ID for input type at runtime index
    template<size_t... Is>
    uint32_t get_input_type_id_at_index_impl(size_t index, std::index_sequence<Is...>) const {
        uint32_t result = 0;
        ((index == Is ? (result = UserRegistry::template get_message_id<std::tuple_element_t<Is, InputTypesTuple>>(), true) : false) || ...);
        return result;
    }
    
    uint32_t get_input_type_id_at_index(size_t index) const {
        if constexpr (has_multi_input) {
            return get_input_type_id_at_index_impl(index, std::make_index_sequence<InputCount>{});
        } else {
            return UserRegistry::template get_message_id<InputData>();
        }
    }
    
    // Legacy single-input wrapper (backward compatible)
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "subscribe_to_source() only available for continuous input modules");
        input_subscriptions_.resize(1);
        subscribe_to_source_impl(source_system_id, source_instance_id, 
                                  config_.source_primary_output_type_id, 0);
    }
    
    void unsubscribe_from_source(uint8_t source_system_id, uint8_t source_instance_id) {
        static_assert(has_continuous_input, "unsubscribe_from_source() only available for continuous input modules");
        
        uint32_t our_base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        
        UnsubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr
        };
        
        // Calculate source WORK mailbox
        uint32_t source_base;
        uint32_t source_work_mbx;
        
        if constexpr (has_continuous_input && !has_multi_input) {
            // Single continuous input
            constexpr uint32_t source_data_type_id = UserRegistry::template get_message_id<InputData>();
            constexpr uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
            source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | (source_system_id << 8) | source_instance_id;
            source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        }
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_.send(request, source_work_mbx);
    }
    
    void unsubscribe_from_multi_input_source(const InputSource& source) {
        static_assert(has_multi_input, "unsubscribe_from_multi_input_source() only for multi-input modules");
        
        uint32_t our_base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        
        UnsubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr
        };
        
        // Use source_primary_output_type_id if provided, otherwise we can't compute address
        // In practice, multi-input always requires this field
        if (!source.source_primary_output_type_id.has_value()) {
            std::cerr << "[" << config_.name << "] ERROR: Cannot unsubscribe - source_primary_output_type_id not set!\n";
            return;
        }
        
        uint32_t source_data_type_id = *source.source_primary_output_type_id;
        uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
        uint32_t source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | 
                               (source.system_id << 8) | source.instance_id;
        uint32_t source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_.send(request, source_work_mbx);
    }
    
    void handle_subscribe_request(const SubscribeRequestType& req) {
        try {
            // req.subscriber_mailbox_id is the subscriber's base address
            add_subscriber(req.subscriber_mailbox_id);
            uint32_t subscriber_data_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::DATA);
            std::cout << "[" << config_.name << "] Added subscriber base=" << req.subscriber_mailbox_id 
                      << ", will send to DATA mailbox=" << subscriber_data_mbx
                      << " (total subscribers: " << subscribers_.size() << ")\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = config_.period.count(),
                .success = true,
                .error_code = 0
            };
            
            // Send reply to subscriber's WORK mailbox (base + 1)
            uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_.send(reply, subscriber_work_mbx);
        } catch (...) {
            std::cout << "[" << config_.name << "] Failed to add subscriber: " << req.subscriber_mailbox_id << "\n";
            
            SubscribeReplyType reply{
                .actual_period_ms = 0,
                .success = false,
                .error_code = 1  // Max subscribers exceeded
            };
            
            uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
            work_mailbox_.send(reply, subscriber_work_mbx);
        }
    }
    
    void handle_subscribe_reply(const SubscribeReplyType& reply) {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        
        // Phase 6.6: Mark subscription as complete
        // For now, mark the first non-replied subscription
        // TODO: Need to track which reply corresponds to which source
        for (auto& sub : input_subscriptions_) {
            if (sub.subscribed && !sub.reply_received) {
                sub.reply_received = true;
                sub.actual_period_ms = reply.actual_period_ms;
                std::cout << "[" << config_.name << "] SubscribeReply received: "
                          << (reply.success ? "SUCCESS" : "FAILED")
                          << ", actual_period_ms=" << reply.actual_period_ms << "\n";
                return;
            }
        }
    }
    
    void handle_unsubscribe_request(const UnsubscribeRequestType& req) {
        remove_subscriber(req.subscriber_mailbox_id);
        
        UnsubscribeReplyType reply{.success = true};
        uint32_t subscriber_work_mbx = req.subscriber_mailbox_id + static_cast<uint8_t>(MailboxType::WORK);
        work_mailbox_.send(reply, subscriber_work_mbx);
    }
    
    // ========================================================================
    // Main Loops
    // ========================================================================
    
private:
    void periodic_loop() {
        std::cout << "[" << config_.name << "] periodic_loop started, period=" << config_.period.count() << "ms\n";
        uint32_t iteration = 0;
        while (running_) {
            if (iteration < 3) {
                std::cout << "[" << config_.name << "] periodic_loop iteration " << iteration << "\n";
            }
            
            // Capture timestamp at data generation moment
            uint64_t generation_timestamp = Time::now();
            
            if constexpr (has_multi_output) {
                // Multi-output: create tuple and call process with references
                OutputTypesTuple outputs{};
                // Unpack tuple and call multi-output process(Ts&...) via virtual dispatch
                std::apply([this](auto&... args) { 
                    // Call derived class override of MultiOutputProcessorBase::process(Ts&...)
                    using Base = MultiOutputProcessorBase<OutputTypesTuple, InputData>;
                    static_cast<Base*>(this)->process(args...);
                }, outputs);
                // Phase 6.10: Publish with automatic header.timestamp
                publish_multi_outputs_with_timestamp(outputs, generation_timestamp);
            } else {
                // Single output: call process() with virtual dispatch
                auto output = this->process();  // Virtual call to derived class
                // Phase 6.10: Wrap in TimsMessage with header.timestamp = generation time
                auto tims_msg = create_tims_message(std::move(output), generation_timestamp);
                publish_tims_message(tims_msg);
            }
            std::this_thread::sleep_for(config_.period);
            iteration++;
        }
        std::cout << "[" << config_.name << "] periodic_loop ended after " << iteration << " iterations\n";
    }

    void free_loop() {
        while (running_) {
            // Phase 6.10: Capture timestamp at data generation moment
            uint64_t generation_timestamp = Time::now();
            
            if constexpr (has_multi_output) {
                // Multi-output: create tuple and call process with references
                OutputTypesTuple outputs{};
                // Unpack tuple and call multi-output process(Ts&...) via virtual dispatch
                std::apply([this](auto&... args) { 
                    // Call derived class override of MultiOutputProcessorBase::process(Ts&...)
                    using Base = MultiOutputProcessorBase<OutputTypesTuple, InputData>;
                    static_cast<Base*>(this)->process(args...);
                }, outputs);
                // Phase 6.10: Publish with automatic header.timestamp
                publish_multi_outputs_with_timestamp(outputs, generation_timestamp);
            } else {
                // Single output: call process() with virtual dispatch
                auto output = this->process();  // Virtual call to derived class
                // Phase 6.10: Wrap in TimsMessage with header.timestamp = generation time
                auto tims_msg = create_tims_message(std::move(output), generation_timestamp);
                publish_tims_message(tims_msg);
            }
        }
    }
    
    void continuous_loop() {
        std::cout << "[" << config_.name << "] continuous_loop started, waiting for data...\n";
        while (running_) {
            // BLOCKING receive on data mailbox - no timeout, waits for data
            auto result = data_mailbox_->template receive<InputData>();
            
            if (result) {
                // Phase 6.10: Populate metadata BEFORE process call
                // Single continuous input always uses index 0
                // result is TimsMessage<InputData> - use directly
                update_input_metadata(0, result.value(), true);  // Always new data for continuous
                
                auto output = process_continuous_dispatch(result->payload);
                // Phase 6.10: Use input timestamp from header (data validity time)
                // TimsMessage.header.timestamp is the authoritative timestamp
                auto tims_msg = create_tims_message(std::move(output), result->header.timestamp);
                publish_tims_message(tims_msg);
            }
        }
        std::cout << "[" << config_.name << "] continuous_loop ended\n";
    }
    
    // ========================================================================
    // Phase 6.6: Multi-Input Processing Loop
    // ========================================================================
    
    void multi_input_loop() {
        static_assert(has_multi_input, "multi_input_loop only for multi-input modules");
        
        std::cout << "[" << config_.name << "] multi_input_loop started (" << InputCount << " inputs)\n";
        
        // Identify primary input index from InputSpec
        // InputSpec should be Inputs<T1, T2, ...> with PrimaryInput<Tprimary> specified
        constexpr size_t primary_idx = get_primary_input_index();
        
        std::cout << "[" << config_.name << "] Primary input index: " << primary_idx << "\n";
        
        uint32_t loop_iteration = 0;
        while (running_) {
            // Step 1: BLOCK on primary input (drives execution)
            if (loop_iteration < 3) {
                std::cout << "[" << config_.name << "] Waiting for primary input... (iteration " << loop_iteration << ")\n";
            }
            auto primary_result = receive_primary_input<primary_idx>();
            
            if (!primary_result.has_value()) {
                if (loop_iteration < 3) {
                    std::cout << "[" << config_.name << "] No primary data received\n";
                }
                loop_iteration++;
                continue;  // No data, loop again
            }
            
            if (loop_iteration < 3) {
                std::cout << "[" << config_.name << "] Primary input received!\n";
            }
            
            // Phase 6.10: Populate primary metadata BEFORE gather_all_inputs
            // Primary input always uses index 0 in metadata array
            // primary_result is TimsMessage<PrimaryType> - use directly
            update_input_metadata(0, primary_result.value(), true);  // Primary always new data
            
            // Step 2: Sync all secondary inputs using primary timestamp
            auto all_inputs = gather_all_inputs<primary_idx>(primary_result.value());
            
            if (!all_inputs) {
                // Failed to sync all inputs - skip this cycle
                if (loop_iteration < 3) {
                    std::cout << "[" << config_.name << "] Failed to sync inputs\n";
                }
                loop_iteration++;
                continue;
            }
            
            if (loop_iteration < 3) {
                std::cout << "[" << config_.name << "] All inputs synced, calling process()\n";
            }
            
            // Phase 6.10: Extract primary input timestamp from header (synchronization point)
            // TimsMessage.header.timestamp is the authoritative timestamp
            uint64_t primary_timestamp = primary_result->header.timestamp;
            
            // Step 3: Call process with all inputs
            if constexpr (has_multi_output) {
                // Multi-input + Multi-output: void process(const T1&, ..., O1&, O2&, ...)
                OutputTypesTuple outputs{};
                call_multi_input_multi_output_process(*all_inputs, outputs);
                // Phase 6.10: Publish with header.timestamp = primary timestamp (sync point)
                publish_multi_outputs_with_timestamp(outputs, primary_timestamp);
            } else {
                // Multi-input + Single output: OutputData process(const T1&, ...)
                auto output = call_multi_input_process(*all_inputs);
                // Phase 6.10: Wrap with header.timestamp = primary timestamp (sync point)
                auto tims_msg = create_tims_message(std::move(output), primary_timestamp);
                publish_tims_message(tims_msg);
            }
            loop_iteration++;
        }
        
        std::cout << "[" << config_.name << "] multi_input_loop ended\n";
    }
    
    // Phase 6.9: Secondary input receive loop
    // Continuously receives from secondary input mailboxes to populate their buffers
    template<size_t InputIdx>
    void secondary_input_receive_loop() {
        using InputType = std::tuple_element_t<InputIdx, InputTypesTuple>;
        auto& mailbox = std::get<InputIdx>(*input_mailboxes_);
        
        std::cout << "[" << config_.name << "] secondary_input_receive_loop[" << InputIdx << "] started\n";
        
        int receive_count = 0;
        while (running_) {
            // Blocking receive - stores in historical buffer automatically
            auto result = mailbox.template receive<InputType>();
            if (!result.has_value()) {
                std::cout << "[" << config_.name << "] secondary_input_receive_loop[" << InputIdx 
                          << "] receive failed after " << receive_count << " messages\n";
                break;
            }
            receive_count++;
            if (receive_count <= 3) {
                std::cout << "[" << config_.name << "] secondary_input_receive_loop[" << InputIdx 
                          << "] received message #" << receive_count 
                          << ", timestamp=" << result.value().header.timestamp << "\n";
            }
        }
        
        std::cout << "[" << config_.name << "] secondary_input_receive_loop[" << InputIdx 
                  << "] ended (total: " << receive_count << " messages)\n";
    }
    
    // Helper: Get primary input index at compile time
    static constexpr size_t get_primary_input_index() {
        // Phase 6.7: Extract primary input from CommandTypes (PrimaryInput<T> passed as command)
        if constexpr (has_primary_input_spec) {
            // PrimaryInput<T> found in CommandTypes, extract T and find index
            return PrimaryInputIndex_v<PrimaryPayloadType, InputTypesTuple>;
        } else {
            // No explicit PrimaryInput - use first input as primary
            return 0;
        }
    }
    
    // Helper: Receive from primary input mailbox
    template<size_t PrimaryIdx>
    auto receive_primary_input() -> MailboxResult<TimsMessage<std::tuple_element_t<PrimaryIdx, InputTypesTuple>>> {
        if (!input_mailboxes_) {
            return MailboxResult<TimsMessage<std::tuple_element_t<PrimaryIdx, InputTypesTuple>>>(MailboxError::NotInitialized);
        }
        
        using PrimaryType = std::tuple_element_t<PrimaryIdx, InputTypesTuple>;
        auto& primary_mailbox = std::get<PrimaryIdx>(*input_mailboxes_);
        
        // BLOCKING receive - drives execution rate
        return primary_mailbox.template receive<PrimaryType>();
    }
    
    // Helper: Gather all inputs synchronized to primary timestamp
    template<size_t PrimaryIdx, typename PrimaryMsgType>
    std::optional<InputTypesTuple> gather_all_inputs(const PrimaryMsgType& primary_msg) {
        if (!input_mailboxes_) {
            return std::nullopt;
        }
        
        // Create tuple to hold all inputs
        InputTypesTuple all_inputs{};
        
        // Place primary input at its index
        std::get<PrimaryIdx>(all_inputs) = primary_msg.payload;
        
        // Phase 6.10: Sync secondary inputs using getData with primary timestamp from header
        // TimsMessage.header.timestamp is the authoritative timestamp
        bool all_synced = sync_secondary_inputs<PrimaryIdx>(primary_msg.header.timestamp, all_inputs);
        
        if (!all_synced) {
            return std::nullopt;
        }
        
        return all_inputs;
    }
    
    // Helper: Sync all secondary inputs via getData
    template<size_t PrimaryIdx>
    bool sync_secondary_inputs(uint64_t primary_timestamp, InputTypesTuple& all_inputs) {
        return sync_secondary_inputs_impl<PrimaryIdx>(primary_timestamp, all_inputs, 
                                                       std::make_index_sequence<InputCount>{});
    }
    
    template<size_t PrimaryIdx, size_t... Is>
    bool sync_secondary_inputs_impl(uint64_t primary_timestamp, InputTypesTuple& all_inputs,
                                     std::index_sequence<Is...>) {
        // For each input index (except primary), call getData
        bool all_success = true;
        
        // Fold expression: process each secondary input
        ((Is != PrimaryIdx ? 
          (all_success = sync_input_at_index<Is>(primary_timestamp, all_inputs) && all_success) : 
          true), ...);
        
        return all_success;
    }
    
    template<size_t Index>
    bool sync_input_at_index(uint64_t primary_timestamp, InputTypesTuple& all_inputs) {
        using InputType = std::tuple_element_t<Index, InputTypesTuple>;
        auto& mailbox = std::get<Index>(*input_mailboxes_);
        
        // Non-blocking getData with tolerance
        auto result = mailbox.template getData<InputType>(
            primary_timestamp,
            config_.sync_tolerance,
            InterpolationMode::NEAREST
        );
        
        if (result) {
            std::get<Index>(all_inputs) = result->payload;
            
            // Phase 6.10: Populate secondary metadata
            // Secondary inputs: Index 1..N in metadata array (primary is 0)
            // Calculate metadata index: if Index < PrimaryIdx, use Index+1, else use Index
            // Actually, we store primary at [0] and secondaries at [1..N-1] in order
            // For now, use simple mapping: metadata[Index] = input[Index]
            // TODO: This needs proper mapping when primary is not at index 0
            
            // Determine if this is fresh data or stale
            bool is_new = (result->header.timestamp == primary_timestamp);
            
            // Create TimsMessage wrapper for metadata extraction (already is TimsMessage!)
            // Store metadata (secondary inputs use their actual index for now)
            // This assumes primary is at index 0 in InputTypesTuple
            update_input_metadata(Index, *result, is_new);
            
            return true;
        } else {
            std::cout << "[" << config_.name << "] Failed to sync input " << Index 
                      << " (primary_ts=" << primary_timestamp << ", tolerance=" << config_.sync_tolerance << ")\n";
            
            // Phase 6.10: Mark as invalid
            mark_input_invalid(Index);
            return false;
        }
    }
    
    // Phase 6.7: Call multi-input process with single output
    OutputData call_multi_input_process(const InputTypesTuple& inputs) {
        return call_multi_input_process_impl(inputs, std::make_index_sequence<InputCount>{});
    }
    
    template<size_t... Is>
    OutputData call_multi_input_process_impl(const InputTypesTuple& inputs, std::index_sequence<Is...>) {
        // Unpack tuple and call process(const T1&, const T2&, ...)
        using Base = MultiInputProcessorBase<InputTypesTuple, OutputData, InputCount>;
        return static_cast<Base*>(this)->process(std::get<Is>(inputs)...);
    }
    
    // Phase 6.7: Call multi-input process with multi-output
    void call_multi_input_multi_output_process(const InputTypesTuple& inputs, OutputTypesTuple& outputs) {
        call_multi_input_multi_output_process_impl(inputs, outputs, 
                                                    std::make_index_sequence<InputCount>{},
                                                    std::make_index_sequence<std::tuple_size_v<OutputTypesTuple>>{});
    }
    
    template<size_t... InputIs, size_t... OutputIs>
    void call_multi_input_multi_output_process_impl(const InputTypesTuple& inputs, OutputTypesTuple& outputs,
                                                      std::index_sequence<InputIs...>,
                                                      std::index_sequence<OutputIs...>) {
        // Unpack both tuples and call process(const T1&, ..., O1&, O2&, ...)
        using Base = MultiInputProcessorBase<InputTypesTuple, OutputTypesTuple, InputCount>;
        static_cast<Base*>(this)->process(std::get<InputIs>(inputs)..., std::get<OutputIs>(outputs)...);
    }
    
    void command_loop() {
        std::cout << "[" << config_.name << "] command_loop started\n";
        while (running_) {
            // Use receive_any with visitor pattern on cmd_mailbox
            // BLOCKING receive - waits indefinitely for user commands
            auto visitor = [this](auto&& tims_msg) {
                // tims_msg is TimsMessage<PayloadT>, extract payload
                auto& msg = tims_msg.payload;
                using MsgType = std::decay_t<decltype(msg)>;
                
                std::cout << "[" << config_.name << "] Received command in command_loop\n";
                
                // Handle user command types only
                handle_user_command(msg);
            };
            
            // BLOCKING receive on command mailbox (no timeout)
            cmd_mailbox_.receive_any(visitor);
        }
        std::cout << "[" << config_.name << "] command_loop ended\n";
    }
    
    void work_loop() {
        std::cout << "[" << config_.name << "] work_loop started, listening on WORK mailbox " 
                  << get_mailbox_address(config_.system_id, config_.instance_id, MailboxType::WORK) 
                  << "\n" << std::flush;
        while (running_) {
            // Use receive_any with visitor pattern on work_mailbox
            // BLOCKING receive - waits indefinitely for subscription messages
            std::cout << "[" << config_.name << "] work_loop: waiting for message...\n" << std::flush;
            auto visitor = [this](auto&& tims_msg) {
                // tims_msg is TimsMessage<PayloadT>, extract payload
                auto& msg = tims_msg.payload;
                using MsgType = std::decay_t<decltype(msg)>;
                
                // Handle subscription protocol
                if constexpr (std::is_same_v<MsgType, SubscribeRequestType>) {
                    std::cout << "[" << config_.name << "] Handling SubscribeRequest\n";
                    handle_subscribe_request(msg);
                } else if constexpr (std::is_same_v<MsgType, SubscribeReplyType>) {
                    std::cout << "[" << config_.name << "] Handling SubscribeReply\n";
                    handle_subscribe_reply(msg);
                } else if constexpr (std::is_same_v<MsgType, UnsubscribeRequestType>) {
                    std::cout << "[" << config_.name << "] Handling UnsubscribeRequest\n";
                    handle_unsubscribe_request(msg);
                }
            };
            
            // BLOCKING receive on work mailbox (no timeout)
            work_mailbox_.receive_any(visitor);
        }
        std::cout << "[" << config_.name << "] work_loop ended\n";
    }
    
    /**
     * @brief Dispatch user command to on_command handler
     */
    template<typename CmdT>
    void handle_user_command(const CmdT& cmd) {
        // Check if this is one of our declared CommandTypes
        if constexpr ((std::is_same_v<CmdT, CommandTypes> || ...)) {
            on_command(cmd);
        }
        // Otherwise ignore (not in our command list)
    }
    
    /**
     * @brief Handle a specific command type (override in derived class)
     * 
     * @tparam CmdT Command payload type
     * @param cmd Command payload
     */
    template<typename CmdT>
    void on_command(const CmdT& cmd) {
        // Default: no-op - override in derived classes for specific CommandTypes
    }
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
    static MailboxConfig createMailboxConfig(const ModuleConfig& config, MailboxType type) {
        uint32_t mailbox_id = get_mailbox_address(config.system_id, config.instance_id, type);
        return MailboxConfig{
            .mailbox_id = mailbox_id,
            .message_slots = config.message_slots,
            .max_message_size = UserRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_" + 
                           (type == MailboxType::CMD ? "cmd" : 
                            type == MailboxType::WORK ? "work" : "data")
        };
    }
    
    static MailboxConfig createWorkMailboxConfig(const ModuleConfig& config) {
        uint32_t mailbox_id = get_mailbox_address(config.system_id, config.instance_id, MailboxType::WORK);
        return MailboxConfig{
            .mailbox_id = mailbox_id,
            .message_slots = config.message_slots,
            .max_message_size = SystemRegistry::max_message_size,  // Smaller buffer for system messages
            .send_priority = static_cast<uint8_t>(config.priority),
            .realtime = config.realtime,
            .mailbox_name = config.name + "_work"
        };
    }
    
    // ========================================================================
    // Phase 6.6: Multi-Input Mailbox Helpers
    // ========================================================================
    
    // Phase 6.9: Start receive threads for secondary inputs (all except primary)
    void start_secondary_input_threads() {
        if constexpr (has_multi_input) {
            constexpr size_t primary_idx = get_primary_input_index();
            start_secondary_threads_impl<primary_idx>(std::make_index_sequence<InputCount>{});
        }
    }
    
    template<size_t PrimaryIdx, size_t... Is>
    void start_secondary_threads_impl(std::index_sequence<Is...>) {
        // Start thread for each input except primary
        ((Is != PrimaryIdx ? 
          (secondary_input_threads_.emplace_back(&Module::secondary_input_receive_loop<Is>, this), true) : 
          true), ...);
    }
    
    // Create HistoricalMailbox for specific input at compile-time index
    template<size_t Index>
    auto create_historical_mailbox_for_input() {
        using InputType = std::tuple_element_t<Index, InputTypesTuple>;
        
        // Create DATA mailbox config for this input
        // Use input type's message ID to calculate base address
        constexpr uint32_t input_msg_id = UserRegistry::template get_message_id<InputType>();
        constexpr uint16_t input_type_id_low = static_cast<uint16_t>(input_msg_id & 0xFFFF);
        uint32_t base_addr = (static_cast<uint32_t>(input_type_id_low) << 16) | 
                             (config_.system_id << 8) | config_.instance_id;
        uint32_t data_mailbox_id = base_addr + static_cast<uint8_t>(MailboxType::DATA);
        
        std::cout << "[" << config_.name << "] Creating input mailbox[" << Index 
                  << "] at address " << data_mailbox_id << " (base=" << base_addr << ")\n";
        
        MailboxConfig mbx_config{
            .mailbox_id = data_mailbox_id,
            .message_slots = config_.message_slots,
            .max_message_size = UserRegistry::max_message_size,
            .send_priority = static_cast<uint8_t>(config_.priority),
            .realtime = config_.realtime,
            .mailbox_name = config_.name + "_data_" + std::to_string(Index)
        };
        
        return HistoricalMailboxFor<InputType>(
            mbx_config,
            config_.sync_tolerance
        );
    }
    
    // Create tuple of HistoricalMailbox instances
    template<size_t... Is>
    void create_input_mailboxes_impl(std::index_sequence<Is...>) {
        if constexpr (has_multi_input) {
            input_mailboxes_ = std::make_tuple(
                create_historical_mailbox_for_input<Is>()...
            );
        }
    }
    
    void initialize_multi_input_mailboxes() {
        if constexpr (has_multi_input) {
            create_input_mailboxes_impl(std::make_index_sequence<InputCount>{});
        }
    }
    
    // Start all input mailboxes
    template<size_t... Is>
    void start_input_mailboxes_impl(std::index_sequence<Is...>) {
        if (input_mailboxes_) {
            (std::get<Is>(*input_mailboxes_).start(), ...);
        }
    }
    
    void start_input_mailboxes() {
        if constexpr (has_multi_input) {
            start_input_mailboxes_impl(std::make_index_sequence<InputCount>{});
        }
    }
};

} // namespace commrat
