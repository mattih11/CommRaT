#pragma once

#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"  // Phase 7.3: Type-optimized mailboxes
#include "commrat/mailbox/historical_mailbox.hpp"
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/platform/threading.hpp"
#include "commrat/platform/timestamp.hpp"
#include "commrat/module/io_spec.hpp"
#include "commrat/module/module_config.hpp"
#include "commrat/module/traits/type_extraction.hpp"
#include "commrat/module/traits/processor_bases.hpp"
#include "commrat/module/traits/multi_input_resolver.hpp"
#include "commrat/module/helpers/address_helpers.hpp"
#include "commrat/module/helpers/tims_helpers.hpp"
#include "commrat/module/metadata/input_metadata.hpp"
#include "commrat/module/metadata/input_metadata_accessors.hpp"  // Phase 5: Metadata accessor mixin
#include "commrat/module/subscription.hpp"
#include "commrat/module/publishing.hpp"
#include "commrat/module/loops/loop_executor.hpp"  // Phase 5: Loop implementations (must be before Module)
#include "commrat/module/mailbox_set.hpp"  // Phase 7.4: Per-output-type mailbox sets
#include <sertial/sertial.hpp>
#include <atomic>
#include <vector>
#include <functional>
#include <optional>

namespace commrat {

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
// Note: Now imported from module/traits/multi_input_resolver.hpp

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
    , public commrat::SubscriberManager // Subscriber management for subscription protocol
    , public LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>  // Loop implementations for periodic and free loops
    , public InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>  // Input metadata accessors
{
    // Friend declarations for CRTP mixins
    friend class LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    
private:
    // Normalize OutputSpec: raw type T -> Output<T>
    using OutputSpec = NormalizeOutput_t<OutputSpec_>;
    
    // Normalize InputSpec: ContinuousInput<T> -> Input<T>, keep others as-is
    using InputSpec = NormalizeInput_t<InputSpec_>;
    
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
    static constexpr size_t num_output_types = std::tuple_size_v<OutputTypesTuple>;
    
    // Phase 7.4: Generate MailboxSet for each output type
    template<typename... OutputTypes>
    struct MakeMailboxSetTuple;
    
    template<typename... OutputTypes>
    struct MakeMailboxSetTuple<std::tuple<OutputTypes...>> {
        using type = std::tuple<MailboxSet<UserRegistry, OutputTypes, CommandTypes...>...>;
    };
    
    using MailboxSetTuple = typename MakeMailboxSetTuple<OutputTypesTuple>::type;
    
public:
    using OutputData = typename ExtractOutputData<OutputSpec>::type;
    using InputData = typename ExtractInputData<InputSpec>::type;
    
    static constexpr bool has_continuous_input = HasContinuousInput<InputSpec>;
    static constexpr bool has_periodic_input = std::is_same_v<InputSpec, PeriodicInput>;
    static constexpr bool has_loop_input = std::is_same_v<InputSpec, LoopInput>;
    static constexpr bool has_multi_output = OutputCount_v<OutputSpec> > 1;
    
private:
    // Phase 7.3: Type-optimized mailboxes
    // CMD mailbox: Commands + Outputs (receives commands, sends outputs to subscribers)
    template<typename... Ts>
    struct MakeTypedCmdMailbox;
    
    template<typename... Ts>
    struct MakeTypedCmdMailbox<std::tuple<Ts...>> {
        using type = std::conditional_t<
            sizeof...(Ts) == 0,
            RegistryMailbox<UserRegistry>,  // No types → regular mailbox
            TypedMailbox<UserRegistry, Ts...>  // Has types → restrict
        >;
    };
    
    using CommandTuple = std::tuple<CommandTypes...>;
    using CombinedCmdTypes = decltype(std::tuple_cat(
        std::declval<CommandTuple>(),
        std::declval<OutputTypesTuple>()
    ));
    
    using CmdMailbox = typename MakeTypedCmdMailbox<CommandTuple>::type;
    
    // WORK mailbox: Subscription protocol (all 4 types from SystemRegistry)
    using WorkMailbox = RegistryMailbox<SystemRegistry>;
    
    // PUBLISH mailbox: Output publishing (producer sends outputs to subscribers)
    using PublishMailbox = typename MakeTypedCmdMailbox<OutputTypesTuple>::type;
    
    // DATA mailbox: Input data types only
    // Extract payload types from InputSpec and use them to restrict mailbox
    template<typename T>
    struct ExtractDataTypes { using type = std::tuple<>; };
    
    template<typename T>
    struct ExtractDataTypes<Input<T>> { using type = std::tuple<T>; };
    
    template<typename... Ts>
    struct ExtractDataTypes<Inputs<Ts...>> { using type = std::tuple<Ts...>; };
    
    using DataTypesTuple = typename ExtractDataTypes<InputSpec>::type;
    
    // Convert tuple<Ts...> to TypedMailbox<Registry, Ts...>
    template<typename Tuple>
    struct MakeTypedDataMailbox;
    
    template<typename... Ts>
    struct MakeTypedDataMailbox<std::tuple<Ts...>> {
        using type = std::conditional_t<
            sizeof...(Ts) == 0,
            RegistryMailbox<UserRegistry>,  // No inputs → regular mailbox
            TypedMailbox<UserRegistry, Ts...>  // Has inputs → restrict to input types
        >;
    };
    
    using DataMailbox = typename MakeTypedDataMailbox<DataTypesTuple>::type;
    
    // Phase 7.4: Conditional mailbox structure
    // Single output: Use traditional cmd/work/publish mailboxes
    // Multiple outputs: Use tuple of MailboxSets (one per output type)
    static constexpr bool use_mailbox_sets = (num_output_types > 1);
    
protected:
    ModuleConfig config_;
    
    // Phase 7.4: Per-output mailbox infrastructure
    // For single output: Direct mailboxes (backward compatible)
    // For multi-output: Tuple of MailboxSets (each output at its own base address)
    std::conditional_t<
        use_mailbox_sets,
        MailboxSetTuple,  // Multi-output: tuple of MailboxSets
        std::tuple<CmdMailbox, WorkMailbox, PublishMailbox>  // Single-output: wrapped in tuple for uniform access
    > mailbox_infrastructure_;
    
    // Helper accessors for single-output case (backward compatible)
    CmdMailbox& cmd_mailbox() {
        if constexpr (!use_mailbox_sets) {
            return std::get<0>(mailbox_infrastructure_);
        } else {
            // Multi-output: return first MailboxSet's cmd mailbox
            return *std::get<0>(mailbox_infrastructure_).cmd;
        }
    }
    
    WorkMailbox& work_mailbox() {
        if constexpr (!use_mailbox_sets) {
            return std::get<1>(mailbox_infrastructure_);
        } else {
            // Multi-output: return first MailboxSet's work mailbox
            return *std::get<0>(mailbox_infrastructure_).work;
        }
    }
    
    PublishMailbox& publish_mailbox() {
        static_assert(!use_mailbox_sets, "publish_mailbox() accessor only available for single-output modules. Use get_publish_mailbox_public<Index>() for multi-output");
        return std::get<2>(mailbox_infrastructure_);
    }
    
    // Multi-output accessor - get specific MailboxSet by index
    template<std::size_t Index>
    auto& get_mailbox_set() {
        static_assert(use_mailbox_sets, "get_mailbox_set() only available for multi-output modules");
        return std::get<Index>(mailbox_infrastructure_);
    }
    
    // Get publish mailbox by index for multi-output modules
    template<std::size_t Index>
    auto& get_publish_mailbox() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).publish;
        } else {
            static_assert(Index == 0, "Single-output modules only have one publish mailbox (index 0)");
            return std::get<2>(mailbox_infrastructure_);
        }
    }
    
    // Get work mailbox by index for multi-output modules
    template<std::size_t Index>
    auto& get_work_mailbox() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).work;
        } else {
            static_assert(Index == 0, "Single-output modules only have one work mailbox (index 0)");
            return std::get<1>(mailbox_infrastructure_);
        }
    }
    
    // Get cmd mailbox by index for multi-output modules
    template<std::size_t Index>
    auto& get_cmd_mailbox() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).cmd;
        } else {
            static_assert(Index == 0, "Single-output modules only have one cmd mailbox (index 0)");
            return std::get<0>(mailbox_infrastructure_);
        }
    }
    
    // Phase 6.6: Multi-input support
    // Single-input mode (backward compatible)
    std::optional<DataMailbox> data_mailbox_;  // base + 48: Receives input data (only for single ContinuousInput)
    
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
    
    // Subscription protocol (Phase 5: extracted to subscription.hpp)
    using SubscriptionProtocolType = commrat::SubscriptionProtocol<
        UserRegistry,
        has_continuous_input,
        has_multi_input,
        InputData,
        InputTypesTuple,
        InputCount,
        OutputData,
        OutputTypesTuple
    >;
    SubscriptionProtocolType subscription_protocol_;
    
    // Publishing logic (Phase 5: extracted to publishing.hpp)
    // Phase 7.4: Add Module type for multi-output mailbox access
    using PublisherType = commrat::Publisher<
        UserRegistry, 
        OutputData, 
        commrat::SubscriberManager, 
        PublishMailbox,
        Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>  // Module type for get_publish_mailbox<Index>()
    >;
    PublisherType publisher_;
    
    std::atomic<bool> running_;
    
    // Module threads
    std::optional<std::thread> data_thread_;
    std::optional<std::thread> command_thread_;
    std::optional<std::thread> work_thread_;              // Single-output: one WORK mailbox
    std::vector<std::thread> output_work_threads_;        // Multi-output: one thread per MailboxSet
    std::vector<std::thread> secondary_input_threads_;    // Phase 6.9: Receive loops for secondary inputs
    
    // Subscriber management - inherited from SubscriberManager base class
    // (subscribers_, subscribers_mutex_ now in SubscriberManager)
    
    // ========================================================================
    // Phase 6.10: Input Metadata Storage
    // ========================================================================
    
    /**
     * @brief Storage for input message metadata
     * 
     * Stores metadata about received input messages, populated before process() calls.
     * Available to derived classes via get_input_metadata<Index>() accessors.
     * 
     * Note: InputMetadataStorage defined in commrat/module/metadata/input_metadata.hpp
     */
    
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
    
    /**
     * @brief Helper to create mailbox infrastructure (Phase 7.4)
     * 
     * Single output: Returns tuple<CmdMailbox, WorkMailbox, PublishMailbox>
     * Multi-output: Returns tuple<MailboxSet<T1>, MailboxSet<T2>, ...>
     */
    static auto create_mailbox_infrastructure(const ModuleConfig& config) {
        if constexpr (!use_mailbox_sets) {
            // Single output: Create traditional mailboxes
            CmdMailbox cmd(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::CMD),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_cmd"
            });
            
            WorkMailbox work(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::WORK),
                .message_slots = config.message_slots,
                .max_message_size = SystemRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_work"
            });
            
            PublishMailbox publish(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::PUBLISH),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_publish"
            });
            
            return std::make_tuple(std::move(cmd), std::move(work), std::move(publish));
        } else {
            // Multi-output: Create tuple of MailboxSets
            return create_mailbox_sets_impl(config, std::make_index_sequence<num_output_types>{});
        }
    }
    
    /**
     * @brief Helper to create MailboxSet tuple for multi-output modules
     */
    template<std::size_t... Is>
    static auto create_mailbox_sets_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        // Use fold expression to construct tuple with initialized MailboxSets
        return MailboxSetTuple{create_mailbox_set<Is>(config)...};
    }
    
    /**
     * @brief Create a single MailboxSet for output type at index I
     */
    template<std::size_t I>
    static auto create_mailbox_set(const ModuleConfig& config) {
        using OutputType = std::tuple_element_t<I, OutputTypesTuple>;
        MailboxSet<UserRegistry, OutputType, CommandTypes...> set;
        set.initialize(config);
        return set;
    }
    
public:
    explicit Module(const ModuleConfig& config)
        : config_(config)
        , mailbox_infrastructure_(create_mailbox_infrastructure(config))
        , data_mailbox_(has_continuous_input && !has_multi_input ? 
            std::make_optional<DataMailbox>(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.system_id, config.instance_id, MailboxType::DATA),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime,
                .mailbox_name = config.name + "_data"
            }) : 
            std::nullopt)
        , running_(false)
    {
        // Initialize SubscriberManager
        this->set_max_subscribers(config.max_subscribers);
        
        // Initialize subscription protocol
        subscription_protocol_.set_config(&config_);
        subscription_protocol_.set_work_mailbox(&work_mailbox());
        subscription_protocol_.set_module_name(config.name);
        
        // Initialize publisher
        publisher_.set_subscriber_manager(this);  // Module inherits from SubscriberManager
        publisher_.set_module_ptr(this);  // For multi-output mailbox access
        
        // Only set publish mailbox for single-output modules
        if constexpr (!use_mailbox_sets) {
            publisher_.set_publish_mailbox(&publish_mailbox());
        }
        // Multi-output modules access mailboxes via get_publish_mailbox<Index>()
        
        publisher_.set_module_name(config.name);
        
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
    // Phase 7.4: Public Mailbox Accessors for Multi-Output
    // ========================================================================
    
    /**
     * @brief Get publish mailbox by index (public for Publisher access)
     */
    template<std::size_t Index>
    auto& get_publish_mailbox_public() {
        if constexpr (use_mailbox_sets) {
            return *std::get<Index>(mailbox_infrastructure_).publish;
        } else {
            static_assert(Index == 0, "Single-output modules only have one publish mailbox (index 0)");
            return std::get<2>(mailbox_infrastructure_);
        }
    }
    
    // ========================================================================
    // Lifecycle Management
    // ========================================================================
    
    void start() {
        if (running_) {
            return;
        }
        
        on_init();
        
        // Phase 7.4: Start mailboxes (all MailboxSets for multi-output)
        if constexpr (use_mailbox_sets) {
            // Multi-output: start all MailboxSets
            start_all_mailbox_sets(std::make_index_sequence<num_output_types>{});
        } else {
            // Single-output: start individual mailboxes
            auto cmd_result = cmd_mailbox().start();
            if (!cmd_result) {
                throw std::runtime_error("Failed to start command mailbox");
            }
            
            auto work_result = work_mailbox().start();
            if (!work_result) {
                throw std::runtime_error("Failed to start work mailbox");
            }
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
        
        // Start work thread(s) FIRST to handle subscriptions
        if constexpr (use_mailbox_sets) {
            // Multi-output: spawn one thread per MailboxSet
            std::cout << "[" << config_.name << "] Spawning " << num_output_types << " output work threads...\n";
            spawn_all_output_work_threads(std::make_index_sequence<num_output_types>{});
        } else {
            // Single-output: one work thread
            work_thread_ = std::thread(&Module::work_loop, this);
        }
        
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
    
    /**
     * @brief Start all MailboxSets for multi-output modules
     */
    template<std::size_t... Is>
    void start_all_mailbox_sets(std::index_sequence<Is...>) {
        // Start CMD and WORK mailboxes for each output type
        (void)std::initializer_list<int>{
            (start_mailbox_set<Is>(), 0)...
        };
    }
    
    template<std::size_t Index>
    void start_mailbox_set() {
        auto& cmd = get_cmd_mailbox<Index>();
        auto cmd_result = cmd.start();
        if (!cmd_result) {
            throw std::runtime_error("[Module] Failed to start CMD mailbox for output " + std::to_string(Index));
        }
        
        auto& work = get_work_mailbox<Index>();
        auto work_result = work.start();
        if (!work_result) {
            throw std::runtime_error("[Module] Failed to start WORK mailbox for output " + std::to_string(Index));
        }
    }
    
    /**
     * @brief Spawn all output work threads for multi-output modules
     * 
     * Creates one thread per MailboxSet to handle subscriptions independently.
     */
    template<std::size_t... Is>
    void spawn_all_output_work_threads(std::index_sequence<Is...>) {
        // Reserve space for all threads
        output_work_threads_.reserve(sizeof...(Is));
        
        // Spawn one thread per output type using fold expression
        (void)std::initializer_list<int>{
            (spawn_output_work_thread<Is>(), 0)...
        };
    }
    
    /**
     * @brief Spawn work thread for a specific output index
     */
    template<std::size_t Index>
    void spawn_output_work_thread() {
        output_work_threads_.emplace_back(&Module::output_work_loop<Index>, this);
    }
    
    /**
     * @brief Stop all MailboxSets for multi-output modules
     */
    template<std::size_t... Is>
    void stop_all_mailbox_sets(std::index_sequence<Is...>) {
        (void)std::initializer_list<int>{
            (stop_mailbox_set<Is>(), 0)...
        };
    }
    
    template<std::size_t Index>
    void stop_mailbox_set() {
        get_cmd_mailbox<Index>().stop();
        get_work_mailbox<Index>().stop();
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
        
        // Join work threads (single or multi-output)
        if constexpr (use_mailbox_sets) {
            // Multi-output: join all output work threads
            std::cout << "[" << config_.name << "] Joining " << output_work_threads_.size() 
                      << " output work threads...\n";
            for (auto& thread : output_work_threads_) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
        } else {
            // Single-output: join single work thread
            if (work_thread_ && work_thread_->joinable()) {
                work_thread_->join();
            }
        }
        
        if (command_thread_ && command_thread_->joinable()) {
            command_thread_->join();
        }
        
        // Stop all mailboxes
        if constexpr (use_mailbox_sets) {
            // Multi-output: stop all MailboxSets
            stop_all_mailbox_sets(std::make_index_sequence<num_output_types>{});
        } else {
            // Single-output
            cmd_mailbox().stop();
            work_mailbox().stop();
        }
        if (data_mailbox_) {
            data_mailbox_->stop();
        }
        
        on_cleanup();
    }
    
    bool is_running() const { return running_; }

    // ========================================================================
    // Subscription Protocol (delegated to SubscriptionProtocol member)
    // ========================================================================
protected:
    void subscribe_to_all_sources() {
        subscription_protocol_.subscribe_to_all_sources();
    }
    
    void subscribe_to_source(uint8_t source_system_id, uint8_t source_instance_id) {
        subscription_protocol_.subscribe_to_source(source_system_id, source_instance_id);
    }
    
    void unsubscribe_from_source(uint8_t source_system_id, uint8_t source_instance_id) {
        subscription_protocol_.unsubscribe_from_source(source_system_id, source_instance_id);
    }
    
    void unsubscribe_from_multi_input_source(const InputSource& source) {
        subscription_protocol_.unsubscribe_from_multi_input_source(source);
    }
    
    void handle_subscribe_request(const SubscribeRequestPayload& req) {
        subscription_protocol_.handle_subscribe_request(req, static_cast<commrat::SubscriberManager&>(*this));
    }
    
    void handle_subscribe_reply(const SubscribeReplyPayload& reply) {
        subscription_protocol_.handle_subscribe_reply(reply);
    }
    
    void handle_unsubscribe_request(const UnsubscribeRequestPayload& req) {
        subscription_protocol_.handle_unsubscribe_request(req, static_cast<commrat::SubscriberManager&>(*this));
    }
protected:

    // ========================================================================
    // Publishing (Phase 5: extracted to publishing.hpp)
    // ========================================================================
    // Delegation wrappers for publishing operations
    
    template<typename T = OutputData>
        requires (!std::is_void_v<T>)
    void publish_to_subscribers(T& data) {
        publisher_.publish_to_subscribers(data);
    }
    
    template<typename T>
    void publish_tims_message(TimsMessage<T>& tims_msg) {
        publisher_.publish_tims_message(tims_msg);
    }
    
    template<typename... Ts>
    void publish_multi_outputs(std::tuple<Ts...>& outputs) {
        publisher_.publish_multi_outputs(outputs);
    }
    
    template<typename... Ts>
    void publish_multi_outputs_with_timestamp(std::tuple<Ts...>& outputs, uint64_t timestamp_ns) {
        publisher_.publish_multi_outputs_with_timestamp(outputs, timestamp_ns);
    }
    
    template<typename T>
    static TimsMessage<T> create_tims_message(T&& payload, uint64_t timestamp_ns) {
        return PublisherType::create_tims_message(std::forward<T>(payload), timestamp_ns);
    }

    // ========================================================================
    // Main Loops
    // ========================================================================
    // See: commrat/module/loops/loop_executor.hpp
    // - periodic_loop(): Time-driven generation (PeriodicInput)
    // - free_loop(): Maximum throughput (LoopInput)
    // - continuous_loop(): Event-driven single input (Input<T>)
    // - multi_input_loop(): Synchronized multi-input (Inputs<T,U,V>)
    // ========================================================================
    
private:
    
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
            cmd_mailbox().receive_any(visitor);
        }
        std::cout << "[" << config_.name << "] command_loop ended\n";
    }
    
    void work_loop() {
        std::cout << "[" << config_.name << "] work_loop started, listening on WORK mailbox " 
                  << commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                         config_.system_id, config_.instance_id, MailboxType::WORK) 
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
            work_mailbox().receive_any(visitor);
        }
        std::cout << "[" << config_.name << "] work_loop ended\n";
    }
    
    /**
     * @brief Multi-output work loop for a specific output index
     * 
     * Each MailboxSet gets its own work thread to handle subscriptions
     * independently. This allows consumers to subscribe to specific output
     * types without interference.
     * 
     * @tparam Index Output index (0-based)
     */
    template<std::size_t Index>
    void output_work_loop() {
        using OutputType = std::tuple_element_t<Index, OutputTypesTuple>;
        uint32_t work_mailbox_addr = commrat::get_mailbox_address<OutputType, OutputTypesTuple, UserRegistry>(
            config_.system_id, config_.instance_id, MailboxType::WORK);
        
        std::cout << "[" << config_.name << "] output_work_loop[" << Index << "] started for "
                  << typeid(OutputType).name() << ", listening on WORK mailbox " 
                  << work_mailbox_addr << "\n" << std::flush;
        
        auto& work_mbx = get_work_mailbox<Index>();
        
        while (running_) {
            std::cout << "[" << config_.name << "] output_work_loop[" << Index << "]: waiting for message...\n" << std::flush;
            auto visitor = [this](auto&& tims_msg) {
                auto& msg = tims_msg.payload;
                using MsgType = std::decay_t<decltype(msg)>;
                
                if constexpr (std::is_same_v<MsgType, SubscribeRequestType>) {
                    std::cout << "[" << config_.name << "] output_work_loop[" << Index 
                              << "] Handling SubscribeRequest\n";
                    handle_subscribe_request(msg);
                } else if constexpr (std::is_same_v<MsgType, SubscribeReplyType>) {
                    std::cout << "[" << config_.name << "] output_work_loop[" << Index 
                              << "] Handling SubscribeReply\n";
                    handle_subscribe_reply(msg);
                } else if constexpr (std::is_same_v<MsgType, UnsubscribeRequestType>) {
                    std::cout << "[" << config_.name << "] output_work_loop[" << Index 
                              << "] Handling UnsubscribeRequest\n";
                    handle_unsubscribe_request(msg);
                }
            };
            
            work_mbx.receive_any(visitor);
        }
        
        std::cout << "[" << config_.name << "] output_work_loop[" << Index << "] ended\n";
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
    
private:
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
        
        if (!result.has_value()) {
            // Phase 6.10: Mark input as invalid
            mark_input_invalid(Index);
            return false;  // getData failed
        }
        
        // Phase 6.10: Populate metadata for this input
        // Index matches position in Inputs<T1, T2, T3, ...>
        // getData succeeded - data is "new" (successfully retrieved from buffer)
        // Note: is_new_data = true means getData returned a value (not nullopt)
        //       is_new_data = false would indicate using fallback/default data
        update_input_metadata(Index, result.value(), true);
        
        // Store payload in tuple
        std::get<Index>(all_inputs) = result->payload;
        return true;
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
};

} // namespace commrat
