#pragma once

// Mailbox infrastructure
#include "commrat/mailbox/registry_mailbox.hpp"
#include "commrat/mailbox/typed_mailbox.hpp"
#include "commrat/mailbox/historical_mailbox.hpp"

// System messaging and platform
#include "commrat/messaging/system/subscription_messages.hpp"
#include "commrat/messaging/system/system_registry.hpp"
#include "commrat/platform/threading.hpp"
#include "commrat/platform/timestamp.hpp"

// Module aggregator headers (reduce visual clutter)
#include "commrat/module/module_core.hpp"      // I/O specs, traits, config
#include "commrat/module/module_mixins.hpp"    // CRTP mixins (lifecycle, loops, I/O)
#include "commrat/module/module_services.hpp"  // Subscription, publishing, MailboxSet

// External dependencies
#include <sertial/sertial.hpp>
#include <atomic>
#include <vector>
#include <functional>
#include <optional>

namespace commrat {

// Empty base classes for conditional multi-input mixin inheritance
struct EmptyBase2 {};  // MultiInputInfrastructure conditional base
struct EmptyBase3 {};  // MultiInputProcessor conditional base

// Type trait: Check if a type is std::tuple
template<typename T>
struct is_tuple : std::false_type {};

template<typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template<typename T>
inline constexpr bool is_tuple_v = is_tuple<T>::value;

// ============================================================================
// Automatic Timestamp Management
// ============================================================================

/**
 * @brief Timestamp Management Architecture
 * 
 * Timestamps are stored ONLY in TimsHeader.timestamp, never in payload.
 * Module automatically wraps payloads in TimsMessage and sets header.timestamp:
 * - PeriodicInput/LoopInput: timestamp = Time::now() (data generation time)
 * - Multi-input: timestamp = primary_input.header.timestamp (sync point)
 * - Input<T>: timestamp = input.header.timestamp (propagation)
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
 * @tparam InputSpec_ Input specification: Input<T>, Inputs<T,U,V>, PeriodicInput, LoopInput
 * @tparam CommandTypes Optional variadic command types this module handles
 * 
 * Example:
 * @code
 * class SensorModule : public Module<Registry, Output<TempData>, PeriodicInput> {
 * protected:
 *     TempData process() override {
 *         return TempData{.temperature_celsius = read_sensor()}
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
    // ========================================================================
    // Processing Virtual Function Providers (conditional based on I/O specs)
    // ========================================================================
    : public ContinuousProcessorBase<
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type,
        typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
      >
    , public MultiOutputProcessorBase<
        typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type,
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type
      >
    , public SingleOutputProcessorBase<
        typename ExtractInputPayload<typename NormalizeInput<InputSpec_>::Type>::type,
        typename ExtractOutputPayload<typename NormalizeOutput<OutputSpec_>::Type>::type
      >
    , public ResolveMultiInputBase<InputSpec_, OutputSpec_>::type
    
    // ========================================================================
    // Core Module Infrastructure (CRTP mixins - always active)
    // ========================================================================
    , public MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>
    , public LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public CommandDispatcher<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>
    
    // ========================================================================
    // Multi-Input Support (conditional - only when Inputs<T,U,V> specified)
    // ========================================================================
    , public std::conditional_t<
        module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::has_multi_input,
        MultiInputInfrastructure<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>,
        EmptyBase2
      >
    , public std::conditional_t<
        module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::has_multi_input,
        MultiInputProcessor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputData, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>,
        EmptyBase3
      >
    
    // ========================================================================
    // Lifecycle & Infrastructure (CRTP mixins - always active)
    // ========================================================================
    , public LifecycleManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public WorkLoopHandler<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
    , public MailboxInfrastructureBuilder<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry>
    , public InputMetadataManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>
{
    // Friend declarations for CRTP mixins
    friend class LoopExecutor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename OutputTypesTuple<typename NormalizeOutput<OutputSpec_>::Type>::type>;
    friend class InputMetadataAccessors<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class CommandDispatcher<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, CommandTypes...>;
    friend class MultiInputInfrastructure<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>;
    friend class MultiInputProcessor<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputTypesTuple, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputData, typename module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::OutputTypesTuple, module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_>::InputCount>;
    friend class LifecycleManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class WorkLoopHandler<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    friend class MailboxInfrastructureBuilder<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry>;
    friend class InputMetadataManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>>;
    
private:
    // ========================================================================
    // Type Definitions
    // ========================================================================
    
    // Import all type computations from module_traits::ModuleTypes
    using ModuleTypes = module_traits::ModuleTypes<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>;
    
    // Normalized specs
    using OutputSpec = typename ModuleTypes::OutputSpec;
    using InputSpec = typename ModuleTypes::InputSpec;
    
    // Type tuples
    using OutputTypesTuple = typename ModuleTypes::OutputTypesTuple;
    using InputTypesTuple = typename ModuleTypes::InputTypesTuple;
    using MailboxSetTuple = typename ModuleTypes::MailboxSetTuple;
    using CommandTuple = typename ModuleTypes::CommandTuple;
    using CombinedCmdTypes = typename ModuleTypes::CombinedCmdTypes;
    using DataTypesTuple = typename ModuleTypes::DataTypesTuple;
    
    // Mailbox types
    using CmdMailbox = typename ModuleTypes::CmdMailbox;
    using WorkMailbox = typename ModuleTypes::WorkMailbox;
    using PublishMailbox = typename ModuleTypes::PublishMailbox;
    using DataMailbox = typename ModuleTypes::DataMailbox;
    
    // Counts and flags
    static constexpr size_t num_output_types = ModuleTypes::num_output_types;
    static constexpr size_t num_command_types = ModuleTypes::num_command_types;
    static constexpr size_t InputCount = ModuleTypes::InputCount;
    static constexpr bool has_multi_input = ModuleTypes::has_multi_input;
    static constexpr bool use_mailbox_sets = ModuleTypes::use_mailbox_sets;
    static constexpr bool has_primary_input_spec = ModuleTypes::has_primary_input_spec;
    using PrimaryPayloadType = typename ModuleTypes::PrimaryPayloadType;
    
    // Compile-time validation: If PrimaryInput<T> specified, T must be in Inputs<...>
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
    
public:
    // Public type aliases (user-visible)
    using OutputData = typename ModuleTypes::OutputData;
    using InputData = typename ModuleTypes::InputData;
    
    // Input mode flags
    static constexpr bool has_continuous_input = ModuleTypes::has_continuous_input;
    static constexpr bool has_periodic_input = ModuleTypes::has_periodic_input;
    static constexpr bool has_loop_input = ModuleTypes::has_loop_input;
    static constexpr bool has_multi_output = ModuleTypes::has_multi_output;
    
protected:
    ModuleConfig config_;
    
    // Mailbox infrastructure: Tuple of MailboxSets (one per output type)
    // Single-output modules have a tuple with 1 MailboxSet
    // Multi-output modules have a tuple with N MailboxSets
    MailboxSetTuple mailbox_infrastructure_;
    
    // Helper accessors - always use first MailboxSet
    CmdMailbox& cmd_mailbox() {
        return *std::get<0>(mailbox_infrastructure_).cmd;
    }
    
    WorkMailbox& work_mailbox() {
        return *std::get<0>(mailbox_infrastructure_).work;
    }
    
    PublishMailbox& publish_mailbox() {
        static_assert(num_output_types == 1, "publish_mailbox() accessor only available for single-output modules. Use get_publish_mailbox<Index>() for multi-output");
        return *std::get<0>(mailbox_infrastructure_).publish;
    }
    
    // Get specific MailboxSet by index
    template<std::size_t Index>
    auto& get_mailbox_set() {
        return std::get<Index>(mailbox_infrastructure_);
    }
    
    // Get publish mailbox by index
    template<std::size_t Index>
    auto& get_publish_mailbox() {
        return *std::get<Index>(mailbox_infrastructure_).publish;
    }
    
    // Get work mailbox by index
    template<std::size_t Index>
    auto& get_work_mailbox() {
        return *std::get<Index>(mailbox_infrastructure_).work;
    }
    
    // Get cmd mailbox by index
    template<std::size_t Index>
    auto& get_cmd_mailbox() {
        return *std::get<Index>(mailbox_infrastructure_).cmd;
    }
    
    // Multi-input support
    // Single-input mode
    std::optional<DataMailbox> data_mailbox_;  // base + 48: Receives input data (only for single Input<T>)
    
    // Multi-input mailbox infrastructure (in MultiInputInfrastructure mixin)
    // - input_mailboxes_: Tuple of HistoricalMailbox instances
    // - secondary_input_threads_: Background receive threads
    
    // Subscription protocol
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
    
    // Publishing logic (post-unification: uses MultiOutputManager for subscribers)
    using PublisherType = commrat::Publisher<
        UserRegistry, 
        OutputData, 
        PublishMailbox,
        Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>  // Module type for get_publish_mailbox<Index>()
    >;
    PublisherType publisher_;
    
    std::atomic<bool> running_;
    
    // Module threads
    std::optional<std::thread> data_thread_;       // Periodic/loop/continuous/multi-input processing
    std::optional<std::thread> command_thread_;     // User commands on CMD mailbox
    // Work threads: output_work_threads_ in MultiOutputManager mixin (ALL modules)
    // Secondary input threads: secondary_input_threads_ in MultiInputInfrastructure mixin
    
    // Subscriber management - inherited from MultiOutputManager mixin
    // ALL modules now use per-output subscriber lists (post-unification):
    // - output_subscribers_: Per-output subscriber lists (index 0 for single-output)
    // - output_subscribers_mutex_: Protects output_subscribers_
    
    // ========================================================================
    // Input Metadata Storage
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
    
    // Input metadata management (in InputMetadataManager mixin)
    // - update_input_metadata<T>()
    // - mark_input_invalid()
    
    // Mailbox infrastructure creation (in MailboxInfrastructureBuilder mixin)
    // - create_mailbox_infrastructure()
    // - create_mailbox_sets_impl()
    // - create_mailbox_set()
    
    // Helper: Initialize mailbox infrastructure in place
    template<std::size_t... Is>
    void initialize_mailbox_infrastructure_impl(const ModuleConfig& config, std::index_sequence<Is...>) {
        // Initialize each MailboxSet in the tuple
        ((std::get<Is>(mailbox_infrastructure_).initialize(config)), ...);
    }

public:
    explicit Module(const ModuleConfig& config)
        : config_(config)
        , mailbox_infrastructure_{}  // Default construct, will initialize in body
        , data_mailbox_(has_continuous_input && !has_multi_input ? 
            std::make_optional<DataMailbox>(MailboxConfig{
                .mailbox_id = commrat::get_mailbox_address<OutputData, OutputTypesTuple, UserRegistry>(
                    config.has_multi_output_config() ? config.system_id(0) : config.system_id(),
                    config.has_multi_output_config() ? config.instance_id(0) : config.instance_id(),
                    static_cast<uint8_t>(MailboxType::DATA)),
                .message_slots = config.message_slots,
                .max_message_size = UserRegistry::max_message_size,
                .send_priority = static_cast<uint8_t>(config.priority),
                .realtime = config.realtime
            }) : 
            std::nullopt)
        , running_(false)
    {
        // Initialize mailbox infrastructure in place
        initialize_mailbox_infrastructure_impl(config, std::make_index_sequence<num_output_types>{});
        
        // Initialize per-output subscriber lists (MultiOutputManager)
        this->initialize_output_subscribers();
        
        // Initialize subscription protocol
        subscription_protocol_.set_config(&config_);
        subscription_protocol_.set_work_mailbox(&work_mailbox());
        subscription_protocol_.set_module_name(config.name);
        
        // Initialize publisher (post-unification: uses module_ptr_ for mailboxes and subscribers)
        // REMOVED: set_subscriber_manager() - subscribers accessed via module_ptr_->get_output_subscribers()
        publisher_.set_module_ptr(this);  // For mailbox/subscriber access via module_ptr_
        publisher_.set_module_name(config.name);
        
        // Initialize multi-input mailboxes
        if constexpr (has_multi_input) {
            this->initialize_multi_input_mailboxes();
        }
    }
    
    virtual ~Module() {
        this->stop();
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
    // For Input<T> with single output: process input and populate output reference
    // Calls process which derived classes should override
    // SFINAE: Only enabled for single output (not void, not tuple)
    // Uses dummy template parameter O to delay evaluation until instantiation
    template<typename T = InputData, typename O = OutputData,
             typename = std::enable_if_t<!std::is_void_v<O> && !is_tuple_v<O>>>
    void process_dispatch(const T& input, O& output) {
        // this->process will resolve to derived class version
        this->process(input, output);
    }
    
    // Derived classes define this (non-template) for continuous input:
    //   void process(const InputData& input, OutputData& output) { ... }


public:
    // ========================================================================
    // Public Mailbox Accessors
    // ========================================================================
    
    /**
     * @brief Get publish mailbox by index (public for Publisher access)
     */
    template<std::size_t Index>
    auto& get_publish_mailbox_public() {
        return *std::get<Index>(mailbox_infrastructure_).publish;
    }
    
    /**
     * @brief Get cmd mailbox by index (public for Publisher access)
     * Phase 7: CMD mailbox used for publishing
     */
    template<std::size_t Index>
    auto& get_cmd_mailbox_public() {
        return *std::get<Index>(mailbox_infrastructure_).cmd;
    }
    
    // ========================================================================
    // Lifecycle Management
    // ========================================================================
    // start(), stop(), is_running() in LifecycleManager mixin

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
    
    void unsubscribe_from_multi_input_source(const MultiInputConfig::InputSource& source) {
        subscription_protocol_.unsubscribe_from_multi_input_source(source);
    }

public:
    // Subscription protocol handlers - PUBLIC for SubscriptionProtocol callbacks
    void handle_subscribe_request(const SubscribeRequestPayload& req, std::size_t output_idx = 0) {
        subscription_protocol_.handle_subscribe_request(req, *this, output_idx);
    }
    
    void handle_subscribe_reply(const SubscribeReplyPayload& reply) {
        subscription_protocol_.handle_subscribe_reply(reply);
    }
    
    void handle_unsubscribe_request(const UnsubscribeRequestPayload& req) {
        subscription_protocol_.handle_unsubscribe_request(req, *this);
    }
    
    /**
     * @brief Add subscriber to correct output-specific list
     * 
     * Delegates to MultiOutputManager mixin.
     * PUBLIC: Called by SubscriptionProtocol handlers.
     */
    void add_subscriber_to_output(uint32_t subscriber_base_addr, uint8_t mailbox_index, std::size_t output_idx = 0) {
        // Delegate to inherited MultiOutputManager::add_subscriber_to_output
        this->MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, OutputTypesTuple>::add_subscriber_to_output(subscriber_base_addr, mailbox_index, output_idx);
    }
    
    /**
     * @brief Remove subscriber (for unsubscribe)
     * 
     * Delegates to MultiOutputManager mixin.
     * PUBLIC: Called by SubscriptionProtocol handlers.
     */
    void remove_subscriber(uint32_t subscriber_base_addr) {
        // Delegate to inherited MultiOutputManager::remove_subscriber
        this->MultiOutputManager<Module<UserRegistry, OutputSpec_, InputSpec_, CommandTypes...>, UserRegistry, OutputTypesTuple>::remove_subscriber(subscriber_base_addr);
    }
    
protected:

    // ========================================================================
    // Publishing
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
    // See: commrat/module/lifecycle/loop_executor.hpp
    // - periodic_loop(): Time-driven generation (PeriodicInput)
    // - free_loop(): Maximum throughput (LoopInput)
    // - continuous_loop(): Event-driven single input (Input<T>)
    // - multi_input_loop(): Synchronized multi-input (Inputs<T,U,V>)
    // ========================================================================
    
private:
    // secondary_input_receive_loop() in MultiInputInfrastructure mixin
    // command_loop() in CommandDispatcher mixin
    // work_loop() in WorkLoopHandler mixin
    // output_work_loop() in MultiOutputManager mixin
    // handle_user_command() and on_command() in CommandDispatcher mixin
    
    // ========================================================================
    // Helper Functions
    // ========================================================================
    
private:
    // Helper: Get primary input index at compile time
    static constexpr size_t get_primary_input_index() {
        // Extract primary input from CommandTypes (PrimaryInput<T> passed as command)
        if constexpr (has_primary_input_spec) {
            // PrimaryInput<T> found in CommandTypes, extract T and find index
            return PrimaryInputIndex_v<PrimaryPayloadType, InputTypesTuple>;
        } else {
            // No explicit PrimaryInput - use first input as primary
            return 0;
        }
    }
    
    // Multi-input mailbox creation/initialization (in MultiInputInfrastructure mixin)
    // - create_historical_mailbox_for_input<Index>()
    // - create_input_mailboxes_impl()
    // - initialize_multi_input_mailboxes()
    // - start_input_mailboxes_impl()
    // - start_input_mailboxes()
    // - start_secondary_input_threads()
    // - start_secondary_threads_impl()
    
    // Multi-input processing (in MultiInputProcessor mixin)
    // - receive_primary_input<PrimaryIdx>()
    // - gather_all_inputs<PrimaryIdx>()
    // - sync_secondary_inputs<PrimaryIdx>()
    // - sync_secondary_inputs_impl<PrimaryIdx>()
    // - sync_input_at_index<Index>()
    // - call_multi_input_process()
    // - call_multi_input_process_impl()
    // - call_multi_input_multi_output_process()
    // - call_multi_input_multi_output_process_impl()
};

} // namespace commrat
