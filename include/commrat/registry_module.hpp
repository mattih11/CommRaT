#pragma once

#include "commrat/registry_mailbox.hpp"
#include "commrat/historical_mailbox.hpp"
#include "commrat/subscription_messages.hpp"
#include "commrat/system_registry.hpp"
#include "commrat/io_spec.hpp"
#include "commrat/threading.hpp"
#include "commrat/timestamp.hpp"
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
    
    // Subscriber management
    std::vector<uint32_t> subscribers_;
    std::mutex subscribers_mutex_;
    
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
            data_thread_ = std::thread(&Module::periodic_loop, this);
        } else if constexpr (has_loop_input) {
            data_thread_ = std::thread(&Module::free_loop, this);
        } else if constexpr (has_multi_input) {
            // Phase 6.6: Multi-input processing
            data_thread_ = std::thread(&Module::multi_input_loop, this);
        } else if constexpr (has_continuous_input) {
            // Single continuous input (backward compatible)
            data_thread_ = std::thread(&Module::continuous_loop, this);
        }
    }
    
    void stop() {
        if (!running_) {
            return;
        }
        
        on_stop();
        
        // Unsubscribe from source
        if constexpr (has_continuous_input) {
            if (config_.source_system_id && config_.source_instance_id) {
                unsubscribe_from_source(*config_.source_system_id, *config_.source_instance_id);
            }
        }
        
        running_ = false;
        
        // Wait for threads to finish
        if (data_thread_ && data_thread_->joinable()) {
            data_thread_->join();
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
        // Send to subscriber's DATA mailbox (base + 2)
        uint32_t subscriber_data_mbx = subscriber_base_addr + static_cast<uint8_t>(MailboxType::DATA);
        auto result = cmd_mailbox_.send(data, subscriber_data_mbx);  // Can send from any mailbox
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
}    // ========================================================================
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
        // Calculate our base address to include in request
        uint32_t our_base_addr = calculate_base_address(config_.system_id, config_.instance_id);
        
        SubscribeRequestType request{
            .subscriber_mailbox_id = our_base_addr,  // Send our base address, not specific mailbox
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
        
        if constexpr (has_continuous_input) {
            constexpr uint32_t source_data_type_id = UserRegistry::template get_message_id<InputData>();
            constexpr uint16_t source_data_type_id_low = static_cast<uint16_t>(source_data_type_id & 0xFFFF);
            source_base = (static_cast<uint32_t>(source_data_type_id_low) << 16) | (source_system_id << 8) | source_instance_id;
            source_work_mbx = source_base + static_cast<uint8_t>(MailboxType::WORK);
        }
        
        // Send unsubscribe request from work mailbox (SystemRegistry messages)
        work_mailbox_.send(request, source_work_mbx);
    }
    
    void handle_subscribe_request(const SubscribeRequestType& req) {
        try {
            // req.subscriber_mailbox_id is the subscriber's base address
            add_subscriber(req.subscriber_mailbox_id);
            std::cout << "[" << config_.name << "] Added subscriber: " << req.subscriber_mailbox_id 
                      << " (total: " << subscribers_.size() << ")\n";
            
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
        while (running_) {
            if constexpr (has_multi_output) {
                // Multi-output: create tuple and call process with references
                OutputTypesTuple outputs{};
                // Unpack tuple and call multi-output process(Ts&...) via virtual dispatch
                std::apply([this](auto&... args) { 
                    // Call derived class override of MultiOutputProcessorBase::process(Ts&...)
                    using Base = MultiOutputProcessorBase<OutputTypesTuple, InputData>;
                    static_cast<Base*>(this)->process(args...);
                }, outputs);
                publish_multi_outputs(outputs);
            } else {
                // Single output: call SingleOutputProcessorBase::process()
                auto output = SingleOutputProcessorBase<OutputData>::process();
                publish_to_subscribers(output);
            }
            std::this_thread::sleep_for(config_.period);
        }
    }

    void free_loop() {
        while (running_) {
            if constexpr (has_multi_output) {
                // Multi-output: create tuple and call process with references
                OutputTypesTuple outputs{};
                // Unpack tuple and call multi-output process(Ts&...) via virtual dispatch
                std::apply([this](auto&... args) { 
                    // Call derived class override of MultiOutputProcessorBase::process(Ts&...)
                    using Base = MultiOutputProcessorBase<OutputTypesTuple, InputData>;
                    static_cast<Base*>(this)->process(args...);
                }, outputs);
                publish_multi_outputs(outputs);
            } else {
                // Single output: call SingleOutputProcessorBase::process()
                auto output = SingleOutputProcessorBase<OutputData>::process();
                publish_to_subscribers(output);
            }
        }
    }
    
    void continuous_loop() {
        std::cout << "[" << config_.name << "] continuous_loop started, waiting for data...\n";
        while (running_) {
            // BLOCKING receive on data mailbox - no timeout, waits for data
            auto result = data_mailbox_->template receive<InputData>();
            
            if (result) {
                auto output = process_continuous_dispatch(result->message);
                publish_to_subscribers(output);
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
        
        while (running_) {
            // Step 1: BLOCK on primary input (drives execution)
            auto primary_result = receive_primary_input<primary_idx>();
            
            if (!primary_result) {
                continue;  // No data, loop again
            }
            
            // Step 2: Sync all secondary inputs using primary timestamp
            auto all_inputs = gather_all_inputs<primary_idx>(*primary_result);
            
            if (!all_inputs) {
                // Failed to sync all inputs - skip this cycle
                continue;
            }
            
            // Step 3: Call process with all inputs
            if constexpr (has_multi_output) {
                // Multi-input + Multi-output: void process(const T1&, ..., O1&, O2&, ...)
                OutputTypesTuple outputs{};
                call_multi_input_multi_output_process(*all_inputs, outputs);
                publish_multi_outputs(outputs);
            } else {
                // Multi-input + Single output: OutputData process(const T1&, ...)
                auto output = call_multi_input_process(*all_inputs);
                publish_to_subscribers(output);
            }
        }
        
        std::cout << "[" << config_.name << "] multi_input_loop ended\n";
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
    auto receive_primary_input() {
        if (!input_mailboxes_) {
            return std::optional<ReceivedMessage<std::tuple_element_t<PrimaryIdx, InputTypesTuple>>>{};
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
        std::get<PrimaryIdx>(all_inputs) = primary_msg.message;
        
        // Sync secondary inputs using getData
        bool all_synced = sync_secondary_inputs<PrimaryIdx>(primary_msg.timestamp, all_inputs);
        
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
            return true;
        } else {
            std::cout << "[" << config_.name << "] Failed to sync input " << Index << "\n";
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
            auto visitor = [this](auto&& received_msg) {
                // received_msg is ReceivedMessage<PayloadT>, extract payload
                auto& msg = received_msg.message;
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
            auto visitor = [this](auto&& received_msg) {
                // received_msg is ReceivedMessage<PayloadT>, extract payload
                auto& msg = received_msg.message;
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
